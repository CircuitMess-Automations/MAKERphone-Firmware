#include "IntroScreen.h"
#include "../Screens/MainMenu.h"
#include "../Screens/LockScreen.h"
#include "../MAKERphoneConfig.h"

#if MAKERPHONE_USE_HOMESCREEN
#include "../Screens/PhoneHomeScreen.h"
#include "../Screens/PhoneMainMenu.h"
#include "../Screens/PhoneAppStubScreen.h"
#include "../Screens/InboxScreen.h"
#include "../Screens/FriendsScreen.h"
#include "../Screens/SettingsScreen.h"
#include "../Screens/GamesScreen.h"
#include "../Interface/LVScreen.h"

// S20: Per-icon dispatch from the new phone-style PhoneMainMenu.
//
// PhoneMainMenu::SoftKeyHandler is a plain function pointer, not a
// std::function, so we cannot capture state - the handler reads the
// currently-focused icon back off the menu and pushes the appropriate
// concrete screen. Icons that do not yet have a real implementation
// (Phone dialer / Music player / Camera) push PhoneAppStubScreen so the
// user sees the dispatch happen and can BACK out, while the actually
// implemented Chatter equivalents (Messages -> InboxScreen,
// Contacts -> FriendsScreen, Games -> GamesScreen, Settings ->
// SettingsScreen) are reachable today.
//
// Note: we instantiate the concrete legacy screens lazily inside the
// handler so the resulting binary still links if any of the legacy
// screens are #ifdef'd out in the future. Each legacy screen extends
// LVScreen and self-deletes on pop(), so leaving the new instance
// dangling here would leak - push() takes ownership and pop() in the
// child screen frees it.
static void launchPhoneMainMenuIcon(PhoneMainMenu* self){
	if(self == nullptr) return;

	LVScreen* dest = nullptr;
	switch(self->getSelectedIcon()){
		case PhoneIconTile::Icon::Messages:
			// Chat / inbox is the closest current Chatter equivalent of
			// "Messages" on a feature-phone. The InboxScreen lists every
			// conversation and lets the user open one to read/reply.
			dest = new InboxScreen();
			break;

		case PhoneIconTile::Icon::Contacts:
			// "Contacts" maps to the existing Friends list - same data
			// model (the LoRa peers the user has paired with). Phase F
			// (S35-S38) replaces this with a phone-style contacts screen
			// built on top of the Friends storage; for now FriendsScreen
			// is what the icon launches.
			dest = new FriendsScreen();
			break;

		case PhoneIconTile::Icon::Games:
			// Games tile maps directly onto the existing GamesScreen
			// (Snake / Pong / SpaceInvaders / SpaceRocks). S65 later
			// replaces this with a phone-style PhoneGames grid.
			dest = new GamesScreen();
			break;

		case PhoneIconTile::Icon::Settings:
			// Settings tile maps onto the existing SettingsScreen.
			// Phase J (S50-S55) restyles this into a phone-style
			// PhoneSettingsScreen; for now the legacy screen is what
			// the icon launches.
			dest = new SettingsScreen();
			break;

		case PhoneIconTile::Icon::Phone:
			// Dialer ships in S23 (PhoneDialerScreen). Until then the
			// stub keeps the dispatch wiring exercised.
			dest = new PhoneAppStubScreen("PHONE");
			break;

		case PhoneIconTile::Icon::Music:
			// Music player ships in S42 (PhoneMusicPlayer).
			dest = new PhoneAppStubScreen("MUSIC");
			break;

		case PhoneIconTile::Icon::Camera:
			// Camera ships in S44 (PhoneCameraScreen).
			dest = new PhoneAppStubScreen("CAMERA");
			break;

		case PhoneIconTile::Icon::Mail:
			// Not actually exposed by PhoneMainMenu in S19 (Mail and
			// Messages would duplicate each other on the grid), but the
			// switch is exhaustive so future menu re-orderings still
			// compile cleanly.
			dest = new PhoneAppStubScreen("MAIL");
			break;
	}

	if(dest != nullptr){
		// push() reparents `dest` under `self`, so when the child screen
		// pop()s the user lands back on the PhoneMainMenu icon grid with
		// the same selection still focused. Same lifetime contract every
		// LVScreen consumer in this codebase relies on.
		self->push(dest);
	}
}

// S22: forward declarations for the long-press helper handlers defined
// further down in this file (they need to be visible to launchPhoneMainMenu
// below, which wires them onto each freshly-built PhoneMainMenu).
static void launchQuickDialFromMenu(PhoneMainMenu* self);
static void lockFromMenu(PhoneMainMenu* self);

// Free-function softkey handler (PhoneHomeScreen::SoftKeyHandler is a plain
// function pointer, not a std::function, so we cannot capture state). When
// the user taps the right softkey ("MENU") on the home screen we push a
// freshly-allocated PhoneMainMenu (S19) wired to the per-icon dispatch
// above (S20). The new menu replaces the legacy vertical carousel; the
// carousel class still exists in the source tree (we keep it around for
// reference and as a build-flag fallback) but is no longer reachable from
// the default user flow.
static void launchPhoneMainMenu(PhoneHomeScreen* self){
	if(self == nullptr) return;
	auto* menu = new PhoneMainMenu();
	menu->setOnSelect(launchPhoneMainMenuIcon);
	// S22: same long-press shortcuts as the homescreen, so muscle memory
	// works from anywhere in the top-level UI.
	menu->setOnQuickDial(launchQuickDialFromMenu);
	menu->setOnLockHold(lockFromMenu);
	// Leave setOnBack unset so PhoneMainMenu's built-in default (pop()
	// back to the home screen) is what BACK does - exactly what we want.
	//
	// S21: push the menu with a horizontal slide (new menu enters from
	// the right, home slides off to the left) for the feature-phone
	// "drill into menu" feel. PhoneMainMenu's default BTN_BACK then pops
	// with LV_SCR_LOAD_ANIM_MOVE_RIGHT so the unwound transition is the
	// visual mirror of this push - a signature SE-style flick.
	self->push(menu, LV_SCR_LOAD_ANIM_MOVE_LEFT);
}

// S22: long-press shortcut handlers shared by PhoneHomeScreen and
// PhoneMainMenu. Both screens emit a hold-0 / hold-Back gesture through
// their own callback hooks; the host wires both screens to the same
// free functions below so the behaviour is identical from either entry.

// "Hold 0 to quick-dial" - lands in the dialer with the user's quick-
// dial number pre-loaded. The dialer ships in S23, so for now we push a
// PhoneAppStubScreen labelled "QUICK DIAL" so the gesture is visibly
// wired and back-able. Once S23 lands, this single helper is the place
// to swap the stub for the real PhoneDialerScreen.
static void launchQuickDialFromHome(PhoneHomeScreen* self){
	if(self == nullptr) return;
	auto* stub = new PhoneAppStubScreen("QUICK DIAL");
	self->push(stub);
}
static void launchQuickDialFromMenu(PhoneMainMenu* self){
	if(self == nullptr) return;
	auto* stub = new PhoneAppStubScreen("QUICK DIAL");
	self->push(stub);
}

// "Hold Back to lock" - drop into the LockScreen. LockScreen::activate
// stops the current screen and resumes it on unlock, so the user lands
// back exactly where they were holding from. Same call-pattern that
// MainMenu (legacy) and SleepService already use.
static void lockFromHome(PhoneHomeScreen* self){
	if(self == nullptr) return;
	LockScreen::activate(self);
}
static void lockFromMenu(PhoneMainMenu* self){
	if(self == nullptr) return;
	LockScreen::activate(self);
}
#endif

IntroScreen::IntroScreen(void (* callback)()) : callback(callback){
	gif = lv_gif_create(obj);
	lv_gif_set_src(gif, "S:/intro.gif");
	lv_gif_set_loop(gif, LV_GIF_LOOP_SINGLE);
	lv_gif_stop(gif);
	lv_gif_restart(gif);

	lv_obj_add_event_cb(gif, [](lv_event_t * e){
		IntroScreen* intro = static_cast<IntroScreen*>(e->user_data);
		intro->stop();
		volatile auto temp  = intro->callback;
		lv_obj_del(intro->getLvObj());

#if MAKERPHONE_USE_HOMESCREEN
		// S18 + S20: boot through LockScreen into the new MAKERphone home
		// screen (synthwave wallpaper + retro clock + soft-keys). The
		// LockScreen resumes its parent on unlock, so PhoneHomeScreen is
		// what greets the user once they slide-to-unlock. Right softkey
		// ("MENU") now hops to the new phone-style PhoneMainMenu (S19)
		// with each icon wired (S20) to its current Chatter equivalent
		// or to PhoneAppStubScreen for apps not yet built.
		auto* home = new PhoneHomeScreen();
		home->setOnRightSoftKey(launchPhoneMainMenu);
		// S22: long-press shortcuts on the homescreen.
		home->setOnQuickDial(launchQuickDialFromHome);
		home->setOnLockHold(lockFromHome);
		LockScreen::activate(home);
#else
		// Legacy boot path - LockScreen resumes directly into MainMenu,
		// matching the original Chatter firmware. Kept for fall-back via
		// the MAKERPHONE_USE_HOMESCREEN build flag (see MAKERphoneConfig.h).
		MainMenu* menu = new MainMenu();
		LockScreen::activate(menu);
#endif

		if(temp != nullptr) temp();
	}, LV_EVENT_READY, this);
}

void IntroScreen::onStart(){
	lv_gif_restart(gif);
	lv_gif_start(gif);
}

void IntroScreen::onStop(){
	lv_gif_stop(gif);
	lv_obj_del(gif);
}
