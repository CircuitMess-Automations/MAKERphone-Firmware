#include "PhoneWelcomeScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <Settings.h>
#include <ctype.h>
#include <stddef.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - kept identical to every other Phone* widget so
// the welcome greeting slots in beside PhoneBootSplash (S56) and
// PhoneCallEnded (S26) without a visual seam. Inlined per the established
// pattern (see PhoneSynthwaveBg / PhoneOwnerNameScreen).
#define MP_BG_DARK      lv_color_make( 20,  12,  36)   // deep purple fallback
#define MP_ACCENT       lv_color_make(255, 140,  30)   // sunset orange (name)
#define MP_TEXT         lv_color_make(255, 220, 180)   // warm cream (greeting)
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)   // dim purple (hint)

// ---- vertical layout ------------------------------------------------------
//
// 160x128 budget. The greeting reads as a focal centerpiece so the labels
// cluster vertically near the geometric centre, with a faint hint glued
// to the bottom of the screen. Y-coordinates are absolute - the screen is
// a full-bleed canvas with no status bar / soft-key chrome.
static constexpr lv_coord_t kGreetY  = 44;   // "Hello," prefix line
static constexpr lv_coord_t kNameY   = 64;   // upper-cased owner name
static constexpr lv_coord_t kHintY   = 110;  // dim "press any key" footer

// ---- ctor / dtor ----------------------------------------------------------

PhoneWelcomeScreen::PhoneWelcomeScreen(DismissHandler onDismiss,
									   uint32_t       durationMs)
		: LVScreen(),
		  dismissCb(onDismiss),
		  durationMs(durationMs),
		  dismissedAlready(false),
		  wallpaper(nullptr),
		  greetLabel(nullptr),
		  nameLabel(nullptr),
		  hintLabel(nullptr),
		  dismissTimer(nullptr) {

	// Full-screen container, no scrollbars, zero padding - blank-canvas
	// pattern every other Phone* boot-flow screen uses. Hard-fill the
	// background with deep purple so a single-frame flush before the
	// synthwave wallpaper is built reads as MAKERphone purple rather
	// than the LVGL theme tint.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_border_width(obj, 0, 0);
	lv_obj_set_style_bg_color(obj, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);

	// Synthwave wallpaper at the bottom of the z-order. Picks up
	// whatever wallpaper / theme the user has selected (Synthwave,
	// Plain, GridOnly, Stars) so the greeting feels consistent with
	// the rest of the shell.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Build the labels in z-order back-to-front. LVGL z-orders by
	// creation order on a non-scrollable parent, so later builds sit
	// on top of earlier ones.
	buildGreeting();
	buildName();
	buildHint();
}

PhoneWelcomeScreen::~PhoneWelcomeScreen() {
	// Cancel any in-flight dismiss timer before LVGL teardown so its
	// callback never fires against a half-freed screen during
	// destruction. Same defensive pattern PhoneBootSplash uses.
	stopDismissTimer();
	// Every child is parented to obj. LVScreen's destructor frees obj
	// and LVGL recursively tears down their backing storage.
}

// ---- lifecycle ------------------------------------------------------------

void PhoneWelcomeScreen::onStart() {
	// Subscribe to hardware input so any key short-circuits the timer.
	// Every key path (digits, L/R, BACK, ENTER, bumpers) feeds through
	// Input::getInstance() and gets forwarded here via buttonPressed.
	Input::getInstance()->addListener(this);

	// Reset the dismissed flag so a screen reused after a fire (unlikely
	// for a boot greeting, but cheap to support and consistent with the
	// PhoneBootSplash / PhoneCallEnded overlays) starts fresh.
	dismissedAlready = false;
	startDismissTimer();
}

void PhoneWelcomeScreen::onStop() {
	stopDismissTimer();
	Input::getInstance()->removeListener(this);
}

// ---- enable probe ---------------------------------------------------------

bool PhoneWelcomeScreen::isEnabled() {
	// A non-empty Settings.ownerName means the user has gone through
	// PhoneOwnerNameScreen (S144) at least once and wants the greeting.
	// A first-boot device with an empty name skips the screen entirely
	// so a fresh Chatter still gets straight from the intro into the
	// lock screen with no perceptible delta.
	const char* name = Settings.get().ownerName;
	if(name == nullptr) return false;
	// Treat a single trailing nul (or all-spaces) as "no name". The
	// PhoneOwnerNameScreen already trims trailing whitespace via the
	// T9 widget, so the most likely empty case is name[0] == '\0'.
	for(size_t i = 0; i < sizeof(Settings.get().ownerName); ++i){
		const char c = name[i];
		if(c == '\0') return false;
		if(!isspace((unsigned char) c)) return true;
	}
	// Buffer was full of whitespace with no nul - treat as empty.
	return false;
}

// ---- builders -------------------------------------------------------------

void PhoneWelcomeScreen::buildGreeting() {
	// "Hello," prefix in pixelbasic16 warm cream. Centered on the
	// horizontal axis, anchored at the kGreetY row.
	greetLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(greetLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(greetLabel, MP_TEXT, 0);
	lv_obj_set_style_text_align(greetLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(greetLabel, "Hello,");
	lv_obj_set_align(greetLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(greetLabel, kGreetY);
}

void PhoneWelcomeScreen::buildName() {
	// Owner name in pixelbasic16 sunset orange. We upper-case at render
	// time both for the retro Sony-Ericsson "Hello, ALBERT" feel and
	// because the pixelbasic16 ASCII glyph table only covers the
	// upper-case Latin set with reliable kerning. The persisted
	// Settings.ownerName is left exactly as the user typed it.
	nameLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(nameLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(nameLabel, MP_ACCENT, 0);
	lv_obj_set_style_text_align(nameLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_align(nameLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(nameLabel, kNameY);

	// Width-cap to 156 px (the same usable strip every other
	// PhoneSynthwaveBg-backed screen uses) and dot-truncate so an
	// over-long name (e.g. the user typed "ALEXANDRIATHEGREAT") never
	// wraps into the hint row below.
	lv_obj_set_width(nameLabel, 156);
	lv_label_set_long_mode(nameLabel, LV_LABEL_LONG_DOT);

	// Copy the persisted name into a local buffer, upper-case it, and
	// hand the result to LVGL. We deliberately avoid in-place mutation
	// of Settings.ownerName so the SETTINGS round-trip stays a pure
	// read.
	char rendered[MaxRenderLen + 1];
	const char* src = Settings.get().ownerName;
	size_t i = 0;
	if(src != nullptr) {
		for(; i < MaxRenderLen && src[i] != '\0'; ++i) {
			const unsigned char c = (unsigned char) src[i];
			rendered[i] = (char) toupper(c);
		}
	}
	rendered[i] = '\0';

	if(rendered[0] == '\0') {
		// Defensive: if the screen is somehow shown with an empty
		// name, fall back to a generic "FRIEND" tag rather than
		// rendering an empty label that the user would read as
		// "Hello,". Production callers should have already short-
		// circuited via isEnabled() so this is purely a safety net.
		lv_label_set_text(nameLabel, "FRIEND");
	} else {
		lv_label_set_text(nameLabel, rendered);
	}
}

void PhoneWelcomeScreen::buildHint() {
	// Dim "press any key" affordance glued to the bottom of the
	// screen. Pixelbasic7 / dim purple to match the rest of the
	// boot-flow hints. The whole point is to look like an
	// optional, faint "you can skip this" cue rather than a
	// chrome label.
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(hintLabel, "press any key");
	lv_obj_set_align(hintLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hintLabel, kHintY);
}

// ---- timer + dismiss dispatch --------------------------------------------

void PhoneWelcomeScreen::startDismissTimer() {
	// Idempotent: if a previous onStart() left a timer running we reuse
	// it rather than stacking a second one. A 0 ms duration disables
	// the auto-advance entirely (any-key dismiss only) - mostly useful
	// from tests that want to drive the dismiss path manually.
	if(dismissTimer != nullptr) return;
	if(durationMs == 0)         return;
	dismissTimer = lv_timer_create(onDismissTimer, durationMs, this);
}

void PhoneWelcomeScreen::stopDismissTimer() {
	if(dismissTimer == nullptr) return;
	lv_timer_del(dismissTimer);
	dismissTimer = nullptr;
}

void PhoneWelcomeScreen::onDismissTimer(lv_timer_t* timer) {
	auto self = static_cast<PhoneWelcomeScreen*>(timer->user_data);
	if(self == nullptr) return;
	self->fireDismiss();
}

void PhoneWelcomeScreen::fireDismiss() {
	// Guard against double-fire: a hardware key press and the auto-
	// dismiss timer can race within a single LVGL tick. Collapsing both
	// to a single dispatch keeps the host's DismissHandler from being
	// invoked twice (which would push two LockScreens stacked on top
	// of each other and confuse every subsequent unlock).
	if(dismissedAlready) return;
	dismissedAlready = true;

	// Stop the timer up-front so a slow handler (e.g. one that does
	// heavyweight LockScreen building) can not let it re-enter
	// fireDismiss via the static callback.
	stopDismissTimer();

	// Tear ourselves down BEFORE invoking the host callback. The
	// welcome screen is the active LVGL screen at this point - the
	// host callback is going to swap to LockScreen::activate(home),
	// which itself does an lv_scr_load via LVScreen::start. Stash the
	// callback on the stack first so the `this` access happens before
	// any teardown.
	auto cb = dismissCb;
	stop();
	lv_obj_del(getLvObj());

	if(cb != nullptr) cb();
}

// ---- input ----------------------------------------------------------------

void PhoneWelcomeScreen::buttonPressed(uint i) {
	// Any hardware key skips the greeting. We deliberately do not look
	// at the button index here - the user pressing anything signals
	// "I'm done with the greeting" and we honour it. Same any-key
	// dismiss pattern PhoneBootSplash / PhoneCallEnded use.
	(void) i;
	fireDismiss();
}
