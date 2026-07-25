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
#include "nvs_flash.h"

#include "board.h"
#include "chart.h"
#include "config.h"
#include "epd.h"
#include "net_time.h"
#include "tides.h"

static const char *TAG = "crowtides";

// The cache is anchored CACHE_FIRST_DAY days back from the day of fetch:
// one week of the past stays browsable, three weeks of future.
#define CACHE_FIRST_DAY (-7)

// Refetch when fewer than this many future days remain cached.
#define FETCH_MIN_DAYS_AHEAD 7

#define TIME_VALID_AFTER 1735689600   // RTC clock plausible if past 2025
// Guard against integer truncation waking a hair before midnight (which
// would redraw yesterday). Fetches use explicit dates, so nothing else
// cares about the exact wake second.
#define WAKE_MARGIN_SEC 2
#define RETRY_SLEEP_SEC (30 * 60)

// View state survives deep sleep; the tide cache itself lives in NVS so
// it also survives power loss (battery swaps).
RTC_DATA_ATTR static int s_offset;

static tide_data_t s_tides;

static const int WAKE_BTNS[] = { BTN_MENU, BTN_EXIT, BTN_WHEEL_UP,
                                 BTN_WHEEL_DOWN };

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

    uint64_t mask = 0;
    for (int i = 0; i < sizeof(WAKE_BTNS) / sizeof(WAKE_BTNS[0]); i++) {
        rtc_gpio_pullup_en(WAKE_BTNS[i]);
        rtc_gpio_pulldown_dis(WAKE_BTNS[i]);
        mask |= 1ULL << WAKE_BTNS[i];
    }
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);

    // Make sure the power LED can't sit half-on through deep sleep.
    gpio_config_t led = {
        .pin_bit_mask = 1ULL << BOARD_PWR_LED,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&led);
    gpio_set_level(BOARD_PWR_LED, 0);
    gpio_hold_en(BOARD_PWR_LED);
    gpio_deep_sleep_hold_en();

    epd_hold_power();
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

// Recompute the derived view state: clock validity, today's midnight, the
// viewed day (offset clamped to what the cache can render).
static void refresh_view(bool *time_ok, time_t *today, time_t *view_day)
{
    *time_ok = time(NULL) > TIME_VALID_AFTER;
    *today = *time_ok ? local_midnight(time(NULL)) : 0;
    if (*time_ok) {
        while (s_offset > 0 &&
               !tides_has_day(&s_tides,
                              *today + (time_t)s_offset * TIDES_DAY_SEC)) {
            s_offset--;
        }
        while (s_offset < 0 &&
               !tides_has_day(&s_tides,
                              *today + (time_t)s_offset * TIDES_DAY_SEC)) {
            s_offset++;
        }
    }
    *view_day = *today + (time_t)s_offset * TIDES_DAY_SEC;
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
    tides_cache_load(&s_tides);

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    uint64_t pins = cause == ESP_SLEEP_WAKEUP_EXT1
                        ? esp_sleep_get_ext1_wakeup_status() : 0;
    ESP_LOGI(TAG, "wake cause %d pins %llx offset %d", cause, pins, s_offset);

    bool time_ok;
    time_t today, view_day;
    refresh_view(&time_ok, &today, &view_day);

    bool force_fetch = false;  // definitely fetch
    bool need_net = false;     // need the clock before deciding
    if (cause == ESP_SLEEP_WAKEUP_TIMER || cause == ESP_SLEEP_WAKEUP_UNDEFINED) {
        // Daily redraw (or cold boot). Only fetch when the cached window
        // runs low — tide predictions don't change. Without a valid clock
        // (power loss) we must sync first; the cache may still be fine.
        s_offset = 0;
        if (!time_ok) {
            need_net = true;
        } else if (tides_days_ahead(&s_tides, today) < FETCH_MIN_DAYS_AHEAD) {
            force_fetch = true;
        }
    } else if (pins & (1ULL << BTN_MENU)) {
        s_offset = 0;
        force_fetch = true;
    } else if (pins & (1ULL << BTN_EXIT)) {
        s_offset = 0;
    } else if (pins & (1ULL << BTN_WHEEL_UP)) {
        s_offset++;
        accumulate_wheel_input();
    } else if (pins & (1ULL << BTN_WHEEL_DOWN)) {
        s_offset--;
        accumulate_wheel_input();
    }

    refresh_view(&time_ok, &today, &view_day);
    if (!(time_ok && tides_has_day(&s_tides, view_day))) {
        need_net = true;
    }

    if (force_fetch || need_net) {
        bool wifi_ok = net_connect();
        if (wifi_ok) {
            net_sync_time();
        }
        refresh_view(&time_ok, &today, &view_day);

        // With the clock now valid, fetch only if the cache really needs
        // it — a battery swap with a fresh cache costs no API credits.
        if (!force_fetch && time_ok &&
            tides_has_day(&s_tides, view_day) &&
            tides_days_ahead(&s_tides, today) >= FETCH_MIN_DAYS_AHEAD) {
            ESP_LOGI(TAG, "clock synced, cache still fresh — no fetch");
        } else {
            bool fetched = time_ok && wifi_ok &&
                tides_fetch(&s_tides,
                            today + (time_t)CACHE_FIRST_DAY * TIDES_DAY_SEC);
            if (fetched) {
                tides_cache_save(&s_tides);
            }
        }
        if (wifi_ok) {
            net_disconnect();
        }
        refresh_view(&time_ok, &today, &view_day);
        if (!(time_ok && tides_has_day(&s_tides, view_day))) {
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

    int64_t secs = (int64_t)(local_midnight(time(NULL)) + TIDES_DAY_SEC
                             - time(NULL)) + WAKE_MARGIN_SEC;
    go_to_sleep(secs < 60 ? 60 : secs);
}
