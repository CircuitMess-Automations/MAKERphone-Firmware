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

/*
 * S-MP20/4: canonical TFT_eSPI RGB565 color macros.
 *
 * Upstream Chatter sources (Games/Snake, JigHWTest, the
 * GameEngine debug-draw, etc.) reference TFT_BLACK / TFT_RED /
 * TFT_GREEN / ... as preprocessor macros. The real
 * Bodmer/TFT_eSPI library defines these in its TFT_eSPI.h public
 * header; we vendor the canonical RGB565 values here so any TU
 * that pulls our shim header gets the same constants. Values are
 * byte-for-byte from Bodmer's library.
 */
#ifndef TFT_BLACK
#define TFT_BLACK       0x0000
#define TFT_NAVY        0x000F
#define TFT_DARKGREEN   0x03E0
#define TFT_DARKCYAN    0x03EF
#define TFT_MAROON      0x7800
#define TFT_PURPLE      0x780F
#define TFT_OLIVE       0x7BE0
#define TFT_LIGHTGREY   0xC618
#define TFT_DARKGREY    0x7BEF
#define TFT_BLUE        0x001F
#define TFT_GREEN       0x07E0
#define TFT_CYAN        0x07FF
#define TFT_RED         0xF800
#define TFT_MAGENTA     0xF81F
#define TFT_YELLOW      0xFFE0
#define TFT_WHITE       0xFFFF
#define TFT_ORANGE      0xFDA0
#define TFT_GREENYELLOW 0xB7E0
#define TFT_PINK        0xFE19
#define TFT_BROWN       0x9A60
#define TFT_GOLD        0xFEA0
#define TFT_SILVER      0xC618
#define TFT_SKYBLUE     0x867D
#define TFT_VIOLET      0x915C
/*
 * S-MP20/4d: canonical Bodmer/TFT_eSPI sentinel value for
 * "transparent" -- the colour key meaning "do not blit this pixel".
 * Upstream Sprite.cpp default-initialises chromaKey to this value,
 * GIFAnimatedSprite uses it as the clear-colour for the temp
 * sprite during pushRotateZoomWithAA, and StaticRC.cpp passes it
 * to drawIcon + pushRotateZoomWithAA as the maskingColor. Value is
 * byte-for-byte from Bodmer's library.
 */
#define TFT_TRANSPARENT 0x0120
#endif

/*
 * S-MP20/6d: LovyanGFX-style text API stubs.
 *
 * The upstream Chatter games (Snake, SpaceInvaders, Pong/Bonk
 * sub-states) were written against the LovyanGFX (LGFX) text
 * API rather than Bodmer/TFT_eSPI. They reference:
 *
 *   - the `textdatum_t` enum class (top_center, middle_center,
 *     bottom_center, ...), to set text alignment;
 *   - the `fonts::IFont` family of font selectors (fonts::Font0,
 *     fonts::Font2) passed to setFont via pointer;
 *   - method names that don't exist on Bodmer's API:
 *     setTextDatum(textdatum_t) and setFont(const IFont*).
 *
 * To let those .cpp files parse + link cleanly without dragging
 * in real LovyanGFX (Decision 9C — no software rasteriser yet),
 * we provide minimal LGFX-shaped stubs here. The enum is real,
 * the font selectors are addressable inline-constexpr empties,
 * and the actual Sprite methods that take these types are added
 * as no-op overloads in Display/Sprite.h. Visual output is silent
 * until 9A lands.
 *
 * Values are not preserved bit-for-bit from LovyanGFX -- this
 * shim only needs the symbols to exist; the games' usage doesn't
 * depend on the numeric values.
 */
enum class textdatum_t : uint8_t {
    top_left = 0,
    top_center,
    top_right,
    middle_left,
    middle_center,
    middle_right,
    bottom_left,
    bottom_center,
    bottom_right,
    baseline_left,
    baseline_center,
    baseline_right,
};

namespace fonts {
    /* Opaque marker type. The shim never inspects its contents;
     * Snake/SpaceInvaders take the address of `fonts::Font0` /
     * `fonts::Font2` and pass it to setFont, which discards. */
    struct IFont {};
    /* C++17 inline constexpr gives external linkage with one
     * definition across translation units, and the lvalue is
     * still addressable (`&fonts::Font0` is well-defined). */
    inline constexpr IFont Font0{};
    inline constexpr IFont Font2{};
}


/*
 * S-MP20/8a: Adafruit-GFX-style font selector stubs.
 *
 * Upstream SpaceInvaders.cpp uses Bodmer/TFT_eSPI's "FreeFont" API:
 *   baseSprite->setFreeFont(&TomThumb);
 * which expects a `const GFXfont*` argument and an externally
 * defined `TomThumb` symbol (defined in
 * mp24/components/circuitos/src/Devices/Matrix/Font.hpp, which
 * is NOT compiled on MP2.4 because the Matrix device is absent).
 *
 * To let SpaceInvaders.cpp parse + link cleanly we provide empty
 * Adafruit-GFX-shaped struct stubs here and an inline constexpr
 * TomThumb symbol with external linkage (C++17 rule, same pattern
 * as fonts::Font0 above). The shim never inspects the structs --
 * SpaceInvaders just takes &TomThumb and hands it to a setFreeFont
 * no-op. Visual rendering through this path remains silent until
 * Decision 9A vendors real TFT_eSPI.
 */
struct GFXglyph {};
struct GFXfont {};
inline constexpr GFXfont TomThumb{};


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
    /* S-MP20/7f3/1: Snake.cpp's print() call sites pass uint16_t
     * (score), char (a single character from `String name[i]`),
     * and String objects. Real Bodmer/TFT_eSPI inherits Print's
     * full overload set; our shim TFT_eSPI doesn't derive from
     * Print, so we add minimum-viable no-op overloads here. All
     * are silent stubs (rendering through this path stays
     * silent until Decision 9A vendors real TFT_eSPI).
     * The argument shapes mirror Print's print() family. */
    void print(char c)         { (void)c; }
    void print(unsigned char c, int base = 10) { (void)c; (void)base; }
    void print(int n,           int base = 10) { (void)n; (void)base; }
    void print(unsigned int n,  int base = 10) { (void)n; (void)base; }
    void print(long n,          int base = 10) { (void)n; (void)base; }
    void print(unsigned long n, int base = 10) { (void)n; (void)base; }
    void print(double n,        int digits = 2) { (void)n; (void)digits; }
    void print(const String &s) { (void)s; }
    void println(char c)         { (void)c; }
    void println(unsigned char c, int base = 10) { (void)c; (void)base; }
    void println(int n,           int base = 10) { (void)n; (void)base; }
    void println(unsigned int n,  int base = 10) { (void)n; (void)base; }
    void println(long n,          int base = 10) { (void)n; (void)base; }
    void println(unsigned long n, int base = 10) { (void)n; (void)base; }
    void println(double n,        int digits = 2) { (void)n; (void)digits; }
    void println(const String &s) { (void)s; }
    void println() {}

    /* S-MP20/8a: Bodmer/TFT_eSPI FreeFont selector. Upstream
     * SpaceInvaders.cpp calls baseSprite->setFreeFont(&TomThumb)
     * to switch to an Adafruit-GFX bitmap font. Our shim accepts
     * any GFXfont* (including nullptr) and discards -- the text
     * rasterisation path itself is a no-op until Decision 9A
     * vendors real TFT_eSPI. */
    void setFreeFont(const GFXfont *font) { (void)font; }
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
    /* S-MP20/8a: 7-arg drawBitmap with a scale factor. Upstream
     * SpaceInvaders.cpp's drawBitmap helper calls
     *   baseSprite->drawBitmap(x, y, bitmap, w, h, color, scale);
     * The trailing scale is the integer pixel-multiplier for
     * blitting the monochrome bitmap. Real TFT_eSPI exposes this
     * via the U8g2-style overload; our shim takes the same shape
     * and discards. No-op for 9C. */
    void drawBitmap(int32_t x, int32_t y, const uint8_t *bitmap,
                    int32_t w, int32_t h, uint16_t color,
                    uint8_t scale) {
        (void)x; (void)y; (void)bitmap; (void)w; (void)h;
        (void)color; (void)scale;
    }

protected:
    TFT_eSPI *parentSPI;
};
