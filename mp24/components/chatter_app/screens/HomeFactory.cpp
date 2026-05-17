/*
 * mp24/components/chatter_app/screens/HomeFactory.cpp
 *
 * Boot-path factory for PhoneHomeScreen + navigation wiring.
 *
 * Original (S-MP18z): bare PhoneHomeScreen with no callbacks —
 * soft-keys flash but go nowhere.
 *
 * S-MP19 (current): wires the two main navigation hooks that
 * have working destinations in our compile set:
 *
 *   - BTN_RIGHT (MENU)        → push PhoneMainMenu
 *   - BTN_BACK hold (lock)    → push LockScreen
 *
 * BTN_LEFT (CALL) intentionally stays unwired — its natural
 * destination is PhoneDialerScreen which we can't compile yet
 * (Snake Easter-egg pulls Games engine → glm.h). The soft-key
 * still flashes when pressed; it just has no callback.
 *
 * Heap-leak pattern: the menu / lock instance pushed by each
 * navigation handler is never freed — push() transfers it into
 * LVGL's screen-load animation logic which keeps the new screen
 * active until something else pushes over it. A second visit to
 * the menu leaks a second instance; deferred fix.
 *
 * s_home stays a module-static pointer so the callbacks can
 * invoke s_home->push(...). PhoneHomeScreen itself isn't aware
 * of its own pointer.
 */

#include "Screens/PhoneHomeScreen.h"
#include "Screens/PhoneMainMenu.h"
#include "Screens/LockScreen.h"

static PhoneHomeScreen *s_home = nullptr;

static void on_menu_softkey(PhoneHomeScreen * /*self*/)
{
    /* Right softkey = MENU. Heap-allocate a new PhoneMainMenu
     * and push it. */
    auto *menu = new PhoneMainMenu();
    s_home->push(menu);
}

static void on_lock_hold(PhoneHomeScreen * /*self*/)
{
    /* Long-press BTN_BACK = lock. Same push pattern. */
    auto *lock = new LockScreen();
    s_home->push(lock);
}

extern "C" void chatter_app_start_home_screen(void)
{
    s_home = new PhoneHomeScreen();

    /* S-MP19: wire MENU and lock-hold callbacks. CALL (BTN_LEFT)
     * deliberately stays unwired pending the dialer screen. */
    s_home->setOnRightSoftKey(on_menu_softkey);
    s_home->setOnLockHold(on_lock_hold);

    s_home->start(false);
}
