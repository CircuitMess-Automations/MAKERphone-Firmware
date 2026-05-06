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
// S159 - classic Sony-Ericsson five-state Profile picker that supersedes
// the S52 three-state PhoneSoundScreen as the user-facing surface for the
// SOUND row of PhoneSettingsScreen. The legacy soundProfile + sound fields
// are still written by PhoneProfileScreen on save so existing readers stay
// untouched.
#include "../Screens/PhoneProfileScreen.h"
#include "../Screens/PhoneHapticsScreen.h"
#include "../Screens/PhoneWallpaperScreen.h"
#include "../Screens/PhoneDateTimeScreen.h"
#include "../Screens/PhoneAboutScreen.h"
#include "../Screens/PhoneThemeScreen.h"
#include "../Screens/PhoneLockWidgetScreen.h"
#include "../Screens/PhoneHomeLayoutScreen.h"
#include "../Screens/PhoneAccentScreen.h"
#include "../Screens/PhoneOwnerNameScreen.h"
#include "../Screens/PhoneOwnerEmojiScreen.h"
#include "../Screens/PhonePowerOffMessageScreen.h"
#include "../Screens/PhoneOperatorScreen.h"
#include "../Screens/PhoneSpeedDialScreen.h"
#include "../Screens/PhoneProfileRingtoneScreen.h"
#include "../Screens/PhoneSoftKeyToneScreen.h"
#include "../Screens/PhoneWelcomeScreen.h"
#include "../Screens/PhoneGamesScreen.h"
#include "../Interface/LVScreen.h"
#include "../Interface/PhoneTransitions.h"
#include "../Services/PhoneCallService.h"
#include "../Storage/Storage.h"
#include <Settings.h>

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
						// S159: classic Sony-Ericsson "Profile" five-state
						// selector (General / Silent / Meeting / Outdoor /
						// Headset) supersedes the S52 three-state Mute /
						// Vibrate / Loud picker as the user-facing surface
						// for the SOUND row of PhoneSettingsScreen.
						// PhoneProfileScreen reads the persisted profile
						// from Settings.phoneProfile, lets the user switch
						// between the five named profiles, and on SAVE fans
						// the choice out to the legacy soundProfile + sound
						// fields so every existing reader (BuzzerService,
						// PhoneRingtoneEngine, SettingsScreen,
						// PhoneSoundScreen as a back-compat picker) keeps
						// working with no churn.
						self->push(new PhoneProfileScreen());
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
					case PhoneSettingsScreen::Item::OwnerEmoji:
						// S188: owner emoji / avatar picker replaces the
						// OWNER EMOJI placeholder stub. PhoneOwnerEmojiScreen
						// is a horizontal pager over the curated
						// PhoneOwnerEmoji catalogue (None / Heart / Star /
						// Smile / Music / Crown / Skull / Bolt / Cat /
						// Coffee / Pizza / Dice / Rocket) that reads /
						// writes Settings.ownerEmoji directly, so the
						// LockScreen picks the new glyph up on the next
						// push without any extra wiring. The stub fallback
						// in launchDefault() still works for any future row
						// that lands without a dedicated screen yet.
						self->push(new PhoneOwnerEmojiScreen());
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
					case PhoneSettingsScreen::Item::Operator:
						// S147: operator-banner editor replaces the
						// OPERATOR placeholder stub. PhoneOperatorScreen
						// is a two-mode editor (T9 entry for the carrier
						// name + a 16x5 cursor-driven pixel toggler for
						// the bitmap) that reads / writes
						// Settings.operatorText and
						// Settings.operatorLogo directly, so
						// PhoneHomeScreen's PhoneOperatorBanner picks
						// the new banner up on the next push without
						// any extra wiring. The stub fallback in
						// launchDefault() still works for any future
						// row that lands without a dedicated screen
						// yet.
						self->push(new PhoneOperatorScreen());
						break;
					case PhoneSettingsScreen::Item::SpeedDial:
						// S151 (UI half): per-slot speed-dial configuration
						// editor replaces the SPEED DIAL placeholder stub.
						// PhoneSpeedDialScreen exposes the nine 1..9 slots
						// (slot 0 stays reserved for the S22 hold-0 quick-dial
						// gesture) and lets the user pick a paired contact for
						// each, persisting each pick into Settings.speedDial[d]
						// directly. The S151 gesture half on PhoneHomeScreen
						// (long-press 1..9 -> launchSpeedDialFromHome ->
						// Phone.placeCall) reads the same array on every press
						// so the new assignment takes effect from the next
						// homescreen long-press without any extra wiring.
						self->push(new PhoneSpeedDialScreen());
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
					case PhoneSettingsScreen::Item::ProfileRingtone:
						// S160: per-profile ringtone selection drills into
						// PhoneProfileRingtoneScreen, a two-mode list+pick
						// screen patterned after PhoneSpeedDialScreen. The
						// list view shows the five PhoneProfileScreen
						// profiles (General / Silent / Meeting / Outdoor /
						// Headset) each with its current ringtone name; the
						// pick view replays the standard PhoneContactRingtone
						// picker (5 library tones plus any populated
						// composer slots). Selection is persisted into
						// Settings.profileRingtones[5] via
						// PhoneContactRingtone::setProfileRingtoneId so
						// PhoneCallService picks up the active profile's
						// choice on the next incoming call from a peer that
						// has no contact override set.
						self->push(new PhoneProfileRingtoneScreen());
						break;
					case PhoneSettingsScreen::Item::SoftKeyTone:
						// S183: soft-key click-tone customisation. Drills
						// into PhoneSoftKeyToneScreen, a single-list picker
						// patterned after PhoneHapticsScreen / PhoneSoundScreen.
						// Lets the user pick which of the five PhoneSoftKeyToneLib
						// entries (Classic / Click / Bloop / Chirp / Silent)
						// the BuzzerService plays when the user taps BTN_LEFT
						// or BTN_RIGHT (the two Sony-Ericsson-style soft-key
						// hardware buttons). On SAVE the screen calls
						// PhoneSoftKeyToneLib::setActive() which writes
						// Settings.softKeyTone and flushes via Settings.store(),
						// so the BuzzerService picks the new tone up on the
						// next BTN_LEFT / BTN_RIGHT press without any extra
						// wiring. Classic (id 0, the factory default) keeps
						// the legacy NOTE_B4 / 25 ms feedback so a freshly-
						// flashed device sounds byte-identical to every prior
						// firmware on its soft-keys.
						self->push(new PhoneSoftKeyToneScreen());
						break;
					case PhoneSettingsScreen::Item::LockWidget:
						// S184: lock-screen widget composition picker. Drills
						// into PhoneLockWidgetScreen, a single-list picker
						// patterned after PhoneHapticsScreen / PhoneSoundScreen
						// that lets the user pick whether the LockScreen
						// renders the classic clock + weekday + date
						// (ClockDate, factory default), a HH:MM-only watch
						// face (ClockOnly), or a clock + next-armed-alarm
						// preview line (ClockEvent). On SAVE the screen
						// writes Settings.lockWidgetMode and flushes via
						// Settings.store(); the LockScreen picks the chosen
						// mode up on the next push without any extra wiring.
						// ClockDate (id 0, the factory default) keeps the
						// legacy weekday + date layout so a freshly-flashed
						// device looks byte-identical to every prior firmware
						// on its lock screen.
						self->push(new PhoneLockWidgetScreen());
						break;
					case PhoneSettingsScreen::Item::HomeLayout:
						// S185: home-screen layout-mode picker. Drills
						// into PhoneHomeLayoutScreen, a single-list
						// picker patterned after PhoneLockWidgetScreen
						// that lets the user pick whether
						// PhoneHomeScreen renders the classic Sony-
						// Ericsson silhouette (Classic, factory default),
						// a watch-face-style minimalist composition
						// (Minimal, no operator banner / tip banner /
						// idle hint), or an extra "HOLD 0:DIAL HOLD #:
						// LOCK" shortcut hint baked into the wallpaper
						// band (Stack). On SAVE the screen writes
						// Settings.homeLayoutMode and flushes via
						// Settings.store(); the home screen picks the
						// chosen mode up on the next push without any
						// extra wiring. Classic (id 0, the factory
						// default) keeps the legacy full-feature
						// homescreen so a freshly-flashed device looks
						// byte-identical to every prior firmware.
						self->push(new PhoneHomeLayoutScreen());
						break;
					case PhoneSettingsScreen::Item::Accent:
						// S187: custom RGB accent picker. Drills
						// into PhoneAccentScreen, a three-channel
						// R/G/B slider screen patterned after
						// PhoneBrightnessScreen but with a live
						// preview slab that repaints in the chosen
						// RGB so the user can dial in any 24-bit
						// accent and see what it looks like before
						// SAVE. On SAVE the screen writes
						// Settings.customAccentEnabled and the
						// three RGB byte slots and flushes via
						// Settings.store(); MakerphoneTheme::accent()
						// picks the chosen override up on the next
						// screen build, so every Phone* widget that
						// already calls the central resolver
						// (PhoneIconTile halo, PhoneSoftKeyBar arrow
						// tint, PhoneChatBubble Sent fill, ...)
						// repaints with the new colour without any
						// per-widget plumbing. With the override
						// left at factory default 0 the resolver
						// falls back to the per-theme accent map
						// exactly the way every prior firmware
						// shipped, so a freshly-flashed device
						// looks byte-identical until the user
						// dials in a custom hue.
						self->push(new PhoneAccentScreen());
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

// S151: long-press 1..9 on the homescreen looks up the matching speed-
// dial slot in Settings and either fires PhoneCallService::placeCall()
// on the assigned peer or falls through to opening an empty dialer
// when the slot has not been assigned yet. Keeping the fall-through
// to the dialer means the gesture is *always* meaningful — even on
// a fresh boot with no slots assigned the user lands somewhere they
// can actually dial from, instead of the long-press silently no-
// op'ing. Mirrors the S37 PhoneContactDetail "CALL" softkey wiring
// (Phone.placeCall on a paired peer) so the call-flow design is the
// same regardless of how the user reached it.
static void launchSpeedDialFromHome(PhoneHomeScreen* self, uint8_t digit){
	if(self == nullptr) return;
	if(digit < 1 || digit > 9) return;

	const UID_t uid = Settings.get().speedDial[digit];

	if(uid != 0 && Storage.Friends.exists(uid)){
		// Slot is set and the peer is still in the local Friends repo
		// (a wiped Storage between assignment and call would leave a
		// stale UID; we treat that the same as "unassigned" and fall
		// through). Fire the LoRa CALL_REQUEST through the same path
		// PhoneContactDetail's CALL softkey uses.
		Phone.placeCall(uid);
		return;
	}

	// Empty / stale slot: open the real dialer so the gesture still
	// reads as "start a call" instead of silently no-op'ing. Same
	// fall-through the S22 hold-0 quick-dial uses today, so the muscle
	// memory carries over to the unassigned digits.
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
		// S151: long-press 1..9 on the homescreen runs the speed-dial
		// dispatcher (look up Settings.speedDial[d] and fire
		// Phone.placeCall(uid) when set, otherwise open an empty
		// dialer so the gesture is always meaningful).
		home->setOnSpeedDial(launchSpeedDialFromHome);

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
