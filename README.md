# CrowTides

A daily tide chart on an [Elecrow CrowPanel 5.79" E-Paper HMI](https://www.elecrow.com/crowpanel-esp32-5-79-e-paper-hmi-display-with-272-792-resolution-black-white-color-driven-by-spi-interface.html)
(ESP32-S3, 792x272 dual-SSD1683 panel), built on plain ESP-IDF.

Once a day, just after midnight in the tide station's timezone, the device
wakes from deep sleep, syncs the clock over SNTP, fetches the day's tide
heights and extremes from the [WorldTides API](https://www.worldtides.info/apidocs),
draws a 24-hour chart (curve, high/low times and heights, gridlines), and
goes back to sleep. The e-paper keeps the image with essentially no power;
pressing the MENU button forces a refresh at any time. A failed refresh
(no WiFi, no time, no data) leaves the previous chart on screen and
retries half an hour later.

## Setup

1. Install [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) v5.5.x
   and activate it (`. <idf-path>/export.sh`).
2. Get a WorldTides API key and pick your station's coordinates from
   [worldtides.com/tidestations](https://www.worldtides.com/tidestations).
3. Configure:

   ```sh
   cp main/config.example.h main/config.h
   # edit main/config.h: WiFi, timezone, API key, station
   ```

4. Build and flash:

   ```sh
   idf.py set-target esp32s3
   idf.py -p <port> flash monitor
   ```

## Notes

- Tide heights are requested relative to Chart Datum (`datum=CD`), the
  convention used on tide tables; one fetch of heights + extremes costs
  2 API credits per day.
- The vertical scale fits each day's range; the horizontal scale is fixed,
  midnight to midnight in the station's timezone.
- The display driver lives in
  [components/crowpanel579](components/crowpanel579) and is reusable for
  other apps on this board. The dual-controller panel has several sharp
  edges (RAM bank swapping after every update, waveforms that can only
  clear or only draw, a seam byte shared by both controllers) — read
  [components/crowpanel579/HARDWARE.md](components/crowpanel579/HARDWARE.md)
  before touching it.
