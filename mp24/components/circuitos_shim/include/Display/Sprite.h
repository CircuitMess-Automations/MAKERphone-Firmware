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
#include <TFT_eSPI.h>
#include <Display/Color.h>

class Display;

class Sprite {
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
    int  textWidth(const char *s) { (void)s; return 0; }
    int  textWidth(String s)      { (void)s; return 0; }
    int  fontHeight()             { return 8; }

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
