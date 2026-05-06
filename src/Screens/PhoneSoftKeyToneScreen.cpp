#include "PhoneSoftKeyToneScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <Settings.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../Services/PhoneSoftKeyTone.h"

// MAKERphone retro palette - inlined per the established pattern in this
// codebase (see PhoneHapticsScreen.cpp / PhoneSoundScreen.cpp). Cyan for
// the caption, sunset orange for the saved-state dot, warm cream for
// option names, dim purple for descriptions / hint and the cursor
// highlight rectangle.
#define MP_ACCENT       lv_color_make(255, 140,  30)
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)
#define MP_TEXT         lv_color_make(255, 220, 180)
#define MP_DIM          lv_color_make( 70,  56, 100)
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)

namespace {
// Geometry inside the list container. Columns shifted left vs. the
// 18 px PhoneSoundScreen rows so a 14 px row + 5 catalogue entries
// still reads cleanly with the description column starting under the
// 5-letter name column.
constexpr lv_coord_t kListX     = 4;
constexpr lv_coord_t kColDotX   = 4;
constexpr lv_coord_t kColNameX  = 14;
constexpr lv_coord_t kColDescX  = 64;
constexpr lv_coord_t kDotSize   = 6;
} // namespace

PhoneSoftKeyToneScreen::PhoneSoftKeyToneScreen()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  listContainer(nullptr),
		  highlight(nullptr),
		  hintLabel(nullptr),
		  initialId(PhoneSoftKeyToneLib::DefaultId) {

	for(uint8_t i = 0; i < OptionCount; ++i) {
		rows[i].dotObj  = nullptr;
		rows[i].nameObj = nullptr;
		rows[i].descObj = nullptr;
		rows[i].y       = 0;
		rows[i].toneId  = i;
	}

	// Snapshot the persisted tone id. BACK reverts to this value.
	initialId = PhoneSoftKeyToneLib::getActive();
	cursor    = initialId;

	// Full-screen container, no scrollbars, no padding - same blank-canvas
	// pattern PhoneHapticsScreen / PhoneSoundScreen use.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildCaption();
	buildListContainer();
	buildList();
	buildHint();

	softKeys = new PhoneSoftKeyBar(obj);
	refreshSoftKeys();

	refreshHighlight();
	refreshCheckmarks(initialId);
	// No preview on screen open -- opening the page should never produce
	// an audible click (matches PhoneHapticsScreen). Subsequent cursor
	// moves do fire the preview.
}

PhoneSoftKeyToneScreen::~PhoneSoftKeyToneScreen() {
	// Children parented to obj - LVGL frees recursively.
}

void PhoneSoftKeyToneScreen::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneSoftKeyToneScreen::onStop() {
	Input::getInstance()->removeListener(this);
}

// ----- builders --------------------------------------------------------

void PhoneSoftKeyToneScreen::buildCaption() {
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "SOFTKEY TONE");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, 12);
}

void PhoneSoftKeyToneScreen::buildListContainer() {
	const lv_coord_t totalH = OptionCount * RowH;

	listContainer = lv_obj_create(obj);
	lv_obj_remove_style_all(listContainer);
	lv_obj_set_size(listContainer, ListW, totalH);
	lv_obj_set_pos(listContainer, kListX, ListY);
	lv_obj_set_scrollbar_mode(listContainer, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(listContainer, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_pad_all(listContainer, 0, 0);
	lv_obj_set_style_bg_opa(listContainer, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(listContainer, 0, 0);

	highlight = lv_obj_create(listContainer);
	lv_obj_remove_style_all(highlight);
	lv_obj_set_size(highlight, ListW, RowH);
	lv_obj_set_pos(highlight, 0, 0);
	lv_obj_set_scrollbar_mode(highlight, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(highlight, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_radius(highlight, 2, 0);
	lv_obj_set_style_bg_color(highlight, MP_DIM, 0);
	lv_obj_set_style_bg_opa(highlight, LV_OPA_70, 0);
	lv_obj_set_style_border_color(highlight, MP_HIGHLIGHT, 0);
	lv_obj_set_style_border_opa(highlight, LV_OPA_50, 0);
	lv_obj_set_style_border_width(highlight, 1, 0);
}

void PhoneSoftKeyToneScreen::buildList() {
	for(uint8_t i = 0; i < OptionCount; ++i) {
		Row& r = rows[i];

		r.toneId = i;
		r.y      = i * RowH;

		r.dotObj = lv_obj_create(listContainer);
		lv_obj_remove_style_all(r.dotObj);
		lv_obj_set_size(r.dotObj, kDotSize, kDotSize);
		lv_obj_set_pos(r.dotObj, kColDotX, r.y + (RowH - kDotSize) / 2);
		lv_obj_set_scrollbar_mode(r.dotObj, LV_SCROLLBAR_MODE_OFF);
		lv_obj_clear_flag(r.dotObj, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_style_radius(r.dotObj, kDotSize / 2, 0);
		lv_obj_set_style_border_color(r.dotObj, MP_LABEL_DIM, 0);
		lv_obj_set_style_border_width(r.dotObj, 1, 0);
		lv_obj_set_style_bg_opa(r.dotObj, LV_OPA_TRANSP, 0);

		r.nameObj = lv_label_create(listContainer);
		lv_obj_set_style_text_font(r.nameObj, &pixelbasic7, 0);
		lv_obj_set_style_text_color(r.nameObj, MP_TEXT, 0);
		lv_label_set_text(r.nameObj, PhoneSoftKeyToneLib::name(i));
		lv_obj_set_pos(r.nameObj, kColNameX, r.y + (RowH - 7) / 2);

		r.descObj = lv_label_create(listContainer);
		lv_obj_set_style_text_font(r.descObj, &pixelbasic7, 0);
		lv_obj_set_style_text_color(r.descObj, MP_LABEL_DIM, 0);
		lv_label_set_long_mode(r.descObj, LV_LABEL_LONG_DOT);
		lv_obj_set_width(r.descObj, ListW - kColDescX - 4);
		lv_label_set_text(r.descObj, PhoneSoftKeyToneLib::desc(i));
		lv_obj_set_pos(r.descObj, kColDescX, r.y + (RowH - 7) / 2);
	}
}

void PhoneSoftKeyToneScreen::buildHint() {
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(hintLabel, "UP / DOWN to choose");
	lv_obj_set_align(hintLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hintLabel, 100);
}

// ----- live updates ----------------------------------------------------

void PhoneSoftKeyToneScreen::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	const bool dirty = (getFocusedId() != initialId);
	softKeys->set(dirty ? "SAVE"   : "",
	              dirty ? "CANCEL" : "BACK");
}

void PhoneSoftKeyToneScreen::refreshHighlight() {
	if(highlight == nullptr) return;
	if(cursor >= OptionCount) cursor = 0;
	lv_obj_set_y(highlight, rows[cursor].y);
}

void PhoneSoftKeyToneScreen::refreshCheckmarks(uint8_t savedId) {
	for(uint8_t i = 0; i < OptionCount; ++i) {
		if(rows[i].dotObj == nullptr) continue;
		const bool active = (rows[i].toneId == savedId);
		if(active) {
			lv_obj_set_style_bg_color(rows[i].dotObj, MP_ACCENT, 0);
			lv_obj_set_style_bg_opa(rows[i].dotObj, LV_OPA_COVER, 0);
			lv_obj_set_style_border_color(rows[i].dotObj, MP_ACCENT, 0);
		} else {
			lv_obj_set_style_bg_opa(rows[i].dotObj, LV_OPA_TRANSP, 0);
			lv_obj_set_style_border_color(rows[i].dotObj, MP_LABEL_DIM, 0);
		}
	}
}

void PhoneSoftKeyToneScreen::moveCursorBy(int8_t delta) {
	if(OptionCount == 0) return;
	int16_t next = static_cast<int16_t>(cursor) + delta;
	if(next < 0)                                   next = 0;
	if(next >= static_cast<int16_t>(OptionCount))  next = OptionCount - 1;
	if(static_cast<uint8_t>(next) == cursor) return;
	cursor = static_cast<uint8_t>(next);
	refreshHighlight();
	refreshSoftKeys();
	applyPreview(getFocusedId());
}

void PhoneSoftKeyToneScreen::applyPreview(uint8_t toneId) {
	// Drives the Piezo directly via the library so the preview works
	// regardless of Settings.sound -- same convention PhoneHapticsScreen
	// uses for its own preview path. Silent (id 4) is a no-op inside
	// PhoneSoftKeyToneLib::play().
	PhoneSoftKeyToneLib::play(toneId);
}

uint8_t PhoneSoftKeyToneScreen::getFocusedId() const {
	if(cursor >= OptionCount) return PhoneSoftKeyToneLib::DefaultId;
	return rows[cursor].toneId;
}

// ----- save / cancel ---------------------------------------------------

void PhoneSoftKeyToneScreen::saveAndExit() {
	const uint8_t chosen = getFocusedId();
	PhoneSoftKeyToneLib::setActive(chosen);   // also calls Settings.store()
	refreshCheckmarks(chosen);
	if(softKeys) softKeys->flashLeft();
	pop();
}

void PhoneSoftKeyToneScreen::cancelAndExit() {
	// No live writes happened (we only wrote on saveAndExit), so nothing
	// to revert in Settings. Just pop.
	if(softKeys) softKeys->flashRight();
	pop();
}

// ----- input handling --------------------------------------------------

void PhoneSoftKeyToneScreen::buttonPressed(uint i) {
	switch(i) {
		case BTN_LEFT:
		case BTN_2:
		case BTN_4:
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
