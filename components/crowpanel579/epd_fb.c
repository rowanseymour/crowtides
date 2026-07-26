// Framebuffer drawing primitives. Hardware-free on purpose: this file is
// also compiled outside ESP-IDF by host-side tools (tools/render) so that
// previews use the exact device pixels.
#include "epd_fb.h"

#include <stdlib.h>
#include <string.h>

uint8_t epd_fb[EPD_FB_ROW_BYTES * EPD_HEIGHT];

void epd_fb_clear(void)
{
    memset(epd_fb, 0xFF, sizeof(epd_fb));
}

void epd_fb_set_pixel(int x, int y, bool black)
{
    if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) {
        return;
    }
    uint32_t pos = y * EPD_FB_ROW_BYTES + x / 8;
    uint8_t bit = 1 << (7 - (x % 8));
    if (black) {
        epd_fb[pos] &= ~bit;
    } else {
        epd_fb[pos] |= bit;
    }
}

void epd_fb_fill_rect(int x, int y, int w, int h, bool black)
{
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            epd_fb_set_pixel(xx, yy, black);
        }
    }
}

void epd_fb_line(int x0, int y0, int x1, int y1, bool black)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        epd_fb_set_pixel(x0, y0, black);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}
