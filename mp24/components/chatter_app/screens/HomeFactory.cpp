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
#include "Screens/PhoneGamesScreen.h"
#include "Screens/LockScreen.h"
#include "Screens/InboxScreen.h"
#include "Screens/FriendsScreen.h"
#include "Screens/PhoneMusicPlayer.h"
#include "Screens/PhoneCameraScreen.h"
#include "Screens/PhoneSettingsScreen.h"
#include "Elements/PhoneIconTile.h"

#include "esp_log.h"

static const char *TAG = "HomeFactory";

static PhoneHomeScreen *s_home = nullptr;

/* Forward decls so the helper functions can reference each other. */
static void on_menu_softkey(PhoneHomeScreen *self);
static void on_lock_hold(PhoneHomeScreen *self);
static void on_menu_select(PhoneMainMenu *menu);

static void on_menu_softkey(PhoneHomeScreen * /*self*/)
{
    /* Right softkey = MENU. Heap-allocate a new PhoneMainMenu,
     * wire its SELECT dispatcher, then push. */
    auto *menu = new PhoneMainMenu();
    menu->setOnSelect(on_menu_select);
    s_home->push(menu);
}

static void on_lock_hold(PhoneHomeScreen * /*self*/)
{
    auto *lock = new LockScreen();
    s_home->push(lock);
}

/* S-MP19/2: dispatch the SELECT softkey on PhoneMainMenu to the
 * matching app screen. Eight tiles in the icon enum; mapping:
 *
 *   Phone     → (no destination — PhoneDialerScreen needs glm)
 *   Messages  → InboxScreen
 *   Contacts  → FriendsScreen
 *   Music     → PhoneMusicPlayer
 *   Camera    → PhoneCameraScreen
 *   Games     → PhoneGamesScreen (S-MP20/11)
 *   Settings  → PhoneSettingsScreen
 *   Mail      → InboxScreen (mail tile reuses inbox in MP2.4)
 *
 * Each pushed screen handles its own BACK via either explicit
 * pop() in its buttonPressed handler or the default
 * PhoneTransitions::pop() pattern. */
static void on_menu_select(PhoneMainMenu *menu)
{
    using Icon = PhoneIconTile::Icon;
    LVScreen *target = nullptr;
    switch (menu->getSelectedIcon()) {
        case Icon::Phone:    /* not compiled */                  break;
        case Icon::Messages: target = new InboxScreen();         break;
        case Icon::Contacts: target = new FriendsScreen();       break;
        case Icon::Music:    target = new PhoneMusicPlayer();    break;
        case Icon::Camera:   target = new PhoneCameraScreen();   break;
        case Icon::Games:    target = new PhoneGamesScreen();    break;
        case Icon::Settings: target = new PhoneSettingsScreen(); break;
        case Icon::Mail:     target = new InboxScreen();         break;
    }
    if (target) {
        menu->push(target);
    } else {
        ESP_LOGI(TAG, "icon %u has no compiled destination",
                 (unsigned)menu->getSelectedIcon());
    }
}

extern "C" void chatter_app_start_home_screen(void)
{
    s_home = new PhoneHomeScreen();
    s_home->setOnRightSoftKey(on_menu_softkey);
    s_home->setOnLockHold(on_lock_hold);
    s_home->start(false);
}
