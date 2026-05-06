#include "PhoneDemoSpeedScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <Settings.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

/*
 * S206 - PhoneDemoSpeedScreen
 *
 * See PhoneDemoSpeedScreen.h for the design rationale. Implementation is
 * a near-copy of PhoneLockWidgetScreen / PhoneHomeLayoutScreen so the
 * three sibling pickers in the SOUND / DISPLAY / ADVANCED groups read
 * identically: same caption typography, same 18 px row height, same
 * dirty-aware soft-key labelling, same cursor / highlight movement.
 */

// MAKERphone retro palette - inlined per the established pattern in this
// codebase (see PhoneLockWidgetScreen.cpp / PhoneHomeLayoutScreen.cpp).
// Cyan for the caption, sunset orange for the saved-state checkmark dot,
// warm cream for option names, dim purple for the description / hint and
// the cursor highlight rectangle.
#define MP_ACCENT       lv_color_make(255, 140,  30)
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)
#define MP_TEXT         lv_color_make(255, 220, 180)
#define MP_DIM          lv_color_make( 70,  56, 100)
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)

namespace {
struct OptionDef {
	PhoneDemoSpeedScreen::Speed speed;
	const char*                 name;
	const char*                 desc;
};

// Order matters - rows are emitted top-to-bottom in this order and the
// cursor moves between them. SLOW first, MEDIUM second so the factory
// default sits in the middle of the list (the cursor opens roughly
// centred for the byte-identical default), FAST last (the most rushed
// option closing out the list).
const OptionDef kOptions[] = {
	{ PhoneDemoSpeedScreen::Speed::Slow,   "SLOW",   "5 s per slide"   },
	{ PhoneDemoSpeedScreen::Speed::Medium, "MEDIUM", "3 s per slide"   },
	{ PhoneDemoSpeedScreen::Speed::Fast,   "FAST",   "1.5 s per slide" },
};
constexpr uint8_t kOptionCount = sizeof(kOptions) / sizeof(kOptions[0]);
static_assert(kOptionCount == PhoneDemoSpeedScreen::OptionCount,
              "kOptions must match PhoneDemoSpeedScreen::OptionCount");

// Geometry inside the list container. Mirrors PhoneLockWidgetScreen so
// the sibling pickers read identically.
constexpr lv_coord_t kListX     = 4;
constexpr lv_coord_t kColDotX   = 6;
constexpr lv_coord_t kColNameX  = 18;
constexpr lv_coord_t kColDescX  = 70;
constexpr lv_coord_t kDotSize   = 6;

uint8_t findCursorForSpeed(PhoneDemoSpeedScreen::Speed s) {
	for(uint8_t i = 0; i < kOptionCount; ++i) {
		if(kOptions[i].speed == s) return i;
	}
	// Fallback on the row that carries Medium, the factory default,
	// so a corrupt persisted byte still opens on a sensible row.
	for(uint8_t i = 0; i < kOptionCount; ++i) {
		if(kOptions[i].speed == PhoneDemoSpeedScreen::Speed::Medium) return i;
	}
	return 0;
}
} // namespace

PhoneDemoSpeedScreen::PhoneDemoSpeedScreen()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  listContainer(nullptr),
		  highlight(nullptr),
		  hintLabel(nullptr),
		  initialSpeed(Speed::Medium) {

	for(uint8_t i = 0; i < OptionCount; ++i) {
		rows[i].dotObj  = nullptr;
		rows[i].nameObj = nullptr;
		rows[i].descObj = nullptr;
		rows[i].y       = 0;
		rows[i].speed   = kOptions[i].speed;
	}

	// Snapshot the persisted speed via the shared clamp on
	// PhoneDemoModeScreen so the picker and the screen it configures
	// agree on what counts as "valid". BACK reverts to this value.
	initialSpeed = PhoneDemoModeScreen::speedFromByte(Settings.get().demoSpeed);
	cursor       = findCursorForSpeed(initialSpeed);

	// Full-screen container, no scrollbars, no padding - same
	// blank-canvas pattern PhoneLockWidgetScreen / PhoneHomeLayoutScreen
	// use.
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
	refreshCheckmarks(initialSpeed);
}

PhoneDemoSpeedScreen::~PhoneDemoSpeedScreen() {
	// Children are parented to obj - LVGL frees recursively.
}

void PhoneDemoSpeedScreen::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneDemoSpeedScreen::onStop() {
	Input::getInstance()->removeListener(this);
}

PhoneDemoSpeedScreen::Speed PhoneDemoSpeedScreen::getFocusedSpeed() const {
	if(cursor >= OptionCount) return Speed::Medium;
	return rows[cursor].speed;
}

// ----- builders --------------------------------------------------------

void PhoneDemoSpeedScreen::buildCaption() {
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "DEMO SPEED");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, 12);
}

void PhoneDemoSpeedScreen::buildListContainer() {
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

void PhoneDemoSpeedScreen::buildList() {
	for(uint8_t i = 0; i < OptionCount; ++i) {
		const OptionDef& def = kOptions[i];
		Row& r = rows[i];

		r.speed = def.speed;
		r.y     = i * RowH;

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
		lv_label_set_text(r.nameObj, def.name);
		lv_obj_set_pos(r.nameObj, kColNameX, r.y + (RowH - 7) / 2);

		r.descObj = lv_label_create(listContainer);
		lv_obj_set_style_text_font(r.descObj, &pixelbasic7, 0);
		lv_obj_set_style_text_color(r.descObj, MP_LABEL_DIM, 0);
		lv_label_set_long_mode(r.descObj, LV_LABEL_LONG_DOT);
		lv_obj_set_width(r.descObj, ListW - kColDescX - 4);
		lv_label_set_text(r.descObj, def.desc);
		lv_obj_set_pos(r.descObj, kColDescX, r.y + (RowH - 7) / 2);
	}
}

void PhoneDemoSpeedScreen::buildHint() {
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(hintLabel, "UP / DOWN to choose");
	lv_obj_set_align(hintLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hintLabel, 100);
}

// ----- live updates ----------------------------------------------------

void PhoneDemoSpeedScreen::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	const bool dirty = (getFocusedSpeed() != initialSpeed);
	softKeys->set(dirty ? "SAVE"   : "",
	              dirty ? "CANCEL" : "BACK");
}

void PhoneDemoSpeedScreen::refreshHighlight() {
	if(highlight == nullptr) return;
	if(cursor >= OptionCount) cursor = 0;
	lv_obj_set_y(highlight, rows[cursor].y);
}

void PhoneDemoSpeedScreen::refreshCheckmarks(Speed savedSpeed) {
	for(uint8_t i = 0; i < OptionCount; ++i) {
		if(rows[i].dotObj == nullptr) continue;
		const bool active = (rows[i].speed == savedSpeed);
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

void PhoneDemoSpeedScreen::moveCursorBy(int8_t delta) {
	if(OptionCount == 0) return;
	int16_t next = static_cast<int16_t>(cursor) + delta;
	if(next < 0)                                   next = 0;
	if(next >= static_cast<int16_t>(OptionCount))  next = OptionCount - 1;
	if(static_cast<uint8_t>(next) == cursor) return;
	cursor = static_cast<uint8_t>(next);
	refreshHighlight();
	refreshSoftKeys();
}

// ----- save / cancel ---------------------------------------------------

void PhoneDemoSpeedScreen::saveAndExit() {
	const Speed chosen = getFocusedSpeed();
	Settings.get().demoSpeed = static_cast<uint8_t>(chosen);
	Settings.store();
	refreshCheckmarks(chosen);
	if(softKeys) softKeys->flashLeft();
	pop();
}

void PhoneDemoSpeedScreen::cancelAndExit() {
	if(softKeys) softKeys->flashRight();
	pop();
}

// ----- input handling --------------------------------------------------

void PhoneDemoSpeedScreen::buttonPressed(uint i) {
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
