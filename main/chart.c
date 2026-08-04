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

// Tighter-than-normal letter spacing for the station name header — it's
// often long ("Vero Marine Center"), and the font's default 8*scale
// advance leaves visible gaps beyond each glyph's own ink.
#define STATION_SCALE   3
#define STATION_ADVANCE (8 * STATION_SCALE - 4)

// Vertical range reserves: the bottom one keeps the curve floating above
// the times band, the top one keeps sky-floating height labels — and the
// rain% line stacked above them — inside the chart.
#define RESERVE_BOTTOM (TIMES_BAND + 10)
#define RESERVE_TOP    44

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
static char fmt_height(char *buf, size_t n, float m)
{
    if (strcmp(TIDE_UNITS, "ft") == 0) {
        snprintf(buf, n, "%.1f", m * 3.28084f);
        return '\'';
    }
    snprintf(buf, n, "%.1f", m);
    return 'm';
}

// Number+unit centred on x as one block, matching draw_rain. The "'"
// glyph only fills its left ~3 of 8 columns (see font8x8_basic.h), so a
// block centred on its full nominal cell width reads visually shifted
// left; nudge right by half a character cell — the unit's own "missing"
// width — to compensate. "m" fills its cell normally, so it doesn't need
// this.
static void draw_height(int x, int y, float eh)
{
    char buf[16];
    char unit = fmt_height(buf, sizeof(buf), eh);
    char ubuf[2] = { unit, '\0' };
    int num_w = epd_fb_text_width(buf, 2);
    int unit_w = epd_fb_text_width(ubuf, 2);
    int total_w = num_w + unit_w;
    int lx = x - total_w / 2;
    if (lx < CH_LEFT + 6) lx = CH_LEFT + 6;
    if (lx > CH_RIGHT - total_w - 6) lx = CH_RIGHT - total_w - 6;
    if (unit == '\'') {
        lx += unit_w / 2;
    }
    epd_fb_text(lx, y, buf, 2, true);
    epd_fb_text(lx + num_w, y, ubuf, 2, true);
}

// Daily low/high, e.g. "54/68". Rain% and the degree mark are drawn
// separately (see chart_render's header block and draw_rain) so pieces
// can be spaced out or paired with icons independently.
static void fmt_temps(char *buf, size_t n, const weather_day_t *w)
{
    snprintf(buf, n, "%d/%d", w->temp_lo, w->temp_hi);
}

// Small 8x8 pixel-art icons, drawn the same way as font glyphs (LSB =
// leftmost column) so they match the display's style. Bottom rows left
// blank like the font's own glyphs, so they sit level with adjacent text.
static void draw_glyph(int x, int y, int scale, const uint8_t bitmap[8])
{
    for (int gy = 0; gy < 8; gy++) {
        for (int gx = 0; gx < 8; gx++) {
            if (!(bitmap[gy] & (1 << gx))) {
                continue;
            }
            epd_fb_fill_rect(x + gx * scale, y + gy * scale, scale, scale,
                             true);
        }
    }
}

static const uint8_t RAINDROP[8] = {
    0x18, 0x18, 0x3C, 0x7E, 0x7E, 0x7E, 0x3C, 0x00,
};

// A small calendar page — hanging rings, a header-strip divider — for
// next to the date.
static const uint8_t CALENDAR[8] = {
    0x24, 0x7E, 0x42, 0x7E, 0x42, 0x42, 0x7E, 0x00,
};

// Small superscript ring for a temperature's degree mark — no degree
// glyph in the basic-ASCII font (U+0000-U+007F only), so hand-drawn like
// the raindrop. Ink sits in the top rows, like a real degree sign.
static const uint8_t DEGREE[8] = {
    0x1C, 0x22, 0x22, 0x1C, 0x00, 0x00, 0x00, 0x00,
};

// Raindrop + rain% centred as one block on x, same as draw_height's
// number+unit — so both stacked lines share the same centring approach.
static void draw_rain(int x, int y, int rain_pct)
{
    char rbuf[16];
    snprintf(rbuf, sizeof(rbuf), "%d%%", rain_pct);
    const int icon_w = 8 * 2, gap = 3;
    int text_w = epd_fb_text_width(rbuf, 2);
    int total_w = icon_w + gap + text_w;
    int lx = x - total_w / 2;
    if (lx < CH_LEFT + 6) lx = CH_LEFT + 6;
    if (lx > CH_RIGHT - total_w - 6) lx = CH_RIGHT - total_w - 6;
    draw_glyph(lx, y, 2, RAINDROP);
    epd_fb_text(lx + icon_w + gap, y, rbuf, 2, true);
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

void chart_render(const tide_data_t *t, const weather_data_t *w,
                  time_t day_start, int day_offset)
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
    // y=4 (not 8) so the scale-3 station name's baseline lands level with
    // the scale-2 weather/date line at y=12 — larger scale means a
    // proportionally taller blank margin below the glyphs, so matching
    // top-of-cell y would leave it looking like it sits lower.
    epd_fb_text_tracked(8, 4, TIDE_STATION_NAME, STATION_SCALE, true,
                        STATION_ADVANCE);
    fmt_date(buf, sizeof(buf), &tm);
    const char *rel = NULL;
    char nbuf[24];
    const int cal_w = 8 * 2, cal_gap = 3;
    int date_w = epd_fb_text_width(buf, 2);
    int date_x = EPD_WIDTH - 8 - date_w;
    if (day_offset == 0) {
        draw_glyph(date_x - cal_gap - cal_w, 12, 2, CALENDAR);
        epd_fb_text(date_x, 12, buf, 2, true);
    } else {
        draw_glyph(date_x - cal_gap - cal_w, 4, 2, CALENDAR);
        epd_fb_text(date_x, 4, buf, 2, true);
        rel = nbuf;
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

    // Daily lo/hi (with a degree mark on the high) and a raindrop+rain%,
    // spread evenly across whatever horizontal gap is left between the
    // station name and the date/relative-label column — both variable
    // width, so this is measured, not assumed. Silently omitted (rather
    // than truncated) if a long station name leaves no room, or the
    // forecast doesn't cover this day.
    const weather_day_t *wd = weather_for_day(w, day_start);
    if (wd != NULL) {
        int gap_left = 8 +
            epd_fb_text_tracked_width(TIDE_STATION_NAME, STATION_ADVANCE) +
            12;
        int right_edge = EPD_WIDTH - 8;
        int boundary = right_edge - date_w - cal_gap - cal_w;
        if (rel != NULL) {
            int rel_left = right_edge - epd_fb_text_width(rel, 2);
            if (rel_left < boundary) boundary = rel_left;
        }
        int gap_right = boundary - 12;

        char tbuf[16];
        fmt_temps(tbuf, sizeof(tbuf), wd);
        const int icon_w = 8 * 2, icon_gap = 3;
        int temp_text_w = epd_fb_text_width(tbuf, 2);
        int temp_w = temp_text_w + icon_w;  // number + degree mark

        char rbuf[8];
        snprintf(rbuf, sizeof(rbuf), "%d%%", wd->rain_pct);
        int rain_w = icon_w + icon_gap + epd_fb_text_width(rbuf, 2);

        int content_w = temp_w + rain_w;
        int avail = gap_right - gap_left;
        if (content_w <= avail) {
            // Space-evenly: equal gaps before, between, and after the two
            // pieces, so they spread across the gap rather than
            // clustering in the middle — more noticeable now that the
            // tracked station name leaves more room here.
            int space = (avail - content_w) / 3;
            // y=12 matches the date line's baseline (both scale-2 text),
            // vertically centred against the station name's scale-3 line.
            int tx = gap_left + space;
            epd_fb_text(tx, 12, tbuf, 2, true);
            draw_glyph(tx + temp_text_w, 12, 2, DEGREE);

            int rx = tx + temp_w + space;
            draw_glyph(rx, 12, 2, RAINDROP);
            epd_fb_text(rx + icon_w + icon_gap, 12, rbuf, 2, true);
        }
    }

    // Times band and baseline — no frame.
    epd_fb_fill_rect(CH_LEFT, CH_BOTTOM - TIMES_BAND, CH_W + 1, TIMES_BAND,
                     true);
    epd_fb_line(CH_LEFT, CH_BOTTOM, CH_RIGHT, CH_BOTTOM, true);

    // Water and curve.
    draw_sea(t, i0, i1, day_start, hmin, hmax);

    // Gridlines every 3h after the sea, sky only — they stop at the curve
    // rather than fading unevenly into the dither. Hour labels for all
    // but the midnight edges (a clipped or shifted label looks worse
    // than none).
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
        if (hr % 24 != 0) {
            fmt_axis_hour(buf, sizeof(buf), hr);
            epd_fb_text(x - epd_fb_text_width(buf, 2) / 2,
                        CH_BOTTOM + 10, buf, 2, true);
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

        draw_height(x, y - 26, eh);

        // Rain chance at this extreme's own time, stacked above the
        // height — omitted (not clamped) if outside the fetched hourly
        // window or if RESERVE_TOP somehow doesn't leave room.
        int rain = weather_rain_at(w, edt);
        if (rain >= 0 && y - 44 >= CH_TOP) {
            draw_rain(x, y - 44, rain);  // 2px gap above the height line
        }

        localtime_r(&edt, &tm);
        fmt_time(buf, sizeof(buf), &tm);
        // Centre the digits' ink (14px tall — the glyphs' bottom row is
        // empty) in the band, not the nominal 16px text cell.
        text_centered(x, CH_BOTTOM - TIMES_BAND + (TIMES_BAND - 14) / 2,
                      buf, 2, false);
    }
}
