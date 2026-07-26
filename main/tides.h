#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define TIDES_DAY_SEC 86400

// Tide predictions are deterministic, so cache a whole month and only
// refetch when the window runs low. The API bills 1 credit per 7 days
// (heights and extremes each), so a big window costs the same as fetching
// it piecemeal — but the radio only has to come up every couple of weeks.
// The cache is fetched in 7-day chunks to keep peak JSON memory small.
#define TIDES_STEP_SEC 1800
#define TIDES_DAYS 28
#define TIDES_FETCH_CHUNK_DAYS 7
#define TIDES_SAMPLES_PER_DAY (TIDES_DAY_SEC / TIDES_STEP_SEC)
#define TIDES_MAX_HEIGHTS (TIDES_DAYS * TIDES_SAMPLES_PER_DAY + 2)
#define TIDES_MAX_EXTREMES 160

typedef struct {
    int64_t dt;
    int16_t h_mm;
} tide_extreme_t;

typedef struct {
    int64_t start;  // dt of heights[0]; heights are TIDES_STEP_SEC apart
    int16_t n_heights;
    int16_t h_mm[TIDES_MAX_HEIGHTS];  // millimetres above Chart Datum
    int16_t n_extremes;
    tide_extreme_t extremes[TIDES_MAX_EXTREMES];
} tide_data_t;

// Fetch TIDES_DAYS days of heights+extremes starting at the local date of
// `start_day`. Requires WiFi. Returns false on any failure.
bool tides_fetch(tide_data_t *out, time_t start_day);

// NVS persistence. The cache is tagged with the configured coordinates,
// so a location change in config.h invalidates it. Load leaves `out`
// zeroed when there is no valid cache.
void tides_cache_load(tide_data_t *out);
void tides_cache_save(const tide_data_t *t);

// Sample index range [*i0, *i1] (inclusive) covering the day that starts
// at local midnight `day_start`, clamped to the cached data. Returns
// false (empty range) if the cache holds nothing for that day. Inline so
// hardware-free consumers (tools/render) share the exact device logic.
static inline bool tides_day_range(const tide_data_t *t, time_t day_start,
                                   int *i0, int *i1)
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

// True if the cache fully covers the day starting at `day_start`.
bool tides_has_day(const tide_data_t *t, time_t day_start);

// Whole days of coverage the cache still holds from `from` onwards.
int tides_days_ahead(const tide_data_t *t, time_t from);

// Timestamp and height (metres above Chart Datum) of sample i.
static inline time_t tides_sample_time(const tide_data_t *t, int i)
{
    return (time_t)(t->start + (int64_t)i * TIDES_STEP_SEC);
}

static inline float tides_height_m(const tide_data_t *t, int i)
{
    return t->h_mm[i] / 1000.0f;
}
