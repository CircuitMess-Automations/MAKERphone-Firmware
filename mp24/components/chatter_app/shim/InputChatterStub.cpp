/*
 * mp24/components/chatter_app/shim/InputChatterStub.cpp
 *
 * S-MP20/6b -- pre-emptively defines InputChatter symbols so that
 * when the first game subclass (Snake / Bonk / SpaceInvaders /
 * SpaceRocks) lands in chatter_app SRCS in S-MP20/7, the linker
 * can resolve InputChatter::getInputInstance() called from
 * Games/GameEngine/Game.cpp's start() and stop() bodies.
 *
 * Why upstream src/InputChatter.cpp can NOT be compiled directly:
 *
 *   - Its keyMap initialiser uses BTN_UP / BTN_DOWN / BTN_A /
 *     BTN_B from the legacy <Pins.hpp>. The MP2.4 circuitos_shim
 *     Chatter.h was narrowed to NOT pull Pins.hpp (Pins.hpp
 *     drags every legacy GPIO macro into every TU that
 *     #include's Chatter.h). InputChatter.cpp itself doesn't
 *     #include Pins.hpp -- it inherits the macros from its
 *     `#include <Chatter.h>`. Compiling the upstream .cpp under
 *     our shimmed header would therefore fail to find BTN_UP
 *     etc.
 *
 *   - More importantly: the upstream buttonPressed / Released
 *     bodies fan key events into LVGL via the InputLVGL indev
 *     they own. On MP2.4 the indev is owned by lvgl_glue, NOT
 *     by InputLVGL -- see shim/InputLVGL.cpp. Routing key
 *     events through InputChatter's read() into the InputLVGL
 *     indev would dead-end (the indev's real read_cb is the
 *     one in lvgl_glue.c). Stub bodies that no-op are the
 *     semantically correct override on MP2.4.
 *
 * What this stub provides:
 *
 *   InputChatter::keyMap     storage for the empty static
 *                            map<uint8_t, lv_key_t> declared in
 *                            InputChatter.h.
 *   InputChatter::instance   storage for the static self-pointer.
 *   InputChatter::InputChatter() ctor -- forwards to InputLVGL
 *                            with LV_INDEV_TYPE_ENCODER and sets
 *                            instance.
 *   InputChatter::read()     no-op (lvgl_glue owns the keypad
 *                            indev; this read_cb path is never
 *                            invoked).
 *   InputChatter::buttonPressed()  no-op.
 *   InputChatter::buttonReleased() no-op.
 *   InputChatter::getInputInstance() returns a lazily-constructed
 *                            singleton.
 *
 * Why getInputInstance() returns a non-null instance rather than
 * just nullptr: Game::stop() calls
 *
 *     Input::getInstance()->addListener(InputChatter::getInputInstance());
 *
 * and Input::addListener(nullptr) (see
 * mp24/components/circuitos/src/Input/Input.cpp) would push a
 * nullptr into the listener vector. The next btnPress() call
 * would then dispatch buttonPressed() through that nullptr --
 * crashing on the first button event after a game pops. Returning
 * a real InputListener-derived singleton lets the path land
 * cleanly; the singleton's buttonPressed / buttonReleased
 * overrides are no-ops so it adds no behaviour.
 *
 * Build-time effect: this TU is currently DEAD (no in-SRCS file
 * references the symbols it provides -- Game.cpp's start/stop/
 * loop bodies are gc-sections'd away because no game subclass
 * has been added to SRCS yet). --gc-sections drops the TU. Once
 * S-MP20/7 lands the first game subclass, the symbols here
 * become live and resolve the unresolved externs that would
 * otherwise fail the link.
 */

#include "InputChatter.h"

/* Empty initialiser -- the keyMap is referenced only by the
 * upstream buttonPressed / buttonReleased bodies in
 * src/InputChatter.cpp (which we are NOT compiling). Our
 * overrides below are no-ops and don't touch keyMap. The
 * storage definition is still required because InputChatter.h
 * declares keyMap as a static member; the linker needs storage
 * for it as soon as anything forms a reference to InputChatter
 * (its vtable references methods that close over the class type;
 * the C++ ABI emits the static-member symbols alongside the
 * class info). */
std::map<uint8_t, lv_key_t> InputChatter::keyMap = {};

InputChatter *InputChatter::instance = nullptr;

InputChatter::InputChatter() : InputLVGL(LV_INDEV_TYPE_ENCODER)
{
    instance = this;
}

void InputChatter::read(lv_indev_drv_t * /*drv*/, lv_indev_data_t * /*data*/)
{
    /* No-op. The keypad indev is owned and read by lvgl_glue
     * (mp24/main/lvgl_glue.c -> lvgl_keypad_read_cb). LVGL only
     * ever invokes read() on the indev that registered it; our
     * lvgl_glue indev was registered with its own read_cb so this
     * path is never reached. */
}

void InputChatter::buttonPressed(uint /*i*/)
{
    /* No-op -- key events do not need to be funnelled into LVGL
     * via InputChatter on MP2.4. See file header. */
}

void InputChatter::buttonReleased(uint /*i*/)
{
    /* No-op -- see file header. */
}

InputChatter *InputChatter::getInputInstance()
{
    if (instance == nullptr) {
        /* Lazy construction. The InputChatter ctor calls the
         * InputLVGL ctor in shim/InputLVGL.cpp, which sets
         * InputLVGL::instance = this and stashes the lvgl_glue
         * indev pointer. From that point on InputLVGL::
         * getInstance() returns this InputChatter (it IS-A
         * InputLVGL, so the type system is fine). The
         * MP24InputLVGL singleton in InputLVGL.cpp is created
         * only if InputLVGL::getInstance() is called BEFORE
         * any game has started; once a game pops, our singleton
         * here takes over -- both have no-op read() bodies so
         * the swap is invisible to LVGL. */
        instance = new InputChatter();
    }
    return instance;
}
