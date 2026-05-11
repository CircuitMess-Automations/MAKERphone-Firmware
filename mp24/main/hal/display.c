/*
 * mp24/main/hal/display.c — ST7735 1.77" 128x160 SPI driver
 *
 * Lifted from /mnt/project/test_display.c with light refactoring:
 *   - Pin defines pulled inline from pin_config.h (no header to
 *     re-import for now; Session S-MP03 introduces hal/pins.h).
 *   - The test-only RGB cycling helper is removed.
 *   - The 5x7 font table is kept verbatim so any code lifted from
 *     /mnt/project/dashboard.c compiles unchanged later.
 *
 * Panel quirks (from /mnt/project/HW_TEST_NOTES.md + hardware
 * verification): MADCTL=0x60 for 90° landscape, COL+1 / ROW+2 offsets,
 * SPI mode 0, 26 MHz clock, CS tied to GND so no chip-select line.
 */

#include "hal/display.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

/* ----------------------------------------------------------------- */
/* Pin assignments — from /mnt/project/pin_config.h                  */
/* ----------------------------------------------------------------- */
#define PIN_TFT_MOSI       5
#define PIN_TFT_SCK        4
#define PIN_TFT_DC         6
#define PIN_TFT_RST        7
#define PIN_TFT_CS         (-1)   /* tied to GND on the display sub-PCB */

#define TFT_COL_OFFSET     1
#define TFT_ROW_OFFSET     2

/* ST7735 command codes */
#define ST77_SWRESET       0x01
#define ST77_SLPOUT        0x11
#define ST77_NORON         0x13
#define ST77_INVOFF        0x20
#define ST77_DISPON        0x29
#define ST77_CASET         0x2A
#define ST77_RASET         0x2B
#define ST77_RAMWR         0x2C
#define ST77_MADCTL        0x36
#define ST77_COLMOD        0x3A

static const char *TAG = "DISP";
static spi_device_handle_t s_spi;

/* ----------------------------------------------------------------- */
/* Low-level helpers                                                 */
/* ----------------------------------------------------------------- */

static void st77_cmd(uint8_t cmd)
{
    gpio_set_level(PIN_TFT_DC, 0);
    spi_transaction_t t = { .length = 8, .tx_buffer = &cmd };
    spi_device_polling_transmit(s_spi, &t);
}

static void st77_data(const uint8_t *data, size_t len)
{
    if (!len) return;
    gpio_set_level(PIN_TFT_DC, 1);
    spi_transaction_t t = { .length = len * 8, .tx_buffer = data };
    spi_device_polling_transmit(s_spi, &t);
}

static void st77_data1(uint8_t d) { st77_data(&d, 1); }

static void set_window(int x0, int y0, int x1, int y1)
{
    uint8_t caset[4] = {
        (uint8_t)((x0 + TFT_COL_OFFSET) >> 8), (uint8_t)((x0 + TFT_COL_OFFSET) & 0xFF),
        (uint8_t)((x1 + TFT_COL_OFFSET) >> 8), (uint8_t)((x1 + TFT_COL_OFFSET) & 0xFF),
    };
    uint8_t raset[4] = {
        (uint8_t)((y0 + TFT_ROW_OFFSET) >> 8), (uint8_t)((y0 + TFT_ROW_OFFSET) & 0xFF),
        (uint8_t)((y1 + TFT_ROW_OFFSET) >> 8), (uint8_t)((y1 + TFT_ROW_OFFSET) & 0xFF),
    };
    st77_cmd(ST77_CASET); st77_data(caset, 4);
    st77_cmd(ST77_RASET); st77_data(raset, 4);
    st77_cmd(ST77_RAMWR);
}

/* ----------------------------------------------------------------- */
/* Init                                                              */
/* ----------------------------------------------------------------- */

esp_err_t display_init(void)
{
    /* Clear any boot-ROM strap state on the SCK pin before SPI claims it */
    gpio_reset_pin(PIN_TFT_SCK);

    spi_bus_config_t buscfg = {
        .mosi_io_num     = PIN_TFT_MOSI,
        .miso_io_num     = -1,
        .sclk_io_num     = PIN_TFT_SCK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = TFT_WIDTH * 2 * 16,   /* 16 scanlines worth */
    };
    esp_err_t r = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %d", r);
        return r;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 26 * 1000 * 1000,
        .mode           = 0,
        .spics_io_num   = PIN_TFT_CS,
        .queue_size     = 7,
    };
    r = spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %d", r);
        return r;
    }

    /* Configure DC + RST GPIOs as outputs */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_TFT_DC) | (1ULL << PIN_TFT_RST),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    /* Hardware reset pulse */
    gpio_set_level(PIN_TFT_RST, 1); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TFT_RST, 0); vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_TFT_RST, 1); vTaskDelay(pdMS_TO_TICKS(150));

    /* ST7735 init — minimal sequence proven on this exact panel */
    st77_cmd(ST77_SWRESET); vTaskDelay(pdMS_TO_TICKS(150));
    st77_cmd(ST77_SLPOUT);  vTaskDelay(pdMS_TO_TICKS(500));

    st77_cmd(ST77_COLMOD);  st77_data1(0x05);   /* 16-bit RGB565 */
    st77_cmd(ST77_MADCTL);  st77_data1(0x60);   /* 90° CW landscape (MV=1, MX=1) */

    st77_cmd(ST77_CASET);
    {
        uint8_t d[] = { 0x00, TFT_COL_OFFSET,
                        0x00, TFT_COL_OFFSET + TFT_WIDTH  - 1 };
        st77_data(d, 4);
    }
    st77_cmd(ST77_RASET);
    {
        uint8_t d[] = { 0x00, TFT_ROW_OFFSET,
                        0x00, TFT_ROW_OFFSET + TFT_HEIGHT - 1 };
        st77_data(d, 4);
    }

    st77_cmd(ST77_INVOFF);
    st77_cmd(ST77_NORON);   vTaskDelay(pdMS_TO_TICKS(10));
    st77_cmd(ST77_DISPON);  vTaskDelay(pdMS_TO_TICKS(100));

    display_fill(COLOR_BLACK);

    ESP_LOGI(TAG, "ST7735 ready (%dx%d, 16-bit RGB565, SPI2 @ 26 MHz)",
             TFT_WIDTH, TFT_HEIGHT);
    return ESP_OK;
}

/* ----------------------------------------------------------------- */
/* Primitive draw calls                                              */
/* ----------------------------------------------------------------- */

void display_fill(uint16_t color)
{
    display_fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, color);
}

void display_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > TFT_WIDTH)  w = TFT_WIDTH  - x;
    if (y + h > TFT_HEIGHT) h = TFT_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    set_window(x, y, x + w - 1, y + h - 1);

    /* Build one row of pixels in big-endian byte order, then ship it
     * once per row. h iterations of an SPI polling transmit — fine for
     * a 128x160 panel; the GUI layer will move to DMA in a later session. */
    static uint16_t line[TFT_WIDTH];
    uint16_t swapped = (color >> 8) | (color << 8);
    for (int i = 0; i < w; i++) line[i] = swapped;

    gpio_set_level(PIN_TFT_DC, 1);
    spi_transaction_t t = {
        .length    = (size_t)w * 2 * 8,
        .tx_buffer = line,
    };
    for (int row = 0; row < h; row++) {
        spi_device_polling_transmit(s_spi, &t);
    }
}

/* ----------------------------------------------------------------- */
/* 5x7 font + text rendering                                         */
/* Glyph layout: 5 column bytes, bit 0 = top row.                    */
/* Range 0x20–0x7E. Anything outside renders as '?'.                 */
/* ----------------------------------------------------------------- */

static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* ' '  */ {0x00,0x00,0x5F,0x00,0x00}, /* '!'  */
    {0x00,0x07,0x00,0x07,0x00}, /* '"'  */ {0x14,0x7F,0x14,0x7F,0x14}, /* '#'  */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* '$'  */ {0x23,0x13,0x08,0x64,0x62}, /* '%'  */
    {0x36,0x49,0x55,0x22,0x50}, /* '&'  */ {0x00,0x05,0x03,0x00,0x00}, /* '\'' */
    {0x00,0x1C,0x22,0x41,0x00}, /* '('  */ {0x00,0x41,0x22,0x1C,0x00}, /* ')'  */
    {0x14,0x08,0x3E,0x08,0x14}, /* '*'  */ {0x08,0x08,0x3E,0x08,0x08}, /* '+'  */
    {0x00,0x50,0x30,0x00,0x00}, /* ','  */ {0x08,0x08,0x08,0x08,0x08}, /* '-'  */
    {0x00,0x60,0x60,0x00,0x00}, /* '.'  */ {0x20,0x10,0x08,0x04,0x02}, /* '/'  */
    {0x3E,0x51,0x49,0x45,0x3E}, /* '0'  */ {0x00,0x42,0x7F,0x40,0x00}, /* '1'  */
    {0x42,0x61,0x51,0x49,0x46}, /* '2'  */ {0x21,0x41,0x45,0x4B,0x31}, /* '3'  */
    {0x18,0x14,0x12,0x7F,0x10}, /* '4'  */ {0x27,0x45,0x45,0x45,0x39}, /* '5'  */
    {0x3C,0x4A,0x49,0x49,0x30}, /* '6'  */ {0x01,0x71,0x09,0x05,0x03}, /* '7'  */
    {0x36,0x49,0x49,0x49,0x36}, /* '8'  */ {0x06,0x49,0x49,0x29,0x1E}, /* '9'  */
    {0x00,0x36,0x36,0x00,0x00}, /* ':'  */ {0x00,0x56,0x36,0x00,0x00}, /* ';'  */
    {0x08,0x14,0x22,0x41,0x00}, /* '<'  */ {0x14,0x14,0x14,0x14,0x14}, /* '='  */
    {0x00,0x41,0x22,0x14,0x08}, /* '>'  */ {0x02,0x01,0x51,0x09,0x06}, /* '?'  */
    {0x32,0x49,0x79,0x41,0x3E}, /* '@'  */ {0x7E,0x11,0x11,0x11,0x7E}, /* 'A'  */
    {0x7F,0x49,0x49,0x49,0x36}, /* 'B'  */ {0x3E,0x41,0x41,0x41,0x22}, /* 'C'  */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 'D'  */ {0x7F,0x49,0x49,0x49,0x41}, /* 'E'  */
    {0x7F,0x09,0x09,0x09,0x01}, /* 'F'  */ {0x3E,0x41,0x49,0x49,0x7A}, /* 'G'  */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 'H'  */ {0x00,0x41,0x7F,0x41,0x00}, /* 'I'  */
    {0x20,0x40,0x41,0x3F,0x01}, /* 'J'  */ {0x7F,0x08,0x14,0x22,0x41}, /* 'K'  */
    {0x7F,0x40,0x40,0x40,0x40}, /* 'L'  */ {0x7F,0x02,0x04,0x02,0x7F}, /* 'M'  */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 'N'  */ {0x3E,0x41,0x41,0x41,0x3E}, /* 'O'  */
    {0x7F,0x09,0x09,0x09,0x06}, /* 'P'  */ {0x3E,0x41,0x51,0x21,0x5E}, /* 'Q'  */
    {0x7F,0x09,0x19,0x29,0x46}, /* 'R'  */ {0x46,0x49,0x49,0x49,0x31}, /* 'S'  */
    {0x01,0x01,0x7F,0x01,0x01}, /* 'T'  */ {0x3F,0x40,0x40,0x40,0x3F}, /* 'U'  */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 'V'  */ {0x3F,0x40,0x38,0x40,0x3F}, /* 'W'  */
    {0x63,0x14,0x08,0x14,0x63}, /* 'X'  */ {0x07,0x08,0x70,0x08,0x07}, /* 'Y'  */
    {0x61,0x51,0x49,0x45,0x43}, /* 'Z'  */ {0x00,0x7F,0x41,0x41,0x00}, /* '['  */
    {0x02,0x04,0x08,0x10,0x20}, /* '\\' */ {0x00,0x41,0x41,0x7F,0x00}, /* ']'  */
    {0x04,0x02,0x01,0x02,0x04}, /* '^'  */ {0x40,0x40,0x40,0x40,0x40}, /* '_'  */
    {0x00,0x01,0x02,0x04,0x00}, /* '`'  */ {0x20,0x54,0x54,0x54,0x78}, /* 'a'  */
    {0x7F,0x48,0x44,0x44,0x38}, /* 'b'  */ {0x38,0x44,0x44,0x44,0x20}, /* 'c'  */
    {0x38,0x44,0x44,0x48,0x7F}, /* 'd'  */ {0x38,0x54,0x54,0x54,0x18}, /* 'e'  */
    {0x08,0x7E,0x09,0x01,0x02}, /* 'f'  */ {0x0C,0x52,0x52,0x52,0x3E}, /* 'g'  */
    {0x7F,0x08,0x04,0x04,0x78}, /* 'h'  */ {0x00,0x44,0x7D,0x40,0x00}, /* 'i'  */
    {0x20,0x40,0x44,0x3D,0x00}, /* 'j'  */ {0x7F,0x10,0x28,0x44,0x00}, /* 'k'  */
    {0x00,0x41,0x7F,0x40,0x00}, /* 'l'  */ {0x7C,0x04,0x18,0x04,0x78}, /* 'm'  */
    {0x7C,0x08,0x04,0x04,0x78}, /* 'n'  */ {0x38,0x44,0x44,0x44,0x38}, /* 'o'  */
    {0x7C,0x14,0x14,0x14,0x08}, /* 'p'  */ {0x08,0x14,0x14,0x18,0x7C}, /* 'q'  */
    {0x7C,0x08,0x04,0x04,0x08}, /* 'r'  */ {0x48,0x54,0x54,0x54,0x20}, /* 's'  */
    {0x04,0x3F,0x44,0x40,0x20}, /* 't'  */ {0x3C,0x40,0x40,0x40,0x7C}, /* 'u'  */
    {0x1C,0x20,0x40,0x20,0x1C}, /* 'v'  */ {0x3C,0x40,0x30,0x40,0x3C}, /* 'w'  */
    {0x44,0x28,0x10,0x28,0x44}, /* 'x'  */ {0x0C,0x50,0x50,0x50,0x3C}, /* 'y'  */
    {0x44,0x64,0x54,0x4C,0x44}, /* 'z'  */ {0x00,0x08,0x36,0x41,0x00}, /* '{'  */
    {0x00,0x00,0x7F,0x00,0x00}, /* '|'  */ {0x00,0x41,0x36,0x08,0x00}, /* '}'  */
    {0x10,0x08,0x08,0x10,0x08}, /* '~'  */
};

int display_str(int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    uint16_t fg_sw = (fg >> 8) | (fg << 8);
    uint16_t bg_sw = (bg >> 8) | (bg << 8);
    uint16_t buf[6 * 7];   /* 6 cols (5 glyph + 1 space) x 7 rows */

    while (*s) {
        char c = *s++;
        if (c < 0x20 || c > 0x7E) c = '?';
        const uint8_t *glyph = font5x7[c - 0x20];

        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 5; col++) {
                buf[row * 6 + col] = ((glyph[col] >> row) & 1) ? fg_sw : bg_sw;
            }
            buf[row * 6 + 5] = bg_sw;   /* trailing spacing column */
        }

        set_window(x, y, x + 5, y + 6);
        gpio_set_level(PIN_TFT_DC, 1);
        spi_transaction_t t = {
            .length    = 6 * 7 * 2 * 8,
            .tx_buffer = buf,
        };
        spi_device_polling_transmit(s_spi, &t);
        x += 6;
    }
    return x;
}
