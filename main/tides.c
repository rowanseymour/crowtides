#include "tides.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "config.h"

static const char *TAG = "tides";

#define RESPONSE_MAX (32 * 1024)

static const char *URL =
    "https://www.worldtides.info/api/v3?heights&extremes&date=today&days=1"
    "&datum=CD&lat=" TIDE_LAT "&lon=" TIDE_LON "&key=" TIDE_API_KEY;

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

    out->n_heights = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, heights) {
        if (out->n_heights >= TIDES_MAX_HEIGHTS) {
            break;
        }
        cJSON *dt = cJSON_GetObjectItem(item, "dt");
        cJSON *h = cJSON_GetObjectItem(item, "height");
        if (cJSON_IsNumber(dt) && cJSON_IsNumber(h)) {
            out->heights[out->n_heights].dt = (time_t)dt->valuedouble;
            out->heights[out->n_heights].height = (float)h->valuedouble;
            out->n_heights++;
        }
    }

    out->n_extremes = 0;
    cJSON_ArrayForEach(item, extremes) {
        if (out->n_extremes >= TIDES_MAX_EXTREMES) {
            break;
        }
        cJSON *dt = cJSON_GetObjectItem(item, "dt");
        cJSON *h = cJSON_GetObjectItem(item, "height");
        cJSON *type = cJSON_GetObjectItem(item, "type");
        if (cJSON_IsNumber(dt) && cJSON_IsNumber(h) && cJSON_IsString(type)) {
            out->extremes[out->n_extremes].dt = (time_t)dt->valuedouble;
            out->extremes[out->n_extremes].height = (float)h->valuedouble;
            out->extremes[out->n_extremes].high =
                strcmp(type->valuestring, "High") == 0;
            out->n_extremes++;
        }
    }

    ok = out->n_heights >= 2;
    ESP_LOGI(TAG, "parsed %d heights, %d extremes", out->n_heights,
             out->n_extremes);
done:
    cJSON_Delete(root);
    return ok;
}

bool tides_fetch(tide_data_t *out)
{
    esp_http_client_config_t cfg = {
        .url = URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 20000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return false;
    }

    char *buf = malloc(RESPONSE_MAX);
    bool ok = false;
    if (buf == NULL) {
        goto done;
    }

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
    free(buf);
    esp_http_client_cleanup(client);
    return ok;
}
