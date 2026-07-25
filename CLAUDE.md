# CrowTides

Tide chart display running on an Elecrow CrowPanel 5.79" E-Paper HMI board.

## Hardware

- ESP32-S3, 8MB flash, 8MB octal (OPI) PSRAM
- 5.79" e-paper panel, 272x792, monochrome, SPI
- **The panel uses two SSD1683 controllers in a master/slave cascade**, each
  driving half the width. Generic single-controller SSD1683 libraries will
  only drive half the screen. Getting the cascade init sequence right is the
  main bring-up risk.
- Onboard: back button, home button, rotary switch, BAT connector,
  UART0, 2x10 GPIO header
- Pinout is not obvious from the silkscreen — get it from the Elecrow wiki
  page for this board before writing any SPI code.

## Constraints

- **ESP-IDF only.** No Arduino framework, no Arduino-as-a-component, no
  PlatformIO. Plain CMake, `idf.py build flash monitor`.
- Battery powered — deep sleep between refreshes is a requirement, not a
  nice-to-have. E-paper holds its image with no power, so the device should
  be asleep the vast majority of the time.
- Partial refresh where possible; full refresh causes a visible flash and
  is slow.

## Build order

Each step must end with something observable on the device.

1. Project skeleton; confirm target is esp32s3 and PSRAM initialises
2. SPI bus + dual SSD1683 init; screen clears fully to white
3. Framebuffer + full refresh; draw a rect at a known position
4. Partial refresh; only the changed region updates
5. Text rendering
6. WiFi + SNTP for correct local time
7. Tide data source + chart rendering
8. Deep sleep with scheduled wake

Steps 2 and 3 carry nearly all the risk. Do not move past step 3 until
partial refresh is reliable.

## Reference material

Existing community drivers handle the dual-controller init correctly. Read
them for the init sequence even though we're writing C against ESP-IDF:
- The ESPHome external component for this board (DIS08792E)
- The CrowPanel e-paper library from bukys.eu
- Elecrow's own Arduino examples — useful for register sequences and pin
  assignments, not as a dependency

## Decisions

- **Tide data source: WorldTides API** (worldtides.info/apidocs), station
  FES2022:9535 "Manta" (America/Guayaquil, UTC-5). Queried by lat/lon;
  heights (30-min steps) + extremes for one day costs 2 API credits per
  fetch. API key, coordinates, and TZ live in `main/config.h` (gitignored).

## Working style

Go step by step. Confirm each step works on real hardware before starting
the next one. Prefer small, verifiable increments over large scaffolds.

## Environment

- ESP-IDF v5.5.5 at `~/electronics/esp-idf`; activate with `. ~/electronics/esp-idf/export.sh`
