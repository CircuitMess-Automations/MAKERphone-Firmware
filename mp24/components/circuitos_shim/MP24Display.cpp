/*
 * MP24Display.cpp — implementation of the shim Display class.
 *
 * Backed by hal/display from the C HAL. The actual hardware bring-
 * up (SPI, ST7735 init sequence, gamma curves) is already done by
 * display_init() at boot in app_main.cpp; Display::begin() here is
 * therefore idempotent and mostly just allocates the base Sprite.
 *
 * `commit()` flushes the base Sprite framebuffer to the panel.
 * With 9C this is a row-by-row copy through display_fill_rect for
 * each pixel — slow but correct and dependency-free. When 9A
 * lands (vendored TFT_eSPI) we can replace this with a proper
 * push_image call. For now the LVGL flush callback won't go
 * through Display::commit anyway — it'll call hal/display
 * primitives directly — so the slowness here is purely a fallback
 * for legacy non-LVGL render paths.
 */

#include <Display/Display.h>
#include <Display/Sprite.h>

extern "C" {
#include "hal/display.h"
}

Display::Display(uint16_t w, uint16_t h,
                 int8_t bl, int8_t rot, bool mir)
    : tft()
    , width(w)
    , height(h)
    , blPin(bl)
    , rotation(rot)
    , mirror(mir)
    , baseSprite(nullptr)
{
    /* baseSprite is allocated lazily in begin() — the constructor
     * runs before C++ static init is complete in some boot paths
     * and we don't want to call into heap_caps_malloc this early. */
}

Display::~Display()
{
    delete baseSprite;
    baseSprite = nullptr;
}

void Display::begin()
{
    /* hal/display_init was already called by app_main.cpp before
     * Arduino-layer init; this is harmless to skip. If a future
     * code path constructs a Display before that, uncomment:
     *   display_init();
     */

    if (baseSprite == nullptr) {
        /* Lazily build the base sprite at the panel dimensions. */
        baseSprite = new Sprite(&tft, width, height);
    }
}

void Display::commit()
{
    /* Row-by-row blit of the base sprite framebuffer to the panel.
     * The framebuffer is RGB565 native; hal/display speaks RGB565
     * too, so no colour conversion needed. */
    if (baseSprite == nullptr) {
        return;
    }
    uint16_t *fb = baseSprite->getFrameBuffer();
    if (fb == nullptr) {
        return;
    }
    /* TODO(S-MP16): replace with a hal/display_blit_rect that
     * pushes the whole buffer in one shot via the existing SPI
     * driver. For now, fill_rect per-pixel is slow but correct. */
    for (int yy = 0; yy < (int)height; ++yy) {
        for (int xx = 0; xx < (int)width; ++xx) {
            display_fill_rect(xx, yy, 1, 1, fb[yy * width + xx]);
        }
    }
}

void Display::clear(uint32_t color)
{
    if (baseSprite != nullptr) {
        baseSprite->clear((uint16_t)color);
    }
    /* Also paint the panel directly so a clear() with no commit()
     * still has visible effect — matches upstream semantics. */
    display_fill((uint16_t)color);
}

TFT_eSPI *Display::getTft()
{
    return &tft;
}

Sprite *Display::getBaseSprite()
{
    if (baseSprite == nullptr) {
        baseSprite = new Sprite(&tft, width, height);
    }
    return baseSprite;
}

void Display::setPower(bool power)
{
    /* Decision C: backlight is hardware-on through Q1 G3035. No
     * software control. The legacy idle-dim code path treats this
     * as a no-op too. */
    (void)power;
}

void Display::swapBytes(bool swap)
{
    tft.setSwapBytes(swap);
}
