#!/usr/bin/env python3
"""Laser-cut layer files for the CrowTides wall case.

7 layers of 3mm ply, front view, mm units. Red = cut, blue = engrave.
Stack (front to back): L0 bezel | L1 board pocket | L2 cavity+tabs+channel
| L3-L5 cavity+channel (L5 adds keyhole head recesses) | L6 back frame
(keyholes, screw head recesses).

GLASS_X_ADJ shifts the window horizontally after the 1:1 template check:
positive moves the window toward the FPC (left) edge. Re-run after
adjusting.
"""
import math

GLASS_X_ADJ = 0.0

# case
W, H, R = 207.92, 96.97, 4.0
BORDER = 20.0
# board pocket (board 167.92x56.97 + 0.3 clearance each side)
PX1, PY1, PX2, PY2 = 19.7, 19.7, 188.22, 77.27
PR = 2.3
# bezel window (active area inset 1mm/side), board-local (15.92,5.615)
WX1 = BORDER + 15.92 - GLASS_X_ADJ
WY1 = BORDER + 5.615
WW, WH = 137.0, 45.74
# thumb channel (right edge, through L2..L6)
C1, C2 = 32.0, 65.0
TAB = 8.8          # L2 corner press-tabs
SCREWS = [(30, 10), (178, 10), (60, 87), (148, 87), (10, 48.5),
          (198, 16), (198, 81)]
KEYHOLES = [(39, 84.5), (169, 84.5)]  # circle center; slot rises 8mm

CUT, ENG = "#FF0000", "#0000FF"
SW = 0.2

def fillet_path(pts, close=True):
    """pts = [(x, y, r)] polygon in y-up coords; returns SVG d in y-up
    (caller flips). Fillets each vertex with radius r via arcs."""
    n = len(pts)
    segs = []
    for i in range(n):
        x, y, r = pts[i]
        if r <= 0:
            segs.append(("pt", x, y))
            continue
        xp, yp, _ = pts[(i - 1) % n]
        xn, yn, _ = pts[(i + 1) % n]
        v1 = (xp - x, yp - y)
        v2 = (xn - x, yn - y)
        l1 = math.hypot(*v1)
        l2 = math.hypot(*v2)
        u1 = (v1[0] / l1, v1[1] / l1)
        u2 = (v2[0] / l2, v2[1] / l2)
        ang = math.acos(max(-1, min(1, u1[0] * u2[0] + u1[1] * u2[1])))
        d = r / math.tan(ang / 2)
        p1 = (x + u1[0] * d, y + u1[1] * d)
        p2 = (x + u2[0] * d, y + u2[1] * d)
        cross = u1[0] * u2[1] - u1[1] * u2[0]
        sweep = 1 if cross < 0 else 0  # y-up; group transform mirrors this
        segs.append(("arc", p1, p2, r, sweep))
    d = []
    first = None
    for s in segs:
        if s[0] == "pt":
            cmd = "M" if not d else "L"
            d.append(f"{cmd}{s[1]:.3f},{s[2]:.3f}")
            if first is None:
                first = (s[1], s[2])
        else:
            _, p1, p2, r, sweep = s
            if not d:
                d.append(f"M{p1[0]:.3f},{p1[1]:.3f}")
                first = p1
            else:
                d.append(f"L{p1[0]:.3f},{p1[1]:.3f}")
            d.append(f"A{r},{r} 0 0 {sweep} {p2[0]:.3f},{p2[1]:.3f}")
    if close:
        d.append("Z")
    return " ".join(d)

def flip(d_pts):
    return [(x, H - y, r) for x, y, r in d_pts]

def rrect_pts(x1, y1, x2, y2, r):
    return [(x1, y1, r), (x2, y1, r), (x2, y2, r), (x1, y2, r)]

def pocket_pts_plain():
    return rrect_pts(PX1, PY1, PX2, PY2, PR)

def outer_with_channel(pocket_pts):
    """Single boundary: case outline + channel notch walking the pocket.
    pocket_pts must start at (PX2, C1) going down the right edge (y-up CCW
    traversal of the pocket as part of the outer boundary)."""
    pts = [(0, 0, R), (W, 0, R), (W, C1, 0)]
    pts += pocket_pts
    pts += [(W, C2, 0), (W, H, R), (0, H, R)]
    return pts

def pocket_walk_plain():
    # from (PX2,C1) down, around, back to (PX2,C2)
    return [(PX2, C1, 0), (PX2, PY1, PR), (PX1, PY1, PR),
            (PX1, PY2, PR), (PX2, PY2, PR), (PX2, C2, 0)]

def pocket_walk_tabs():
    t = TAB
    f = 0.8
    return [(PX2, C1, 0),
            (PX2, PY1 + t, f), (PX2 - t, PY1 + t, f), (PX2 - t, PY1, f),
            (PX1 + t, PY1, f), (PX1 + t, PY1 + t, f), (PX1, PY1 + t, f),
            (PX1, PY2 - t, f), (PX1 + t, PY2 - t, f), (PX1 + t, PY2, f),
            (PX2 - t, PY2, f), (PX2 - t, PY2 - t, f), (PX2, PY2 - t, f),
            (PX2, C2, 0)]

def keyhole_path(cx, cy):
    """Circle d7.5 with a 4mm slot rising 8mm, as one path (y-up)."""
    r, hw, rise = 3.75, 2.0, 8.0
    y_j = cy + math.sqrt(r * r - hw * hw)
    top = cy + rise
    return (f"M{cx - hw:.3f},{y_j:.3f} "
            f"L{cx - hw:.3f},{top:.3f} "
            f"A{hw},{hw} 0 0 0 {cx + hw:.3f},{top:.3f} "
            f"L{cx + hw:.3f},{y_j:.3f} "
            f"A{r},{r} 0 1 0 {cx - hw:.3f},{y_j:.3f} Z")

def yflip_path(d):
    # paths are built in y-up; emit inside a group transform instead
    return d

layers = []  # (name, [path d strings], [circles (cx,cy,r)], notes)

# L0 bezel
layers.append(("L0 bezel", [
    fillet_path(rrect_pts(0, 0, W, H, R)),
    fillet_path(rrect_pts(WX1, WY1, WX1 + WW, WY1 + WH, 1.0)),
], [], "window; no holes"))

# L1 board pocket
layers.append(("L1 board pocket", [
    fillet_path(rrect_pts(0, 0, W, H, R)),
    fillet_path(pocket_pts_plain()),
], [(x, y, 1.0) for x, y in SCREWS], "pilot holes d2"))

# L2 cavity + tabs + channel
layers.append(("L2 tabs+channel", [
    fillet_path(outer_with_channel(pocket_walk_tabs())),
], [(x, y, 1.6) for x, y in SCREWS], "corner tabs press board"))

# L3, L4
for name in ("L3 cavity", "L4 cavity"):
    layers.append((name, [
        fillet_path(outer_with_channel(pocket_walk_plain())),
    ], [(x, y, 1.6) for x, y in SCREWS], ""))

# L5 cavity + keyhole recesses
l5_paths = [fillet_path(outer_with_channel(pocket_walk_plain()))]
for cx, cy in KEYHOLES:
    l5_paths.append(fillet_path(rrect_pts(cx - 8, 79.3, cx + 8, 93.5, 2)))
layers.append(("L5 keyhole recess", l5_paths,
               [(x, y, 1.6) for x, y in SCREWS], ""))

# L6 back frame
l6_paths = [fillet_path(outer_with_channel(pocket_walk_plain()))]
for cx, cy in KEYHOLES:
    l6_paths.append(keyhole_path(cx, cy))
layers.append(("L6 back+keyholes", l6_paths,
               [(x, y, 3.0) for x, y in SCREWS], "screw heads recess here"))

# ---- sheet layout: 2 cols x 4 rows ----
GAP = 12.0
COLS = 2
SHEET_W = COLS * W + (COLS + 1) * GAP
rows = (len(layers) + COLS - 1) // COLS
SHEET_H = rows * H + (rows + 1) * GAP + 10

out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{SHEET_W:.1f}mm" '
       f'height="{SHEET_H:.1f}mm" viewBox="0 0 {SHEET_W:.1f} {SHEET_H:.1f}">']
for i, (name, paths, circles, note) in enumerate(layers):
    col, row = i % COLS, i // COLS
    tx = GAP + col * (W + GAP)
    ty = GAP + row * (H + GAP)
    # y-up geometry -> flip inside group
    out.append(f'<g transform="translate({tx:.2f},{ty + H:.2f}) scale(1,-1)">')
    for d in paths:
        out.append(f'<path d="{d}" fill="none" stroke="{CUT}" stroke-width="{SW}"/>')
    for cx, cy, r in circles:
        out.append(f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="{r}" fill="none" '
                   f'stroke="{CUT}" stroke-width="{SW}"/>')
    out.append('</g>')
    label = name + (f" - {note}" if note else "")
    out.append(f'<text x="{tx + 3:.2f}" y="{ty + H - 3:.2f}" font-size="4" '
               f'font-family="Helvetica,Arial" fill="{ENG}">{label}</text>')
out.append("</svg>")

import pathlib
p = pathlib.Path(__file__).parent / "crowtides-case-layers.svg"
p.write_text("\n".join(out))
print("wrote", p)
