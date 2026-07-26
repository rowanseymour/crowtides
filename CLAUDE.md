# CrowTides

Tide chart display on an Elecrow CrowPanel 5.79" e-paper board (ESP32-S3).
What it does and how to configure it: [README.md](README.md). Board pinout
and the panel driver's hard-won rules:
[components/crowpanel579/HARDWARE.md](components/crowpanel579/HARDWARE.md)
— read that before touching anything in `epd.c`; the dual-controller
panel's behavior was reverse-engineered at length and deviations that look
harmless usually aren't.

## Environment

- ESP-IDF v5.5.5 at `~/electronics/esp-idf`; activate with
  `. ~/electronics/esp-idf/export.sh`, then `idf.py build flash`.
- The board enumerates as `/dev/cu.usbserial-210`.
- `main/config.h` (gitignored) holds WiFi credentials, timezone, and the
  WorldTides key/station; copy from `main/config.example.h`.

## Working notes

- Verify display changes on the real panel — the user confirms visually.
  Before flashing, iterate on the host: `tools/render` compiles the real
  `chart.c` against stubbed framebuffer primitives and dumps a PBM
  (pixel-exact — `cmp` before/after renders to prove a refactor is a
  visual no-op), and `components/crowpanel579/tools/epdsim.py` is a
  pixel-faithful Python twin of the framebuffer API for sketching layouts
  that don't exist in C yet.
- `idf.py monitor` is interactive; to capture boot logs scriptably, open
  the port with pyserial and pulse RTS (with DTR low) to reset the board.
  Only one process can hold the serial port — a lingering background
  monitor makes flashing fail.
- WorldTides API calls spend credits (~8 per 28-day fetch). The firmware
  fetches rarely by design; keep it that way and avoid ad-hoc test calls.
- Work in small increments confirmed on hardware before moving on.
