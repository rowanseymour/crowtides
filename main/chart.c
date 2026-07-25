#include "chart.h"

#include <math.h>
#include <stdio.h>

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

static int x_for(time_t t, time_t t0, time_t span)
{
    if (t < t0) t = t0;
    if (t > t0 + span) t = t0 + span;
    return CH_LEFT + (int)((long long)(t - t0) * CH_W / span);
}

static int y_for(float h, float hmin, float hmax)
{
    return CH_BOTTOM - (int)((h - hmin) / (hmax - hmin) * CH_H);
}

void chart_render(const tide_data_t *t, time_t now)
{
    epd_fb_clear();

    time_t t0 = t->heights[0].dt;
    time_t span = t->heights[t->n_heights - 1].dt - t0;
    if (span <= 0) span = 24 * 3600;

    // Dynamic height range: fit the day's data, rounded out to 0.25m.
    float hmin = t->heights[0].height, hmax = hmin;
    for (int i = 0; i < t->n_heights; i++) {
        float h = t->heights[i].height;
        if (h < hmin) hmin = h;
        if (h > hmax) hmax = h;
    }
    hmin = floorf((hmin - 0.1f) / 0.25f) * 0.25f;
    hmax = ceilf((hmax + 0.1f) / 0.25f) * 0.25f;

    // Header: station name + date (no clock — the display refreshes
    // infrequently). Heights are relative to Chart Datum.
    struct tm tm;
    localtime_r(&now, &tm);
    char buf[48];
    epd_fb_text(8, 8, TIDE_STATION_NAME, 3, true);
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    epd_fb_text(EPD_WIDTH - 8 - (int)(sizeof("0000-00-00") - 1) * 16,
                12, buf, 2, true);

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
        time_t tick = t0 + (time_t)hr * 3600;
        int x = x_for(tick, t0, span);
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
    for (int i = 0; i + 1 < t->n_heights; i++) {
        int x0 = x_for(t->heights[i].dt, t0, span);
        int y0 = y_for(t->heights[i].height, hmin, hmax);
        int x1 = x_for(t->heights[i + 1].dt, t0, span);
        int y1 = y_for(t->heights[i + 1].height, hmin, hmax);
        for (int d = -1; d <= 1; d++) {
            epd_fb_line(x0, y0 + d, x1, y1 + d, true);
        }
    }

    // Extremes: time + height labels near each peak/trough.
    for (int i = 0; i < t->n_extremes; i++) {
        const tide_extreme_t *e = &t->extremes[i];
        int x = x_for(e->dt, t0, span);
        int y = y_for(e->height, hmin, hmax);
        localtime_r(&e->dt, &tm);
        snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
        // Place labels inside the curve's wedge — below a peak, above a
        // trough. That space always exists (peaks hug the chart top with a
        // dynamic scale) and widens with distance from the vertex.
        int lx = x - 40;
        if (lx < CH_LEFT + 2) lx = CH_LEFT + 2;
        if (lx > CH_RIGHT - 82) lx = CH_RIGHT - 82;
        int ly = e->high ? y + 14 : y - 50;
        if (ly < CH_TOP + 2) ly = CH_TOP + 2;
        if (ly > CH_BOTTOM - 36) ly = CH_BOTTOM - 36;
        epd_fb_text(lx, ly, buf, 2, true);
        snprintf(buf, sizeof(buf), "%.1fm", e->height);
        epd_fb_text(lx, ly + 18, buf, 2, true);
    }

    // No current-time marker: like the clock, it would only be correct at
    // the moment of refresh.
}
