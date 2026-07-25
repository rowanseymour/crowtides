#pragma once

#include <stdbool.h>
#include <stdint.h>

// CrowPanel 5.79" DIS08792E: 792x272 mono, two SSD1683 in master/slave
// cascade on one SPI bus with a single CS. Slave is addressed by mirroring
// master commands at +0x80 (0x24 -> 0xA4 etc).

#define EPD_WIDTH  792
#define EPD_HEIGHT 272

// Init SPI bus + both controllers, then clear the panel to white so the
// session starts from a known coherent state (one flash per call).
void epd_init(void);

// Re-run the panel init sequence (reset + registers, no flash, no clear)
// and restore both RAM banks from the current framebuffer. Call after the
// panel has been idle or in deep sleep, before a partial refresh; the
// framebuffer must hold what is physically on the panel.
void epd_wake(void);

// Clear the whole panel to white with a full refresh (visible flash).
void epd_clear_white(void);

// Framebuffer drawing. Origin top-left, landscape 792x272.
void epd_fb_clear(void);
void epd_fb_set_pixel(int x, int y, bool black);
void epd_fb_fill_rect(int x, int y, int w, int h, bool black);
void epd_fb_line(int x0, int y0, int x1, int y1, bool black);

// Push the framebuffer to the panel with a full refresh (visible flash).
void epd_display(void);

// Partial refresh: push only the given framebuffer region to the panel,
// no flash. Requires a prior epd_display() as baseline. Ghosting
// accumulates — do a full epd_display() periodically.
void epd_display_partial(int x, int y, int w, int h);

// Put the panel controllers into deep sleep (needs epd_init/reset to wake).
void epd_sleep(void);

// Cut panel power (PWR pin low) and hold it low through MCU deep sleep.
// The panel keeps its image unpowered. epd_init releases the hold.
void epd_power_off(void);
