#include "PhoneDialerScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Elements/PhoneDialerPad.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - inlined per the established pattern in this
// codebase (see PhoneMainMenu.cpp / PhoneHomeScreen.cpp / PhoneAppStubScreen.cpp).
// Keeping the typed digits in cyan and the empty-buffer hint in dim
// purple matches the rest of the phone-style screens, so a stray newcomer
// dialer feels visually at home next to home / menu.
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)   // cyan typed digits
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)   // dim purple placeholder

PhoneDialerScreen::PhoneDialerScreen()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  pad(nullptr),
		  bufferLabel(nullptr),
		  hintLabel(nullptr) {

	// Zero the buffer up front so getBuffer() returns a valid c-string
	// even before the user has typed anything (an empty handler call site
	// would otherwise see uninitialised memory).
	buffer[0] = '\0';

	// Full-screen container, no scrollbars, no inner padding - same blank
	// canvas pattern PhoneHomeScreen / PhoneMainMenu use. Children below
	// either pin themselves with IGNORE_LAYOUT or are LVGL primitives that
	// we anchor manually on the 160x128 display.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order. The pad,
	// status bar, soft-keys and buffer label all overlay it without any
	// opacity gymnastics on the parent. Same z-order pattern as every
	// other Phone* screen.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px tall).
	statusBar = new PhoneStatusBar(obj);

	// Buffer label + placeholder hint between the status bar and the pad.
	buildBufferLabel();

	// Centerpiece: the 3x4 numpad atom (S10). Anchored centred horizontally
	// and just under the buffer label.
	buildPad();

	// Bottom: feature-phone soft-keys. CALL on the left is the Phase-D
	// entry point (S24-S28 add the actual call screens behind it). BACK
	// on the right is the standard back-out softkey - a *short* press
	// also doubles as backspace on the buffer (see buttonReleased), and a
	// long-press exits the dialer back to whichever screen pushed us.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("CALL");
	softKeys->setRight("BACK");

	// Wire the pad's onPress so direct numpad presses (BTN_0..BTN_9 +
	// '*' and '#') append to the buffer with the same visual flash as
	// arrow-key navigation. The pad itself takes care of the press
	// flash + cursor movement; the screen only owns the buffer state.
	pad->setOnPress([this](char glyph, const char* /*letters*/) {
		this->appendGlyph(glyph);
	});

	// Long-press detection on BTN_BACK so a hold exits the dialer (and a
	// short press is interpreted as backspace). Same 600 ms threshold as
	// the rest of the MAKERphone shell, so the gesture feels identical
	// from any screen.
	setButtonHoldTime(BTN_BACK, 600);
}

PhoneDialerScreen::~PhoneDialerScreen() {
	// Children (wallpaper, statusBar, softKeys, pad, labels) are all
	// parented to obj - LVGL frees them recursively when the screen's
	// obj is destroyed by the LVScreen base destructor. Nothing manual.
}

void PhoneDialerScreen::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneDialerScreen::onStop() {
	Input::getInstance()->removeListener(this);
}

// ----- builders -----

void PhoneDialerScreen::buildBufferLabel() {
	// Big typed digits in pixelbasic16 - the focal point of the screen
	// when the user is dialling. Centred horizontally, sitting directly
	// under the 10 px status bar. Right-aligned text within an 80 px wide
	// label so a long number scrolls in from the right (matching the
	// classic Sony-Ericsson dialer behaviour where the most recently
	// typed digit is always visible at the right edge).
	bufferLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(bufferLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(bufferLabel, MP_HIGHLIGHT, 0);
	lv_label_set_long_mode(bufferLabel, LV_LABEL_LONG_CLIP);
	lv_obj_set_width(bufferLabel, 140);
	lv_obj_set_style_text_align(bufferLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(bufferLabel, "");
	lv_obj_set_align(bufferLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(bufferLabel, 12);

	// Placeholder hint that sits in the same slot when the buffer is
	// empty. pixelbasic7 + dim purple so it reads as "secondary" - we
	// only show one of (bufferLabel, hintLabel) at a time. The hint
	// gives the user a clear "type a number" prompt the moment the
	// dialer opens; refreshBufferLabel() flips the visibility.
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(hintLabel, "ENTER NUMBER");
	lv_obj_set_align(hintLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hintLabel, 18);
}

void PhoneDialerScreen::buildPad() {
	pad = new PhoneDialerPad(obj);

	// Anchor centred horizontally and pin the top edge directly under
	// the buffer label. Pad geometry (114 wide, 88 tall) leaves a 2 px
	// gap above the soft-key bar (118 - (28 + 88) = 2 px) and a 23 px
	// margin on each horizontal side.
	lv_obj_set_align(pad->getLvObj(), LV_ALIGN_TOP_LEFT);
	lv_obj_set_pos(pad->getLvObj(), (160 - PhoneDialerPad::PadWidth) / 2, 28);
}

// ----- buffer maintenance -----

void PhoneDialerScreen::appendGlyph(char c) {
	// Only digits + '*' + '#' are accepted. The pad keymap is already
	// constrained to that set, but we double-check defensively so a
	// future caller-driven append (e.g. setBuffer) can route through
	// here if desired.
	const bool ok = (c >= '0' && c <= '9') || c == '*' || c == '#';
	if(!ok) return;

	// Hard cap to MaxDigits so the buffer cannot grow off-screen. We
	// silently drop further presses instead of beeping; a future
	// session can wire a buzzer chirp here if the UX needs it.
	if(bufferLen >= MaxDigits) return;

	buffer[bufferLen++] = c;
	buffer[bufferLen]   = '\0';
	refreshBufferLabel();
}

void PhoneDialerScreen::backspace() {
	if(bufferLen == 0) return;
	bufferLen--;
	buffer[bufferLen] = '\0';
	refreshBufferLabel();
}

void PhoneDialerScreen::refreshBufferLabel() {
	// Show the placeholder hint only while the buffer is empty - the moment
	// the user types anything, the hint hides and the typed digits take its
	// place. We use lv_obj_add_flag(LV_OBJ_FLAG_HIDDEN) rather than
	// lv_obj_clear_flag because LVGL hides the object outright (no layout
	// gymnastics) which is the cleanest fit for this single-slot toggle.
	if(bufferLen == 0) {
		lv_label_set_text(bufferLabel, "");
		if(hintLabel)   lv_obj_clear_flag(hintLabel, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_label_set_text(bufferLabel, buffer);
		if(hintLabel)   lv_obj_add_flag(hintLabel, LV_OBJ_FLAG_HIDDEN);
	}
}

// ----- public API -----

void PhoneDialerScreen::setOnCall(SoftKeyHandler cb) {
	callCb = cb;
}

void PhoneDialerScreen::setOnExit(SoftKeyHandler cb) {
	exitCb = cb;
}

void PhoneDialerScreen::setLeftLabel(const char* label) {
	if(softKeys) softKeys->setLeft(label);
}

void PhoneDialerScreen::setRightLabel(const char* label) {
	if(softKeys) softKeys->setRight(label);
}

void PhoneDialerScreen::setBuffer(const char* text) {
	bufferLen = 0;
	buffer[0] = '\0';
	if(text != nullptr) {
		while(*text != '\0' && bufferLen < MaxDigits) {
			const char c = *text++;
			const bool ok = (c >= '0' && c <= '9') || c == '*' || c == '#';
			if(ok) {
				buffer[bufferLen++] = c;
			}
		}
		buffer[bufferLen] = '\0';
	}
	refreshBufferLabel();
}

void PhoneDialerScreen::clearBuffer() {
	bufferLen = 0;
	buffer[0] = '\0';
	refreshBufferLabel();
}

void PhoneDialerScreen::flashLeftSoftKey() {
	if(softKeys) softKeys->flashLeft();
}

void PhoneDialerScreen::flashRightSoftKey() {
	if(softKeys) softKeys->flashRight();
}

// ----- input -----

void PhoneDialerScreen::buttonPressed(uint i) {
	switch(i) {
		// Direct numpad input - route through the pad so the user sees
		// the same press-flash + cursor-jump as if they had navigated
		// with arrow keys + ENTER. PhoneDialerPad::pressGlyph fires the
		// onPress callback we wired in the ctor, which in turn appends
		// the glyph to the buffer.
		case BTN_0: if(pad) pad->pressGlyph('0'); break;
		case BTN_1: if(pad) pad->pressGlyph('1'); break;
		case BTN_2: if(pad) pad->pressGlyph('2'); break;
		case BTN_3: if(pad) pad->pressGlyph('3'); break;
		case BTN_4: if(pad) pad->pressGlyph('4'); break;
		case BTN_5: if(pad) pad->pressGlyph('5'); break;
		case BTN_6: if(pad) pad->pressGlyph('6'); break;
		case BTN_7: if(pad) pad->pressGlyph('7'); break;
		case BTN_8: if(pad) pad->pressGlyph('8'); break;
		case BTN_9: if(pad) pad->pressGlyph('9'); break;

		// L / R bumpers cycle through '*' and '#' since the Chatter has
		// no dedicated *,# keys. This keeps the dialer fully driveable
		// from hardware-only input on the prototype board. The cursor
		// also hops onto the matched key so a follow-up arrow-press
		// starts from the expected position.
		case BTN_L: if(pad) pad->pressGlyph('*'); break;
		case BTN_R: if(pad) pad->pressGlyph('#'); break;

		// ENTER on the centre A button: press whatever the pad cursor is
		// currently focused on. Same press-flash + onPress dispatch path
		// as the direct-numpad case above.
		case BTN_ENTER:
			if(pad) pad->pressSelected();
			break;

		// Arrow-key navigation across the pad. The board's BTN_LEFT and
		// BTN_RIGHT serve double duty as up/down navigation (BTN_UP is
		// aliased to BTN_LEFT, BTN_DOWN to BTN_RIGHT in Pins.hpp), but
		// here we use them as the dialer's softkeys: LEFT = CALL, RIGHT
		// = BACK. Pad navigation is therefore done with the same keys
		// once the user is dialling - 2/4/6/8 act as the up/left/right/
		// down arrows on every classic feature phone.
		case BTN_LEFT:
			// "CALL" softkey - flash before invoking the host handler so
			// the user gets a click cue even if the handler immediately
			// pushes the next screen. With an empty buffer we still flash
			// (the user clearly tried to call) but skip the callback so a
			// future Active-Call screen does not have to defensively
			// check for empty input.
			if(softKeys) softKeys->flashLeft();
			if(bufferLen > 0 && callCb) callCb(this);
			break;

		case BTN_RIGHT:
			// "BACK" softkey - flash on press, but defer the actual
			// short-press action (backspace) to buttonReleased so a
			// long-press that already fired (exit) does not double-fire
			// on key release.
			if(softKeys) softKeys->flashRight();
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneDialerScreen::buttonReleased(uint i) {
	// Short-press dispatch for BTN_RIGHT lives here so a hold-then-release
	// does not also backspace the buffer when the long-press already
	// exited the dialer.
	switch(i) {
		case BTN_RIGHT:
			if(!backLongFired) {
				if(bufferLen > 0) {
					// Buffer has content - short press = backspace last
					// digit. The user can long-press to exit only when
					// they have finished editing.
					backspace();
				} else {
					// Empty buffer + short BACK press = exit the dialer.
					// This matches feature-phone muscle memory: a fresh
					// dialer with nothing typed exits on the first BACK,
					// without forcing the user to long-press just because
					// they opened it by mistake.
					if(exitCb) {
						exitCb(this);
					} else {
						pop();
					}
				}
			}
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneDialerScreen::buttonHeld(uint i) {
	switch(i) {
		case BTN_RIGHT:
			// Long-press BACK = exit the dialer regardless of buffer
			// state. Useful when the user has typed half a number and
			// wants to bail out without backspacing twenty times.
			backLongFired = true;
			if(softKeys) softKeys->flashRight();
			if(exitCb) {
				exitCb(this);
			} else {
				pop();
			}
			break;

		default:
			break;
	}
}
