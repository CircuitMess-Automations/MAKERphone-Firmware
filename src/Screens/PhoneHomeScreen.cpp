#include "PhoneHomeScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneClockFace.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Elements/PhoneChargingOverlay.h"
#include "../Elements/PhoneOperatorBanner.h"
#include "../Elements/PhoneConfettiOverlay.h"
#include "../Elements/PhoneNotificationToast.h"
#include "../Elements/PhoneIdleHint.h"
#include "PhoneBirthdayReminders.h"

PhoneHomeScreen::PhoneHomeScreen() : LVScreen() {
	// Full-screen container, no scrollbars, no inner padding - the four
	// widgets we mount below all anchor themselves with IGNORE_LAYOUT and
	// know their own (x,y) on the 160x128 display.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Synthwave wallpaper FIRST so it sits at the bottom of LVGL's z-order.
	// Same pattern LockScreen uses to keep the gradient/grid behind everything
	// else - the rest of the home screen overlays it without any opacity
	// gymnastics on the parent.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: signal | clock | battery (Phase B widget, S01 + S13-S15).
	statusBar = new PhoneStatusBar(obj);

	// Centerpiece: big retro clock + day-of-week + month/year (S03).
	// Default y-offset already lives directly under the 10 px status bar.
	clockFace = new PhoneClockFace(obj);

	// Bottom: two-label softkey bar with the unmistakable feature-phone
	// silhouette. CALL on the left is the Phase-D entry point (becomes the
	// dialer route at S23); MENU on the right is the Phase-C entry point
	// (becomes PhoneMainMenu at S20). For S17 both are advertised but
	// inert - the binding hooks (setOnLeftSoftKey / setOnRightSoftKey)
	// let a host wire them without modifying this class.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("CALL");
	softKeys->setRight("MENU");

	// S147 - operator banner just under the status bar. Reads
	// Settings.operatorText / Settings.operatorLogo on construction and
	// hides itself if both are empty. Mounted *after* the soft-key bar
	// to keep it on top of the wallpaper but clear of the clock face.
	// When the banner is visible we shift the clock face down by the
	// banner height so the cyan time digits do not overlap the cream
	// carrier label / sunset-orange logo cells. When the banner is
	// hidden (factory-cleared state) we leave the clock face at its
	// pre-S147 y so the homescreen reads exactly the way it always did.
	operatorBanner = new PhoneOperatorBanner(obj);
	if(clockFace != nullptr && operatorBanner->isVisible()) {
		lv_obj_set_y(clockFace->getLvObj(),
		             11 + PhoneOperatorBanner::BannerHeight);
	}

	// S59 charging overlay - hidden by default. Drops just above the
	// soft-key bar so the wallpaper / clock face are untouched while
	// the device is unplugged. Auto-detect lets the widget flip its
	// own visibility from the BatteryService voltage trend.
	chargingOverlay = new PhoneChargingOverlay(obj);
	lv_obj_set_align(chargingOverlay->getLvObj(), LV_ALIGN_BOTTOM_MID);
	// PhoneSoftKeyBar is 10 px tall; sit the chip 4 px above it.
	lv_obj_set_y(chargingOverlay->getLvObj(), -(int16_t)(10 + 4));
	chargingOverlay->setAutoDetect(true);

	// S154 - "PRESS ANY KEY" idle hint. Boots invisible and only
	// fades in after PhoneIdleHint::IdleMs (10 s) of stillness, so
	// at boot the homescreen reads exactly the way the previous
	// (S17/S18) skeleton did - this is purely additive. The hint
	// hangs above the charging overlay (y = -30 from BOTTOM_MID,
	// well clear of the chip's y = 101..114 strip), but we still
	// gate it off whenever the charging chip is visible so the
	// two never share screen attention. The host's onStart /
	// loop run wires the gate; the cheap setActive() call here
	// is just the safe initial state.
	idleHint = new PhoneIdleHint(obj);
	idleHint->setActive(true);

	// S22: enable long-press detection on BTN_0 (homescreen quick-dial)
	// and BTN_BACK (homescreen lock). 600 ms is the sweet spot used by
	// classic feature-phones - long enough to be intentional, short
	// enough not to feel laggy. The hold time is set on the listener
	// itself, so it follows the listener's lifetime.
	setButtonHoldTime(BTN_0, 600);
	setButtonHoldTime(BTN_BACK, 600);

	// S151: enable long-press detection on BTN_1..BTN_9 so the classic
	// Sony-Ericsson "hold a digit on home dials the matching speed-
	// dial entry" gesture fires. Same 600 ms hold time the BTN_0
	// quick-dial / BTN_BACK lock gestures use, so the homescreen
	// long-press feel stays consistent across the entire numeric
	// keypad. The host wires a single SpeedDialHandler via
	// setOnSpeedDial(); each digit's suppression flag (digitLongFired
	// [d]) keeps the matching short-press from double-firing on key
	// release.
	setButtonHoldTime(BTN_1, 600);
	setButtonHoldTime(BTN_2, 600);
	setButtonHoldTime(BTN_3, 600);
	setButtonHoldTime(BTN_4, 600);
	setButtonHoldTime(BTN_5, 600);
	setButtonHoldTime(BTN_6, 600);
	setButtonHoldTime(BTN_7, 600);
	setButtonHoldTime(BTN_8, 600);
	setButtonHoldTime(BTN_9, 600);
}

PhoneHomeScreen::~PhoneHomeScreen() {
	// Children (wallpaper, statusBar, clockFace, softKeys) are LVObjects
	// parented to obj - LVGL frees them when obj is destroyed by the base
	// class destructor. Nothing to do here.
}

void PhoneHomeScreen::onStart() {
	Input::getInstance()->addListener(this);

	// S154 - sync the "PRESS ANY KEY" hint to the current
	// charging-overlay visibility so a homescreen surfaced
	// while the chip is already up does not bring an idle
	// hint into the same y strip. The PhoneChargingOverlay
	// flips its own visibility from BatteryService voltage
	// trends, so on the next idle window we re-enable; for
	// now an immediate read is enough.
	if(idleHint != nullptr && chargingOverlay != nullptr){
		idleHint->setActive(!chargingOverlay->isCharging());
	}

	// S152 - check whether today is anyone's birthday and, if so,
	// drop a one-shot confetti volley + a slide-down toast on top of
	// the homescreen. The check runs every onStart() so a user who
	// rolls past midnight while the device is awake (or who tweaks
	// the wall clock from PhoneDateTimeScreen) sees the celebration
	// the next time the homescreen surfaces. The overlay and the
	// toast are children of obj, so they are torn down with the
	// screen automatically.
	const auto match = PhoneBirthdayReminders::firstBirthdayToday();
	if(match.hasMatch) {
		if(confettiOverlay == nullptr) confettiOverlay = new PhoneConfettiOverlay(obj);
		confettiOverlay->start();

		if(birthdayToast == nullptr) {
			birthdayToast = new PhoneNotificationToast(obj);
		}
		char msg[40] = {0};
		// Compose "<NAME> turns one year older!" so a user without
		// stored age sees a friendly greeting either way. The toast
		// truncates with an ellipsis on overflow, so the line is safe
		// for every reasonable contact name.
		snprintf(msg, sizeof(msg), "Wish %s well today", match.name);
		birthdayToast->show(PhoneNotificationToast::Variant::Generic,
		                    "Happy birthday!", msg);
	} else if(confettiOverlay != nullptr) {
		// Birthday rolled past while we were elsewhere - cancel any
		// stale animation so the next visit doesn't see a half-faded
		// volley left over from yesterday.
		confettiOverlay->stop();
	}
}

void PhoneHomeScreen::onStop() {
	Input::getInstance()->removeListener(this);
}

void PhoneHomeScreen::setOnLeftSoftKey(SoftKeyHandler cb) {
	leftCb = cb;
}

void PhoneHomeScreen::setOnRightSoftKey(SoftKeyHandler cb) {
	rightCb = cb;
}

void PhoneHomeScreen::setOnQuickDial(SoftKeyHandler cb) {
	quickDialCb = cb;
}

void PhoneHomeScreen::setOnLockHold(SoftKeyHandler cb) {
	lockHoldCb = cb;
}

void PhoneHomeScreen::setOnSpeedDial(SpeedDialHandler cb) {
	// S151: long-press BTN_1..BTN_9 dispatch hook. Stored as a single
	// function pointer so a host can route all nine slots through one
	// helper without per-digit binding boilerplate.
	speedDialCb = cb;
}

void PhoneHomeScreen::setLeftLabel(const char* label) {
	if(softKeys) softKeys->setLeft(label);
}

void PhoneHomeScreen::setRightLabel(const char* label) {
	if(softKeys) softKeys->setRight(label);
}

void PhoneHomeScreen::flashLeftSoftKey() {
	if(softKeys) softKeys->flashLeft();
}

void PhoneHomeScreen::flashRightSoftKey() {
	if(softKeys) softKeys->flashRight();
}

void PhoneHomeScreen::buttonPressed(uint i) {
	switch(i) {
		case BTN_LEFT:
			// S21: flash before invoking the host handler so the user
			// gets a "click" cue even if the handler immediately starts
			// a screen transition - the old screen lives until the slide
			// completes (lv_scr_load_anim with auto_del=false), so the
			// flash is visible during the slide-out.
			if(softKeys) softKeys->flashLeft();
			if(leftCb) leftCb(this);
			break;

		case BTN_RIGHT:
			if(softKeys) softKeys->flashRight();
			if(rightCb) rightCb(this);
			break;

		case BTN_0:
			// S22: a *short* press of 0 is reserved for "type the digit 0
			// into the dialer once the dialer is reachable from here". For
			// the home screen alone the short press is currently inert -
			// the long-press is what jumps to quick-dial, and it fires
			// from buttonHeld() below.
			zeroLongFired = false;
			break;

		case BTN_BACK:
			// S22 changes BTN_BACK on the homescreen: a *short* press still
			// pop()s the parent (legacy behaviour - lets a host that pushed
			// us pop back), and a long-press locks via the lockHoldCb. We
			// defer the short-press action to buttonReleased so we can
			// suppress it when a long-press fired.
			backLongFired = false;
			break;

		case BTN_1: digitLongFired[1] = false; break;
		case BTN_2: digitLongFired[2] = false; break;
		case BTN_3: digitLongFired[3] = false; break;
		case BTN_4: digitLongFired[4] = false; break;
		case BTN_5: digitLongFired[5] = false; break;
		case BTN_6: digitLongFired[6] = false; break;
		case BTN_7: digitLongFired[7] = false; break;
		case BTN_8: digitLongFired[8] = false; break;
		case BTN_9: digitLongFired[9] = false; break;

		default:
			break;
	}
}

void PhoneHomeScreen::buttonReleased(uint i) {
	// S22: short-press dispatch lives here so a hold-then-release does not
	// trigger the short-press action when the long-press already fired.
	switch(i) {
		case BTN_BACK:
			if(!backLongFired){
				// Same legacy short-press behaviour as before S22 - if a
				// parent screen pushed us, return there. When PhoneHomeScreen
				// is the root post-LockScreen (S18 wiring), there is no
				// parent and pop() is a no-op, which is what we want.
				pop();
			}
			backLongFired = false;
			break;

		case BTN_0:
			// Short press of 0 on the homescreen has no current effect; this
			// just clears the long-fired flag so a future press starts clean.
			zeroLongFired = false;
			break;

		case BTN_1: digitLongFired[1] = false; break;
		case BTN_2: digitLongFired[2] = false; break;
		case BTN_3: digitLongFired[3] = false; break;
		case BTN_4: digitLongFired[4] = false; break;
		case BTN_5: digitLongFired[5] = false; break;
		case BTN_6: digitLongFired[6] = false; break;
		case BTN_7: digitLongFired[7] = false; break;
		case BTN_8: digitLongFired[8] = false; break;
		case BTN_9: digitLongFired[9] = false; break;

		default:
			break;
	}
}

void PhoneHomeScreen::buttonHeld(uint i) {
	// S22: long-press shortcuts.
	//   - Hold 0    -> quick-dial   (homescreen + main menu, classic SE).
	//   - Hold Back -> lock device  (homescreen + main menu).
	// Both gestures flash the corresponding softkey when one exists, so the
	// user gets the same visual "click" cue used everywhere else on the
	// phone. The flag set here suppresses the matching buttonReleased so
	// the short-press path does not double-fire on lift-off.
	switch(i) {
		case BTN_0:
			zeroLongFired = true;
			if(softKeys) softKeys->flashLeft();
			if(quickDialCb) quickDialCb(this);
			break;

		case BTN_BACK:
			backLongFired = true;
			if(softKeys) softKeys->flashRight();
			if(lockHoldCb) lockHoldCb(this);
			break;

		// S151: long-press BTN_1..BTN_9 maps to speed-dial slots
		// 1..9. We flash the LEFT softkey ("CALL") so the gesture
		// reads as the same family as the existing hold-0 quick-dial
		// (which also flashes left), then hand off to the host
		// callback with the digit. The matching digit's suppression
		// flag is set so the buttonReleased short-press path on lift-
		// off does not double-fire (currently a no-op for digits, but
		// kept consistent with the BTN_0 / BTN_BACK pattern so a
		// future short-press meaning for digits stays well-behaved).
		case BTN_1: case BTN_2: case BTN_3:
		case BTN_4: case BTN_5: case BTN_6:
		case BTN_7: case BTN_8: case BTN_9: {
			uint8_t digit = 0;
			switch(i){
				case BTN_1: digit = 1; break;
				case BTN_2: digit = 2; break;
				case BTN_3: digit = 3; break;
				case BTN_4: digit = 4; break;
				case BTN_5: digit = 5; break;
				case BTN_6: digit = 6; break;
				case BTN_7: digit = 7; break;
				case BTN_8: digit = 8; break;
				case BTN_9: digit = 9; break;
				default:    break;
			}
			if(digit >= 1 && digit <= 9){
				digitLongFired[digit] = true;
				if(softKeys) softKeys->flashLeft();
				if(speedDialCb) speedDialCb(this, digit);
			}
			break;
		}

		default:
			break;
	}
}
