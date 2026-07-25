"""Python twin of the crowpanel579 framebuffer API, for mocking up screen
layouts as PNGs without flashing the device.

Mirrors the C API pixel-for-pixel: same 792x272 1-bit canvas, the same
font8x8 glyphs (parsed from the component's header), and the same
convention (black=True draws ink). Anything designed here renders
identically on the panel, minus e-paper physics.

Usage:
    from epdsim import FB
    fb = FB()
    fb.text(8, 8, "HELLO", 3)
    fb.line(0, 100, 791, 100)
    fb.save("mock.png")

Requires Pillow. Run directly for a quick self-test:
    python epdsim.py out.png
"""
import os
import re

from PIL import Image

WIDTH, HEIGHT = 792, 272

# e-paper-ish rendering colors
INK = (30, 30, 30)
PAPER = (229, 231, 225)

def _load_font():
    path = os.path.join(os.path.dirname(__file__), '..', 'font8x8_basic.h')
    src = open(path).read()
    rows = re.findall(
        r'\{\s*((?:0x[0-9A-Fa-f]{2},\s*){7}0x[0-9A-Fa-f]{2})\s*\}', src)
    return {i: [int(v, 16) for v in re.findall(r'0x[0-9A-Fa-f]{2}', r)]
            for i, r in enumerate(rows[:128])}

_FONT = _load_font()

class FB:
    """1-bit framebuffer with the epd_fb_* drawing primitives."""

    def __init__(self):
        self.clear()

    # epd_fb_clear
    def clear(self):
        self.px = [[0] * WIDTH for _ in range(HEIGHT)]

    # epd_fb_set_pixel
    def set(self, x, y, black=True):
        if 0 <= x < WIDTH and 0 <= y < HEIGHT:
            self.px[y][x] = 1 if black else 0

    # epd_fb_fill_rect
    def rect(self, x, y, w, h, black=True):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.set(xx, yy, black)

    # epd_fb_line (Bresenham, identical to the C implementation)
    def line(self, x0, y0, x1, y1, black=True):
        dx, sx = abs(x1 - x0), 1 if x0 < x1 else -1
        dy, sy = -abs(y1 - y0), 1 if y0 < y1 else -1
        err = dx + dy
        while True:
            self.set(x0, y0, black)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    # epd_fb_text: font8x8 scaled by an integer factor; returns the x
    # position after the last glyph, like the C version.
    def text(self, x, y, s, scale, black=True):
        for ch in s:
            glyph = _FONT.get(ord(ch), _FONT[ord('?')])
            for gy in range(8):
                for gx in range(8):
                    if glyph[gy] & (1 << gx):
                        self.rect(x + gx * scale, y + gy * scale,
                                  scale, scale, black)
            x += 8 * scale
        return x

    def save(self, path, upscale=2):
        img = Image.new('RGB', (WIDTH, HEIGHT))
        p = img.load()
        for y in range(HEIGHT):
            for x in range(WIDTH):
                p[x, y] = INK if self.px[y][x] else PAPER
        if upscale != 1:
            img = img.resize((WIDTH * upscale, HEIGHT * upscale),
                             Image.NEAREST)
        img.save(path)

if __name__ == '__main__':
    import sys
    fb = FB()
    fb.text(8, 8, 'CROWPANEL 5.79', 3)
    fb.text(8, 48, 'the quick brown fox jumps over the lazy dog', 2)
    fb.rect(8, 80, 100, 60)
    fb.line(120, 80, 220, 140)
    fb.line(120, 140, 220, 80)
    fb.save(sys.argv[1] if len(sys.argv) > 1 else 'epdsim_test.png')
