#include "IntroScreen.h"
#include "../Screens/MainMenu.h"
#include "../Screens/LockScreen.h"
#include "../MAKERphoneConfig.h"

#if MAKERPHONE_USE_HOMESCREEN
#include "../Screens/PhoneHomeScreen.h"
#include "../Screens/PhoneMainMenu.h"
#include "../Screens/PhoneAppStubScreen.h"
#include "../Screens/PhoneMusicPlayer.h"
#include "../Screens/PhoneCameraScreen.h"
#include "../Screens/PhoneDialerScreen.h"
#include "../Screens/InboxScreen.h"
#include "../Screens/FriendsScreen.h"
#include "../Screens/PhoneContactsScreen.h"
#include "../Screens/PhoneContactDetail.h"
#include "../Screens/PhoneContactEdit.h"
#include "../Screens/ConvoScreen.h"
#include "../Screens/SettingsScreen.h"
#include "../Screens/PhoneSettingsScreen.h"
#include "../Screens/PhoneBrightnessScreen.h"
#include "../Screens/PhoneSoundScreen.h"
#include "../Screens/PhoneHapticsScreen.h"
#include "../Screens/PhoneWallpaperScreen.h"
#include "../Screens/PhoneDateTimeScreen.h"
#include "../Screens/PhoneAboutScreen.h"
#include "../Screens/PhoneThemeScreen.h"
#include "../Screens/PhoneOwnerNameScreen.h"
#include "../Screens/PhonePowerOffMessageScreen.h"
#include "../Screens/PhoneWelcomeScreen.h"
#include "../Screens/PhoneGamesScreen.h"
#include "../Interface/LVScreen.h"
#include "../Interface/PhoneTransitions.h"
#include "../Services/PhoneCallService.h"

// S20: Per-icon dispatch from the new phone-style PhoneMainMenu.
//
// PhoneMainMenu::SoftKeyHandler is a plain function pointer, not a
// std::function, so we cannot capture state - the handler reads the
// currently-focused icon back off the menu and pushes the appropriate
// concrete screen. Icons that do not yet have a real implementation
// (Music player / Camera) push PhoneAppStubScreen so the user sees the
// dispatch happen and can BACK out. The actually-implemented Chatter
// equivalents (Messages -> InboxScreen, Contacts -> FriendsScreen,
// Games -> GamesScreen, Settings -> SettingsScreen) are reachable today,
// and S23 wired the Phone tile through to PhoneDialerScreen as well.
//
// Note: we instantiate the concrete legacy screens lazily inside the
// handler so the resulting binary still links if any of the legacy
// screens are #ifdef'd out in the future. Each legacy screen extends
// LVScreen and self-deletes on pop(), so leaving the new instance
// dangling here would leak - push() takes ownership and pop() in the
// child screen frees it.
// S37: handlers wiring PhoneContactsScreen -> PhoneContactDetail. The
// detail screen's CALL action delegates to PhoneCallService::placeCall
// (the same path PhoneDialerScreen uses to ring a peer); MESSAGE pushes
// a ConvoScreen for the contact's uid (the InboxScreen already does the
// same thing when a row is opened, so the muscle memory transfers).
// Sample / placeholder rows from the S36 fallback list have uid==0 -
// we no-op the actions on those so the user gets a flash but nothing
// nonsensical happens.
// S145: stash for the PhoneWelcomeScreen dismiss callback. The dismiss
// callback is a plain `void (*)()` (no captures), so the IntroScreen
// READY handler hands the freshly-built PhoneHomeScreen off through
// this single-slot static. The slot is cleared by the callback as
// soon as it consumes the pointer so a later (somehow) reuse of the
// welcome screen cannot accidentally re-activate a stale home.
static PhoneHomeScreen* s_pendingHomeForWelcome = nullptr;

static void contactDetailCall(PhoneContactDetail* self){
	if(self == nullptr) return;
	const UID_t uid = self->getUid();
	if(uid == 0) return;
	Phone.placeCall(uid);
}

static void contactDetailMessage(PhoneContactDetail* self){
	if(self == nullptr) return;
	const UID_t uid = self->getUid();
	if(uid == 0) return;
	self->push(new ConvoScreen(uid));
}

// S38: long-pressing ENTER on a PhoneContactDetail opens the contact
// editor for that uid. Sample rows (uid==0) cannot be edited so the
// gesture flashes but no-ops, matching the CALL / MESSAGE convention
// the detail screen uses for placeholder contacts.
static void contactDetailEdit(PhoneContactDetail* self){
	if(self == nullptr) return;
	const UID_t uid = self->getUid();
	if(uid == 0) return;
	auto* editor = new PhoneContactEdit(uid,
										self->getName(),
										self->getAvatarSeed());
	// Leave setOnSave / setOnBack unset so the editor's default
	// behaviour applies: SAVE persists through PhoneContacts and
	// pops, BACK pops without persisting. Returning to the detail
	// screen lets the user immediately see the new name / avatar
	// because the detail's own labels are written from the same
	// helpers the editor wrote into.
	self->push(editor);
}

static void launchContactDetail(PhoneContactsScreen* self,
								const PhoneContactsScreen::Entry& entry){
	if(self == nullptr) return;
	auto* detail = new PhoneContactDetail(entry.uid,
										  entry.name,
										  entry.avatarSeed,
										  entry.favorite != 0);
	detail->setOnCall(contactDetailCall);
	detail->setOnMessage(contactDetailMessage);
	// S38: hold-ENTER on the detail screen opens the contact editor.
	detail->setOnEdit(contactDetailEdit);
	// Leave setOnBack unset so the default pop() walks the user back
	// to the contacts list with the same row still focused.
	self->push(detail);
}

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

		case PhoneIconTile::Icon::Contacts: {
			// S36: phone-style contacts screen built on the S35
			// PhoneContacts data model. Walks Storage.Friends.all()
			// and renders the paired peers as an alphabetised
			// phone-book with an A-Z scroll-strip on the right. Falls
			// back to a representative sample list when no peers are
			// paired yet, so the screen reads as a real phone-book on
			// first boot. S37 now wires the OPEN softkey to push a
			// PhoneContactDetail screen for the focused row, so the
			// user lands on the vCard view with CALL / MESSAGE actions.
			auto* contacts = new PhoneContactsScreen();
			contacts->setOnOpen(launchContactDetail);
			dest = contacts;
			break;
		}

		case PhoneIconTile::Icon::Games:
			// S65: Games tile now routes to the phone-styled
			// PhoneGamesScreen (2x2 grid of code-only retro game
			// cards over the synthwave wallpaper) instead of the
			// legacy ListItem-based GamesScreen. The four games
			// themselves (Space Rocks / Invaderz / Snake / Bonk)
			// are launched directly by the new screen using the
			// same load/splash/start sequence the legacy launcher
			// already proved out.
			dest = new PhoneGamesScreen();
			break;

		case PhoneIconTile::Icon::Settings: {
			// S50: phone-style PhoneSettingsScreen replaces the legacy
			// SettingsScreen as the destination of the Settings tile.
			// The new screen lists every Phase-J sub-page (Brightness
			// S51, Wallpaper S53, Sound & Vibration S52, Date & Time
			// S54, About S55) as a chevron row inside grouped sections.
			//
			// S51: the Brightness row now drills into the real
			// PhoneBrightnessScreen instead of the placeholder stub;
			// every other row is still a stub until S52-S55 land. We
			// override the activate dispatch with a single function
			// pointer (matching the PhoneSettingsScreen::ActivateHandler
			// signature) and let it fall through to the screen's
			// built-in launchDefault() for the unimplemented rows.
			auto* settings = new PhoneSettingsScreen();
			settings->setOnActivate([](PhoneSettingsScreen* self,
									   PhoneSettingsScreen::Item item){
				if(self == nullptr) return;
				switch(item){
					case PhoneSettingsScreen::Item::Brightness:
						self->push(new PhoneBrightnessScreen());
						break;
					case PhoneSettingsScreen::Item::Wallpaper:
						// S53: real wallpaper picker replaces the WALLPAPER
						// placeholder stub. PhoneWallpaperScreen reads the
						// persisted style from Settings, lets the user step
						// through the four PhoneSynthwaveBg variants
						// (Synthwave / Plain / GridOnly / Stars) with a
						// live swatch preview, and persists the choice on
						// SAVE so every subsequent screen drop picks up
						// the new look.
						self->push(new PhoneWallpaperScreen());
						break;
					case PhoneSettingsScreen::Item::Sound:
						// S52: Sound + Vibration toggles screen replaces the
						// SOUND placeholder stub. PhoneSoundScreen reads the
						// persisted profile from Settings and lets the user
						// switch between Mute / Vibrate / Loud, persisting
						// the choice on SAVE and reverting on BACK.
						self->push(new PhoneSoundScreen());
						break;
					case PhoneSettingsScreen::Item::Haptics:
						// S68: subtle haptic-style nav-key tick toggle.
						// PhoneHapticsScreen lets the user opt the Mute /
						// Vibrate profiles into a very short, very quiet
						// click on directional / ENTER / BACK presses so
						// the keypad still feels responsive even when the
						// global ringer is off. Persists into
						// Settings.keyTicks; the BuzzerService reads that
						// flag on every buttonPressed() and emits the tick
						// only when sound is otherwise off.
						self->push(new PhoneHapticsScreen());
						break;
					case PhoneSettingsScreen::Item::DateTime:
						// S54: real Date & Time editor replaces the placeholder
						// stub. PhoneDateTimeScreen edits a copy of the wall
						// clock held by PhoneClock (the in-memory wall-clock
						// service introduced in S54) and only commits on SAVE,
						// so a feature-phone-style five-field civil-time editor
						// drives the underlying clock without leaking through
						// any of the existing uptime-driven widgets.
						self->push(new PhoneDateTimeScreen());
						break;
					case PhoneSettingsScreen::Item::About:
						// S55: real About page replaces the placeholder stub. The
						// PhoneAboutScreen is a read-only diagnostics page that shows
						// the device id (efuse MAC), firmware version, free heap,
						// uptime, and paired peer count, with the heap + uptime
						// fields refreshed at 1 Hz so the user sees a live readout.
						self->push(new PhoneAboutScreen());
						break;
					case PhoneSettingsScreen::Item::Owner:
						// S144: owner-name editor replaces the OWNER
						// placeholder stub. PhoneOwnerNameScreen is a
						// single-buffer T9 entry (PhoneT9Input + the
						// usual PhoneScratchpad-style scaffolding) that
						// reads / writes Settings.ownerName directly,
						// so the LockScreen picks the new name up on
						// the next push without any extra wiring. The
						// stub fallback in launchDefault() still works
						// for any future row that lands without a
						// dedicated screen yet.
						self->push(new PhoneOwnerNameScreen());
						break;
					case PhoneSettingsScreen::Item::PowerOffMsg:
						// S146: custom power-off message editor
						// replaces the POWER-OFF MSG placeholder stub.
						// PhonePowerOffMessageScreen is a near-identical
						// twin of PhoneOwnerNameScreen (single-buffer
						// T9 entry, persistence into a 24-byte slot)
						// that reads / writes Settings.powerOffMessage
						// directly, so PhonePowerDown picks the new
						// text up on the next push without any extra
						// wiring. An empty buffer (factory default or
						// after CLEAR) leaves PhonePowerDown's preamble
						// dormant and the existing CRT shrink fires
						// immediately, exactly the way every prior
						// host expects.
						self->push(new PhonePowerOffMessageScreen());
						break;
					case PhoneSettingsScreen::Item::Theme:
						// S101: global theme picker drills into the Default
						// Synthwave / Nokia 3310 Monochrome pair (Phase O
						// grows the list to 10 themes by S119). Each theme
						// bundles a palette + wallpaper variant; selecting a
						// non-default theme overrides the wallpaperStyle byte
						// for as long as it is selected, so the next screen
						// push picks up the matching palette + wallpaper via
						// PhoneSynthwaveBg::resolveStyleFromSettings().
						self->push(new PhoneThemeScreen());
						break;
					default:
						// Defensive: any future row that is added to
						// the Item enum without being wired here lands
						// on a generic stub rather than crashing.
						self->push(new PhoneAppStubScreen("SETTINGS"));
						break;
				}
			});
			dest = settings;
			break;
		}

		case PhoneIconTile::Icon::Phone:
			// S23: real dialer screen replaces the placeholder stub. The
			// CALL softkey on the dialer is currently inert (the actual
			// call screens land in S24-S28); BACK on a non-empty buffer
			// backspaces, BACK on an empty buffer pops back to the menu.
			dest = new PhoneDialerScreen();
			break;

		case PhoneIconTile::Icon::Music:
			// S42: real music-player screen replaces the placeholder stub.
			// S43: PhoneMusicPlayer now self-seeds with the proper 10-tune
			// PhoneMusicLibrary (Neon Drive, Pixel Sunrise, Cyber Dawn,
			// Crystal Cave, Hyperloop, Starfall, Retro Quest, Moonlit
			// Drift, Arcade Hero, Sunset Blvd) — non-looping tracks that
			// auto-advance through the playlist.
			dest = new PhoneMusicPlayer();
			break;

		case PhoneIconTile::Icon::Camera:
			// S44: real camera screen replaces the placeholder stub. The
			// retro viewfinder (corner brackets, dotted edge ticks,
			// crosshair, REC dot, mode label, frame counter) lives in
			// PhoneCameraScreen; ENTER fires the shutter (cyan flash +
			// 3-note click via the global PhoneRingtoneEngine), BACK
			// pops back to the main menu. Mode-cycling on L/R bumpers
			// (PHOTO / EFFECT / SELFIE) ships in S45.
			dest = new PhoneCameraScreen();
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
		// S66: route every main-menu->app push through PhoneTransitions
		// with the Drill gesture so each launch uses the same SE-style
		// horizontal flick (slide-in-from-right). The reparenting and
		// pop-back-to-menu lifetime contract is unchanged - the helper
		// just forwards into LVScreen::push under the hood.
		PhoneTransitions::push(self, dest, PhoneTransition::Drill);
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
	// S66: route through PhoneTransitions so the home->menu drill
	// uses the same Drill gesture every other "go one level deeper"
	// site in the firmware does. Identical visual result
	// (LV_SCR_LOAD_ANIM_MOVE_LEFT) - the helper just centralises the
	// choice so future tweaks live in one file.
	PhoneTransitions::push(self, menu, PhoneTransition::Drill);
}

// S22: long-press shortcut handlers shared by PhoneHomeScreen and
// PhoneMainMenu. Both screens emit a hold-0 / hold-Back gesture through
// their own callback hooks; the host wires both screens to the same
// free functions below so the behaviour is identical from either entry.

// "Hold 0 to quick-dial" - lands in the dialer with the user's quick-
// dial number pre-loaded. S23 swapped the placeholder stub for the real
// PhoneDialerScreen; the quick-dial-number pre-load itself comes with
// the contacts work in Phase F (S35-S38) once the user has a way to
// set "my quick-dial peer". For S23 the gesture just opens an empty
// dialer ready for input.
static void launchQuickDialFromHome(PhoneHomeScreen* self){
	if(self == nullptr) return;
	// S23: the long-press-0 quick-dial gesture (S22) now lands directly
	// in the real PhoneDialerScreen instead of the placeholder stub. We
	// do not pre-populate a quick-dial number yet - that comes with the
	// contacts work in Phase F (S35-S38) which gives the user a real
	// "set my quick-dial peer" entry to draw from. For now the dialer
	// just opens with an empty buffer ready for input.
	auto* dialer = new PhoneDialerScreen();
	self->push(dialer);
}
static void launchQuickDialFromMenu(PhoneMainMenu* self){
	if(self == nullptr) return;
	// S23: same swap as launchQuickDialFromHome - the gesture from the
	// main menu now opens the real dialer rather than the stub.
	auto* dialer = new PhoneDialerScreen();
	self->push(dialer);
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

		// S145: if the user has set Settings.ownerName via the S144
		// PhoneOwnerNameScreen, flash a "Hello, $NAME!" welcome
		// greeting between the intro gif and the lock screen. The
		// PhoneWelcomeScreen auto-dismisses after ~1.5 s (or any-key
		// skip) and then calls our continuation callback, which
		// activates the lock screen with the pre-built home as
		// parent - exactly the same handoff the unconditional path
		// below performs. On a freshly-flashed device with no name
		// set we skip the greeting entirely so the boot flow looks
		// identical to pre-S145 builds.
		if(PhoneWelcomeScreen::isEnabled()){
			s_pendingHomeForWelcome = home;
			auto* welcome = new PhoneWelcomeScreen([](){
				auto* h = s_pendingHomeForWelcome;
				s_pendingHomeForWelcome = nullptr;
				if(h != nullptr) LockScreen::activate(h);
			});
			welcome->start();
		}else{
			LockScreen::activate(home);
		}
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
