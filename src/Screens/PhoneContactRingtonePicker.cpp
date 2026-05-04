#include "PhoneContactRingtonePicker.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../Services/PhoneContactRingtone.h"
#include "../Services/PhoneRingtoneEngine.h"
#include "../Storage/PhoneContacts.h"

// MAKERphone retro palette — kept identical to every other Phone* widget
// so the picker reads visually as part of the same family. Inlined here
// per the established pattern.
#define MP_BG_DARK      lv_color_make( 20,  12,  36)
#define MP_ACCENT       lv_color_make(255, 140,  30)
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)
#define MP_DIM          lv_color_make( 70,  56, 100)
#define MP_TEXT         lv_color_make(255, 220, 180)
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)

// 160 x 128 layout. Vertical budget:
//   y =   0 ..  9   PhoneStatusBar (10 px)
//   y =  12 .. 19   "RINGTONE" caption strip (8 px tall, pixelbasic7)
//   y =  22 .. 93   list body band (72 px = 6 visible rows of 12 px)
//   y =  96 ..115   slack for hint text (currently empty so the band can
//                   grow without breaking layout)
//   y = 118 ..127   PhoneSoftKeyBar (10 px)
//
// The cursor is a translucent dim-purple rectangle that slides between
// rows. Rows are simple labels — each row's text is "savedGlyph + name"
// where savedGlyph is "* " for the contact's currently-saved id and
// "  " (two spaces) otherwise so the list aligns vertically.
static constexpr lv_coord_t kCaptionY  = 12;
static constexpr lv_coord_t kBodyY     = 22;
static constexpr lv_coord_t kBodyW     = 152;   // 4 px gutter on each side
static constexpr lv_coord_t kBodyX     =   4;
static constexpr lv_coord_t kRowInsetX =   8;   // text inset inside the row

// Long-press cadence shared with the rest of the MAKERphone shell.
static constexpr uint32_t   kBackHoldMs = 600;

// ----- ctor / dtor -----

PhoneContactRingtonePicker::PhoneContactRingtonePicker(UID_t inUid)
		: LVScreen(),
		  uid(inUid) {
	buildLayout();
	buildList();
	rebuildEntries();
}

PhoneContactRingtonePicker::~PhoneContactRingtonePicker() {
	// Make sure the engine is not left driving a preview after the
	// screen is destroyed — onStop normally handles this, but a
	// destructor path that bypasses onStop (rare) still needs the
	// safety. Ringtone.stop() is a no-op when nothing is playing.
	if(previewing) {
		Ringtone.stop();
	}
}

void PhoneContactRingtonePicker::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneContactRingtonePicker::onStop() {
	Input::getInstance()->removeListener(this);
	stopPreview();
}

// ----- builders -----

void PhoneContactRingtonePicker::buildLayout() {
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "RINGTONE");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, kCaptionY);

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("PICK");
	softKeys->setRight("BACK");

	setButtonHoldTime(BTN_BACK, kBackHoldMs);
}

void PhoneContactRingtonePicker::buildList() {
	// The body band is a transparent flat container that owns the
	// row labels + the cursor rect. Children are positioned manually
	// so the layout math stays explicit (no LVGL flex), matching the
	// rest of the MAKERphone screen family.
	listContainer = lv_obj_create(obj);
	lv_obj_remove_style_all(listContainer);
	lv_obj_set_size(listContainer, kBodyW, VisibleRows * RowHeight);
	lv_obj_set_pos(listContainer, kBodyX, kBodyY);
	lv_obj_set_scrollbar_mode(listContainer, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(listContainer, 0, 0);
	lv_obj_set_style_radius(listContainer, 2, 0);
	lv_obj_set_style_bg_color(listContainer, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(listContainer, LV_OPA_40, 0);
	lv_obj_set_style_border_width(listContainer, 1, 0);
	lv_obj_set_style_border_color(listContainer, MP_DIM, 0);
	lv_obj_set_style_border_opa(listContainer, LV_OPA_60, 0);

	// Cursor rect — translucent dim purple, full width of the body
	// band. Sits behind the row labels (created later) so the text
	// reads on top of the highlight.
	cursorRect = lv_obj_create(listContainer);
	lv_obj_remove_style_all(cursorRect);
	lv_obj_set_size(cursorRect, kBodyW - 4, RowHeight);
	lv_obj_set_pos(cursorRect, 2, 0);
	lv_obj_set_style_bg_color(cursorRect, MP_ACCENT, 0);
	lv_obj_set_style_bg_opa(cursorRect, LV_OPA_30, 0);
	lv_obj_set_style_radius(cursorRect, 2, 0);
	lv_obj_set_style_border_width(cursorRect, 1, 0);
	lv_obj_set_style_border_color(cursorRect, MP_ACCENT, 0);
	lv_obj_set_style_border_opa(cursorRect, LV_OPA_70, 0);
}

// ----- entry table -----

void PhoneContactRingtonePicker::rebuildEntries() {
	// Tear down any prior row labels (rebuildEntries is called once
	// in the ctor, but the helper is kept idempotent so a future
	// "refresh after composer save" path can call it again).
	for(uint8_t i = 0; i < MaxEntries; ++i) {
		if(rows[i] != nullptr) {
			lv_obj_del(rows[i]);
			rows[i]      = nullptr;
			savedDots[i] = nullptr;
		}
		ids[i] = 0;
	}

	const uint8_t available = PhoneContactRingtone::pickerCount();
	entryCount = (available <= MaxEntries) ? available : MaxEntries;

	// Resolve the contact's currently-saved id so the cursor lands
	// on it on first paint and the "*" mark appears next to the
	// right row.
	savedId = PhoneContactRingtone::DefaultId;
	if(uid != 0) {
		savedId = PhoneContactRingtone::validatedOrDefault(
				PhoneContacts::ringtoneOf(uid));
	}

	uint8_t cursorIdx = PhoneContactRingtone::pickerIndexOf(savedId);
	if(cursorIdx >= entryCount) cursorIdx = 0;
	cursor = cursorIdx;

	for(uint8_t i = 0; i < entryCount; ++i) {
		ids[i] = PhoneContactRingtone::pickerIdAt(i);

		rows[i] = lv_label_create(listContainer);
		lv_obj_set_style_text_font(rows[i], &pixelbasic7, 0);
		lv_obj_set_style_text_color(rows[i], MP_TEXT, 0);
		lv_label_set_long_mode(rows[i], LV_LABEL_LONG_DOT);
		lv_obj_set_width(rows[i], kBodyW - kRowInsetX - 6);
		lv_obj_set_pos(rows[i], kRowInsetX, 2);  // y positioned by scrollIntoView()

		char nameBuf[PhoneContactRingtone::NameBufferSize];
		PhoneContactRingtone::nameOf(ids[i], nameBuf, sizeof(nameBuf));
		lv_label_set_text(rows[i], nameBuf);

		// "saved" dot — a single pixelbasic7 asterisk pinned to the
		// row's left margin. Created visible so refreshSavedMarks()
		// only needs to flip its color.
		savedDots[i] = lv_label_create(listContainer);
		lv_obj_set_style_text_font(savedDots[i], &pixelbasic7, 0);
		lv_obj_set_style_text_color(savedDots[i], MP_DIM, 0);
		lv_label_set_text(savedDots[i], "*");
		lv_obj_set_pos(savedDots[i], 2, 2);
	}

	topVisible = 0;
	scrollIntoView();
	refreshSavedMarks();
	refreshCursor();
}

// ----- cursor -----

void PhoneContactRingtonePicker::moveCursor(int8_t dir) {
	if(entryCount == 0) return;

	if(dir < 0) {
		if(cursor == 0) return;        // hard stop — short list
		--cursor;
	} else if(dir > 0) {
		if(cursor + 1 >= entryCount) return;
		++cursor;
	}

	// Stopping the preview on every cursor step keeps the behavior
	// predictable: ENTER previews, arrows browse silently. Otherwise
	// the previous row's tone would bleed into the user's mental
	// model of the focused row.
	stopPreview();
	scrollIntoView();
	refreshCursor();
}

void PhoneContactRingtonePicker::scrollIntoView() {
	if(entryCount == 0) return;

	// Slide topVisible so cursor sits inside the visible band.
	if(cursor < topVisible) {
		topVisible = cursor;
	} else if(cursor >= (uint8_t)(topVisible + VisibleRows)) {
		topVisible = (uint8_t)(cursor + 1 - VisibleRows);
	}

	for(uint8_t i = 0; i < entryCount; ++i) {
		if(rows[i] == nullptr) continue;
		const int8_t relRow = (int8_t)i - (int8_t)topVisible;
		if(relRow < 0 || relRow >= (int8_t)VisibleRows) {
			lv_obj_add_flag(rows[i],      LV_OBJ_FLAG_HIDDEN);
			lv_obj_add_flag(savedDots[i], LV_OBJ_FLAG_HIDDEN);
			continue;
		}
		lv_obj_clear_flag(rows[i],      LV_OBJ_FLAG_HIDDEN);
		lv_obj_clear_flag(savedDots[i], LV_OBJ_FLAG_HIDDEN);
		const lv_coord_t y = (lv_coord_t)(relRow * RowHeight + 2);
		lv_obj_set_y(rows[i],      y);
		lv_obj_set_y(savedDots[i], y);
	}
}

void PhoneContactRingtonePicker::refreshCursor() {
	if(cursorRect == nullptr) return;
	if(entryCount == 0) {
		lv_obj_add_flag(cursorRect, LV_OBJ_FLAG_HIDDEN);
		return;
	}
	lv_obj_clear_flag(cursorRect, LV_OBJ_FLAG_HIDDEN);
	const int8_t relCursor = (int8_t)cursor - (int8_t)topVisible;
	const lv_coord_t y = (lv_coord_t)(relCursor * RowHeight);
	lv_obj_set_y(cursorRect, y);
}

void PhoneContactRingtonePicker::refreshSavedMarks() {
	for(uint8_t i = 0; i < entryCount; ++i) {
		if(savedDots[i] == nullptr) continue;
		const bool isSaved = (ids[i] == savedId);
		lv_obj_set_style_text_color(savedDots[i],
				isSaved ? MP_HIGHLIGHT : MP_DIM, 0);
	}
}

// ----- preview -----

void PhoneContactRingtonePicker::startPreview() {
	if(cursor >= entryCount) return;
	const uint8_t id = ids[cursor];
	const PhoneRingtoneEngine::Melody* m = PhoneContactRingtone::resolve(id);
	if(m == nullptr) return;
	Ringtone.play(*m);
	previewing = true;
	if(softKeys) softKeys->flashLeft();
}

void PhoneContactRingtonePicker::stopPreview() {
	if(!previewing) return;
	previewing = false;
	Ringtone.stop();
}

// ----- pick / back -----

void PhoneContactRingtonePicker::confirmPick() {
	if(softKeys) softKeys->flashLeft();
	stopPreview();

	uint8_t pickedId = (cursor < entryCount)
			? ids[cursor]
			: PhoneContactRingtone::DefaultId;

	if(uid != 0) {
		PhoneContacts::setRingtone(uid, pickedId);
	}

	if(pickCb) pickCb(this, pickedId);
	pop();
}

void PhoneContactRingtonePicker::invokeBack() {
	if(softKeys) softKeys->flashRight();
	stopPreview();
	pop();
}

// ----- soft-key flash exposure -----

void PhoneContactRingtonePicker::flashLeftSoftKey() {
	if(softKeys) softKeys->flashLeft();
}

void PhoneContactRingtonePicker::flashRightSoftKey() {
	if(softKeys) softKeys->flashRight();
}

// ----- setters -----

void PhoneContactRingtonePicker::setOnPick(PickHandler cb) {
	pickCb = cb;
}

void PhoneContactRingtonePicker::setLeftLabel(const char* label) {
	if(softKeys && label != nullptr) softKeys->setLeft(label);
}

void PhoneContactRingtonePicker::setRightLabel(const char* label) {
	if(softKeys && label != nullptr) softKeys->setRight(label);
}

// ----- input -----

void PhoneContactRingtonePicker::buttonPressed(uint i) {
	switch(i) {
		case BTN_2:
		case BTN_LEFT:
			moveCursor(-1);
			break;

		case BTN_8:
		case BTN_RIGHT:
			moveCursor(+1);
			break;

		case BTN_ENTER:
			// Toggle preview — second tap stops the playback so the
			// user can audition entries without opening the engine
			// twice on the same row.
			if(previewing) {
				stopPreview();
			} else {
				startPreview();
			}
			break;

		case BTN_L:
			confirmPick();
			break;

		case BTN_R:
			// Right bumper = quick BACK. Standard MAKERphone affordance.
			invokeBack();
			break;

		case BTN_BACK:
			// Defer short-press to buttonReleased so the long-press
			// can pre-empt it.
			backHoldFired = false;
			break;

		default:
			break;
	}
}

void PhoneContactRingtonePicker::buttonHeld(uint i) {
	if(i == BTN_BACK) {
		// Hold-BACK = bail. We pop our own screen here; the parent
		// screen continues the unwind chain when the user holds again.
		backHoldFired = true;
		invokeBack();
	}
}

void PhoneContactRingtonePicker::buttonReleased(uint i) {
	if(i == BTN_BACK) {
		if(backHoldFired) return;
		invokeBack();
	}
}
