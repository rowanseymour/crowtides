#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

// Tide predictions are deterministic, so cache a whole month and only
// refetch when the window runs low. The API bills 1 credit per 7 days
// (heights and extremes each), so a big window costs the same as fetching
// it piecemeal — but the radio only has to come up every couple of weeks.
// The cache is fetched in 7-day chunks to keep peak JSON memory small.
#define TIDES_STEP_SEC 1800
#define TIDES_DAYS 28
#define TIDES_FETCH_CHUNK_DAYS 7
#define TIDES_SAMPLES_PER_DAY (86400 / TIDES_STEP_SEC)
#define TIDES_MAX_HEIGHTS (TIDES_DAYS * TIDES_SAMPLES_PER_DAY + 2)
#define TIDES_MAX_EXTREMES 160

typedef struct {
    int64_t dt;
    int16_t h_mm;
    bool high;
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

// True if the cache fully covers the day starting at local midnight
// `day_start`.
bool tides_has_day(const tide_data_t *t, time_t day_start);

// Whole days of coverage the cache still holds from `from` onwards.
int tides_days_ahead(const tide_data_t *t, time_t from);
