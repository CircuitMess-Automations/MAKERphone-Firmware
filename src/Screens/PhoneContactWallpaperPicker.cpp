#include "PhoneContactWallpaperPicker.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../Storage/PhoneContacts.h"

// MAKERphone retro palette — kept identical to every other Phone* widget
// so the picker reads visually as part of the same family.
#define MP_BG_DARK      lv_color_make( 20,  12,  36)
#define MP_ACCENT       lv_color_make(255, 140,  30)
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)
#define MP_DIM          lv_color_make( 70,  56, 100)
#define MP_TEXT         lv_color_make(255, 220, 180)

// 160 x 128 layout. Vertical budget mirrors PhoneContactRingtonePicker:
//   y =   0 ..  9    PhoneStatusBar (10 px)
//   y =  12 .. 19    "WALLPAPER" caption strip (8 px tall, pixelbasic7)
//   y =  22 .. 81    list body band (60 px = 5 rows of 12 px)
//   y = 118 ..127    PhoneSoftKeyBar (10 px)
static constexpr lv_coord_t kCaptionY  = 12;
static constexpr lv_coord_t kBodyY     = 22;
static constexpr lv_coord_t kBodyW     = 152;
static constexpr lv_coord_t kBodyX     =   4;
static constexpr lv_coord_t kRowInsetX =   8;

// Long-press cadence shared with the rest of the MAKERphone shell.
static constexpr uint32_t   kBackHoldMs = 600;

// Sentinel byte returned by entryStyleByte() for the INHERIT row.
// 0xFF is outside the PhoneSynthwaveBg::Style numeric range used by
// the picker (0..3) so confirmPick() can branch on it cleanly.
static constexpr uint8_t kStyleInherit = 0xFF;

// ----- ctor / dtor -----

PhoneContactWallpaperPicker::PhoneContactWallpaperPicker(UID_t inUid)
		: LVScreen(),
		  uid(inUid) {
	buildLayout();
	buildList();
	rebuildEntries();
}

PhoneContactWallpaperPicker::~PhoneContactWallpaperPicker() {
	// Nothing to tear down beyond the lv_obj tree, which LVScreen
	// owns and walks on destruction.
}

void PhoneContactWallpaperPicker::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneContactWallpaperPicker::onStop() {
	Input::getInstance()->removeListener(this);
	// Make sure we don't leave a preview wallpaper applied if the
	// screen is popped while previewing (the host screen will rebuild
	// its own anyway, but the cleanup keeps our state consistent for
	// the unlikely case of LVScreen reuse).
	stopPreview();
}

// ----- builders -----

void PhoneContactWallpaperPicker::buildLayout() {
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Default wallpaper at the bottom of LVGL's z-order. The preview
	// path tears this down + rebuilds a new one with the focused
	// style; the saved-on-pop path lets the next screen rebuild its
	// own from its constructor.
	wallpaper = new PhoneSynthwaveBg(obj);

	statusBar = new PhoneStatusBar(obj);

	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "WALLPAPER");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, kCaptionY);

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("PICK");
	softKeys->setRight("BACK");

	setButtonHoldTime(BTN_BACK, kBackHoldMs);
}

void PhoneContactWallpaperPicker::buildList() {
	listContainer = lv_obj_create(obj);
	lv_obj_remove_style_all(listContainer);
	lv_obj_set_size(listContainer, kBodyW, EntryCount * RowHeight);
	lv_obj_set_pos(listContainer, kBodyX, kBodyY);
	lv_obj_set_scrollbar_mode(listContainer, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(listContainer, 0, 0);
	lv_obj_set_style_radius(listContainer, 2, 0);
	lv_obj_set_style_bg_color(listContainer, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(listContainer, LV_OPA_40, 0);
	lv_obj_set_style_border_width(listContainer, 1, 0);
	lv_obj_set_style_border_color(listContainer, MP_DIM, 0);
	lv_obj_set_style_border_opa(listContainer, LV_OPA_60, 0);

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

void PhoneContactWallpaperPicker::rebuildEntries() {
	// Identify the saved entry. Inherit (entry 0) when no override
	// is stored; otherwise the row matching the saved Style byte.
	savedIndex = 0;
	if(uid != 0 && PhoneContacts::hasWallpaper(uid)) {
		const uint8_t rawByte = PhoneContacts::wallpaperOf(uid);
		// Map raw 0..3 onto entries 1..4 (entry 0 is INHERIT).
		// Out-of-range bytes (e.g. a stale theme-override byte stored
		// in the contact field) fall back to Synthwave (entry 1).
		const auto resolved = PhoneSynthwaveBg::styleFromByte(rawByte);
		uint8_t r = (uint8_t) resolved;
		if(r > 3) r = 0;
		savedIndex = (uint8_t)(r + 1);
	}

	cursor = savedIndex;
	previewing = false;
	previewedIndex = savedIndex;

	for(uint8_t i = 0; i < EntryCount; ++i) {
		rows[i] = lv_label_create(listContainer);
		lv_obj_set_style_text_font(rows[i], &pixelbasic7, 0);
		lv_obj_set_style_text_color(rows[i], MP_TEXT, 0);
		lv_label_set_long_mode(rows[i], LV_LABEL_LONG_DOT);
		lv_obj_set_width(rows[i], kBodyW - kRowInsetX - 6);
		lv_obj_set_pos(rows[i], kRowInsetX, (lv_coord_t)(i * RowHeight + 2));
		lv_label_set_text(rows[i], entryName(i));

		savedDots[i] = lv_label_create(listContainer);
		lv_obj_set_style_text_font(savedDots[i], &pixelbasic7, 0);
		lv_obj_set_style_text_color(savedDots[i], MP_DIM, 0);
		lv_label_set_text(savedDots[i], "*");
		lv_obj_set_pos(savedDots[i], 2, (lv_coord_t)(i * RowHeight + 2));
	}

	refreshSavedMarks();
	refreshCursor();
}

// ----- cursor -----

void PhoneContactWallpaperPicker::moveCursor(int8_t dir) {
	if(EntryCount == 0) return;

	if(dir < 0) {
		if(cursor == 0) return;
		--cursor;
	} else if(dir > 0) {
		if(cursor + 1 >= EntryCount) return;
		++cursor;
	}

	// Stop any active preview on cursor step so the visible swatch
	// goes back to the saved state — exactly like
	// PhoneContactRingtonePicker stops the ringtone preview between
	// rows.
	stopPreview();
	refreshCursor();
}

void PhoneContactWallpaperPicker::refreshCursor() {
	if(cursorRect == nullptr) return;
	if(EntryCount == 0) {
		lv_obj_add_flag(cursorRect, LV_OBJ_FLAG_HIDDEN);
		return;
	}
	lv_obj_clear_flag(cursorRect, LV_OBJ_FLAG_HIDDEN);
	const lv_coord_t y = (lv_coord_t)(cursor * RowHeight);
	lv_obj_set_y(cursorRect, y);
}

void PhoneContactWallpaperPicker::refreshSavedMarks() {
	for(uint8_t i = 0; i < EntryCount; ++i) {
		if(savedDots[i] == nullptr) continue;
		const bool isSaved = (i == savedIndex);
		lv_obj_set_style_text_color(savedDots[i],
				isSaved ? MP_HIGHLIGHT : MP_DIM, 0);
	}
}

// ----- preview -----

void PhoneContactWallpaperPicker::startPreview() {
	if(cursor >= EntryCount) return;
	rebuildWallpaperFor(cursor);
	previewing = true;
	previewedIndex = cursor;
	if(softKeys) softKeys->flashLeft();
}

void PhoneContactWallpaperPicker::stopPreview() {
	if(!previewing) return;
	previewing = false;
	// Restore the saved-state look so the picker's swatch matches
	// what the contact will actually render with after pop.
	rebuildWallpaperFor(savedIndex);
	previewedIndex = savedIndex;
}

void PhoneContactWallpaperPicker::rebuildWallpaperFor(uint8_t entryIndex) {
	// Tear down the prior background widget. PhoneSynthwaveBg is an
	// LVObject; deleting it walks lv_obj_del on its lv_obj which
	// removes any animations the widget owned.
	if(wallpaper != nullptr) {
		delete wallpaper;
		wallpaper = nullptr;
	}

	if(entryIndex == 0) {
		// INHERIT — render with whatever the global setting says.
		wallpaper = new PhoneSynthwaveBg(obj);
	} else {
		const uint8_t raw = entryStyleByte(entryIndex);
		const auto style = PhoneSynthwaveBg::styleFromByte(raw);
		wallpaper = new PhoneSynthwaveBg(obj, style);
	}

	// Push the new background to the bottom of the z-order so the
	// list / status bar / softkeys keep painting on top.
	if(wallpaper != nullptr) {
		lv_obj_move_background(wallpaper->getLvObj());
	}
}

// ----- pick / back -----

void PhoneContactWallpaperPicker::confirmPick() {
	if(softKeys) softKeys->flashLeft();

	const uint8_t pickedIndex = (cursor < EntryCount) ? cursor : 0;
	const uint8_t pickedByte  = entryStyleByte(pickedIndex);
	const bool inheritGlobal  = (pickedByte == kStyleInherit);

	if(uid != 0) {
		if(inheritGlobal) {
			PhoneContacts::clearWallpaper(uid);
		} else {
			PhoneContacts::setWallpaper(uid, pickedByte);
		}
	}

	if(pickCb) pickCb(this, inheritGlobal, pickedByte);

	// stopPreview() before pop so the visible swatch matches the
	// just-saved state on the way out.
	savedIndex = pickedIndex;
	stopPreview();
	pop();
}

void PhoneContactWallpaperPicker::invokeBack() {
	if(softKeys) softKeys->flashRight();
	stopPreview();
	pop();
}

// ----- soft-key flash exposure -----

void PhoneContactWallpaperPicker::flashLeftSoftKey() {
	if(softKeys) softKeys->flashLeft();
}

void PhoneContactWallpaperPicker::flashRightSoftKey() {
	if(softKeys) softKeys->flashRight();
}

// ----- setters -----

void PhoneContactWallpaperPicker::setOnPick(PickHandler cb) {
	pickCb = cb;
}

void PhoneContactWallpaperPicker::setLeftLabel(const char* label) {
	if(softKeys && label != nullptr) softKeys->setLeft(label);
}

void PhoneContactWallpaperPicker::setRightLabel(const char* label) {
	if(softKeys && label != nullptr) softKeys->setRight(label);
}

// ----- name / id helpers -----

uint8_t PhoneContactWallpaperPicker::entryStyleByte(uint8_t entryIndex) {
	switch(entryIndex) {
		case 0: return kStyleInherit;                            // INHERIT
		case 1: return (uint8_t) PhoneSynthwaveBg::Style::Synthwave;
		case 2: return (uint8_t) PhoneSynthwaveBg::Style::Plain;
		case 3: return (uint8_t) PhoneSynthwaveBg::Style::GridOnly;
		case 4: return (uint8_t) PhoneSynthwaveBg::Style::Stars;
		default: return kStyleInherit;
	}
}

const char* PhoneContactWallpaperPicker::entryName(uint8_t entryIndex) {
	switch(entryIndex) {
		case 0: return "Inherit";
		case 1: return "Synthwave";
		case 2: return "Plain";
		case 3: return "Grid Only";
		case 4: return "Stars";
		default: return "?";
	}
}

// ----- input -----

void PhoneContactWallpaperPicker::buttonPressed(uint i) {
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
			// Toggle preview on the focused entry. A second tap
			// restores the saved swatch so the user can audition
			// each row without committing.
			if(previewing && previewedIndex == cursor) {
				stopPreview();
			} else {
				stopPreview();
				startPreview();
			}
			break;

		case BTN_L:
			confirmPick();
			break;

		case BTN_R:
			invokeBack();
			break;

		case BTN_BACK:
			backHoldFired = false;
			break;

		default:
			break;
	}
}

void PhoneContactWallpaperPicker::buttonHeld(uint i) {
	if(i == BTN_BACK) {
		backHoldFired = true;
		invokeBack();
	}
}

void PhoneContactWallpaperPicker::buttonReleased(uint i) {
	if(i == BTN_BACK) {
		if(backHoldFired) return;
		invokeBack();
	}
}
