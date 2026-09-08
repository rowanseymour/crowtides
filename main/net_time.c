#include "net_time.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "config.h"

static const char *TAG = "net";

#define CONNECT_TIMEOUT_MS 15000
#define SNTP_TIMEOUT_MS    15000
#define MAX_RETRIES        3

static EventGroupHandle_t s_events;
#define GOT_IP_BIT BIT0
#define FAILED_BIT BIT1

static int s_retries;

typedef struct {
    const char *ssid;
    const char *pass;
} wifi_net_t;

// Configured networks, in the order they're tried when none is visible.
static const wifi_net_t s_networks[] = WIFI_NETWORKS;
#define N_NETWORKS (sizeof(s_networks) / sizeof(s_networks[0]))

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retries < MAX_RETRIES) {
            s_retries++;
            ESP_LOGI(TAG, "reconnecting (%d/%d)", s_retries, MAX_RETRIES);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_events, FAILED_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_events, GOT_IP_BIT);
    }
}

// Scan and fill `order` with the indices of configured networks that were
// seen, strongest signal first. Returns how many were seen.
static int rank_visible(int *order)
{
    int rssi[N_NETWORKS];
    for (int i = 0; i < N_NETWORKS; i++) {
        rssi[i] = INT_MIN;
    }

    esp_err_t err = esp_wifi_scan_start(NULL, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan failed: %s", esp_err_to_name(err));
        return 0;
    }
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    wifi_ap_record_t *aps = n ? malloc(n * sizeof(*aps)) : NULL;
    if (aps) {
        esp_wifi_scan_get_ap_records(&n, aps);
        for (int a = 0; a < n; a++) {
            for (int i = 0; i < N_NETWORKS; i++) {
                if (strcmp((const char *)aps[a].ssid, s_networks[i].ssid) == 0 &&
                    aps[a].rssi > rssi[i]) {
                    rssi[i] = aps[a].rssi;
                }
            }
        }
        free(aps);
    } else {
        esp_wifi_clear_ap_list();
    }
    ESP_LOGI(TAG, "scan saw %u access points", n);

    // Selection sort by RSSI — the list is a handful of entries.
    int count = 0;
    for (;;) {
        int best = -1;
        for (int i = 0; i < N_NETWORKS; i++) {
            bool taken = false;
            for (int j = 0; j < count; j++) {
                taken |= order[j] == i;
            }
            if (!taken && rssi[i] != INT_MIN &&
                (best < 0 || rssi[i] > rssi[best])) {
                best = i;
            }
        }
        if (best < 0) {
            break;
        }
        ESP_LOGI(TAG, "  %s (%d dBm)", s_networks[best].ssid, rssi[best]);
        order[count++] = best;
    }
    return count;
}

static bool try_connect(const wifi_net_t *net)
{
    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid, net->ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, net->pass, sizeof(wc.sta.password) - 1);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));

    s_retries = 0;
    xEventGroupClearBits(s_events, GOT_IP_BIT | FAILED_BIT);
    ESP_LOGI(TAG, "connecting to %s", net->ssid);
    esp_wifi_connect();

    EventBits_t bits = xEventGroupWaitBits(s_events, GOT_IP_BIT | FAILED_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(CONNECT_TIMEOUT_MS));
    if (bits & GOT_IP_BIT) {
        ESP_LOGI(TAG, "connected to %s", net->ssid);
        return true;
    }
    // Stop any in-flight attempt without the handler retrying it.
    s_retries = MAX_RETRIES;
    esp_wifi_disconnect();
    ESP_LOGW(TAG, "could not join %s", net->ssid);
    return false;
}

bool net_connect(void)
{
    // NVS is initialised by app_main before this can run.
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    // Config comes from config.h every boot — don't persist it to flash.
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    s_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               on_wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               on_wifi_event, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // One scan (a second or two) tells us which configured networks are
    // actually here, so we never burn a connect timeout on an absent one.
    int order[N_NETWORKS];
    int n_candidates = rank_visible(order);
    if (n_candidates == 0) {
        // Nothing seen — maybe a hidden SSID. Fall back to trying them all.
        ESP_LOGW(TAG, "no configured network visible, trying all");
        for (int i = 0; i < N_NETWORKS; i++) {
            order[i] = i;
        }
        n_candidates = N_NETWORKS;
    }

    for (int i = 0; i < n_candidates; i++) {
        if (try_connect(&s_networks[order[i]])) {
            return true;
        }
    }
    ESP_LOGE(TAG, "wifi connect failed");
    return false;
}

bool net_sync_time(void)
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        2, ESP_SNTP_SERVER_LIST("time.google.com", "pool.ntp.org"));
    ESP_ERROR_CHECK(esp_netif_sntp_init(&cfg));
    esp_err_t err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(SNTP_TIMEOUT_MS));
    esp_netif_sntp_deinit();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sntp sync failed: %s", esp_err_to_name(err));
        return false;
    }

    time_t now;
    struct tm tm;
    time(&now);
    localtime_r(&now, &tm);
    ESP_LOGI(TAG, "time synced: %04d-%02d-%02d %02d:%02d:%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec);
    return true;
}

void net_disconnect(void)
{
    // Stop the handler from treating shutdown as connection loss.
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event);
    esp_wifi_stop();
    esp_wifi_deinit();
}
