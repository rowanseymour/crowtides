#pragma once

#include <stdbool.h>

// Draw ASCII text into the framebuffer using the 8x8 font scaled by an
// integer factor. Returns the x position after the last glyph.
// Glyphs are 8*scale px wide/tall.
int epd_fb_text(int x, int y, const char *s, int scale, bool black);

// Rendered width of `s` in pixels at the given scale.
int epd_fb_text_width(const char *s, int scale);

// Same as epd_fb_text/epd_fb_text_width, but with an explicit per-glyph
// advance instead of the default 8*scale — for tightening letter spacing
// on long strings. Glyphs themselves are unchanged (still drawn at their
// normal 8*scale width), so `advance` below that starts overlapping ink.
int epd_fb_text_tracked(int x, int y, const char *s, int scale, bool black,
                        int advance);
int epd_fb_text_tracked_width(const char *s, int advance);
