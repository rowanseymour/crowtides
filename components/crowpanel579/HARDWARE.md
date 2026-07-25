# CrowPanel 5.79" (DIS08792E) hardware notes

Pin assignments confirmed against the ESPHome external component
(github.com/samperk1/esphome-crowpanel-579), which drives this exact board.

## E-paper (dual SSD1683, shared SPI bus)

| Signal   | GPIO |
|----------|------|
| SPI CLK  | 12   |
| SPI MOSI | 11   |
| CS       | 45   |
| DC       | 46   |
| RST      | 47   |
| BUSY     | 48 (input, pulldown) |
| PWR      | 7 (panel power enable) |

Single CS line — the two SSD1683s share the bus; master/slave selection is
done via command sequencing, not separate chip selects.

## Inputs

| Input               | GPIO |
|---------------------|------|
| MENU button         | 2    |
| EXIT button         | 1    |
| Rotary encoder UP   | 6    |
| Rotary encoder DOWN | 4    |
| Rotary encoder CLICK| 5    |

All buttons are active-low (enable pull-ups) and on RTC-capable GPIOs, so
any of them can wake the chip from deep sleep via ext1.

## Other onboard peripherals

| Peripheral | GPIO | Notes                                          |
|------------|------|------------------------------------------------|
| Power LED  | 41   | Drive low + hold through deep sleep for battery |

## Panel

- 272 x 792 physical, 1bpp mono. Landscape coordinates: x 0-791, y 0-271.
- Partial refresh works across the chip seam; uses on-chip OTP waveform
  (no custom LUT). Register 0x1A tweaks partial refresh speed
  (0x6E ~1.5s, 0x5A ~1.0s).
- Always do one full refresh on first boot to establish a clean baseline;
  periodic full refresh clears ghosting.

## Driver lessons (a long night's debugging, distilled)

**The master key: the controller swaps its two RAM banks' roles after
EVERY display update.** Any bank not rewritten after an update holds stale
data with unpredictable parity — the cause of a whole taxonomy of symptoms
we chased (non-window wipes, grey text, whole-panel inversion that
alternates per update, double exposures). Rule: after every update, full
or partial, rewrite BOTH banks (0x24/0xA4 and 0x26/0xA6) with the current
image (GxEPD2's `writeImageAgain` exists for exactly this).

Waveform roles (this panel's OTP LUTs are not general-purpose):
- **0xF7 (mode 1) cannot create black.** Its (old=1,new=0) entry is a
  no-op and (0,0) only weakly maintains existing black. Use it for the
  clear flash (old=00, new=FF -> solid white from any state) and as a
  "seal" pass; never expect it to draw content on white.
- **0xFF (mode 2, diff) is the only waveform that creates crisp black.**
  All content drawing goes through it, windowed.
- **0xDC displays the complement of new RAM when old RAM is 0x00** —
  Elecrow's fast path writes ~data for this reason. Avoid.
- **Command 0x21 must never be sent** — either data byte scrambles source
  output on this board (it is not wired like GxEPD2's GDEY0579T93 target).

Full refresh = three acts (see `epd_display`): F7 clear flash -> windowed
0xFF draw of the whole frame (old=FF) -> F7 seal -> rewrite both banks.

Partial refresh: window the RAM writes to the changed region only, `0x22
0xFF`, then rewrite the window into both banks. Slave seam rule: a slave
window that reaches byte 48 must extend to byte 49 (the seam-overlap
byte), or the update blacks out non-window pixels on the slave chip.

Timing: full refresh (3 acts) ~4s; windowed partial ~1.6s, no flash.
Partials verified stable across 70s idle gaps and back-to-back updates.

## Reference code

- ESPHome component clone (init sequence for the cascade):
  scratchpad copy at `esphome-crowpanel-579/components/crowpanel_579/crowpanel_579.cpp`
  Repo also bundles `docs/SSD1683_Datasheet.pdf` and `docs/CrowPanel_579_Hardware.pdf`.
