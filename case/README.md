# Wooden wall case

Laser-cut case for the CrowPanel 5.79" board: a solid front bezel over a
stack of 3mm ply frame layers, open at the back — the wall hides the
electronics, and unhooking the case exposes USB-C, reset, and the battery.
The rotary wheel and MENU/EXIT stay reachable through a thumb slot in the
right edge. Powered by a 104050 protected LiPo on the board's back
(SH1.0-2P plug — match polarity to the board's silkscreen).

Board geometry (outline, mounting holes, FPC slot, control positions) was
extracted from Elecrow's published Eagle PCB file; the display active-area
position derives from the GDEY0579T93 panel outline and was verified
against the physical board with the 1:1 template.

- `gen_template.py` → `crowtides-case-template.{pdf,svg}`: print at 100%,
  lay the bare board on it to verify geometry before cutting.
- `gen_layers.py` → `crowtides-case-layers.svg`: the cut sheet, seven
  207.9×97mm layers (red = cut, blue = engrave/labels only). Layer roles
  and the stack order are documented at the top of the script, as is
  `GLASS_X_ADJ` for shifting the bezel window if a future board revision
  moves the glass.

Assembly: glue L0+L1 (front block) and L2–L6 (back block, aligned on M3
bolts through its screw holes), felt tape around the window, board in
display-down, battery on foam + velcro, then seven #4×16 pan-head screws
join the blocks from the back — longer screws would dimple the visible
face. Hang on two wall screws 130mm apart via the keyholes.
