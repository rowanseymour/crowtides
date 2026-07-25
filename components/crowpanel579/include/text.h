#pragma once

#include <stdbool.h>

// Draw ASCII text into the framebuffer using the 8x8 font scaled by an
// integer factor. Returns the x position after the last glyph.
// Glyphs are 8*scale px wide/tall.
int epd_fb_text(int x, int y, const char *s, int scale, bool black);
