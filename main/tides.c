#include "tides.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "nvs.h"

#include "config.h"

static const char *TAG = "tides";

#define RESPONSE_MAX (48 * 1024)

#define CACHE_VERSION 1
// Cached data is only valid for the coordinates it was fetched for.
#define CACHE_LOC TIDE_LAT "," TIDE_LON

// Parse one chunk's response, appending to `out`. Heights are placed by
// sample index relative to out->start, which makes chunk-boundary
// duplicates (each chunk includes the next midnight) harmless.
static bool parse_response(const char *json, tide_data_t *out)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        ESP_LOGE(TAG, "JSON parse failed");
        return false;
    }
    bool ok = false;

    cJSON *status = cJSON_GetObjectItem(root, "status");
    if (!cJSON_IsNumber(status) || status->valueint != 200) {
        ESP_LOGE(TAG, "API status %d", status ? status->valueint : -1);
        goto done;
    }

    cJSON *heights = cJSON_GetObjectItem(root, "heights");
    cJSON *extremes = cJSON_GetObjectItem(root, "extremes");
    if (!cJSON_IsArray(heights) || !cJSON_IsArray(extremes)) {
        ESP_LOGE(TAG, "missing heights/extremes");
        goto done;
    }

    cJSON *item;
    cJSON_ArrayForEach(item, heights) {
        cJSON *dt = cJSON_GetObjectItem(item, "dt");
        cJSON *h = cJSON_GetObjectItem(item, "height");
        if (!cJSON_IsNumber(dt) || !cJSON_IsNumber(h)) {
            continue;
        }
        if (out->n_heights == 0 && out->start == 0) {
            out->start = (int64_t)dt->valuedouble;
        }
        int64_t idx = ((int64_t)dt->valuedouble - out->start) / TIDES_STEP_SEC;
        if (idx < 0 || idx >= TIDES_MAX_HEIGHTS) {
            continue;
        }
        out->h_mm[idx] = (int16_t)lrintf((float)h->valuedouble * 1000.0f);
        if (idx + 1 > out->n_heights) {
            out->n_heights = idx + 1;
        }
    }

    cJSON_ArrayForEach(item, extremes) {
        if (out->n_extremes >= TIDES_MAX_EXTREMES) {
            break;
        }
        cJSON *dt = cJSON_GetObjectItem(item, "dt");
        cJSON *h = cJSON_GetObjectItem(item, "height");
        cJSON *type = cJSON_GetObjectItem(item, "type");
        if (cJSON_IsNumber(dt) && cJSON_IsNumber(h) && cJSON_IsString(type)) {
            int64_t edt = (int64_t)dt->valuedouble;
            if (out->n_extremes > 0 &&
                out->extremes[out->n_extremes - 1].dt >= edt) {
                continue;  // chunk-boundary duplicate
            }
            tide_extreme_t *e = &out->extremes[out->n_extremes++];
            e->dt = edt;
            e->h_mm = (int16_t)lrintf((float)h->valuedouble * 1000.0f);
            e->high = strcmp(type->valuestring, "High") == 0;
        }
    }

    ok = true;
done:
    cJSON_Delete(root);
    return ok;
}

static void chunk_url(char *url, size_t size, time_t chunk_day, int days)
{
    struct tm tm;
    localtime_r(&chunk_day, &tm);
    snprintf(url, size,
             "https://www.worldtides.info/api/v3?heights&extremes"
             "&date=%04d-%02d-%02d&days=%d&datum=CD"
             "&lat=" TIDE_LAT "&lon=" TIDE_LON "&key=" TIDE_API_KEY,
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, days);
}

bool tides_fetch(tide_data_t *out, time_t start_day)
{
    char *buf = malloc(RESPONSE_MAX);
    if (buf == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    char url[256];
    chunk_url(url, sizeof(url), start_day, TIDES_FETCH_CHUNK_DAYS);
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 20000,
        .keep_alive_enable = true,
    };
    // One client for all chunks: the connection (and TLS session) is
    // reused across requests to the same host.
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    bool ok = client != NULL;

    for (int d = 0; d < TIDES_DAYS && ok; d += TIDES_FETCH_CHUNK_DAYS) {
        int days = TIDES_DAYS - d;
        if (days > TIDES_FETCH_CHUNK_DAYS) {
            days = TIDES_FETCH_CHUNK_DAYS;
        }
        chunk_url(url, sizeof(url), start_day + (time_t)d * TIDES_DAY_SEC,
                  days);
        esp_http_client_set_url(client, url);

        esp_err_t err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "open failed: %s", esp_err_to_name(err));
            ok = false;
            break;
        }
        esp_http_client_fetch_headers(client);
        int total = esp_http_client_read_response(client, buf,
                                                  RESPONSE_MAX - 1);
        int http_status = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "http %d, %d bytes", http_status, total);
        ok = http_status == 200 && total > 0 &&
             (buf[total] = '\0', parse_response(buf, out));
    }
    if (client != NULL) {
        esp_http_client_cleanup(client);
    }
    free(buf);

    ok = ok && out->n_heights >= TIDES_DAYS * TIDES_SAMPLES_PER_DAY;
    ESP_LOGI(TAG, "fetch %s: %d heights, %d extremes from %lld",
             ok ? "ok" : "FAILED", out->n_heights, out->n_extremes,
             (long long)out->start);
    if (!ok) {
        // Never leave a partial week behind.
        memset(out, 0, sizeof(*out));
    }
    return ok;
}

void tides_cache_load(tide_data_t *out)
{
    memset(out, 0, sizeof(*out));
    nvs_handle_t h;
    if (nvs_open("crowtides", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint32_t ver = 0;
    char loc[40] = "";
    size_t loc_size = sizeof(loc);
    size_t size = sizeof(*out);
    if (nvs_get_u32(h, "ver", &ver) == ESP_OK && ver == CACHE_VERSION &&
        nvs_get_str(h, "loc", loc, &loc_size) == ESP_OK &&
        strcmp(loc, CACHE_LOC) == 0 &&
        nvs_get_blob(h, "tides", out, &size) == ESP_OK &&
        size == sizeof(*out)) {
        ESP_LOGI(TAG, "cache loaded: %d heights from %lld", out->n_heights,
                 (long long)out->start);
    } else {
        memset(out, 0, sizeof(*out));
    }
    nvs_close(h);
}

void tides_cache_save(const tide_data_t *t)
{
    nvs_handle_t h;
    if (nvs_open("crowtides", NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "cache save failed");
        return;
    }
    nvs_set_u32(h, "ver", CACHE_VERSION);
    nvs_set_str(h, "loc", CACHE_LOC);
    nvs_set_blob(h, "tides", t, sizeof(*t));
    nvs_commit(h);
    nvs_close(h);
}

bool tides_day_range(const tide_data_t *t, time_t day_start, int *i0, int *i1)
{
    *i0 = 0;
    *i1 = 0;
    if (t->n_heights <= 0) {
        return false;
    }
    int64_t first = ((int64_t)day_start - t->start) / TIDES_STEP_SEC;
    int64_t last = first + TIDES_SAMPLES_PER_DAY;
    if (first < 0) first = 0;
    if (last > t->n_heights - 1) last = t->n_heights - 1;
    if (last <= first) {
        return false;
    }
    *i0 = (int)first;
    *i1 = (int)last;
    return true;
}

bool tides_has_day(const tide_data_t *t, time_t day_start)
{
    if (t->n_heights <= 0) {
        return false;
    }
    int64_t i0 = ((int64_t)day_start - t->start) / TIDES_STEP_SEC;
    int64_t i1 = i0 + TIDES_SAMPLES_PER_DAY;
    return i0 >= 0 && i1 < t->n_heights;
}

int tides_days_ahead(const tide_data_t *t, time_t from)
{
    if (t->n_heights <= 0) {
        return 0;
    }
    int64_t end = t->start + (int64_t)(t->n_heights - 1) * TIDES_STEP_SEC;
    int64_t days = (end - (int64_t)from) / TIDES_DAY_SEC;
    return days < 0 ? 0 : (int)days;
}
