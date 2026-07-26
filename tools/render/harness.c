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

// Minimal stand-in for tides.c: clamp the day to the sample array.
bool tides_day_range(const tide_data_t *t, time_t day_start, int *i0, int *i1)
{
    long off = (long)((day_start - (time_t)t->start) / TIDES_STEP_SEC);
    long end = off + TIDES_SAMPLES_PER_DAY;
    if (off < 0) off = 0;
    if (end > t->n_heights - 1) end = t->n_heights - 1;
    *i0 = (int)off;
    *i1 = (int)end;
    return *i1 > *i0;
}

// Semidiurnal sinusoid, mm above datum. T_HIGH sets when the highs fall.
#define PERIOD (12.42 * 3600.0)
#ifndef T_HIGH
#define T_HIGH (4.4 * 3600.0)
#endif

static double height_mm(double t)
{
    return 1800 + 1600 * cos(2 * M_PI * (t - T_HIGH) / PERIOD);
}

int main(int argc, char **argv)
{
    setenv("TZ", TIDE_TZ, 1);
    tzset();

    static tide_data_t d;
    time_t day_start = 1785042000;  // an arbitrary fixed local midnight
    d.start = day_start - TIDES_DAY_SEC;
    d.n_heights = 3 * TIDES_SAMPLES_PER_DAY + 1;
    for (int i = 0; i < d.n_heights; i++) {
        double t = (double)(i * TIDES_STEP_SEC) - TIDES_DAY_SEC;
        d.h_mm[i] = (int16_t)height_mm(t);
    }
    // True extremes of the sinusoid across the rendered day.
    for (double t = T_HIGH - 2 * PERIOD; t < 2.0 * TIDES_DAY_SEC;
         t += PERIOD / 2) {
        if (t < 0 || t >= TIDES_DAY_SEC) continue;
        d.extremes[d.n_extremes].dt = day_start + (int64_t)t;
        d.extremes[d.n_extremes].h_mm = (int16_t)height_mm(t);
        d.n_extremes++;
    }

    chart_render(&d, day_start, 0);
    epd_stub_dump(argc > 1 ? argv[1] : "out.pbm");
    return 0;
}
