/*
 * mp24/components/chatter_app/screens/AppStubFactory.cpp
 *
 * Boot-path factory for PhoneAppStubScreen — the 'NOT YET BUILT'
 * placeholder screen the upstream firmware uses for menu tiles
 * whose real screens haven't been ported. Perfect smoke target
 * for our universal-UI Elements (PhoneStatusBar +
 * PhoneSoftKeyBar) because PhoneAppStubScreen is the leanest
 * upstream screen that instantiates all three (wallpaper +
 * status bar + soft-key bar) in one place.
 *
 * Constructor takes an app-name string for the centred title.
 * We pass "MP2.4" to advertise the porting context. Same
 * heap-leak + leave-on-display pattern as WelcomeFactory:
 *
 *   - Screen is heap-allocated and intentionally never freed.
 *   - On a BTN_BACK or BTN_ENTER press the screen's
 *     buttonPressed handler calls LVScreen::pop(), which tries
 *     to return to the parent. We never set a parent (this is
 *     the boot screen), so pop()'s behaviour with no parent is
 *     either a no-op or an assertion fire — TBD when first
 *     button-press hits hardware. Either way, no successor
 *     screen is wired in this commit.
 */

#include "Screens/PhoneAppStubScreen.h"

extern "C" void chatter_app_start_appstub_screen(void)
{
    /* Caller of PhoneAppStubScreen's ctor must outlive the screen
     * — string literal is fine. The persistent name advertises
     * that the firmware is the MP2.4 port-in-progress. */
    auto *s = new PhoneAppStubScreen("MP2.4");
    s->start(false);
}
