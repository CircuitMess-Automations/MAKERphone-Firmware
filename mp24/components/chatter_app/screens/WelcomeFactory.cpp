/*
 * mp24/components/chatter_app/screens/WelcomeFactory.cpp
 *
 * Entry point that wires PhoneWelcomeScreen into the boot path.
 *
 * Originally TestScreen was the first runtime LVScreen subclass
 * we proved on hardware (S-MP17c). With the theme + fonts +
 * PhoneClock + PhoneSynthwaveBg stack now compiling (S-MP18a),
 * we can swap in a real upstream screen as the boot destination
 * — PhoneWelcomeScreen is the leanest such screen and has zero
 * service deps once PhoneSynthwaveBg is present.
 *
 * Why a factory wrapper rather than inlining in app_main:
 *
 *   1. app_main.cpp lives in mp24/main/, which has none of the
 *      src/ headers in its include path. Putting the constructor
 *      call here keeps mp24/main free of "src/Screens/..." paths
 *      and exposes a single extern "C" entry point instead. Same
 *      pattern as chatter_app_force_link() (S-MP17a) and
 *      chatter_app_start_test_screen() (S-MP17c).
 *
 *   2. We need to pre-populate Settings.ownerName so the greeting
 *      shows something visible. Settings is normally initialised
 *      by Chatter.begin() loading from NVS; we haven't wired that
 *      call into app_main yet, so SettingsData is just its
 *      default-constructed state with ownerName = "". The screen
 *      would render "Hello," with a blank line below where the
 *      name should go — visually still proves the screen + bg
 *      rendered, but less satisfying than "Hello, MP24". One
 *      strncpy here gets us the cleaner visual.
 *
 * Construction parameters:
 *
 *   DismissHandler = nullptr — when the screen dismisses there's
 *                              no next-screen to launch yet
 *                              (LockScreen / PhoneHomeScreen are
 *                              unported), so we accept the
 *                              "screen tears down with no
 *                              successor" outcome. The LVGL
 *                              display will hold the last frame.
 *   durationMs     = 0       — disables auto-dismiss. Screen
 *                              holds until the user presses a
 *                              key (which fires InputListener
 *                              path, dismisses + tears down).
 *                              For a static smoke test, 0 keeps
 *                              the visible window indefinite.
 *
 * The screen is heap-allocated and intentionally leaked.
 * Lifecycle ownership lands later when we have a real screen
 * stack with push/pop semantics.
 */

#include "Screens/PhoneWelcomeScreen.h"
#include <Settings.h>      /* Chatter-Library Settings singleton */

#include <cstring>

extern "C" void chatter_app_start_welcome_screen(void)
{
    /* Pre-populate the owner name so PhoneWelcomeScreen renders a
     * visible greeting. Without this, the buildName label would
     * be empty and the panel would show only "Hello," and the
     * "press any key" hint. The 23-char cap mirrors the persisted
     * field's MaxLen (see PhoneOwnerNameScreen.h). */
    std::strncpy(Settings.get().ownerName, "MP24", 23);
    Settings.get().ownerName[23] = '\0';

    /* Heap-allocate; leaked on purpose (see file header). */
    auto *s = new PhoneWelcomeScreen(/* DismissHandler */ nullptr,
                                     /* durationMs    */ 0);

    /* animate=false: lv_scr_load synchronously. Avoids depending
     * on LoopManager::defer for the screen swap itself — the
     * deferred onStart still routes through LoopManager, but the
     * swap is immediate. */
    s->start(false);
}
