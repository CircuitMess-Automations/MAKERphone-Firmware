/*
 * mp24/main/hal/display.h — ST7735 1.77" 128x160 SPI display
 *
 * Public API for the native ESP-IDF display driver. Lifted from the
 * standalone hw_test project (test_display.c / .h) with the test-loop
 * helpers stripped out — only init + primitive draw calls survive.
 *
 * Coordinate system: post-MADCTL 0x60 = 90° CW landscape.
 *   TFT_WIDTH  = 160  (horizontal)
 *   TFT_HEIGHT = 128  (vertical)
 *   origin at top-left
 *
 * This is intentionally a thin driver: no double buffer, no DMA chains,
 * no dirty-rect tracking. LVGL integration in a later session will
 * supply its own buffer + flush callback that calls into display_fill_rect
 * or a future raw-pixel-blit helper.
 *
 * Backlight is hardware-on through Q1 G3035 — there is no software
 * brightness API (Decision C).
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* Panel geometry (post-rotation) */
#define TFT_WIDTH       160
#define TFT_HEIGHT      128

/* RGB565 colour constants — match hw_test naming so any code lifted
 * from /mnt/project/dashboard.c etc. compiles unchanged. */
#define COLOR_BLACK    0x0000
#define COLOR_WHITE    0xFFFF
#define COLOR_RED      0xF800
#define COLOR_GREEN    0x07E0
#define COLOR_BLUE     0x001F
#define COLOR_YELLOW   0xFFE0
#define COLOR_CYAN     0x07FF
#define COLOR_MAGENTA  0xF81F
#define COLOR_GRAY     0x7BEF
#define COLOR_DARKGRAY 0x39E7
#define COLOR_ORANGE   0xFD20

/* Pack an RGB888 triplet into RGB565. Useful when reading the
 * MAKERphone palette from the legacy ChatterTheme. */
static inline uint16_t display_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/* Initialise the SPI bus + ST7735 panel.
 * Safe to call once at boot. Clears the framebuffer to black at the end. */
esp_err_t display_init(void);

/* Flood-fill the entire screen with a single RGB565 colour. */
void display_fill(uint16_t color);

/* Flood-fill an axis-aligned rectangle. Clipped on x/y and w/h. */
void display_fill_rect(int x, int y, int w, int h, uint16_t color);

/* Render a null-terminated ASCII string in the bundled 5x7 font.
 * Each glyph is 6px wide (5 + 1 spacing) by 7px tall. Returns the
 * x coordinate one pixel past the final glyph (useful for chaining). */
int display_str(int x, int y, const char *s, uint16_t fg, uint16_t bg);
