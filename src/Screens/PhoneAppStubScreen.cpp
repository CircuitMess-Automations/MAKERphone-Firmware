#include "PhoneAppStubScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette, kept identical to every other Phone* widget so
// the stub feels visually part of the same tile system. Inlined here
// because that is the established pattern in this codebase (see
// PhoneMainMenu.cpp, PhoneHomeScreen.cpp etc.).
#define MP_ACCENT      lv_color_make(255, 140, 30)    // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)   // cyan
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)   // dim purple sub-caption

PhoneAppStubScreen::PhoneAppStubScreen(const char* appName)
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  titleLabel(nullptr),
		  statusLabel(nullptr),
		  hintLabel(nullptr){

	// Full-screen container, no scrollbars, no inner padding - same blank
	// canvas pattern PhoneHomeScreen / PhoneMainMenu use. Children below
	// either pin themselves with IGNORE_LAYOUT or are LVGL primitives that
	// we anchor manually on the 160x128 display.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper at the bottom of the z-order so the centred labels overlay
	// it cleanly. PhoneSynthwaveBg already styles the full 160x128 viewport
	// (gradient sky + sun + grid + stars), so the placeholder still feels
	// like "a MAKERphone app" rather than a debug screen.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery bar (10 px) so the user
	// always knows the device is alive even on a stub.
	statusBar = new PhoneStatusBar(obj);

	// Big app name in pixelbasic16, centred horizontally, sitting roughly
	// a third of the way down the visible content area (between the 10 px
	// status bar and the 10 px softkey bar). Cyan to match the home-screen
	// clock face accent so it reads as the focal point.
	titleLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(titleLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(titleLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(titleLabel, (appName != nullptr) ? appName : "");
	lv_obj_set_align(titleLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(titleLabel, 32);

	// "NOT YET BUILT" status caption in sunset orange directly under the
	// title. pixelbasic7 keeps the visual hierarchy clear (title bigger,
	// status smaller) and matches the focused-app caption on PhoneMainMenu.
	statusLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(statusLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(statusLabel, MP_ACCENT, 0);
	lv_label_set_text(statusLabel, "NOT YET BUILT");
	lv_obj_set_align(statusLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(statusLabel, 64);

	// Sub-caption hint in dim purple, deliberately subdued so it does not
	// compete with the orange status line. Tells the user the situation is
	// expected (this app is on the roadmap, just not shipped yet).
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(hintLabel, "coming in a future session");
	lv_obj_set_align(hintLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hintLabel, 78);

	// Bottom: single-action softkey bar - only BACK is meaningful here.
	// We leave the left softkey blank rather than wiring something inert,
	// so the user is not invited to press a key that does nothing.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("");
	softKeys->setRight("BACK");
}

PhoneAppStubScreen::~PhoneAppStubScreen() {
	// Children (wallpaper, statusBar, softKeys, labels) are all parented
	// to obj; LVGL frees them recursively when the screen's obj is
	// destroyed by the LVScreen base destructor. Nothing manual.
}

void PhoneAppStubScreen::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneAppStubScreen::onStop() {
	Input::getInstance()->removeListener(this);
}

void PhoneAppStubScreen::buttonPressed(uint i) {
	// BTN_BACK returns to whoever pushed us (PhoneMainMenu in S20). We
	// also accept BTN_ENTER as a friendly second way out so the user can
	// tap A from the centre keypad without hunting for the back button -
	// the stub has no other meaningful action so this avoids a feeling of
	// "stuck" on real hardware.
	switch(i) {
		case BTN_BACK:
		case BTN_ENTER:
			pop();
			break;
		default:
			break;
	}
}
