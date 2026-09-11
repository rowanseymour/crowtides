// Renders the real chart_render() on the host with synthetic tide data
// and dumps a PBM — for previewing chart changes without flashing, and
// for pixel-diffing refactors (render before and after, then `cmp`).
//
//   make && ./render out.pbm
//
// Uses main/config.h, so the preview reflects the configured station
// name, timezone, and formats. Override the high-tide phase to exercise
// edge cases, e.g. a high exactly at midnight:
//
//   make clean render CFLAGS_EXTRA='-DT_HIGH=0.0'
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "chart.h"
#include "config.h"

void epd_stub_dump(const char *path);

// Semidiurnal sinusoid, mm above datum, with a range that decays through
// the day so successive highs/lows differ like real tides. All the knobs
// are overridable via CFLAGS_EXTRA, e.g. -DT_HIGH=0.0 for a high exactly
// at midnight, or mean/amplitude to mimic a particular station.
#define PERIOD (12.42 * 3600.0)
#ifndef T_HIGH
#define T_HIGH (4.4 * 3600.0)
#endif
#ifndef MEAN_MM
#define MEAN_MM 1800
#endif
#ifndef AMP_MM
#define AMP_MM 1600
#endif
#ifndef AMP_DECAY_MM
#define AMP_DECAY_MM 400
#endif

static double height_mm(double t)
{
    double amp = AMP_MM - AMP_DECAY_MM * (t / TIDES_DAY_SEC);
    return MEAN_MM + amp * cos(2 * M_PI * (t - T_HIGH) / PERIOD);
}

// The rendered day; must be a local midnight in the configured TIDE_TZ,
// so override it when rendering for a different timezone.
#ifndef DAY_START
#define DAY_START 1785042000  // 2026-07-26 00:00 UTC-5
#endif

int main(int argc, char **argv)
{
#ifdef _WIN32
    _putenv_s("TZ", TIDE_TZ);
#else
    setenv("TZ", TIDE_TZ, 1);
#endif
    tzset();

    static tide_data_t d;
    time_t day_start = DAY_START;
    d.start = day_start;
    d.n_heights = TIDES_SAMPLES_PER_DAY + 1;
    for (int i = 0; i < d.n_heights; i++) {
        d.h_mm[i] = (int16_t)height_mm((double)(i * TIDES_STEP_SEC));
    }
    // True extremes of the sinusoid across the rendered day.
    for (double t = T_HIGH - 2 * PERIOD; t < TIDES_DAY_SEC; t += PERIOD / 2) {
        if (t < 0) continue;
        d.extremes[d.n_extremes].dt = day_start + (int64_t)t;
        d.extremes[d.n_extremes].h_mm = (int16_t)height_mm(t);
        d.n_extremes++;
    }

    // Synthetic forecast for the rendered day, to preview the header
    // weather line and the per-extreme rain% labels.
    static weather_data_t w;
    w.n_days = 1;
    w.days[0].date = day_start;
    w.days[0].temp_hi = 74;
    w.days[0].temp_lo = 61;
    w.days[0].rain_pct = 20;
    w.hourly_start = day_start;
    w.n_hours = 24;
    for (int h = 0; h < 24; h++) {
        // A swing from low to high and back, so different extremes show
        // visibly different rain% in the preview.
        w.hourly_rain[h] = (uint8_t)(50 + 45 * cos(2 * M_PI * h / 24.0));
    }

    chart_render(&d, &w, day_start, 0);
    epd_stub_dump(argc > 1 ? argv[1] : "out.pbm");
    return 0;
}
