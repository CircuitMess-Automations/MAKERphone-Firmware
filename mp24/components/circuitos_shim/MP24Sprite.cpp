/*
 * MP24Sprite.cpp — implementation of the shim Sprite class.
 *
 * The 9C design (see docs/CIRCUITOS_SHIM_PLAN.md §9):
 *
 *   - Sprite owns an RGB565 framebuffer allocated in PSRAM at the
 *     requested width × height.
 *   - drawPixel / clear write into that framebuffer directly.
 *   - All other drawing primitives (lines, circles, strings, icon
 *     blitters) are no-ops for now. Compile passes; rendering is
 *     silent until 9A vendors real TFT_eSPI.
 *   - push() asks the parent (another Sprite, or a Display) to
 *     blit our framebuffer to the panel.
 *
 * The point of this file: every method declared in Sprite.h has a
 * link-time definition so the upstream phone firmware can compile
 * against `#include <Display/Sprite.h>` without dangling symbols.
 */

#include <Display/Sprite.h>
#include <Display/Display.h>
#include <esp_heap_caps.h>
#include <string.h>

/* -------- construction / destruction -------- */

Sprite::Sprite(TFT_eSPI *spi, uint16_t w, uint16_t h)
    : TFT_eSprite(spi)            /* S-MP20/4e: init base */
    , parentSPI(spi)
    , myWidth(w)
    , myHeight(h)
{
    /* Lazy-allocate the framebuffer from PSRAM (the panel is small,
     * but a 160×128 RGB565 sprite is 40 KB — fits SRAM too, prefer
     * PSRAM to keep internal RAM for time-critical paths). */
    framebuffer = (uint16_t *)heap_caps_malloc(
        (size_t)w * h * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (framebuffer == nullptr) {
        framebuffer = (uint16_t *)heap_caps_malloc(
            (size_t)w * h * sizeof(uint16_t),
            MALLOC_CAP_8BIT);
    }
    if (framebuffer != nullptr) {
        memset(framebuffer, 0, (size_t)w * h * sizeof(uint16_t));
    }
}

Sprite::Sprite(Display &display, uint16_t w, uint16_t h)
    : Sprite(display.getTft(), w, h)
{
}

Sprite::Sprite(Sprite *spr, uint16_t w, uint16_t h)
    : TFT_eSprite(spr ? spr->parentSPI : nullptr) /* S-MP20/4e */
    , parent(spr)
    , parentSPI(spr ? spr->parentSPI : nullptr)
    , myWidth(w)
    , myHeight(h)
{
    framebuffer = (uint16_t *)heap_caps_malloc(
        (size_t)w * h * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (framebuffer == nullptr) {
        framebuffer = (uint16_t *)heap_caps_malloc(
            (size_t)w * h * sizeof(uint16_t),
            MALLOC_CAP_8BIT);
    }
    if (framebuffer != nullptr) {
        memset(framebuffer, 0, (size_t)w * h * sizeof(uint16_t));
    }
}

Sprite::~Sprite()
{
    if (framebuffer != nullptr) {
        heap_caps_free(framebuffer);
        framebuffer = nullptr;
    }
}

/* -------- fluent state setters -------- */

Sprite &Sprite::push()
{
    /* Pushing a child to its parent is a blit-with-offset. For 9C
     * we skip the blit (no-op) — the rendering path that matters
     * is the base sprite → panel one, handled by Display::commit. */
    return *this;
}

Sprite &Sprite::clear(uint16_t color)
{
    if (framebuffer == nullptr) {
        return *this;
    }
    const size_t n = (size_t)myWidth * myHeight;
    for (size_t i = 0; i < n; ++i) {
        framebuffer[i] = color;
    }
    return *this;
}

Sprite &Sprite::setPos(int32_t nx, int32_t ny)
{
    x = nx;
    y = ny;
    return *this;
}

Sprite &Sprite::resize(uint w, uint h)
{
    /* Reallocate the framebuffer to the new dimensions. */
    if (framebuffer != nullptr) {
        heap_caps_free(framebuffer);
        framebuffer = nullptr;
    }
    myWidth  = (uint16_t)w;
    myHeight = (uint16_t)h;
    framebuffer = (uint16_t *)heap_caps_malloc(
        (size_t)w * h * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (framebuffer == nullptr) {
        framebuffer = (uint16_t *)heap_caps_malloc(
            (size_t)w * h * sizeof(uint16_t),
            MALLOC_CAP_8BIT);
    }
    if (framebuffer != nullptr) {
        memset(framebuffer, 0, (size_t)w * h * sizeof(uint16_t));
    }
    return *this;
}

Sprite &Sprite::setTransparent(bool transparent)
{
    chroma = transparent;
    return *this;
}

Sprite &Sprite::setChroma(Color c)
{
    chromaKey = c;
    chroma    = true;
    return *this;
}

void Sprite::pushData(uint w, uint h, uint16_t *data)
{
    if (framebuffer == nullptr || data == nullptr) {
        return;
    }
    const uint copyW = w < myWidth  ? w : myWidth;
    const uint copyH = h < myHeight ? h : myHeight;
    for (uint yy = 0; yy < copyH; ++yy) {
        memcpy(&framebuffer[yy * myWidth],
               &data[yy * w],
               copyW * sizeof(uint16_t));
    }
}

void Sprite::push(Sprite *canvas, int16_t /*nx*/, int16_t /*ny*/) const
{
    /* Blit self onto another sprite at (nx, ny). Skipped for 9C. */
    (void)canvas;
}

void Sprite::rotate(uint /*times*/) {}

/* -------- icon / bitmap stubs -------- */

void Sprite::drawMonochromeIcon(bool *, int16_t, int16_t,
                                uint16_t, uint16_t, uint8_t, uint16_t) {}
void Sprite::drawMonochromeIcon(const uint8_t *, int16_t, int16_t,
                                uint16_t, uint16_t, uint8_t, uint16_t) {}
void Sprite::drawIcon(const unsigned short *, int16_t, int16_t,
                      uint16_t, uint16_t, uint8_t, int32_t) {}
void Sprite::drawIcon(const Pixel *, int16_t, int16_t,
                      uint16_t, uint16_t, uint8_t, int32_t) {}

/* S-MP20/4d: File-backed icon blit -- no-op shim. The real call
 * reads RGB565 pixels off disk and writes them into the
 * framebuffer at (x, y); for 9C the framebuffer is left
 * untouched. Argument names suppressed since none are used. */
void Sprite::drawIcon(File, int16_t, int16_t,
                      uint16_t, uint16_t, uint8_t, int32_t) {}

/* S-MP20/4d: pushRotateZoomWithAA -- no-op shim. Real call rotates
 * + zooms self onto the parent sprite (set at ctor); we don't have
 * a software rasteriser for that yet. */
void Sprite::pushRotateZoomWithAA(int16_t, int16_t, float,
                                  float, float, uint16_t) {}

/* S-MP20/4e: 7-arg WITH-parent variant. Real call pushes self
 * onto the explicit parent argument with rotation+zoom+chroma.
 * Used by upstream SpriteRC::push() on the non-zero-rot
 * branch. No-op shim. Argument names suppressed since none
 * are used. */
void Sprite::pushRotateZoomWithAA(Sprite*, int16_t, int16_t, float,
                                  float, float, uint16_t) {}

/* -------- centred text stubs -------- */

void Sprite::printCenter(const char *)  {}
void Sprite::printCenter(String)        {}
void Sprite::printCenter(uint32_t)      {}
void Sprite::printCenter(int)           {}
void Sprite::printCenter(float)         {}

/* -------- housekeeping -------- */

void Sprite::cleanup()
{
    if (framebuffer != nullptr) {
        heap_caps_free(framebuffer);
        framebuffer = nullptr;
    }
}

/* -------- TFT_eSprite-style draw primitives -------- */

/*
 * S-MP20/4: 2-arg overload -- forwards to the 3-arg form with
 * TFT_WHITE. See Sprite.h for the rationale.
 */
void Sprite::drawPixel(int32_t px, int32_t py)
{
    drawPixel(px, py, TFT_WHITE);
}

void Sprite::drawPixel(int32_t px, int32_t py, uint16_t color)
{
    if (framebuffer == nullptr) return;
    if (px < 0 || py < 0 || px >= (int32_t)myWidth || py >= (int32_t)myHeight) {
        return;
    }
    framebuffer[py * myWidth + px] = color;
}

void Sprite::drawLine(int32_t, int32_t, int32_t, int32_t, uint16_t)   {}
void Sprite::drawRect(int32_t, int32_t, int32_t, int32_t, uint16_t)   {}

void Sprite::fillRect(int32_t fx, int32_t fy, int32_t fw, int32_t fh,
                      uint16_t color)
{
    if (framebuffer == nullptr) return;
    /* Clip to sprite bounds. */
    if (fx < 0) { fw += fx; fx = 0; }
    if (fy < 0) { fh += fy; fy = 0; }
    if (fx + fw > (int32_t)myWidth)  fw = (int32_t)myWidth  - fx;
    if (fy + fh > (int32_t)myHeight) fh = (int32_t)myHeight - fy;
    if (fw <= 0 || fh <= 0) return;
    for (int32_t yy = fy; yy < fy + fh; ++yy) {
        for (int32_t xx = fx; xx < fx + fw; ++xx) {
            framebuffer[yy * myWidth + xx] = color;
        }
    }
}

void Sprite::drawCircle(int32_t, int32_t, int32_t, uint16_t)          {}
void Sprite::fillCircle(int32_t, int32_t, int32_t, uint16_t)          {}
void Sprite::drawString(const char *, int32_t, int32_t)               {}
void Sprite::drawString(String,       int32_t, int32_t)               {}
void Sprite::setTextColor(uint16_t, uint16_t)                         {}
void Sprite::setTextColor(uint16_t)                                   {}
void Sprite::setTextSize(uint8_t)                                     {}
void Sprite::setTextDatum(uint8_t)                                    {}
void Sprite::setTextFont(uint8_t)                                     {}
