#include "epd.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "epd";

// Pinout per Elecrow reference code (weather-crow5.7 spi.h) and the
// ESPHome crowpanel_579 component — see docs/hardware.md.
#define PIN_SCK  12
#define PIN_MOSI 11
#define PIN_CS   45
#define PIN_DC   46
#define PIN_RST  47
#define PIN_BUSY 48
#define PIN_PWR  7

// Each SSD1683 covers a 400x272 window: 50 bytes per row.
#define HALF_ROW_BYTES 50
#define HALF_BUF (HALF_ROW_BYTES * EPD_HEIGHT)

// Framebuffer: row-major, 99 bytes per row (792px), bit7 = leftmost pixel,
// 1 = white, 0 = black. Byte 49 of each row spans the chip seam and is sent
// to both controllers (slave: bytes 0-49, master: bytes 49-98).
#define ROW_BYTES (EPD_WIDTH / 8)
static uint8_t s_fb[ROW_BYTES * EPD_HEIGHT];

static spi_device_handle_t s_spi;

static void wait_busy(void)
{
    // BUSY is high while the controller is working.
    for (int i = 0; i < 1000; i++) {
        if (gpio_get_level(PIN_BUSY) == 0) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGE(TAG, "busy timeout");
}

static void spi_write(const uint8_t *data, int len)
{
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(s_spi, &t));
}

static void cmd(uint8_t c)
{
    gpio_set_level(PIN_DC, 0);
    spi_write(&c, 1);
}

static void data(uint8_t d)
{
    gpio_set_level(PIN_DC, 1);
    spi_write(&d, 1);
}

// Scratch for assembling one controller's RAM data so each RAM write is a
// single SPI transaction with CS held throughout, matching the reference
// driver's framing (data reaches the slave relayed through the cascade).
static uint8_t s_half[HALF_BUF];

// Fill one controller RAM bank (cmd 0x24/0x26 master, 0xA4/0xA6 slave)
// with a constant byte.
static void fill_ram(uint8_t ram_cmd, uint8_t fill)
{
    memset(s_half, fill, sizeof(s_half));
    cmd(ram_cmd);
    gpio_set_level(PIN_DC, 1);
    spi_write(s_half, sizeof(s_half));
}

// RAM window + address counter setup, from the ESPHome crowpanel_579
// component (seam alignment verified there on this exact panel).
// Master: right half, data entry Y-inc/X-dec, X counter starts at 49.
static void set_ram_master(void)
{
    cmd(0x11); data(0x02);
    cmd(0x44); data(0x31); data(0x00);
    cmd(0x45); data(0x00); data(0x00); data(0x0F); data(0x01);
    cmd(0x4E); data(0x31);
    cmd(0x4F); data(0x00); data(0x00);
}

// Slave: left half, data entry Y-inc/X-inc.
static void set_ram_slave(void)
{
    cmd(0x91); data(0x03);
    cmd(0xC4); data(0x00); data(0x31);
    cmd(0xC5); data(0x00); data(0x00); data(0x0F); data(0x01);
    cmd(0xCE); data(0x00);
    cmd(0xCF); data(0x00); data(0x00);
}

static void reset(void)
{
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    wait_busy();
}

static void write_fb(uint8_t master_cmd, uint8_t slave_cmd);

// Panel register init, shared by cold init and wake.
static void panel_init(void)
{
    reset();
    cmd(0x12);              // SW reset
    wait_busy();
    cmd(0x18); data(0x80);  // use internal temperature sensor
    cmd(0x22); data(0xB1);  // load temperature
    cmd(0x20);
    wait_busy();
    cmd(0x1A); data(0x64); data(0x00);  // write temperature register
    cmd(0x22); data(0x91);  // load LUT from OTP
    cmd(0x20);
    wait_busy();
    cmd(0x3C); data(0x03);  // border waveform
    wait_busy();
}

void epd_wake(void)
{
    panel_init();
    // Restore both RAM banks to match the panel (framebuffer must hold
    // the currently displayed image).
    write_fb(0x24, 0xA4);
    write_fb(0x26, 0xA6);
}

void epd_power_off(void)
{
    // Keep the panel rail ON through MCU deep sleep: the controller is in
    // its own deep-sleep mode (~1uA) which actively holds the pixels
    // quiescent. Cutting VCI leaves the panel floating and the image
    // drifts grey within minutes. Standalone-safe: may be called on boots
    // where epd_init never ran.
    gpio_hold_dis(PIN_PWR);
    gpio_config_t out = {
        .pin_bit_mask = 1ULL << PIN_PWR,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&out);
    gpio_set_level(PIN_PWR, 1);
    gpio_hold_en(PIN_PWR);
    gpio_deep_sleep_hold_en();
}

void epd_init(void)
{
    gpio_hold_dis(PIN_PWR);  // release a hold left by deep sleep

    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_DC) | (1ULL << PIN_RST) | (1ULL << PIN_PWR),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&out));
    gpio_config_t in = {
        .pin_bit_mask = 1ULL << PIN_BUSY,
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in));

    gpio_set_level(PIN_PWR, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    spi_bus_config_t bus = {
        .sclk_io_num = PIN_SCK,
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = HALF_BUF,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev = {
        .clock_speed_hz = 10 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = PIN_CS,
        .queue_size = 4,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &s_spi));

    // Init sequence common to Elecrow's examples and the ESPHome component.
    panel_init();

    // Start every session from a known state: white panel, both RAM banks
    // coherent. Costs one flash per boot but makes the first epd_display
    // deterministic regardless of what RAM survived the last session.
    epd_clear_white();

    ESP_LOGI(TAG, "init done");
}

void epd_clear_white(void)
{
    // New RAM = 0xFF (white), old RAM = 0x00, on both controllers, then a
    // full refresh — matches Elecrow's EPD_Display_Clear. This clears
    // reliably from ANY stale panel/RAM state.
    set_ram_master();
    fill_ram(0x24, 0xFF);
    set_ram_master();
    fill_ram(0x26, 0x00);
    set_ram_slave();
    fill_ram(0xA4, 0xFF);
    set_ram_slave();
    fill_ram(0xA6, 0x00);

    cmd(0x22); data(0xF7);  // full update
    cmd(0x20);
    wait_busy();
    // Old RAM deliberately stays 0x00 (Elecrow leaves it so): the F7
    // waveform on this panel drives white->black only for (old=0,new=0)
    // pairs — with old RAM all-white it never blackens anything.
    ESP_LOGI(TAG, "clear to white done");
}

void epd_fb_clear(void)
{
    memset(s_fb, 0xFF, sizeof(s_fb));
}

void epd_fb_set_pixel(int x, int y, bool black)
{
    if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) {
        return;
    }
    uint32_t pos = y * ROW_BYTES + x / 8;
    uint8_t bit = 1 << (7 - (x % 8));
    if (black) {
        s_fb[pos] &= ~bit;
    } else {
        s_fb[pos] |= bit;
    }
}

void epd_fb_fill_rect(int x, int y, int w, int h, bool black)
{
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            epd_fb_set_pixel(xx, yy, black);
        }
    }
}

void epd_fb_line(int x0, int y0, int x1, int y1, bool black)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        epd_fb_set_pixel(x0, y0, black);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

// Write the framebuffer into one RAM bank of both controllers.
// bank 0x24/0xA4 = new image, 0x26/0xA6 = old image (diff baseline).
static void write_fb(uint8_t master_cmd, uint8_t slave_cmd)
{
    // Slave: left half, bytes 0-49 of each row.
    for (int y = 0; y < EPD_HEIGHT; y++) {
        memcpy(&s_half[y * HALF_ROW_BYTES], &s_fb[y * ROW_BYTES], HALF_ROW_BYTES);
    }
    set_ram_slave();
    cmd(slave_cmd);
    gpio_set_level(PIN_DC, 1);
    spi_write(s_half, sizeof(s_half));

    // Master: right half, bytes 49-98 in normal order; its X counter
    // decrements from 49 so byte 49 lands at the seam.
    for (int y = 0; y < EPD_HEIGHT; y++) {
        memcpy(&s_half[y * HALF_ROW_BYTES],
               &s_fb[y * ROW_BYTES + HALF_ROW_BYTES - 1], HALF_ROW_BYTES);
    }
    set_ram_master();
    cmd(master_cmd);
    gpio_set_level(PIN_DC, 1);
    spi_write(s_half, sizeof(s_half));
}

// Plain full refresh: write the frame, one 0xF7 flash, then sync old RAM
// as the diff baseline for partials. Correct because epd_init always
// leaves the panel white with both RAM banks coherent (see below); never
// touch command 0x21 — either data byte of it scrambles output on this
// board — and never update with 0xDC (it shows the complement of new RAM
// when old RAM is 0x00; Elecrow's fast path writes ~data for that reason).
// Full refresh, three acts — each mode on this panel can only do one job:
//  1. F7 clear flash (old=00, new=FF): the only reliable path to solid
//     white from any state. F7 cannot CREATE black — its (1,0) entry is a
//     no-op and its (0,0) entry only maintains existing black.
//  2. 0xFF diff draw from white (old=FF): the only waveform that creates
//     crisp black.
//  3. F7 "seal" pass (old=00): visually a no-op (maintains black, re-drives
//     white bg) but leaves the controller in the state where subsequent
//     0xFF partials preserve non-window content. Partials directly after a
//     full-screen 0xFF wipe everything outside their window.
void epd_display(void)
{
    // Act 1: flash to white.
    set_ram_master();
    fill_ram(0x26, 0x00);
    set_ram_slave();
    fill_ram(0xA6, 0x00);
    set_ram_master();
    fill_ram(0x24, 0xFF);
    set_ram_slave();
    fill_ram(0xA4, 0xFF);
    cmd(0x22); data(0xF7);
    cmd(0x20);
    wait_busy();

    // Act 2: crisp draw via 0xFF diff against the white panel.
    set_ram_master();
    fill_ram(0x26, 0xFF);
    set_ram_slave();
    fill_ram(0xA6, 0xFF);
    epd_display_partial(0, 0, EPD_WIDTH, EPD_HEIGHT);

    // Act 3: seal with F7 so later partials are safe.
    set_ram_master();
    fill_ram(0x26, 0x00);
    set_ram_slave();
    fill_ram(0xA6, 0x00);
    cmd(0x22); data(0xF7);
    cmd(0x20);
    wait_busy();

    // The controller swaps RAM-bank roles after each update (hence GxEPD2's
    // writeImageAgain): rewrite BOTH banks so current and previous both
    // hold the frame, whatever the parity.
    write_fb(0x24, 0xA4);
    write_fb(0x26, 0xA6);
}

// Windowed RAM area setup for partial refresh, in framebuffer byte
// coordinates (0-49 slave, 49-98 master; byte 49 spans the seam).
static void set_window_slave(int bs, int be, int ys, int ye)
{
    cmd(0x91); data(0x03);
    cmd(0xC4); data(bs); data(be);
    cmd(0xC5); data(ys & 0xFF); data((ys >> 8) & 0x01);
               data(ye & 0xFF); data((ye >> 8) & 0x01);
    cmd(0xCE); data(bs);
    cmd(0xCF); data(ys & 0xFF); data((ys >> 8) & 0x01);
}

static void set_window_master(int bs, int be, int ys, int ye)
{
    // Master X address is inverted: X = 98 - fb byte index.
    cmd(0x11); data(0x02);
    cmd(0x44); data(98 - bs); data(98 - be);
    cmd(0x45); data(ys & 0xFF); data((ys >> 8) & 0x01);
               data(ye & 0xFF); data((ye >> 8) & 0x01);
    cmd(0x4E); data(98 - bs);
    cmd(0x4F); data(ys & 0xFF); data((ys >> 8) & 0x01);
}

static void write_window_rows(int bs, int be, int ys, int ye)
{
    int wb = be - bs + 1;
    for (int y = ys; y <= ye; y++) {
        memcpy(&s_half[(y - ys) * wb], &s_fb[y * ROW_BYTES + bs], wb);
    }
    gpio_set_level(PIN_DC, 1);
    spi_write(s_half, wb * (ye - ys + 1));
}

// Windowed partial refresh per the confirmed-working ESPHome driver for
// this board: write only the region's RAM, then update with the OTP
// waveform (0xFF). No old-RAM sync afterwards.
void epd_display_partial(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    int x_end = x + w - 1;
    int y_end = y + h - 1;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x_end > EPD_WIDTH - 1) x_end = EPD_WIDTH - 1;
    if (y_end > EPD_HEIGHT - 1) y_end = EPD_HEIGHT - 1;

    bool slave = x < 400;         // region touches the slave (left) half
    bool master = x_end >= 392;   // region touches the master (right) half

    int sbs = 0, sbe = 0, mbs = 0, mbe = 0;
    if (slave) {
        sbs = x / 8;
        sbe = (x_end < 399 ? x_end : 399) / 8;
        // Byte 49 is the slave's seam-overlap byte. A window ending at
        // byte 48 without it makes the waveform black out everything
        // outside the window on the slave chip.
        if (sbe >= 48) sbe = 49;
        set_window_slave(sbs, sbe, y, y_end);
        cmd(0xA4);
        write_window_rows(sbs, sbe, y, y_end);
    }
    if (master) {
        int px_start = x >= 392 ? x : 392;
        mbs = 49 + (px_start - 392) / 8;
        mbe = 49 + (x_end - 392) / 8;
        set_window_master(mbs, mbe, y, y_end);
        cmd(0x24);
        write_window_rows(mbs, mbe, y, y_end);
    }

    cmd(0x22); data(0xFF);
    cmd(0x20);
    wait_busy();

    // Post-update bank swap: rewrite the window into BOTH banks so both
    // hold the current image (see epd_display).
    if (slave) {
        set_window_slave(sbs, sbe, y, y_end);
        cmd(0xA4);
        write_window_rows(sbs, sbe, y, y_end);
        set_window_slave(sbs, sbe, y, y_end);
        cmd(0xA6);
        write_window_rows(sbs, sbe, y, y_end);
    }
    if (master) {
        set_window_master(mbs, mbe, y, y_end);
        cmd(0x24);
        write_window_rows(mbs, mbe, y, y_end);
        set_window_master(mbs, mbe, y, y_end);
        cmd(0x26);
        write_window_rows(mbs, mbe, y, y_end);
    }
}

void epd_sleep(void)
{
    cmd(0x10); data(0x01);
    vTaskDelay(pdMS_TO_TICKS(5));
}
