#include "text.h"

#include <string.h>

#include "epd.h"
#include "font8x8_basic.h"

int epd_fb_text_width(const char *s, int scale)
{
    return (int)strlen(s) * 8 * scale;
}

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
                epd_fb_fill_rect(x + gx * scale, y + gy * scale,
                                 scale, scale, black);
            }
        }
        x += 8 * scale;
    }
    return x;
}
