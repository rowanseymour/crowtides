#include "weather.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "nvs.h"

#include "config.h"

static const char *TAG = "weather";

// Hourly rain% for WEATHER_MAX_DAYS*24 hours dominates response size.
#define RESPONSE_MAX (16 * 1024)
#define CACHE_VERSION 2
// Cached data is only valid for the coordinates+units it was fetched for
// (units affect the stored values, unlike tide heights which convert at
// display time).
#define CACHE_LOC TIDE_LAT "," TIDE_LON "," TIDE_UNITS

static void build_url(char *url, size_t size, int days)
{
    const char *unit = strcmp(TIDE_UNITS, "ft") == 0 ? "fahrenheit" : "celsius";
    snprintf(url, size,
             "https://api.open-meteo.com/v1/forecast?"
             "latitude=" TIDE_LAT "&longitude=" TIDE_LON
             "&daily=temperature_2m_max,temperature_2m_min,"
             "precipitation_probability_max"
             "&hourly=precipitation_probability"
             "&temperature_unit=%s&timezone=auto&forecast_days=%d",
             unit, days);
}

static bool parse_date(const char *s, time_t *out)
{
    int y, m, d;
    if (sscanf(s, "%d-%d-%d", &y, &m, &d) != 3) {
        return false;
    }
    struct tm tm = { 0 };
    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = d;
    tm.tm_isdst = -1;
    *out = mktime(&tm);
    return true;
}

static bool parse_datetime(const char *s, time_t *out)
{
    int y, m, d, hh, mm;
    if (sscanf(s, "%d-%d-%dT%d:%d", &y, &m, &d, &hh, &mm) != 5) {
        return false;
    }
    struct tm tm = { 0 };
    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = d;
    tm.tm_hour = hh;
    tm.tm_min = mm;
    tm.tm_isdst = -1;
    *out = mktime(&tm);
    return true;
}

static bool parse_response(const char *json, weather_data_t *out)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        ESP_LOGE(TAG, "JSON parse failed");
        return false;
    }
    bool ok = false;

    cJSON *daily = cJSON_GetObjectItem(root, "daily");
    cJSON *time_arr = cJSON_GetObjectItem(daily, "time");
    cJSON *hi_arr = cJSON_GetObjectItem(daily, "temperature_2m_max");
    cJSON *lo_arr = cJSON_GetObjectItem(daily, "temperature_2m_min");
    cJSON *rain_arr = cJSON_GetObjectItem(daily, "precipitation_probability_max");
    if (!cJSON_IsArray(time_arr) || !cJSON_IsArray(hi_arr) ||
        !cJSON_IsArray(lo_arr) || !cJSON_IsArray(rain_arr)) {
        ESP_LOGE(TAG, "missing daily arrays");
        goto done;
    }

    int n = cJSON_GetArraySize(time_arr);
    if (n > WEATHER_MAX_DAYS) {
        n = WEATHER_MAX_DAYS;
    }
    for (int i = 0; i < n; i++) {
        cJSON *t = cJSON_GetArrayItem(time_arr, i);
        cJSON *hi = cJSON_GetArrayItem(hi_arr, i);
        cJSON *lo = cJSON_GetArrayItem(lo_arr, i);
        cJSON *rain = cJSON_GetArrayItem(rain_arr, i);
        if (!cJSON_IsString(t) || !cJSON_IsNumber(hi) ||
            !cJSON_IsNumber(lo) || !cJSON_IsNumber(rain)) {
            continue;
        }
        weather_day_t *day = &out->days[out->n_days];
        if (!parse_date(t->valuestring, &day->date)) {
            continue;
        }
        day->temp_hi = (int16_t)lrintf((float)hi->valuedouble);
        day->temp_lo = (int16_t)lrintf((float)lo->valuedouble);
        int rp = (int)lrintf((float)rain->valuedouble);
        day->rain_pct = (uint8_t)(rp < 0 ? 0 : rp > 100 ? 100 : rp);
        out->n_days++;
    }
    ok = out->n_days > 0;

    // Hourly rain% is supplementary (per-extreme display) — best-effort,
    // doesn't fail the fetch if missing or malformed.
    cJSON *hourly = cJSON_GetObjectItem(root, "hourly");
    cJSON *htime_arr = cJSON_GetObjectItem(hourly, "time");
    cJSON *hrain_arr = cJSON_GetObjectItem(hourly, "precipitation_probability");
    if (cJSON_IsArray(htime_arr) && cJSON_IsArray(hrain_arr)) {
        int hn = cJSON_GetArraySize(htime_arr);
        if (hn > WEATHER_MAX_HOURS) {
            hn = WEATHER_MAX_HOURS;
        }
        for (int i = 0; i < hn; i++) {
            cJSON *t = cJSON_GetArrayItem(htime_arr, i);
            cJSON *rain = cJSON_GetArrayItem(hrain_arr, i);
            if (!cJSON_IsString(t) || !cJSON_IsNumber(rain)) {
                continue;
            }
            time_t dt;
            if (!parse_datetime(t->valuestring, &dt)) {
                continue;
            }
            if (out->n_hours == 0) {
                out->hourly_start = dt;
            }
            int rp = (int)lrintf((float)rain->valuedouble);
            out->hourly_rain[out->n_hours] =
                (uint8_t)(rp < 0 ? 0 : rp > 100 ? 100 : rp);
            out->n_hours++;
        }
    }
done:
    cJSON_Delete(root);
    return ok;
}

static bool weather_fetch_n(weather_data_t *out, int days)
{
    memset(out, 0, sizeof(*out));

    char *buf = malloc(RESPONSE_MAX);
    if (buf == NULL) {
        return false;
    }

    char url[256];
    build_url(url, sizeof(url), days);
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 20000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    bool ok = client != NULL;

    if (ok) {
        esp_err_t err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "open failed: %s", esp_err_to_name(err));
            ok = false;
        } else {
            esp_http_client_fetch_headers(client);
            int total = esp_http_client_read_response(client, buf,
                                                       RESPONSE_MAX - 1);
            int http_status = esp_http_client_get_status_code(client);
            ESP_LOGI(TAG, "http %d, %d bytes", http_status, total);
            ok = http_status == 200 && total > 0 &&
                 (buf[total] = '\0', parse_response(buf, out));
        }
    }
    if (client != NULL) {
        esp_http_client_cleanup(client);
    }
    free(buf);

    ESP_LOGI(TAG, "fetch %s: %d days, %d hours", ok ? "ok" : "FAILED",
             out->n_days, out->n_hours);
    if (!ok) {
        memset(out, 0, sizeof(*out));
    }
    return ok;
}

bool weather_fetch(weather_data_t *out)
{
    return weather_fetch_n(out, WEATHER_MAX_DAYS);
}

bool weather_fetch_today(weather_data_t *out)
{
    return weather_fetch_n(out, 1);
}

void weather_cache_load(weather_data_t *out)
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
    if (nvs_get_u32(h, "wxver", &ver) == ESP_OK && ver == CACHE_VERSION &&
        nvs_get_str(h, "wxloc", loc, &loc_size) == ESP_OK &&
        strcmp(loc, CACHE_LOC) == 0 &&
        nvs_get_blob(h, "wx", out, &size) == ESP_OK &&
        size == sizeof(*out)) {
        ESP_LOGI(TAG, "cache loaded: %d days", out->n_days);
    } else {
        memset(out, 0, sizeof(*out));
    }
    nvs_close(h);
}

void weather_cache_save(const weather_data_t *w)
{
    nvs_handle_t h;
    if (nvs_open("crowtides", NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "cache save failed");
        return;
    }
    nvs_set_u32(h, "wxver", CACHE_VERSION);
    nvs_set_str(h, "wxloc", CACHE_LOC);
    nvs_set_blob(h, "wx", w, sizeof(*w));
    nvs_commit(h);
    nvs_close(h);
}
