#include "chart.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "epd.h"
#include "text.h"

#include "config.h"

// Layout: header strip on top, chart below with y labels at left and hour
// labels along the bottom.
#define CH_LEFT   68
#define CH_RIGHT  786
#define CH_TOP    44
#define CH_BOTTOM 236
#define CH_W (CH_RIGHT - CH_LEFT)
#define CH_H (CH_BOTTOM - CH_TOP)

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

static void dotted_hline(int y)
{
    for (int x = CH_LEFT; x < CH_RIGHT; x += 4) {
        epd_fb_set_pixel(x, y, true);
    }
}

static void dotted_vline(int x)
{
    for (int y = CH_TOP; y < CH_BOTTOM; y += 4) {
        epd_fb_set_pixel(x, y, true);
    }
}

// Right-aligned text against the panel's right margin.
static void text_right(int y, const char *s, int scale)
{
    epd_fb_text(EPD_WIDTH - 8 - (int)strlen(s) * 8 * scale, y, s, scale, true);
}

void chart_render(const tide_data_t *t, time_t day_start, int day_offset)
{
    epd_fb_clear();

    int i0, i1;
    if (!tides_day_range(t, day_start, &i0, &i1)) {
        return;
    }

    // Dynamic height range: fit the day's data, rounded out to 0.25m.
    float hmin = tides_height_m(t, i0), hmax = hmin;
    for (int i = i0; i <= i1; i++) {
        float h = tides_height_m(t, i);
        if (h < hmin) hmin = h;
        if (h > hmax) hmax = h;
    }
    hmin = floorf((hmin - 0.1f) / 0.25f) * 0.25f;
    hmax = ceilf((hmax + 0.1f) / 0.25f) * 0.25f;

    // Header: station name at left; the VIEWED date right-aligned. When
    // browsing away from today a relative label sits under the date.
    struct tm tm;
    localtime_r(&day_start, &tm);
    char buf[48];
    epd_fb_text(8, 8, TIDE_STATION_NAME, 3, true);
    strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    if (day_offset == 0) {
        text_right(12, buf, 2);
    } else {
        text_right(4, buf, 2);
        char rel[24];
        if (day_offset == 1) {
            snprintf(rel, sizeof(rel), "TOMORROW");
        } else if (day_offset == -1) {
            snprintf(rel, sizeof(rel), "YESTERDAY");
        } else if (day_offset > 0) {
            snprintf(rel, sizeof(rel), "IN %d DAYS", day_offset);
        } else {
            snprintf(rel, sizeof(rel), "%d DAYS AGO", -day_offset);
        }
        text_right(24, rel, 2);
    }

    // Horizontal gridlines + y labels at each 0.5m multiple (dotted).
    for (float g = ceilf(hmin / 0.5f) * 0.5f; g <= hmax + 0.01f; g += 0.5f) {
        int y = y_for(g, hmin, hmax);
        dotted_hline(y);
        snprintf(buf, sizeof(buf), "%.1f", g);
        epd_fb_text(2, y - 8, buf, 2, true);
    }

    // Vertical gridlines + hour labels every 3h.
    for (int hr = 0; hr <= 24; hr += 3) {
        int x = x_for(day_start + (time_t)hr * 3600, day_start);
        dotted_vline(x);
        snprintf(buf, sizeof(buf), "%02d", hr % 24);
        epd_fb_text(x - 16, CH_BOTTOM + 10, buf, 2, true);
    }

    // Chart frame.
    epd_fb_line(CH_LEFT, CH_TOP, CH_RIGHT, CH_TOP, true);
    epd_fb_line(CH_LEFT, CH_BOTTOM, CH_RIGHT, CH_BOTTOM, true);
    epd_fb_line(CH_LEFT, CH_TOP, CH_LEFT, CH_BOTTOM, true);
    epd_fb_line(CH_RIGHT, CH_TOP, CH_RIGHT, CH_BOTTOM, true);

    // Tide curve, 3px thick.
    for (int i = i0; i < i1; i++) {
        int x0 = x_for(tides_sample_time(t, i), day_start);
        int y0 = y_for(tides_height_m(t, i), hmin, hmax);
        int x1 = x_for(tides_sample_time(t, i + 1), day_start);
        int y1 = y_for(tides_height_m(t, i + 1), hmin, hmax);
        for (int d = -1; d <= 1; d++) {
            epd_fb_line(x0, y0 + d, x1, y1 + d, true);
        }
    }

    // Extremes within the day: time + height labels placed inside the
    // curve's wedge — below a peak, above a trough — where there is
    // always clear space.
    for (int i = 0; i < t->n_extremes; i++) {
        const tide_extreme_t *e = &t->extremes[i];
        if (e->dt < day_start || e->dt >= day_start + TIDES_DAY_SEC) {
            continue;
        }
        time_t edt = (time_t)e->dt;
        float eh = e->h_mm / 1000.0f;
        int x = x_for(edt, day_start);
        int y = y_for(eh, hmin, hmax);
        localtime_r(&edt, &tm);
        strftime(buf, sizeof(buf), "%H:%M", &tm);
        int lx = x - 40;
        if (lx < CH_LEFT + 2) lx = CH_LEFT + 2;
        if (lx > CH_RIGHT - 82) lx = CH_RIGHT - 82;
        int ly = e->high ? y + 14 : y - 50;
        if (ly < CH_TOP + 2) ly = CH_TOP + 2;
        if (ly > CH_BOTTOM - 36) ly = CH_BOTTOM - 36;
        epd_fb_text(lx, ly, buf, 2, true);
        snprintf(buf, sizeof(buf), "%.1fm", eh);
        epd_fb_text(lx, ly + 18, buf, 2, true);
    }
}
