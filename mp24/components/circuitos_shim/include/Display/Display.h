/*
 * MP2.4 shim — Display.
 *
 * Replaces upstream CircuitOS Display.h (which has `TFT_eSPI tft;`
 * as a member and pulls the full TFT_eSPI library transitively).
 * The public API matches upstream exactly so the 506-file phone
 * firmware compiles unchanged.
 *
 * The big difference: our backing is hal/display from the C HAL,
 * not the TFT_eSPI driver. The `tft` member exists for ABI / class-
 * shape compatibility but is never driven — getTft() returns a
 * pointer to it for any code that still goes through that path, but
 * the methods on the stub TFT_eSPI are no-ops.
 *
 * The S-MP16 LVGL integration rewires the LVGL flush callback to
 * call directly into hal/display, so getTft()'s no-op nature
 * doesn't matter for the LVGL render path.
 */
#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Display/Color.h>

class Sprite;

class Display {
public:
    /* Same signature as upstream Display(width, height, blPin, rotation, mirror). */
    Display(uint16_t width, uint16_t height,
            int8_t blPin = -1, int8_t rotation = -1, bool mirror = false);
    ~Display();

    /* Bring up the underlying ST7735 panel. Idempotent — wraps
     * hal/display_init(). Allocates the base Sprite if not already
     * present. */
    void begin();

    /* Flush the base Sprite contents to the panel. With 9C the
     * Sprite is a thin software framebuffer; commit() blits it
     * through hal/display in horizontal rows. */
    void commit();

    /* Fill the base Sprite with one colour. */
    void clear(uint32_t color);

    /* Pointer to the (stub) TFT_eSPI — never null, but methods on
     * it are no-ops. LVGL flush should NOT go through this. */
    TFT_eSPI *getTft();

    Sprite *getBaseSprite();

    /* Backlight is hardware-on (Decision C); setPower is a no-op. */
    void setPower(bool power);

    uint getWidth()  const { return width; }
    uint getHeight() const { return height; }

    /* Byte-swap flag toggle — relevant when pushing 16-bit colours
     * that come from a big-endian source. Forwarded to the stub. */
    void swapBytes(bool swap);

private:
    TFT_eSPI tft;            /* present for ABI shape; not driven */
    uint16_t width;
    uint16_t height;
    int8_t   blPin;
    int8_t   rotation;
    bool     mirror;
    Sprite  *baseSprite;
};
