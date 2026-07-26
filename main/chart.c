#include "chart.h"

#include <stdio.h>
#include <string.h>

#include "epd.h"
#include "text.h"

#include "config.h"

// Layout, top to bottom: header strip; frameless chart whose bottom
// TIMES_BAND px are a solid black band carrying the extreme times in
// white; baseline; hour labels. No y-axis — extremes carry their own
// height labels.
#define CH_LEFT   10
#define CH_RIGHT  786
#define CH_TOP    44
#define CH_BOTTOM 236
#define CH_W (CH_RIGHT - CH_LEFT)
#define CH_H (CH_BOTTOM - CH_TOP)

#define TIMES_BAND 30

// Vertical range reserves: the bottom one keeps the curve floating above
// the times band, the top one keeps sky-floating height labels inside
// the chart.
#define RESERVE_BOTTOM (TIMES_BAND + 10)
#define RESERVE_TOP    26

static int x_for(time_t t, time_t t0)
{
    if (t < t0) t = t0;
    if (t > t0 + TIDES_DAY_SEC) t = t0 + TIDES_DAY_SEC;
    return CH_LEFT + (int)((long long)(t - t0) * CH_W / TIDES_DAY_SEC);
}

static int y_for(float h, float hmin, float hmax)
{
    return CH_BOTTOM - (int)((h - hmin) / (hmax - hmin) * CH_H);
}

// Right-aligned text against the panel's right margin.
static void text_right(int y, const char *s, int scale)
{
    epd_fb_text(EPD_WIDTH - 8 - epd_fb_text_width(s, scale), y, s, scale, true);
}

// TIDE_TIME_FMT is "12" or "24"; a string so the config reads naturally,
// checked at runtime (the comparison constant-folds anyway).
static bool clock_12h(void)
{
    return strcmp(TIDE_TIME_FMT, "12") == 0;
}

// Hour-of-day for the x-axis: "03".."21", or "3a".."12".."9p" on the
// 12-hour clock (noon is bare "12").
static void fmt_axis_hour(char *buf, size_t n, int hr)
{
    if (clock_12h()) {
        if (hr == 12) {
            snprintf(buf, n, "12");
        } else {
            snprintf(buf, n, "%d%c", hr % 12, hr < 12 ? 'a' : 'p');
        }
    } else {
        snprintf(buf, n, "%02d", hr);
    }
}

// Header date, per TIDE_DATE_FMT: "ymd" 2026-07-26 (ISO, dashes),
// "dmy" 26/07/2026 or "mdy" 07/26/2026 (slashes).
static void fmt_date(char *buf, size_t n, const struct tm *tm)
{
    if (strcmp(TIDE_DATE_FMT, "dmy") == 0) {
        strftime(buf, n, "%d/%m/%Y", tm);
    } else if (strcmp(TIDE_DATE_FMT, "mdy") == 0) {
        strftime(buf, n, "%m/%d/%Y", tm);
    } else {
        strftime(buf, n, "%Y-%m-%d", tm);
    }
}

// Extreme height: heights are stored metric and only converted at
// display time, as "2.0m" or "6.7'" per TIDE_UNITS ("m" / "ft").
static void fmt_height(char *buf, size_t n, float m)
{
    if (strcmp(TIDE_UNITS, "ft") == 0) {
        snprintf(buf, n, "%.1f'", m * 3.28084f);
    } else {
        snprintf(buf, n, "%.1fm", m);
    }
}

// Clock time for tide extremes: "20:00", or "8:00p" on the 12-hour clock.
static void fmt_time(char *buf, size_t n, const struct tm *tm)
{
    if (clock_12h()) {
        int h = tm->tm_hour % 12;
        if (h == 0) h = 12;
        snprintf(buf, n, "%d:%02d%c", h, tm->tm_min,
                 tm->tm_hour < 12 ? 'a' : 'p');
    } else {
        snprintf(buf, n, "%02d:%02d", tm->tm_hour, tm->tm_min);
    }
}

// Text horizontally centred on x, clamped inside the chart.
static void text_centered(int x, int y, const char *s, int scale, bool black)
{
    int w = epd_fb_text_width(s, scale);
    int lx = x - w / 2;
    if (lx < CH_LEFT + 6) lx = CH_LEFT + 6;
    if (lx > CH_RIGHT - w - 6) lx = CH_RIGHT - w - 6;
    epd_fb_text(lx, y, s, scale, black);
}

// Dithered water below the curve: Bayer 4x4 densening with depth, so it
// meets the times band near-solid.
static const uint8_t BAYER4[4][4] = {
    { 0, 8, 2, 10 },
    { 12, 4, 14, 6 },
    { 3, 11, 1, 9 },
    { 15, 7, 13, 5 },
};

// One walk over the day's samples: dithered water below each curve
// segment, then the 3px curve stroke on top of it.
static void draw_sea(const tide_data_t *t, int i0, int i1,
                     time_t day_start, float hmin, float hmax)
{
    const int sea_bottom = CH_BOTTOM - TIMES_BAND;
    for (int i = i0; i < i1; i++) {
        int x0 = x_for(tides_sample_time(t, i), day_start);
        int y0 = y_for(tides_height_m(t, i), hmin, hmax);
        int x1 = x_for(tides_sample_time(t, i + 1), day_start);
        int y1 = y_for(tides_height_m(t, i + 1), hmin, hmax);
        // Inclusive of x1 so the water reaches the chart's right edge
        // (interior boundary columns just repaint identically).
        for (int x = x0; x <= x1; x++) {
            // Fill from yc: the 3px stroke drawn after overpaints the top
            // rows, leaving no white seam between line and water.
            int yc = x1 > x0
                ? y0 + (int)((long long)(y1 - y0) * (x - x0) / (x1 - x0))
                : y0;
            for (int y = yc; y < sea_bottom; y++) {
                int level = 2 + (y - CH_TOP) * 12 / (sea_bottom - CH_TOP);
                if (BAYER4[y % 4][x % 4] < level) {
                    epd_fb_set_pixel(x, y, true);
                }
            }
        }
        for (int d = -1; d <= 1; d++) {
            epd_fb_line(x0, y0 + d, x1, y1 + d, true);
        }
    }
}

void chart_render(const tide_data_t *t, time_t day_start, int day_offset)
{
    epd_fb_clear();

    int i0, i1;
    if (!tides_day_range(t, day_start, &i0, &i1)) {
        return;
    }

    // Fit the day's data with reserved bands top and bottom.
    float hmin = tides_height_m(t, i0), hmax = hmin;
    for (int i = i0; i <= i1; i++) {
        float h = tides_height_m(t, i);
        if (h < hmin) hmin = h;
        if (h > hmax) hmax = h;
    }
    float span = hmax - hmin + 0.2f;
    const int usable = CH_H - RESERVE_BOTTOM - RESERVE_TOP;
    hmin -= 0.1f + span * RESERVE_BOTTOM / usable;
    hmax += 0.1f + span * RESERVE_TOP / usable;

    // Header: station name at left; the VIEWED date right-aligned. When
    // browsing away from today a relative label sits under the date.
    struct tm tm;
    localtime_r(&day_start, &tm);
    char buf[48];
    epd_fb_text(8, 8, TIDE_STATION_NAME, 3, true);
    fmt_date(buf, sizeof(buf), &tm);
    if (day_offset == 0) {
        text_right(12, buf, 2);
    } else {
        text_right(4, buf, 2);
        char nbuf[24];
        const char *rel = nbuf;
        if (day_offset == 1) {
            rel = "TOMORROW";
        } else if (day_offset == -1) {
            rel = "YESTERDAY";
        } else if (day_offset > 0) {
            snprintf(nbuf, sizeof(nbuf), "IN %d DAYS", day_offset);
        } else {
            snprintf(nbuf, sizeof(nbuf), "%d DAYS AGO", -day_offset);
        }
        text_right(24, rel, 2);
    }

    // Hour labels every 3h, for all but the midnight edges (a clipped or
    // shifted label looks worse than none). The gridlines themselves are
    // drawn after the sea, below.
    for (int hr = 0; hr <= 24; hr += 3) {
        int x = x_for(day_start + (time_t)hr * 3600, day_start);
        if (hr % 24 != 0) {
            fmt_axis_hour(buf, sizeof(buf), hr);
            epd_fb_text(x - epd_fb_text_width(buf, 2) / 2,
                        CH_BOTTOM + 10, buf, 2, true);
        }
    }

    // Times band and baseline — no frame.
    epd_fb_fill_rect(CH_LEFT, CH_BOTTOM - TIMES_BAND, CH_W + 1, TIMES_BAND,
                     true);
    epd_fb_line(CH_LEFT, CH_BOTTOM, CH_RIGHT, CH_BOTTOM, true);

    // Water and curve.
    draw_sea(t, i0, i1, day_start, hmin, hmax);

    // Gridlines after the sea, sky only — they stop at the curve rather
    // than fading unevenly into the dither.
    for (int hr = 0; hr <= 24; hr += 3) {
        time_t tg = day_start + (time_t)hr * 3600;
        int x = x_for(tg, day_start);
        int gi = i0 + (int)((tg - tides_sample_time(t, i0)) / TIDES_STEP_SEC);
        if (gi < i0) gi = i0;
        if (gi > i1) gi = i1;
        int ycv = y_for(tides_height_m(t, gi), hmin, hmax);
        for (int y = CH_TOP; y < ycv - 1; y += 4) {
            epd_fb_set_pixel(x, y, true);
        }
    }

    // Extremes: heights float unboxed in the sky above each peak/trough;
    // times sit in white in the band.
    for (int i = 0; i < t->n_extremes; i++) {
        const tide_extreme_t *e = &t->extremes[i];
        if (e->dt < day_start || e->dt >= day_start + TIDES_DAY_SEC) {
            continue;
        }
        time_t edt = (time_t)e->dt;
        float eh = e->h_mm / 1000.0f;
        int x = x_for(edt, day_start);
        int y = y_for(eh, hmin, hmax);

        fmt_height(buf, sizeof(buf), eh);
        text_centered(x, y - 26, buf, 2, true);

        localtime_r(&edt, &tm);
        fmt_time(buf, sizeof(buf), &tm);
        // -22 centres the digits' ink (14px tall — the glyphs' bottom row
        // is empty) in the 30px band, not the nominal 16px text cell.
        text_centered(x, CH_BOTTOM - 22, buf, 2, false);
    }
}
