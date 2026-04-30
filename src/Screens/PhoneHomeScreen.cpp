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

		case BTN_BACK:
			// If a parent screen pushed us, BTN_BACK returns there. This is
			// the same pattern other screens use. When PhoneHomeScreen later
			// becomes the post-LockScreen *default* (S18), back will instead
			// re-lock; the wiring lives in the call site, not here.
			pop();
			break;

		default:
			break;
	}
}
