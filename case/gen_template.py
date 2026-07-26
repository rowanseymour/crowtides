#!/usr/bin/env python3
"""Generate a 1:1 verification template (PDF + SVG) for the CrowTides
wooden case. Front view (looking at the display). All units mm.

Geometry source: Elecrow Eagle .brd (board outline, holes, FPC slot,
control positions) + GDEY0579T93 published outline/active area.
Front-view X = 210.84 - eagle_x ; Y-up = eagle_y - 2.29.
"""

MM = 72 / 25.4  # pt per mm

# ---- board-local geometry, front view, y-up, origin board bottom-left ----
BOARD_W, BOARD_H = 167.92, 56.97
GLASS = (8.96, 0.0, 159.88, 56.97)        # est +/-1.5mm in X
ACTIVE = (14.92, 4.615, 153.92, 52.355)   # 139.00 x 47.74
WINDOW = (15.92, 5.615, 152.92, 51.355)   # active inset 1mm per side
SLOT = (6.97, 11.43, 8.96, 47.53)         # FPC pass-through slot
HOLES = [(4.21, 4.22), (4.21, 52.73), (163.71, 4.22), (163.71, 52.73),
         (33.97, 28.49), (133.97, 28.49)]  # 3.2mm drill (M3)
RIGHT_EDGE = [("MENU", 41.07), ("WHEEL", 28.47), ("EXIT", 15.92)]
BOTTOM_EDGE = [("USB-C", 47.93, 9.0), ("BAT", 32.0, 4.5)]

BORDER = 20.0
CASE_W, CASE_H = BOARD_W + 2 * BORDER, BOARD_H + 2 * BORDER

PAGE_W, PAGE_H = 297.0, 210.0  # A4 landscape
OX = (PAGE_W - CASE_W) / 2     # case origin on page
OY = (PAGE_H - CASE_H) / 2 + 12
BX, BY = OX + BORDER, OY + BORDER  # board origin on page

GRAY = 0.45

prims = []  # (kind, ...)

def rect(x, y, w, h, dash=None, gray=0.0, rx=0.0, lw=0.35):
    prims.append(("rect", x, y, w, h, dash, gray, rx, lw))

def line(x1, y1, x2, y2, dash=None, gray=0.0, lw=0.35):
    prims.append(("line", x1, y1, x2, y2, dash, gray, lw))

def circle(cx, cy, r, dash=None, gray=0.0, lw=0.35):
    prims.append(("circle", cx, cy, r, dash, gray, lw))

def text(x, y, s, size=2.8, gray=0.0, anchor="start"):
    prims.append(("text", x, y, s, size, gray, anchor))

def brect(r, **kw):
    x1, y1, x2, y2 = r
    rect(BX + x1, BY + y1, x2 - x1, y2 - y1, **kw)

# case + board
rect(OX, OY, CASE_W, CASE_H, rx=4.0, lw=0.5)
rect(BX, BY, BOARD_W, BOARD_H, rx=2.0)
brect(GLASS, dash=(3, 2), gray=GRAY)
brect(ACTIVE, dash=(1.2, 1.2), gray=GRAY)
brect(WINDOW, lw=0.6)
brect(SLOT, gray=GRAY)
for hx, hy in HOLES:
    circle(BX + hx, BY + hy, 1.6)
    line(BX + hx - 2.6, BY + hy, BX + hx + 2.6, BY + hy, gray=GRAY, lw=0.2)
    line(BX + hx, BY + hy - 2.6, BX + hx, BY + hy + 2.6, gray=GRAY, lw=0.2)
for name, y in RIGHT_EDGE:
    line(BX + BOARD_W, BY + y, BX + BOARD_W + 6, BY + y, gray=GRAY)
    text(BX + BOARD_W + 7, BY + y - 1.0, name, gray=GRAY)
for name, x, w in BOTTOM_EDGE:
    line(BX + x - w / 2, BY - 3, BX + x + w / 2, BY - 3, lw=0.9, gray=GRAY)
    text(BX + x - w / 2, BY - 7, name, gray=GRAY)

# labels
text(OX, OY + CASE_H + 14, "CrowTides case template  -  print at 100% (no fit-to-page)", 4.0)
text(OX, OY + CASE_H + 8,
     "solid: board outline + bezel window (cut) | dashed: glass edge (est.) | dotted: active area", 2.8, gray=GRAY)
text(OX, OY + CASE_H + 3.5,
     "check: lay bare board on outline, display facing up; glass border and USB-C/BAT/wheel positions should match", 2.8, gray=GRAY)

# scale bar
SBY = OY - 10
line(OX, SBY, OX + 100, SBY, lw=0.5)
for i in range(0, 101, 10):
    line(OX + i, SBY, OX + i, SBY + (3 if i % 50 == 0 else 2), lw=0.35)
text(OX + 102, SBY - 0.8, "100 mm - verify with a ruler", 2.8)

# ---------------- SVG ----------------
def svg_y(y):
    return PAGE_H - y

parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{PAGE_W}mm" height="{PAGE_H}mm" '
         f'viewBox="0 0 {PAGE_W} {PAGE_H}">',
         '<rect width="100%" height="100%" fill="white"/>']
for p in prims:
    if p[0] == "rect":
        _, x, y, w, h, dash, gray, rx, lw = p
        d = f' stroke-dasharray="{dash[0]} {dash[1]}"' if dash else ""
        parts.append(f'<rect x="{x:.3f}" y="{svg_y(y + h):.3f}" width="{w:.3f}" height="{h:.3f}" '
                     f'rx="{rx}" fill="none" stroke="rgb({int(gray*255)},{int(gray*255)},{int(gray*255)})" '
                     f'stroke-width="{lw}"{d}/>')
    elif p[0] == "line":
        _, x1, y1, x2, y2, dash, gray, lw = p
        d = f' stroke-dasharray="{dash[0]} {dash[1]}"' if dash else ""
        parts.append(f'<line x1="{x1:.3f}" y1="{svg_y(y1):.3f}" x2="{x2:.3f}" y2="{svg_y(y2):.3f}" '
                     f'stroke="rgb({int(gray*255)},{int(gray*255)},{int(gray*255)})" stroke-width="{lw}"{d}/>')
    elif p[0] == "circle":
        _, cx, cy, r, dash, gray, lw = p
        parts.append(f'<circle cx="{cx:.3f}" cy="{svg_y(cy):.3f}" r="{r}" fill="none" '
                     f'stroke="rgb({int(gray*255)},{int(gray*255)},{int(gray*255)})" stroke-width="{lw}"/>')
    elif p[0] == "text":
        _, x, y, s, size, gray, anchor = p
        parts.append(f'<text x="{x:.3f}" y="{svg_y(y):.3f}" font-family="Helvetica,Arial" '
                     f'font-size="{size}" fill="rgb({int(gray*255)},{int(gray*255)},{int(gray*255)})">{s}</text>')
parts.append("</svg>")

# ---------------- PDF ----------------
K = 0.5522847498

def pdf_rrect(x, y, w, h, rx):
    if rx <= 0:
        return f"{x*MM:.2f} {y*MM:.2f} {w*MM:.2f} {h*MM:.2f} re\n"
    x2, y2 = x + w, y + h
    c = rx * K
    def pt(px, py):
        return f"{px*MM:.2f} {py*MM:.2f}"
    return (f"{pt(x+rx, y)} m\n"
            f"{pt(x2-rx, y)} l\n"
            f"{pt(x2-rx+c, y)} {pt(x2, y+rx-c)} {pt(x2, y+rx)} c\n"
            f"{pt(x2, y2-rx)} l\n"
            f"{pt(x2, y2-rx+c)} {pt(x2-rx+c, y2)} {pt(x2-rx, y2)} c\n"
            f"{pt(x+rx, y2)} l\n"
            f"{pt(x+rx-c, y2)} {pt(x, y2-rx+c)} {pt(x, y2-rx)} c\n"
            f"{pt(x, y+rx)} l\n"
            f"{pt(x, y+rx-c)} {pt(x+rx-c, y)} {pt(x+rx, y)} c\nh\n")

ops = []
for p in prims:
    if p[0] in ("rect", "line", "circle"):
        if p[0] == "rect":
            dash, gray, lw = p[5], p[6], p[8]
        elif p[0] == "line":
            dash, gray, lw = p[5], p[6], p[7]
        else:
            dash, gray, lw = p[4], p[5], p[6]
        ops.append(f"{gray:.2f} G {lw*MM:.2f} w\n")
        ops.append(f"[{dash[0]*MM:.1f} {dash[1]*MM:.1f}] 0 d\n" if dash else "[] 0 d\n")
        if p[0] == "rect":
            _, x, y, w, h, *_r = p
            ops.append(pdf_rrect(x, y, w, h, p[7]) + "S\n")
        elif p[0] == "line":
            _, x1, y1, x2, y2, *_r = p
            ops.append(f"{x1*MM:.2f} {y1*MM:.2f} m {x2*MM:.2f} {y2*MM:.2f} l S\n")
        else:
            _, cx, cy, r, *_r = p
            c = r * K
            ops.append(f"{(cx+r)*MM:.2f} {cy*MM:.2f} m\n"
                       f"{(cx+r)*MM:.2f} {(cy+c)*MM:.2f} {(cx+c)*MM:.2f} {(cy+r)*MM:.2f} {cx*MM:.2f} {(cy+r)*MM:.2f} c\n"
                       f"{(cx-c)*MM:.2f} {(cy+r)*MM:.2f} {(cx-r)*MM:.2f} {(cy+c)*MM:.2f} {(cx-r)*MM:.2f} {cy*MM:.2f} c\n"
                       f"{(cx-r)*MM:.2f} {(cy-c)*MM:.2f} {(cx-c)*MM:.2f} {(cy-r)*MM:.2f} {cx*MM:.2f} {(cy-r)*MM:.2f} c\n"
                       f"{(cx+c)*MM:.2f} {(cy-r)*MM:.2f} {(cx+r)*MM:.2f} {(cy-c)*MM:.2f} {(cx+r)*MM:.2f} {cy*MM:.2f} c\nh S\n")
    else:
        _, x, y, s, size, gray, anchor = p
        esc = s.replace("\\", r"\\").replace("(", r"\(").replace(")", r"\)")
        ops.append(f"BT {gray:.2f} g /F1 {size*MM:.2f} Tf {x*MM:.2f} {y*MM:.2f} Td ({esc}) Tj ET 0 g\n")

stream = "".join(ops).encode()
objs = [
    b"<</Type/Catalog/Pages 2 0 R>>",
    b"<</Type/Pages/Kids[3 0 R]/Count 1>>",
    f"<</Type/Page/Parent 2 0 R/MediaBox[0 0 {PAGE_W*MM:.2f} {PAGE_H*MM:.2f}]"
    f"/Contents 4 0 R/Resources<</Font<</F1 5 0 R>>>>>>".encode(),
    b"<</Length " + str(len(stream)).encode() + b">>\nstream\n" + stream + b"endstream",
    b"<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>",
]
out = bytearray(b"%PDF-1.4\n")
offsets = []
for i, o in enumerate(objs, 1):
    offsets.append(len(out))
    out += f"{i} 0 obj\n".encode() + o + b"\nendobj\n"
xref = len(out)
out += f"xref\n0 {len(objs)+1}\n0000000000 65535 f \n".encode()
for off in offsets:
    out += f"{off:010d} 00000 n \n".encode()
out += (f"trailer\n<</Size {len(objs)+1}/Root 1 0 R>>\nstartxref\n{xref}\n%%EOF").encode()

import pathlib
d = pathlib.Path(__file__).parent
(d / "crowtides-case-template.svg").write_text("\n".join(parts))
(d / "crowtides-case-template.pdf").write_bytes(bytes(out))
print("wrote", d / "crowtides-case-template.pdf", len(out), "bytes")
