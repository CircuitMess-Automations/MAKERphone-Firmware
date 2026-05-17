/*
 * mp24/components/chatter_app/screens/HomeFactory.cpp
 *
 * Boot-path factory for PhoneHomeScreen — the Sony-Ericsson-
 * silhouette homescreen with synthwave wallpaper + retro clock +
 * status bar + soft-key bar. Visually the most representative
 * MAKERphone 2.0 screen we have compiled at the moment.
 *
 * PhoneHomeScreen uses setter callbacks (setLeftCb, setRightCb,
 * setLockHoldCb, etc.) for navigation. We don't set any — the
 * screen renders + reacts to button presses with a soft-key
 * flash animation but goes nowhere. Sufficient for an on-device
 * smoke test of:
 *
 *   - PhoneSynthwaveBg wallpaper rendering
 *   - PhoneClockFace updating in real time (driven by PhoneClock)
 *   - PhoneStatusBar (top strip — time + battery + signal)
 *   - PhoneSoftKeyBar (bottom strip — soft-key labels)
 *   - PhoneIdleHint / PhoneTipBanner / PhoneNotificationToast
 *     overlay widgets if their trigger conditions hit
 *
 * Same heap-leak / no-parent pattern as WelcomeFactory and
 * AppStubFactory — screen is allocated, started, never freed.
 */

#include "Screens/PhoneHomeScreen.h"

extern "C" void chatter_app_start_home_screen(void)
{
    auto *s = new PhoneHomeScreen();
    s->start(false);
}
