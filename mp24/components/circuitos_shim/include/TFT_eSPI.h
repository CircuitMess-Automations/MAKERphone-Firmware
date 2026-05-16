/*
 * MP2.4 shim — TFT_eSPI / TFT_eSprite stubs.
 *
 * The upstream CircuitOS Display and Sprite classes reference
 * TFT_eSPI as a base / member type. The real Bodmer/TFT_eSPI
 * library is not vendored on MP2.4 (Decision 9C in
 * docs/CIRCUITOS_SHIM_PLAN.md). To let `<Display/Display.h>` and
 * `<Display/Sprite.h>` parse cleanly we provide *empty* TFT_eSPI
 * and TFT_eSprite classes here — same names, same shape, no
 * actual graphics logic.
 *
 * What this means at runtime:
 *   - Code paths that dereference `display->getTft()` (the existing
 *     LVGL flush callback in MAKERphone-Firmware.ino does this) hit
 *     no-op methods and produce no display output.
 *   - The S-MP16 LVGL integration MUST rewrite that flush callback
 *     to call hal/display_blit (or similar) directly, bypassing
 *     TFT_eSPI entirely.
 *   - Code that draws via Sprite (some games, sprite-sheet-style
 *     screens) will also be visually no-op until we promote to
 *     Decision 9A (vendor the real TFT_eSPI library).
 *
 * The methods listed below are the union of what the lvglFlush()
 * function in the existing firmware and the CircuitOS Sprite
 * superclass interface actually call against TFT_eSPI* / TFT_eSprite*.
 * Anything missed will surface as a compile error when we re-enable
 * the relevant source files; add it here at that point.
 */
#pragma once

#include <Arduino.h>

class TFT_eSPI {
public:
    TFT_eSPI() = default;
    virtual ~TFT_eSPI() = default;

    /* Used by MAKERphone-Firmware.ino lvglFlush — replaced in S-MP16. */
    void startWrite() {}
    void endWrite() {}
    void setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h) {
        (void)x; (void)y; (void)w; (void)h;
    }
    void pushColors(uint16_t *data, uint32_t len, bool swap = false) {
        (void)data; (void)len; (void)swap;
    }
    void pushColors(const uint16_t *data, uint32_t len, bool swap = false) {
        (void)data; (void)len; (void)swap;
    }

    /* Panel structure used by ChatterDisplay panel selection.
     * Forward-declared as a plain struct so ChatterDisplay code
     * compiles without us shipping the actual panel definitions. */
    struct PanelStruct {};
    void setPanel(PanelStruct *p) { (void)p; }

    /* Other common methods Sprite ancestors expose via inheritance.
     * No-op stubs — visual operations through these are silent. */
    void fillScreen(uint16_t color) { (void)color; }
    void setSwapBytes(bool swap) { (void)swap; }
    bool getSwapBytes() { return false; }

    /* Cursor / text-print API used by some screens. */
    void setCursor(int16_t x, int16_t y) { (void)x; (void)y; }
    void setTextColor(uint16_t fg, uint16_t bg) { (void)fg; (void)bg; }
    void setTextSize(uint8_t s) { (void)s; }
    void print(const char *s) { (void)s; }
    void println(const char *s) { (void)s; }
};

class TFT_eSprite : public TFT_eSPI {
public:
    explicit TFT_eSprite(TFT_eSPI *parent = nullptr) : parentSPI(parent) {}

    /* Buffer lifecycle */
    uint16_t *createSprite(int16_t w, int16_t h, uint8_t frames = 1) {
        (void)w; (void)h; (void)frames; return nullptr;
    }
    void deleteSprite() {}
    bool created() { return false; }

    /* Identity / position */
    void setPsram(bool psram) { (void)psram; }

    /* Push the sprite contents back to whatever backs it. No-op in
     * 9C — real blit happens via hal/display in MP24Display. */
    void pushSprite(int32_t x, int32_t y) { (void)x; (void)y; }
    void pushSprite(int32_t x, int32_t y, uint16_t chroma) {
        (void)x; (void)y; (void)chroma;
    }
    void pushRotated(int16_t angle, uint16_t chroma = 0) {
        (void)angle; (void)chroma;
    }

    void fillSprite(uint16_t color) { (void)color; }

    /* Image / bitmap pushers used by SpriteElement and friends. */
    void pushImage(int32_t x, int32_t y, int32_t w, int32_t h,
                   uint16_t *data) {
        (void)x; (void)y; (void)w; (void)h; (void)data;
    }
    void drawBitmap(int32_t x, int32_t y, const uint8_t *bitmap,
                    int32_t w, int32_t h, uint16_t color) {
        (void)x; (void)y; (void)bitmap; (void)w; (void)h; (void)color;
    }

protected:
    TFT_eSPI *parentSPI;
};
