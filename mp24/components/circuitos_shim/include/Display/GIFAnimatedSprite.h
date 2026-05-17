/*
 * MP2.4 shim -- GIFAnimatedSprite.
 *
 * Replaces upstream CircuitOS Display/GIFAnimatedSprite.h
 * (which inherits from LoopListener, holds a GIF decoder, and
 * blits via TFT_eSprite). On 9C the upstream impl can't link
 * because the Display path is shimmed; this header provides a
 * forward declaration of the class with no-op methods so the
 * upstream phone firmware's `#include <Display/
 * GIFAnimatedSprite.h>` parses cleanly and src/Games/
 * GameEngine/Rendering/AnimRC.cpp compiles + links against
 * something concrete.
 *
 * S-MP20/4f: 5th and last Rendering leaf. Same gc-sections
 * gambit as /4d (StaticRC) and /4e (SpriteRC): AnimRC.cpp gets
 * added to chatter_app's SRCS, but the TU is left unreferenced
 * by anything else in SRCS (only Game.h's chain instantiates
 * AnimRC, and Game.h is not yet in SRCS). The linker drops the
 * TU at gc-sections time, so the no-op shim methods don't
 * actually need to perform any work -- they just need to link.
 *
 * Visual rendering through this path stays silent until
 * Decision 9A (vendor real TFT_eSPI) or until we ship our own
 * GIF rasteriser. AnimRC consumers in the upstream firmware
 * (the virtual-pet animation, intro splashes) will fall back
 * to a static frame -- not a regression vs. the LoRa-Chatter
 * port baseline, which never displayed animations either.
 */
#pragma once

#include <Arduino.h>
#include <FS.h>
#include <Display/Color.h>   /* Color typedef (uint16_t) */
#include <Util/GIF.h>        /* GIF::LoopMode { Auto, Single, Infinite } */
#include <functional>

class Sprite;                /* fwd-decl -- pulled in by callers via
                                <Display/Sprite.h> when they need
                                a complete type for the push args. */

class GIFAnimatedSprite {
public:
    /* Match upstream ctor shape exactly: parentSprite + File
     * (passed by const-ref upstream, accepted by const-ref here
     * too -- fs::File is a thin RAII wrapper). The shim instance
     * owns no decoder; the ctor performs no I/O. */
    GIFAnimatedSprite(Sprite* parentSprite, const fs::File& gifFile);
    ~GIFAnimatedSprite();

    /* Playback control. No-ops in the shim. */
    void start();
    void stop();
    void reset();

    /* Blit current frame onto the given parent sprite at (x, y).
     * Real impl reads decoded pixels from the GIF and pushes
     * them through TFT_eSprite. Shim is a no-op. The Color
     * (maskingColor) default matches upstream. TFT_TRANSPARENT
     * is reachable here via <Display/Color.h>'s sibling
     * <TFT_eSPI.h>, but the AnimRC.cpp caller only ever uses
     * the 3-arg form, so we keep the default visible for source
     * compatibility but don't rely on it being resolved at the
     * declaration site. */
    void push(Sprite* sprite, int x, int y,
              Color maskingColor = 0x0120 /* TFT_TRANSPARENT */) const;

    /* Blit with rotation -- upstream guards this with
     * #ifdef CIRCUITOS_LOVYANGFX, but AnimRC.cpp calls it
     * unconditionally. Provide the no-op shim regardless so the
     * call site links. */
    void pushRotate(Sprite* sprite, int x, int y, float rot,
                    Color maskingColor = 0x0120 /* TFT_TRANSPARENT */) const;

    /* Loop-mode setter / done-callback. AnimRC owns its own
     * playback-mode tracking, but mirrors changes here so the
     * upstream caller's expectation that the GIF instance also
     * knows the mode stays satisfied. */
    void setLoopMode(GIF::LoopMode loopMode);
    void setLoopDoneCallback(std::function<void(uint32_t)> cb);
};
