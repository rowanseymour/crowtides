// Component-private view of the framebuffer, shared by the driver's
// flush path and hardware-free consumers (tools that render on the host).
#pragma once

#include <stdint.h>

#include "epd.h"

// Row-major, 99 bytes per row (792px), bit7 = leftmost pixel, 1 = white,
// 0 = black.
#define EPD_FB_ROW_BYTES (EPD_WIDTH / 8)

extern uint8_t epd_fb[EPD_FB_ROW_BYTES * EPD_HEIGHT];
