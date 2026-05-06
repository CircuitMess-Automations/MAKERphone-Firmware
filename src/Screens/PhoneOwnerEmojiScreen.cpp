#include "PhoneOwnerEmojiScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <Settings.h>
#include <stdio.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - inlined per the established pattern in this
// codebase (see PhoneLockWidgetScreen.cpp / PhoneAccentScreen.cpp /
// PhoneOwnerNameScreen.cpp). Cyan caption, sunset orange accents, warm
// cream for set pixels, dim purple for unset cells / hint.
#define MP_BG_DARK      lv_color_make( 20,  12,  36)
#define MP_ACCENT       lv_color_make(255, 140,  30)
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)
#define MP_TEXT         lv_color_make(255, 220, 180)
#define MP_DIM          lv_color_make( 70,  56, 100)
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)

namespace {

// Geometry. The preview slab is centred horizontally with its top edge
// at y = 26 (just under the caption strip), leaving y = 64..118 for the
// name + index captions and the soft-key bar.
constexpr lv_coord_t kPreviewY    = 26;
constexpr lv_coord_t kNameY       = 70;
constexpr lv_coord_t kIndexY      = 82;
constexpr lv_coord_t kHintY       = 100;
constexpr lv_coord_t kFrameInset  = 2;   // 2 px border around the cell grid

} // namespace

PhoneOwnerEmojiScreen::PhoneOwnerEmojiScreen()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr) {

	// Snapshot the persisted id so cancelAndExit() reverts cleanly. The
	// cursor opens on the saved entry so a re-entry of the picker shows
	// the user where they left off.
	initialId = PhoneOwnerEmoji::clampedId(Settings.get().ownerEmoji);
	cursor    = initialId;

	// Full-screen container, no scrollbars, no padding -- same blank-canvas
	// pattern PhoneLockWidgetScreen / PhoneAccentScreen use.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildCaption();
	buildPreview();
	buildNameRow();
	buildHint();

	softKeys = new PhoneSoftKeyBar(obj);
	refreshSoftKeys();

	refreshPreview();
	refreshNameRow();
}

PhoneOwnerEmojiScreen::~PhoneOwnerEmojiScreen() {
	// Children are parented to obj - LVGL frees recursively.
}

void PhoneOwnerEmojiScreen::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneOwnerEmojiScreen::onStop() {
	Input::getInstance()->removeListener(this);
}

// ----- builders --------------------------------------------------------

void PhoneOwnerEmojiScreen::buildCaption() {
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "OWNER EMOJI");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, 12);
}

void PhoneOwnerEmojiScreen::buildPreview() {
	// Preview frame: a fixed-size container with a thin sunset-orange
	// border around the cell grid. The cell grid sits inset by
	// kFrameInset px so the border reads as a deliberate frame and not
	// as a stray top row of cells.
	const lv_coord_t outerW = PreviewSize + kFrameInset * 2;
	const lv_coord_t outerH = PreviewSize + kFrameInset * 2;

	previewBox = lv_obj_create(obj);
	lv_obj_remove_style_all(previewBox);
	lv_obj_clear_flag(previewBox, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(previewBox, outerW, outerH);
	lv_obj_set_style_radius(previewBox, 2, 0);
	lv_obj_set_style_bg_color(previewBox, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(previewBox, LV_OPA_70, 0);
	lv_obj_set_style_border_color(previewBox, MP_ACCENT, 0);
	lv_obj_set_style_border_width(previewBox, 1, 0);
	lv_obj_set_style_border_opa(previewBox, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(previewBox, 0, 0);
	lv_obj_set_align(previewBox, LV_ALIGN_TOP_MID);
	lv_obj_set_y(previewBox, kPreviewY);

	// Pre-allocate the cell grid. Each cell is a PreviewScale x
	// PreviewScale plain rect; refreshPreview() recolours them in
	// place on every cursor scrub so the LVGL alloc churn stays at
	// zero.
	for(uint8_t y = 0; y < PhoneOwnerEmoji::Height; ++y) {
		for(uint8_t x = 0; x < PhoneOwnerEmoji::Width; ++x) {
			lv_obj_t* c = lv_obj_create(previewBox);
			lv_obj_remove_style_all(c);
			lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_set_size(c, PreviewScale, PreviewScale);
			lv_obj_set_pos(c,
				kFrameInset + x * PreviewScale,
				kFrameInset + y * PreviewScale);
			lv_obj_set_style_radius(c, 0, 0);
			lv_obj_set_style_border_width(c, 0, 0);
			lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
			cells[y][x] = c;
		}
	}
}

void PhoneOwnerEmojiScreen::buildNameRow() {
	nameLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(nameLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(nameLabel, MP_TEXT, 0);
	lv_label_set_text(nameLabel, "");
	lv_obj_set_align(nameLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(nameLabel, kNameY);

	indexLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(indexLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(indexLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(indexLabel, "");
	lv_obj_set_align(indexLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(indexLabel, kIndexY);
}

void PhoneOwnerEmojiScreen::buildHint() {
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(hintLabel, "LEFT / RIGHT to choose");
	lv_obj_set_align(hintLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hintLabel, kHintY);
}

// ----- repainters ------------------------------------------------------

void PhoneOwnerEmojiScreen::refreshPreview() {
	// Walk the catalogue glyph one cell at a time and flip each cell's
	// fill on / off. Set pixels paint in the same warm-cream MP_TEXT
	// the lock-screen mini uses so the picker preview matches the
	// final on-device look.
	const uint8_t idx = PhoneOwnerEmoji::clampedId(cursor);
	for(uint8_t y = 0; y < PhoneOwnerEmoji::Height; ++y) {
		for(uint8_t x = 0; x < PhoneOwnerEmoji::Width; ++x) {
			lv_obj_t* c = cells[y][x];
			if(c == nullptr) continue;
			const bool on = PhoneOwnerEmoji::pixelAt(idx, x, y);
			if(on) {
				lv_obj_set_style_bg_color(c, MP_TEXT, 0);
				lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
			} else {
				lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
			}
		}
	}
}

void PhoneOwnerEmojiScreen::refreshNameRow() {
	const uint8_t idx = PhoneOwnerEmoji::clampedId(cursor);
	const PhoneOwnerEmoji::Glyph& g = PhoneOwnerEmoji::at(idx);
	if(nameLabel) {
		lv_label_set_text(nameLabel, g.name);
	}
	if(indexLabel) {
		char buf[16];
		// 1-based for the user-facing display; "1 / 13" is more
		// natural than "0 / 12" for a feature-phone pager.
		snprintf(buf, sizeof(buf), "%u / %u",
			(unsigned)(idx + 1u),
			(unsigned)(PhoneOwnerEmoji::count()));
		lv_label_set_text(indexLabel, buf);
	}
}

void PhoneOwnerEmojiScreen::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	const bool dirty = (cursor != initialId);
	softKeys->set(dirty ? "PICK" : "PICK", "BACK");
	(void)dirty; // softkey labels stay constant; we keep the local for
	             // future "SAVED / DISCARD" wording experiments.
}

// ----- input -----------------------------------------------------------

void PhoneOwnerEmojiScreen::moveCursorBy(int8_t delta) {
	const uint8_t total = PhoneOwnerEmoji::count();
	if(total == 0) return;
	int16_t next = static_cast<int16_t>(cursor) + delta;
	while(next < 0)                              next += total;
	while(next >= static_cast<int16_t>(total))   next -= total;
	if(static_cast<uint8_t>(next) == cursor) return;
	cursor = static_cast<uint8_t>(next);
	refreshPreview();
	refreshNameRow();
	refreshSoftKeys();
}

void PhoneOwnerEmojiScreen::saveAndExit() {
	const uint8_t chosen = PhoneOwnerEmoji::clampedId(cursor);
	Settings.get().ownerEmoji = chosen;
	Settings.store();
	if(softKeys) softKeys->flashLeft();
	pop();
}

void PhoneOwnerEmojiScreen::cancelAndExit() {
	if(softKeys) softKeys->flashRight();
	pop();
}

void PhoneOwnerEmojiScreen::buttonPressed(uint i) {
	switch(i) {
		case BTN_LEFT:
		case BTN_4:
		case BTN_2:
			moveCursorBy(-1);
			break;

		case BTN_RIGHT:
		case BTN_6:
		case BTN_8:
			moveCursorBy(+1);
			break;

		case BTN_ENTER:
			saveAndExit();
			break;

		case BTN_BACK:
			cancelAndExit();
			break;

		default:
			break;
	}
}
