#include <stdlib.h>
#include <time.h>

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_sleep.h"

#include "chart.h"
#include "config.h"
#include "epd.h"
#include "net_time.h"
#include "text.h"
#include "tides.h"

static const char *TAG = "crowtides";

#define PIN_MENU_BTN GPIO_NUM_2
#define PIN_PWR_LED  GPIO_NUM_41

// Consider the RTC clock valid if it's past 2025 — survives deep sleep,
// so a failed SNTP re-sync doesn't strand us.
#define TIME_VALID_AFTER 1735689600

// Wake 2 minutes past local midnight so the API's "today" has rolled over.
#define WAKE_MARGIN_SEC 120

#define RETRY_SLEEP_SEC (30 * 60)

static void go_to_sleep(int64_t seconds)
{
    ESP_LOGI(TAG, "deep sleep for %lld s", seconds);
    esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
    // MENU button = manual refresh.
    rtc_gpio_pullup_en(PIN_MENU_BTN);
    rtc_gpio_pulldown_dis(PIN_MENU_BTN);
    esp_sleep_enable_ext0_wakeup(PIN_MENU_BTN, 0);
    // Make sure the power LED can't sit half-on through deep sleep.
    gpio_config_t led = {
        .pin_bit_mask = 1ULL << PIN_PWR_LED,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&led);
    gpio_set_level(PIN_PWR_LED, 0);
    gpio_hold_en(PIN_PWR_LED);
    epd_power_off();
    esp_deep_sleep_start();
}

static int64_t seconds_to_next_local_midnight(time_t now)
{
    struct tm tm;
    localtime_r(&now, &tm);
    tm.tm_mday += 1;
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    time_t next = mktime(&tm);
    int64_t secs = (int64_t)(next - now) + WAKE_MARGIN_SEC;
    return secs < 60 ? 60 : secs;
}

void app_main(void)
{
    setenv("TZ", TIDE_TZ, 1);
    tzset();

    ESP_LOGI(TAG, "wake cause: %d", esp_sleep_get_wakeup_cause());

    bool wifi_ok = net_connect();
    bool sntp_ok = wifi_ok && net_sync_time();
    bool time_ok = sntp_ok || time(NULL) > TIME_VALID_AFTER;

    tide_data_t tides;
    bool tides_ok = time_ok && wifi_ok && tides_fetch(&tides);

    if (wifi_ok) {
        net_disconnect();
    }

    if (tides_ok) {
        epd_init();
        chart_render(&tides, time(NULL));
        epd_display();
        epd_sleep();
        ESP_LOGI(TAG, "chart drawn");
    } else {
        // Leave the panel untouched: yesterday's chart is better than an
        // error screen. Retry soon.
        ESP_LOGW(TAG, "refresh failed (wifi=%d sntp=%d time=%d), retrying in %d min",
                 wifi_ok, sntp_ok, time_ok, RETRY_SLEEP_SEC / 60);
        go_to_sleep(RETRY_SLEEP_SEC);
    }

    go_to_sleep(seconds_to_next_local_midnight(time(NULL)));
}
