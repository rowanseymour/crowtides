#pragma once

#include <stdbool.h>
#include <time.h>

#define TIDES_MAX_HEIGHTS  64
#define TIDES_MAX_EXTREMES 8

typedef struct {
    time_t dt;
    float height;
} tide_point_t;

typedef struct {
    time_t dt;
    float height;
    bool high;
} tide_extreme_t;

typedef struct {
    tide_point_t heights[TIDES_MAX_HEIGHTS];
    int n_heights;
    tide_extreme_t extremes[TIDES_MAX_EXTREMES];
    int n_extremes;
} tide_data_t;

// Fetch today's heights (30-min steps) and extremes from WorldTides.
// Requires WiFi to be connected. Returns false on any failure.
bool tides_fetch(tide_data_t *out);
