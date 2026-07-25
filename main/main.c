#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "buttons.h"
#include "chart.h"
#include "config.h"
#include "epd.h"
#include "net_time.h"
#include "tides.h"

static const char *TAG = "crowtides";

// The cache is anchored CACHE_FIRST_DAY days back from the day of fetch,
// so a couple of past days stay browsable.
#define CACHE_FIRST_DAY (-2)

// Refetch when fewer than this many future days remain cached.
#define FETCH_MIN_DAYS_AHEAD 7

#define CACHE_VERSION 1

#define TIME_VALID_AFTER 1735689600   // RTC clock plausible if past 2025
#define WAKE_MARGIN_SEC 120           // wake 00:02 local, not 00:00
#define RETRY_SLEEP_SEC (30 * 60)

// View state survives deep sleep; the tide cache itself lives in NVS so
// it also survives power loss (battery swaps).
RTC_DATA_ATTR static int s_offset;

static tide_data_t s_tides;

#define WAKE_PIN_MASK ((1ULL << BTN_MENU) | (1ULL << BTN_EXIT) | \
                       (1ULL << BTN_WHEEL_UP) | (1ULL << BTN_WHEEL_DOWN))

static void cache_load(void)
{
    nvs_handle_t h;
    if (nvs_open("crowtides", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint32_t ver = 0;
    size_t size = sizeof(s_tides);
    if (nvs_get_u32(h, "ver", &ver) == ESP_OK && ver == CACHE_VERSION &&
        nvs_get_blob(h, "tides", &s_tides, &size) == ESP_OK &&
        size == sizeof(s_tides)) {
        ESP_LOGI(TAG, "cache loaded: %d heights from %lld", s_tides.n_heights,
                 (long long)s_tides.start);
    } else {
        memset(&s_tides, 0, sizeof(s_tides));
    }
    nvs_close(h);
}

static void cache_save(void)
{
    nvs_handle_t h;
    if (nvs_open("crowtides", NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "cache save failed");
        return;
    }
    nvs_set_u32(h, "ver", CACHE_VERSION);
    nvs_set_blob(h, "tides", &s_tides, sizeof(s_tides));
    nvs_commit(h);
    nvs_close(h);
}

static time_t local_midnight(time_t t)
{
    struct tm tm;
    localtime_r(&t, &tm);
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    return mktime(&tm);
}

static void go_to_sleep(int64_t seconds)
{
    ESP_LOGI(TAG, "deep sleep for %lld s", seconds);
    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);

    const int pins[] = { BTN_MENU, BTN_EXIT, BTN_WHEEL_UP, BTN_WHEEL_DOWN };
    for (int i = 0; i < 4; i++) {
        rtc_gpio_pullup_en(pins[i]);
        rtc_gpio_pulldown_dis(pins[i]);
    }
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    esp_sleep_enable_ext1_wakeup(WAKE_PIN_MASK, ESP_EXT1_WAKEUP_ANY_LOW);

    // Make sure the power LED can't sit half-on through deep sleep.
    gpio_config_t led = {
        .pin_bit_mask = 1ULL << GPIO_NUM_41,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&led);
    gpio_set_level(GPIO_NUM_41, 0);
    gpio_hold_en(GPIO_NUM_41);

    epd_power_off();
    esp_deep_sleep_start();
}

// After a wheel wake, keep listening briefly so rapid clicks accumulate
// into one redraw instead of one redraw per click.
static void accumulate_wheel_input(void)
{
    gpio_config_t in = {
        .pin_bit_mask = (1ULL << BTN_WHEEL_UP) | (1ULL << BTN_WHEEL_DOWN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in));

    int prev_up = 0, prev_down = 0;  // wake press still held counts as low
    for (int t = 0; t < 900 / 20; t++) {
        int up = gpio_get_level(BTN_WHEEL_UP);
        int down = gpio_get_level(BTN_WHEEL_DOWN);
        if (up == 0 && prev_up == 1) {
            s_offset++;
            t = 0;  // extend the window on activity
        }
        if (down == 0 && prev_down == 1) {
            s_offset--;
            t = 0;
        }
        prev_up = up;
        prev_down = down;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// Clamp the view offset to days the cache can actually render.
static void clamp_offset_to_cache(time_t today)
{
    while (s_offset > 0 &&
           !tides_has_day(&s_tides, today + (time_t)s_offset * 86400)) {
        s_offset--;
    }
    while (s_offset < 0 &&
           !tides_has_day(&s_tides, today + (time_t)s_offset * 86400)) {
        s_offset++;
    }
}

void app_main(void)
{
    setenv("TZ", TIDE_TZ, 1);
    tzset();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    cache_load();

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    uint64_t pins = cause == ESP_SLEEP_WAKEUP_EXT1
                        ? esp_sleep_get_ext1_wakeup_status() : 0;
    ESP_LOGI(TAG, "wake cause %d pins %llx offset %d", cause, pins, s_offset);

    bool time_ok = time(NULL) > TIME_VALID_AFTER;
    time_t today = time_ok ? local_midnight(time(NULL)) : 0;

    bool need_fetch = false;
    if (cause == ESP_SLEEP_WAKEUP_TIMER || cause == ESP_SLEEP_WAKEUP_UNDEFINED) {
        // Daily redraw (or cold boot). Only fetch when the cached window
        // runs low — tide predictions don't change.
        s_offset = 0;
        need_fetch = !time_ok ||
                     tides_days_ahead(&s_tides, today) < FETCH_MIN_DAYS_AHEAD;
    } else if (pins & (1ULL << BTN_MENU)) {
        s_offset = 0;
        need_fetch = true;
    } else if (pins & (1ULL << BTN_EXIT)) {
        s_offset = 0;
    } else if (pins & (1ULL << BTN_WHEEL_UP)) {
        s_offset++;
        accumulate_wheel_input();
    } else if (pins & (1ULL << BTN_WHEEL_DOWN)) {
        s_offset--;
        accumulate_wheel_input();
    }

    if (time_ok) {
        clamp_offset_to_cache(today);
    }
    time_t view_day = today + (time_t)s_offset * 86400;

    if (!need_fetch && !(time_ok && tides_has_day(&s_tides, view_day))) {
        need_fetch = true;
    }

    if (need_fetch) {
        bool wifi_ok = net_connect();
        bool sntp_ok = wifi_ok && net_sync_time();
        time_ok = sntp_ok || time(NULL) > TIME_VALID_AFTER;
        today = time_ok ? local_midnight(time(NULL)) : 0;
        view_day = today + (time_t)s_offset * 86400;

        bool fetched = time_ok && wifi_ok &&
            tides_fetch(&s_tides, today + CACHE_FIRST_DAY * 86400);
        if (wifi_ok) {
            net_disconnect();
        }
        if (fetched) {
            cache_save();
        } else if (!(time_ok && tides_has_day(&s_tides, view_day))) {
            // Nothing fit to draw: keep whatever is on screen, retry soon.
            ESP_LOGW(TAG, "refresh failed, retrying in %d min",
                     RETRY_SLEEP_SEC / 60);
            go_to_sleep(RETRY_SLEEP_SEC);
        }
    }

    epd_init();
    chart_render(&s_tides, view_day, s_offset);
    epd_display();
    epd_sleep();
    ESP_LOGI(TAG, "chart drawn for offset %+d, %d cached days ahead",
             s_offset, tides_days_ahead(&s_tides, today));

    int64_t secs = (int64_t)(local_midnight(time(NULL)) + 86400 - time(NULL))
                   + WAKE_MARGIN_SEC;
    go_to_sleep(secs < 60 ? 60 : secs);
}
