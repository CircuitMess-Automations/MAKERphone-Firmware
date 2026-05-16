/*
 * mp24/components/chatter_app/shim/Anchor.cpp
 *
 * Linker anchor for the chatter_app static archive. ESP-IDF builds
 * each component as a .a file and the final link does archive-style
 * resolution: a .o is only included if it provides a symbol that
 * something already in the link wants. With no external references
 * into chatter_app, the entire archive would be dropped and the
 * 'green' build would not actually contain LVScreen / LVObject /
 * our InputLVGL shim.
 *
 * mp24/main/app_main.cpp has a __attribute__((used)) function that
 * calls chatter_app_force_link() — that unresolved reference is
 * what pulls THIS .o in. From here, the explicit references to
 * InputLVGL::getInstance, LVScreen ctor/dtor, and LVScreen::start
 * + stop force the linker to load LVScreen.o + LVObject.o +
 * shim/InputLVGL.o as well.
 *
 * The function is never called at runtime. The branch is dead code.
 * Its only job is to encode "these classes must exist in the final
 * binary" as a compile-time dependency the linker can follow.
 *
 * Once a real screen instance is created from somewhere in main
 * (S-MP17b+ when we wire MainMenu or whatever), this anchor can
 * be removed — the live instantiation will carry the references
 * naturally.
 */

#include "Interface/LVScreen.h"   /* via PRIV_INCLUDE_DIRS = src/ */
#include "InputLVGL.h"

extern "C" void chatter_app_force_link(void)
{
    /* InputLVGL::getInstance() returns the lvgl_glue indev wrapped
     * in our shim singleton. This single call exercises the entire
     * shim/InputLVGL.cpp surface. */
    (void)InputLVGL::getInstance();

    /* Dead branch — never taken — but the compiler emits the
     * method-call instructions, leaving unresolved external
     * references for the linker to fill from LVScreen.o. */
    LVScreen *s = nullptr;
    if (s != nullptr) {
        s->start();
        s->stop();
        delete s;
    }
}
