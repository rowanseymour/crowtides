// Copy this file to config.h and fill in your own values.
// config.h is gitignored — never commit real credentials.
#pragma once

// 2.4GHz WiFi network (the ESP32-S3 has no 5GHz support).
#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"

// POSIX TZ string for the tide station's timezone. This drives the
// chart's hour axis and the midnight refresh schedule, so it should be
// the STATION's zone, not necessarily yours.
// Examples:
//   UK (GMT, BST in summer):  "GMT0BST,M3.5.0/1,M10.5.0"
//   UTC-5 fixed (Ecuador):    "<-05>5"
//   Central Europe:           "CET-1CEST,M3.5.0,M10.5.0/3"
#define TIDE_TZ "GMT0BST,M3.5.0/1,M10.5.0"

// WorldTides API key (https://www.worldtides.info/apidocs).
#define TIDE_API_KEY "00000000-0000-0000-0000-000000000000"

// Tide station coordinates in decimal degrees; the API attaches to the
// nearest station/model point. Find stations at
// https://www.worldtides.com/tidestations
#define TIDE_LAT "55.206"
#define TIDE_LON "-6.656"

// Name shown in the chart header.
#define TIDE_STATION_NAME "PORTRUSH"

// Clock format: "24" (08:00 / 20:00, hour axis 03..21) or
// "12" (8:00a / 8:00p, hour axis 3a..9p).
#define TIDE_TIME_FMT "24"
