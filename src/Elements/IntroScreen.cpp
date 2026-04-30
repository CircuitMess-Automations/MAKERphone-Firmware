#include "IntroScreen.h"
#include "../Screens/MainMenu.h"
#include "../Screens/LockScreen.h"
#include "../MAKERphoneConfig.h"

#if MAKERPHONE_USE_HOMESCREEN
#include "../Screens/PhoneHomeScreen.h"

// Free-function softkey handler (PhoneHomeScreen::SoftKeyHandler is a plain
// function pointer, not a std::function, so we cannot capture state). When
// the user taps the right softkey ("MENU") on the home screen we push a
// freshly-allocated MainMenu so every legacy feature (inbox, friends,
// profile, games, settings) is one keypress away while the new phone-style
// main menu (S19/S20) is being built. push() sets the home screen as the
// menu's parent, so BTN_BACK / BTN_R inside MainMenu still re-locks via the
// existing LockScreen.activate(this) path - and once MainMenu later pops it
// is auto-deleted by LVScreen::pop(). No leak, no behavioural regression.
static void launchLegacyMainMenu(PhoneHomeScreen* self){
	if(self == nullptr) return;
	self->push(new MainMenu());
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
		// S18: boot through LockScreen into the new MAKERphone home screen
		// (synthwave wallpaper + retro clock + soft-keys). The LockScreen
		// resumes its parent on unlock, so PhoneHomeScreen is what greets
		// the user once they slide-to-unlock. The right softkey ("MENU")
		// hops to the legacy MainMenu - keeping every existing feature
		// reachable while the new phone-style main menu is being built.
		auto* home = new PhoneHomeScreen();
		home->setOnRightSoftKey(launchLegacyMainMenu);
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
