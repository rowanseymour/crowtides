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

#define DAY_SEC 86400

static int x_for(time_t t, time_t t0)
{
    if (t < t0) t = t0;
    if (t > t0 + DAY_SEC) t = t0 + DAY_SEC;
    return CH_LEFT + (int)((long long)(t - t0) * CH_W / DAY_SEC);
}

static int y_for(float h, float hmin, float hmax)
{
    return CH_BOTTOM - (int)((h - hmin) / (hmax - hmin) * CH_H);
}

void chart_render(const tide_data_t *t, time_t day_start, int day_offset)
{
    epd_fb_clear();

    // Sample index range for this day (inclusive endpoints).
    int i0 = (int)(((int64_t)day_start - t->start) / TIDES_STEP_SEC);
    int i1 = i0 + TIDES_SAMPLES_PER_DAY;
    if (i0 < 0) i0 = 0;
    if (i1 > t->n_heights - 1) i1 = t->n_heights - 1;
    if (i1 <= i0) {
        return;
    }

    // Dynamic height range: fit the day's data, rounded out to 0.25m.
    float hmin = t->h_mm[i0] / 1000.0f, hmax = hmin;
    for (int i = i0; i <= i1; i++) {
        float h = t->h_mm[i] / 1000.0f;
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
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    int date_x = EPD_WIDTH - 8 - (int)(sizeof("0000-00-00") - 1) * 16;
    if (day_offset == 0) {
        epd_fb_text(date_x, 12, buf, 2, true);
    } else {
        epd_fb_text(date_x, 4, buf, 2, true);
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
        epd_fb_text(EPD_WIDTH - 8 - (int)strlen(rel) * 16, 24, rel, 2, true);
    }

    // Horizontal gridlines + y labels at each 0.5m multiple (dotted).
    for (float g = ceilf(hmin / 0.5f) * 0.5f; g <= hmax + 0.01f; g += 0.5f) {
        int y = y_for(g, hmin, hmax);
        for (int x = CH_LEFT; x < CH_RIGHT; x += 4) {
            epd_fb_set_pixel(x, y, true);
        }
        snprintf(buf, sizeof(buf), "%.1f", g);
        epd_fb_text(2, y - 8, buf, 2, true);
    }

    // Vertical gridlines + hour labels every 3h.
    for (int hr = 0; hr <= 24; hr += 3) {
        time_t tick = day_start + (time_t)hr * 3600;
        int x = x_for(tick, day_start);
        for (int y = CH_TOP; y < CH_BOTTOM; y += 4) {
            epd_fb_set_pixel(x, y, true);
        }
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
        int x0 = x_for(t->start + (time_t)i * TIDES_STEP_SEC, day_start);
        int y0 = y_for(t->h_mm[i] / 1000.0f, hmin, hmax);
        int x1 = x_for(t->start + (time_t)(i + 1) * TIDES_STEP_SEC, day_start);
        int y1 = y_for(t->h_mm[i + 1] / 1000.0f, hmin, hmax);
        for (int d = -1; d <= 1; d++) {
            epd_fb_line(x0, y0 + d, x1, y1 + d, true);
        }
    }

    // Extremes within the day: time + height labels placed inside the
    // curve's wedge — below a peak, above a trough — where there is
    // always clear space.
    for (int i = 0; i < t->n_extremes; i++) {
        const tide_extreme_t *e = &t->extremes[i];
        if (e->dt < day_start || e->dt >= day_start + DAY_SEC) {
            continue;
        }
        time_t edt = (time_t)e->dt;
        float eh = e->h_mm / 1000.0f;
        int x = x_for(edt, day_start);
        int y = y_for(eh, hmin, hmax);
        localtime_r(&edt, &tm);
        snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
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
