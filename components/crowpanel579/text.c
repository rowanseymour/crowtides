#include "text.h"

#include "epd.h"
#include "font8x8_basic.h"

int epd_fb_text(int x, int y, const char *s, int scale, bool black)
{
    for (; *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;
        if (c > 127) {
            c = '?';
        }
        const char *glyph = font8x8_basic[c];
        for (int gy = 0; gy < 8; gy++) {
            for (int gx = 0; gx < 8; gx++) {
                if (!(glyph[gy] & (1 << gx))) {
                    continue;
                }
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        epd_fb_set_pixel(x + gx * scale + sx,
                                         y + gy * scale + sy, black);
                    }
                }
            }
        }
        x += 8 * scale;
    }
    return x;
}
