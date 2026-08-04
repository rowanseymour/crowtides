#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

// Daily hi/lo/rain for the header, plus hourly rain% (so each tide extreme
// can show the chance right at that time) — Open-Meteo, no API key
// required. Refetched once a day piggybacked on the tide module's
// existing midnight wake, since (unlike tides) forecasts go stale fast.
#define WEATHER_MAX_DAYS 8
#define WEATHER_HOURLY_STEP_SEC 3600
#define WEATHER_MAX_HOURS (WEATHER_MAX_DAYS * 24)

typedef struct {
    time_t date;      // local midnight
    int16_t temp_hi;  // degrees, F or C per TIDE_UNITS ("ft" -> F, "m" -> C)
    int16_t temp_lo;
    uint8_t rain_pct; // 0-100, daily max precipitation probability
} weather_day_t;

typedef struct {
    int n_days;
    weather_day_t days[WEATHER_MAX_DAYS];

    time_t hourly_start;  // dt of hourly_rain[0]
    int n_hours;
    uint8_t hourly_rain[WEATHER_MAX_HOURS];  // precip probability %, per hour
} weather_data_t;

// Fetch WEATHER_MAX_DAYS of daily highs/lows/rain chance starting today.
// Requires WiFi. Returns false on any failure (leaves *out untouched).
bool weather_fetch(weather_data_t *out);

// Fetch just today (n_days=1, n_hours=24) — for an on-demand "refresh
// now" that doesn't need the full multi-day pull. Caller is responsible
// for merging the result into an existing cache; this doesn't touch NVS.
bool weather_fetch_today(weather_data_t *out);

// NVS persistence, same pattern as tides_cache_load/save. The cache is
// tagged with coordinates+units, so either changing invalidates it. Load
// leaves `out` zeroed when there is no valid cache.
void weather_cache_load(weather_data_t *out);
void weather_cache_save(const weather_data_t *w);

// The forecast day matching local midnight `day_start`, or NULL if it
// falls outside the fetched window. Inline (like tides_day_range) so
// hardware-free consumers (tools/render) can use it without linking
// weather.c, which has ESP-IDF network/NVS dependencies.
static inline const weather_day_t *weather_for_day(const weather_data_t *w,
                                                    time_t day_start)
{
    for (int i = 0; i < w->n_days; i++) {
        if (w->days[i].date == day_start) {
            return &w->days[i];
        }
    }
    return NULL;
}

// Rain chance (0-100) nearest to time `t`, or -1 if outside the fetched
// hourly window. Inline for the same host-tool-reuse reason as above.
static inline int weather_rain_at(const weather_data_t *w, time_t t)
{
    if (w->n_hours <= 0) {
        return -1;
    }
    int64_t idx = ((int64_t)t - w->hourly_start +
                   WEATHER_HOURLY_STEP_SEC / 2) / WEATHER_HOURLY_STEP_SEC;
    if (idx < 0 || idx >= w->n_hours) {
        return -1;
    }
    return w->hourly_rain[idx];
}
