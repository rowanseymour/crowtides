#include "tides.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "config.h"

static const char *TAG = "tides";

#define RESPONSE_MAX (48 * 1024)

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

static bool fetch_chunk(char *buf, time_t chunk_day, int days, tide_data_t *out)
{
    struct tm tm;
    localtime_r(&chunk_day, &tm);
    char url[256];
    snprintf(url, sizeof(url),
             "https://www.worldtides.info/api/v3?heights&extremes"
             "&date=%04d-%02d-%02d&days=%d&datum=CD"
             "&lat=" TIDE_LAT "&lon=" TIDE_LON "&key=" TIDE_API_KEY,
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, days);

    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 20000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return false;
    }
    bool ok = false;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open failed: %s", esp_err_to_name(err));
        goto done;
    }
    esp_http_client_fetch_headers(client);

    int total = 0;
    while (total < RESPONSE_MAX - 1) {
        int n = esp_http_client_read(client, buf + total,
                                     RESPONSE_MAX - 1 - total);
        if (n <= 0) {
            break;
        }
        total += n;
    }
    buf[total] = '\0';

    int http_status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "http %d, %d bytes", http_status, total);
    if (http_status == 200 && total > 0) {
        ok = parse_response(buf, out);
    }

done:
    esp_http_client_cleanup(client);
    return ok;
}

bool tides_fetch(tide_data_t *out, time_t start_day)
{
    char *buf = malloc(RESPONSE_MAX);
    if (buf == NULL) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    bool ok = true;
    for (int d = 0; d < TIDES_DAYS && ok; d += TIDES_FETCH_CHUNK_DAYS) {
        int days = TIDES_DAYS - d;
        if (days > TIDES_FETCH_CHUNK_DAYS) {
            days = TIDES_FETCH_CHUNK_DAYS;
        }
        ok = fetch_chunk(buf, start_day + (time_t)d * 86400, days, out);
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
    int64_t days = (end - (int64_t)from) / 86400;
    return days < 0 ? 0 : (int)days;
}
