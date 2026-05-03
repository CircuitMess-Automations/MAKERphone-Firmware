#include "PhonePowerOffMessageScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <Settings.h>
#include <stdio.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Elements/PhoneT9Input.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - kept identical to every other Phone* widget so
// the power-off-message editor slots in beside PhoneOwnerNameScreen (S144)
// and PhoneScratchpad (S140) without a visual seam. Inlined per the
// established pattern.
#define MP_BG_DARK         lv_color_make( 20,  12,  36)  // deep purple
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan caption
#define MP_DIM             lv_color_make( 70,  56, 100)  // muted purple
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim cream

// ---------- geometry ------------------------------------------------------
//
// 160 x 128 layout. Single edit-mode view -- there is only one buffer.
// Mirrors PhoneOwnerNameScreen 1:1 so the two SYSTEM T9-entry sub-screens
// read as one visual family.
//
//   y =  0..  9   PhoneStatusBar (10 px)
//   y = 12        caption "POWER-OFF MSG" (pixelbasic7, ~7 px tall)
//   y = 22        top divider rule (1 px)
//   y = 26..  55  PhoneT9Input slab (Width 156, Height 22 + HelpHeight 8)
//   y = 60        char counter "X of 23 chars" (pixelbasic7)
//   y = 72        dirty marker "* UNSAVED" / "SAVED"
//   y = 105       bottom divider rule (1 px)
//   y = 118..127  PhoneSoftKeyBar (10 px)

static constexpr lv_coord_t kCaptionY      = 12;
static constexpr lv_coord_t kTopDividerY   = 22;
static constexpr lv_coord_t kT9Y           = 26;
static constexpr lv_coord_t kCounterY      = 60;
static constexpr lv_coord_t kDirtyY        = 72;
static constexpr lv_coord_t kBotDividerY   = 105;
static constexpr lv_coord_t kRowLeftX      = 6;
static constexpr lv_coord_t kRowWidth      = 148;

// ---------- ctor / dtor --------------------------------------------------

PhonePowerOffMessageScreen::PhonePowerOffMessageScreen() : LVScreen() {
	// Full-screen container, no scrollbars, no padding - same blank-canvas
	// pattern PhoneOwnerNameScreen / PhoneScratchpad / PhoneNotepad use.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Synthwave wallpaper at the bottom of the z-order.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Status bar: signal | clock | battery, anchored to the top.
	statusBar = new PhoneStatusBar(obj);

	// Caption + divider + T9 input + counter + dirty marker + bottom divider.
	buildView();

	// Soft-key bar at the bottom. CLEAR wipes, DONE persists + pops.
	softKeys = new PhoneSoftKeyBar(obj);
	refreshSoftKeys();

	// Pre-load the persisted message so the screen opens with the
	// existing value (rather than blank) and the user can extend or
	// edit in-place. The dirty flag stays false until the first
	// keystroke mutates the buffer.
	if(t9Input) {
		const char* persisted = Settings.get().powerOffMessage;
		if(persisted != nullptr && persisted[0] != '\0') {
			t9Input->setText(String(persisted));
			dirty = false;
		}
	}

	refreshCharCounter();
	refreshDirty();
}

PhonePowerOffMessageScreen::~PhonePowerOffMessageScreen() = default;

void PhonePowerOffMessageScreen::onStart() {
	Input::getInstance()->addListener(this);
}

void PhonePowerOffMessageScreen::onStop() {
	Input::getInstance()->removeListener(this);
}

// ---------- accessors ----------------------------------------------------

uint16_t PhonePowerOffMessageScreen::getLength() const {
	if(t9Input == nullptr) return 0;
	return (uint16_t) t9Input->getText().length();
}

const char* PhonePowerOffMessageScreen::getText() const {
	if(t9Input == nullptr) return "";
	// Returning a c_str pointer into a temporary String would be UB,
	// so we cache via a static buffer; the firmware is single-threaded
	// for the UI loop so a per-instance dance is overkill. Same
	// "lower-case const char*" pattern PhoneOwnerNameScreen uses.
	static char cache[MaxLen + 1];
	const String s = t9Input->getText();
	const size_t n = s.length() < MaxLen ? s.length() : MaxLen;
	memcpy(cache, s.c_str(), n);
	cache[n] = '\0';
	return cache;
}

// ---------- builders -----------------------------------------------------

void PhonePowerOffMessageScreen::buildView() {
	// Caption -- "POWER-OFF MSG". Painted in cyan to match the
	// section captions every other Phone* SYSTEM screen uses.
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, kCaptionY);
	lv_label_set_text(captionLabel, "POWER-OFF MSG");

	// Top divider rule below the caption.
	topDivider = lv_obj_create(obj);
	lv_obj_remove_style_all(topDivider);
	lv_obj_set_size(topDivider, kRowWidth, 1);
	lv_obj_set_style_bg_color(topDivider, MP_DIM, 0);
	lv_obj_set_style_bg_opa(topDivider, LV_OPA_COVER, 0);
	lv_obj_set_pos(topDivider, kRowLeftX, kTopDividerY);

	// PhoneT9Input -- canonical S32 multi-tap entry. Caps at MaxLen.
	t9Input = new PhoneT9Input(obj, MaxLen);
	lv_obj_set_pos(t9Input->getLvObj(),
				   (160 - PhoneT9Input::Width) / 2,
				   kT9Y);
	t9Input->setPlaceholder("BYE!");
	// First-letter-upper fits the "Bye, Albert!"-on-the-CRT-shrink
	// feel.
	t9Input->setCase(PhoneT9Input::Case::First);

	// Char counter strip.
	charCounter = lv_label_create(obj);
	lv_obj_set_style_text_font(charCounter, &pixelbasic7, 0);
	lv_obj_set_style_text_color(charCounter, MP_LABEL_DIM, 0);
	lv_label_set_long_mode(charCounter, LV_LABEL_LONG_DOT);
	lv_obj_set_width(charCounter, kRowWidth);
	lv_obj_set_style_text_align(charCounter, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_pos(charCounter, kRowLeftX, kCounterY);
	lv_label_set_text(charCounter, "0 of 23 chars");

	// Dirty marker line. Cream-on-orange-flag when unsaved, dim-cream
	// when the buffer matches the persisted snapshot.
	dirtyLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(dirtyLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(dirtyLabel, MP_LABEL_DIM, 0);
	lv_label_set_long_mode(dirtyLabel, LV_LABEL_LONG_DOT);
	lv_obj_set_width(dirtyLabel, kRowWidth);
	lv_obj_set_style_text_align(dirtyLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_pos(dirtyLabel, kRowLeftX, kDirtyY);
	lv_label_set_text(dirtyLabel, "SAVED");

	// Bottom divider rule above the soft-keys.
	bottomDivider = lv_obj_create(obj);
	lv_obj_remove_style_all(bottomDivider);
	lv_obj_set_size(bottomDivider, kRowWidth, 1);
	lv_obj_set_style_bg_color(bottomDivider, MP_DIM, 0);
	lv_obj_set_style_bg_opa(bottomDivider, LV_OPA_COVER, 0);
	lv_obj_set_pos(bottomDivider, kRowLeftX, kBotDividerY);

	// Wire the live-update callback. The T9 widget owns the std::function
	// copy so the captured `this` pointer stays valid for the widget's
	// lifetime, which equals this screen's lifetime.
	auto self = this;
	t9Input->setOnTextChanged([self](const String& text) {
		(void) text;
		self->dirty = true;
		self->refreshCharCounter();
		self->refreshDirty();
	});
}

// ---------- repainters ---------------------------------------------------

void PhonePowerOffMessageScreen::refreshCaption() {
	if(captionLabel == nullptr) return;
	lv_label_set_text(captionLabel, "POWER-OFF MSG");
}

void PhonePowerOffMessageScreen::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	softKeys->setLeft("CLEAR");
	softKeys->setRight("DONE");
}

void PhonePowerOffMessageScreen::refreshCharCounter() {
	if(charCounter == nullptr || t9Input == nullptr) return;
	const String text = t9Input->getText();
	char buf[24];
	snprintf(buf, sizeof(buf), "%u of %u chars",
			 (unsigned) text.length(),
			 (unsigned) MaxLen);
	lv_label_set_text(charCounter, buf);
}

void PhonePowerOffMessageScreen::refreshDirty() {
	if(dirtyLabel == nullptr) return;
	if(dirty) {
		lv_obj_set_style_text_color(dirtyLabel, MP_ACCENT, 0);
		lv_label_set_text(dirtyLabel, "* UNSAVED");
	}else{
		lv_obj_set_style_text_color(dirtyLabel, MP_LABEL_DIM, 0);
		lv_label_set_text(dirtyLabel, "SAVED");
	}
}

// ---------- persistence --------------------------------------------------

void PhonePowerOffMessageScreen::persist() {
	if(t9Input == nullptr) return;

	// Force the T9 widget to commit any in-flight pending letter so a
	// user who taps a digit and immediately exits does not lose their
	// last letter. Same gesture PhoneOwnerNameScreen / PhoneScratchpad
	// use.
	t9Input->commitPending();

	const String live = t9Input->getText();
	const size_t maxCopy = MaxLen; // 23, leaves room for nul
	const size_t n = live.length() < maxCopy ? live.length() : maxCopy;

	char* slot = Settings.get().powerOffMessage;
	memset(slot, 0, sizeof(Settings.get().powerOffMessage));
	memcpy(slot, live.c_str(), n);
	// memset zeroed the rest, so slot is already nul-terminated.

	Settings.store();

	dirty = false;
	refreshDirty();
}

void PhonePowerOffMessageScreen::clearBuffer() {
	if(t9Input != nullptr) {
		t9Input->clear();
	}

	// Wipe the persisted slot too so a CLEAR matches the live view
	// even if the user immediately exits. Settings.store() flushes
	// the change to NVS. PhonePowerDown reads the slot fresh on every
	// onStart() so the next shutdown skips the preamble entirely once
	// the slot is empty.
	memset(Settings.get().powerOffMessage,
		   0,
		   sizeof(Settings.get().powerOffMessage));
	Settings.store();

	dirty = false;
	refreshCharCounter();
	refreshDirty();
}

// ---------- input actions ------------------------------------------------

void PhonePowerOffMessageScreen::onClearPressed() {
	if(softKeys) softKeys->flashLeft();
	clearBuffer();
}

void PhonePowerOffMessageScreen::onDonePressed() {
	if(softKeys) softKeys->flashRight();
	persist();
	pop();
}

// ---------- input --------------------------------------------------------

void PhonePowerOffMessageScreen::buttonPressed(uint i) {
	// Always-edit mode: route digits + bumpers through the T9 funnel
	// the same way PhoneOwnerNameScreen / PhoneScratchpad do.
	switch(i) {
		case BTN_0: if(t9Input) t9Input->keyPress('0'); break;
		case BTN_1: if(t9Input) t9Input->keyPress('1'); break;
		case BTN_2: if(t9Input) t9Input->keyPress('2'); break;
		case BTN_3: if(t9Input) t9Input->keyPress('3'); break;
		case BTN_4: if(t9Input) t9Input->keyPress('4'); break;
		case BTN_5: if(t9Input) t9Input->keyPress('5'); break;
		case BTN_6: if(t9Input) t9Input->keyPress('6'); break;
		case BTN_7: if(t9Input) t9Input->keyPress('7'); break;
		case BTN_8: if(t9Input) t9Input->keyPress('8'); break;
		case BTN_9: if(t9Input) t9Input->keyPress('9'); break;

		case BTN_L:
			// Bumper L: T9 backspace -- cancels a pending letter or
			// erases the last committed character.
			if(t9Input) t9Input->keyPress('*');
			break;

		case BTN_R:
			// Bumper R: T9 case toggle (Abc -> ABC -> abc).
			if(t9Input) t9Input->keyPress('#');
			break;

		case BTN_ENTER:
			// Lock in the in-flight pending letter without leaving.
			if(t9Input) t9Input->commitPending();
			break;

		case BTN_LEFT:
			onClearPressed();
			break;

		case BTN_RIGHT:
			// Defer to buttonReleased so a long-press on BACK can
			// pre-empt this on the same hold cycle.
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhonePowerOffMessageScreen::buttonHeld(uint i) {
	if(i == BTN_BACK) {
		// Hold-BACK = bail to the parent screen. Auto-save on the way
		// out so an instant edit never loses content.
		backLongFired = true;
		if(softKeys) softKeys->flashRight();
		persist();
		pop();
	}
}

void PhonePowerOffMessageScreen::buttonReleased(uint i) {
	if(i == BTN_BACK) {
		// Short-press BACK: auto-save and exit. Long-press path is
		// suppressed via backLongFired so a hold does not double-fire.
		if(backLongFired) {
			backLongFired = false;
			return;
		}
		if(softKeys) softKeys->flashRight();
		persist();
		pop();
		return;
	}

	if(i == BTN_RIGHT) {
		// Right softkey = DONE. Suppressed if a long-press already
		// exited the screen on the same hold cycle.
		if(backLongFired) {
			backLongFired = false;
			return;
		}
		onDonePressed();
		return;
	}
}
