/*
 * MP2.4 shim — Sprite.
 *
 * Replaces upstream CircuitOS Sprite.h (which inherits from
 * TFT_eSprite). On 9C we own the class entirely — Sprite holds its
 * own RGB565 framebuffer in PSRAM and the drawing methods are
 * minimal no-ops or trivial framebuffer pokes. The full TFT_eSprite-
 * style draw API (drawCircle, drawString with fonts, etc.) is
 * stubbed — calling these compiles, but produces no output until
 * Decision 9A (vendor real TFT_eSPI) or until we ship our own
 * software rasteriser.
 *
 * The point of this header is for the 506-file phone firmware's
 * `#include <Display/Sprite.h>` to parse cleanly, with class
 * methods that link. Visual correctness on Sprite-based screens
 * comes later.
 */
#pragma once

#include <Arduino.h>
#include <FS.h>           /* S-MP20/4d: <File> type for drawIcon(File, ...) */
#include <TFT_eSPI.h>
#include <Display/Color.h>

class Display;

class Sprite : public TFT_eSprite {
public:
    /* Match upstream Sprite constructors. The framebuffer is
     * allocated lazily on the first draw. */
    Sprite(TFT_eSPI *spi, uint16_t width, uint16_t height);
    Sprite(Display &display, uint16_t width, uint16_t height);
    Sprite(Sprite *sprite, uint16_t width, uint16_t height);
    virtual ~Sprite();

    /* Push self to parent or to the display. */
    Sprite &push();

    Sprite &clear(uint16_t color);
    Sprite &setPos(int32_t x, int32_t y);
    Sprite &resize(uint width, uint height);

    int32_t getX() const { return x; }
    int32_t getY() const { return y; }
    uint    getWidth()  const { return myWidth;  }
    uint    getHeight() const { return myHeight; }

    /* S-MP20/4e: TFT_eSprite-style width()/height() accessors.
     * Upstream SpriteRC.cpp calls sprite->width() / ->height()
     * (not the CircuitOS getWidth() / getHeight() names). Real
     * Bodmer/TFT_eSprite returns int16_t; we mirror that. The
     * existing getWidth()/getHeight() callers stay unaffected. */
    int16_t width()  const { return (int16_t)myWidth;  }
    int16_t height() const { return (int16_t)myHeight; }

    Sprite &setTransparent(bool transparent);
    Sprite &setChroma(Color color);

    void pushData(uint width, uint height, uint16_t *data);
    void push(Sprite *canvas, int16_t x, int16_t y) const;

    void rotate(uint times);

    /* Stub bitmap / icon drawing methods — accept the parameter
     * shapes the upstream phone firmware uses but produce no
     * output. */
    void drawMonochromeIcon(bool *icon, int16_t x, int16_t y,
                            uint16_t width, uint16_t height,
                            uint8_t scale = 1, uint16_t color = 0);
    void drawMonochromeIcon(const uint8_t *icon, int16_t x, int16_t y,
                            uint16_t width, uint16_t height,
                            uint8_t scale = 1, uint16_t color = 0);
    void drawIcon(const unsigned short *icon, int16_t x, int16_t y,
                  uint16_t width, uint16_t height,
                  uint8_t scale = 1, int32_t maskingColor = -1);
    void drawIcon(const Pixel *icon, int16_t x, int16_t y,
                  uint16_t width, uint16_t height,
                  uint8_t scale = 1, int32_t maskingColor = -1);
    /* S-MP20/4d: File-backed icon blit. Upstream Sprite::drawIcon
     * with a File argument reads RGB565 pixels straight off disk
     * (typically SPIFFS) and blits them. In our 9C shim there is
     * no actual blit -- the call is a no-op. Signature matches
     * upstream Sprite.h (FS.h's `File` is forward-declared by the
     * <FS.h> include above; the type is complete for callers
     * that build against arduino-esp32's filesystem stack). */
    void drawIcon(File icon, int16_t x, int16_t y,
                  uint16_t width, uint16_t height,
                  uint8_t scale = 1, int32_t maskingColor = -1);

    /* S-MP20/4d: rotate+zoom self-blit with anti-aliasing. The
     * real TFT_eSprite implementation pushes this sprite onto its
     * parent (chosen at construction) with the given rotation
     * (radians), per-axis scale, and a chroma-key for transparency.
     * We provide a no-op for the 9C shim -- the upstream
     * GIFAnimatedSprite + StaticRC + SpriteRC consumers compile
     * against this signature, but the visual rendering path is
     * silent until Decision 9A vendors real TFT_eSPI. */
    void pushRotateZoomWithAA(int16_t x, int16_t y, float rot,
                              float sx, float sy, uint16_t chroma);

    /* S-MP20/4e: 7-arg WITH-parent variant. Real impl pushes
     * self onto the passed-in parent sprite (rather than the
     * parent fixed at ctor) with rotation + per-axis zoom + a
     * chroma key. Used by upstream SpriteRC::push() on the
     * non-zero-rot branch (rotates the sprite around its own
     * mid-point). No-op shim, same rationale as the 6-arg
     * variant -- visual rendering through this path stays
     * silent until Decision 9A vendors real TFT_eSPI. */
    void pushRotateZoomWithAA(Sprite* parent, int16_t x, int16_t y,
                              float rot, float sx, float sy,
                              uint16_t chroma);

    /* Centered text helpers used by IntroScreen + friends. */
    void printCenter(const char *text);
    void printCenter(String text);
    void printCenter(uint32_t text);
    void printCenter(int text);
    void printCenter(float text);

    /* Pass-through helpers — implementations are trivial. */
    void cleanup();
    bool created() { return framebuffer != nullptr; }
    void setParent(Sprite *p)  { parent = p; }
    Sprite *getParent() const  { return parent; }

    /* Minimum subset of TFT_eSprite drawing API the phone firmware
     * uses. Each is a no-op for 9C; we'll fill them in if/when 9A
     * lands. Argument shapes match Bodmer/TFT_eSPI. */
    /* S-MP20/4: 2-arg overload forwards to the 3-arg form with
     * TFT_WHITE. Upstream TFT_eSprite::drawPixel(x, y) uses the
     * sprite's current foreground color (last setTextColor fg);
     * our shim does not track fg state separately, so we fall
     * back to white. The only caller in the entire codebase is
     * CollisionSystem::drawPolygon's degenerate single-point
     * branch, which is a debug-draw path that never fires in
     * normal play. */
    void drawPixel(int32_t x, int32_t y);
    void drawPixel(int32_t x, int32_t y, uint16_t color);
    void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color);
    void drawRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
    void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color);
    void drawCircle(int32_t x, int32_t y, int32_t r, uint16_t color);
    void fillCircle(int32_t x, int32_t y, int32_t r, uint16_t color);
    void drawString(const char *s, int32_t x, int32_t y);
    void drawString(String s,      int32_t x, int32_t y);
    void setTextColor(uint16_t fg, uint16_t bg);
    void setTextColor(uint16_t fg);
    void setTextSize(uint8_t s);
    void setTextDatum(uint8_t datum);
    void setTextFont(uint8_t font);

    /* S-MP20/7f2: extra TFT_eSprite-style methods Snake.cpp calls.
     * Each is a no-op for the 9C shim (the 5 readPixel call sites
     * in Snake drive collision detection, which will misbehave
     * silently because we always return 0 -- fine for compile-
     * and-link validation but visually broken; full implementation
     * waits on Decision 9A). The three signatures match Bodmer/
     * TFT_eSprite exactly so upstream code links cleanly. */
    uint16_t readPixel(int32_t x, int32_t y);
    int      printf(const char *fmt, ...);
    void     drawFastHLine(int32_t x, int32_t y, int32_t w,
                           uint16_t color);

    /* S-MP20/6d: LovyanGFX-style text API overloads.
     * Upstream Snake / SpaceInvaders / Pong pass a `textdatum_t`
     * enum value (defined in our TFT_eSPI shim) and the address
     * of a `fonts::IFont` selector to setTextDatum / setFont.
     * Both forms are silent no-ops; we keep the parameter shapes
     * so the upstream .cpp files parse + link. */
    void setTextDatum(textdatum_t datum) { (void)datum; }
    void setFont(const fonts::IFont *font) { (void)font; }
    int  textWidth(const char *s) { (void)s; return 0; }
    int  textWidth(String s)      { (void)s; return 0; }
    int  fontHeight()             { return 8; }

    /* S-MP20/9a: LovyanGFX-style 2-arg setTextWrap.
     * Upstream Pong/GameState.cpp + JigHWTest.cpp call
     *   display->setTextWrap(false, false);
     * which is the LGFX shape (wrap_x, wrap_y). Bodmer/TFT_eSPI
     * exposes only the 1-arg form; LovyanGFX added the per-axis
     * variant. Our shim has no text rasteriser yet (Decision 9C),
     * so wrap state is silently discarded. The 1-arg form is also
     * provided for any upstream code that uses the legacy shape.
     * Both are no-ops; visual rendering through this path remains
     * silent until 9A vendors real TFT_eSPI. */
    void setTextWrap(bool wrap_x)              { (void)wrap_x; }
    void setTextWrap(bool wrap_x, bool wrap_y) { (void)wrap_x; (void)wrap_y; }

    /* Framebuffer access for raw blitting. */
    uint16_t *getFrameBuffer() { return framebuffer; }

private:
    Sprite   *parent     = nullptr;
    TFT_eSPI *parentSPI  = nullptr;
    int32_t   x = 0, y = 0;
    uint16_t  myWidth, myHeight;
    bool      chroma     = false;
    Color     chromaKey  = 0;
    uint16_t *framebuffer = nullptr;  /* lazy-allocated RGB565 */
};
