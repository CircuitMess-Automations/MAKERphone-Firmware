#include "PhoneHomeScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneClockFace.h"
#include "../Elements/PhoneSoftKeyBar.h"

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

	// S22: enable long-press detection on BTN_0 (homescreen quick-dial)
	// and BTN_BACK (homescreen lock). 600 ms is the sweet spot used by
	// classic feature-phones - long enough to be intentional, short
	// enough not to feel laggy. The hold time is set on the listener
	// itself, so it follows the listener's lifetime.
	setButtonHoldTime(BTN_0, 600);
	setButtonHoldTime(BTN_BACK, 600);
}

PhoneHomeScreen::~PhoneHomeScreen() {
	// Children (wallpaper, statusBar, clockFace, softKeys) are LVObjects
	// parented to obj - LVGL frees them when obj is destroyed by the base
	// class destructor. Nothing to do here.
}

void PhoneHomeScreen::onStart() {
	Input::getInstance()->addListener(this);
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

		default:
			break;
	}
}
