// Host-side stand-in for epd.c's framebuffer: the primitives are copied
// verbatim so chart.c renders the exact pixels it would on device.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "epd.h"

#define ROW_BYTES (EPD_WIDTH / 8)
static uint8_t s_fb[ROW_BYTES * EPD_HEIGHT];

void epd_fb_clear(void)
{
    memset(s_fb, 0xFF, sizeof(s_fb));
}

void epd_fb_set_pixel(int x, int y, bool black)
{
    if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) {
        return;
    }
    uint32_t pos = y * ROW_BYTES + x / 8;
    uint8_t bit = 1 << (7 - (x % 8));
    if (black) {
        s_fb[pos] &= ~bit;
    } else {
        s_fb[pos] |= bit;
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

void epd_stub_dump(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        perror(path);
        exit(1);
    }
    fprintf(f, "P4\n%d %d\n", EPD_WIDTH, EPD_HEIGHT);
    for (size_t i = 0; i < sizeof(s_fb); i++) {
        fputc(~s_fb[i] & 0xFF, f);  // PBM 1=black, fb 0=black
    }
    fclose(f);
}
