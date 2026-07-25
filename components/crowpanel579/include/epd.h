#pragma once

#include <stdbool.h>
#include <stdint.h>

// CrowPanel 5.79" DIS08792E: 792x272 mono, two SSD1683 in master/slave
// cascade on one SPI bus with a single CS. Slave is addressed by mirroring
// master commands at +0x80 (0x24 -> 0xA4 etc). See HARDWARE.md before
// changing anything in the driver.

#define EPD_WIDTH  792
#define EPD_HEIGHT 272

// Init SPI bus + both controllers. Does not touch the displayed image.
void epd_init(void);

// Clear the whole panel to white with a full refresh (visible flash),
// reliable from any stale panel/RAM state.
void epd_clear_white(void);

// Framebuffer drawing. Origin top-left, landscape 792x272.
void epd_fb_clear(void);
void epd_fb_set_pixel(int x, int y, bool black);
void epd_fb_fill_rect(int x, int y, int w, int h, bool black);
void epd_fb_line(int x0, int y0, int x1, int y1, bool black);

// Push the framebuffer to the panel with a full refresh (visible
// flashes). Deterministic from any stale state.
void epd_display(void);

// Partial refresh: push only the given framebuffer region to the panel,
// no flash. Requires a prior epd_display() as baseline. Ghosting
// accumulates — do a full epd_display() periodically.
void epd_display_partial(int x, int y, int w, int h);

// Put the panel controllers into deep sleep (needs epd_init/reset to wake).
void epd_sleep(void);

// Keep the panel's power rail ON and held through MCU deep sleep. The
// controller's own deep-sleep mode (~1uA, see epd_sleep) holds the pixels
// quiescent; cutting the rail would let the image drift grey. Call last
// before esp_deep_sleep_start(); epd_init releases the hold.
void epd_hold_power(void);
