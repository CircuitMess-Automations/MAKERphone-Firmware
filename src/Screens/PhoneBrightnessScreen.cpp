#include "PhoneBrightnessScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <Chatter.h>
#include <Settings.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - inlined per the established pattern in this
// codebase (see PhoneSettingsScreen.cpp, PhoneCallHistory.cpp). Cyan for
// the caption + percent readout (informational), sunset orange for the
// active segment cells (the "burning" filled portion of the bar), muted
// purple for the empty cells (track) and dim purple for the frame +
// "-"/"+" affordance icons + hint caption.
#define MP_ACCENT       lv_color_make(255, 140,  30)
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)
#define MP_TEXT         lv_color_make(255, 220, 180)
#define MP_DIM          lv_color_make( 70,  56, 100)
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)

// Slider geometry. The 10-cell bar sits centred horizontally with a 1 px
// gap between cells (so 10 cells * 9 px + 9 gaps = 99 px of bar surface,
// plus 2 px of frame border on each side -> 103 px frame), leaving room
// for "-" / "+" affordance icons in the 160 px viewport.
static constexpr lv_coord_t kCellW       = 9;
static constexpr lv_coord_t kCellH       = 8;
static constexpr lv_coord_t kCellGap     = 1;
static constexpr lv_coord_t kFramePadX   = 2;
static constexpr lv_coord_t kFramePadY   = 2;
static constexpr lv_coord_t kFrameW      =
		(10 * kCellW) + (9 * kCellGap) + (2 * kFramePadX);    // 113 px
static constexpr lv_coord_t kFrameH      = kCellH + (2 * kFramePadY); // 12 px
static constexpr lv_coord_t kFrameY      = 70;                        // baseline of slider row

PhoneBrightnessScreen::PhoneBrightnessScreen()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  percentLabel(nullptr),
		  sliderFrame(nullptr),
		  minusLabel(nullptr),
		  plusLabel(nullptr),
		  hintLabel(nullptr) {

	for(uint8_t i = 0; i < 10; ++i) cells[i] = nullptr;

	// Snapshot of the brightness the screen opened with. BACK reverts to
	// this value so the user can preview different stops without
	// committing - the standard Sony-Ericsson "discard changes" flow.
	initialBrightness = Settings.get().screenBrightness;

	// Map the persisted 0..250 brightness onto the closest 0..10 stop.
	// Default Settings value of 200 lands cleanly on stop 8 (80%) so
	// the bar visually agrees with the actual hardware brightness on
	// open. We round to the nearest stop rather than truncating so a
	// previously-saved off-grid value (e.g. an earlier firmware that
	// stepped by 5) still snaps to the closest tick.
	uint16_t snapped = (static_cast<uint16_t>(initialBrightness) + (StepSize / 2)) / StepSize;
	if(snapped >= StopCount) snapped = StopCount - 1;
	cursor = static_cast<uint8_t>(snapped);

	// Full-screen container, no scrollbars, no padding - same blank-canvas
	// pattern PhoneSettingsScreen / PhoneAppStubScreen use.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Synthwave wallpaper at the bottom of the z-order so the slider +
	// readout overlay it cleanly.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Standard signal | clock | battery bar so the user can still see the
	// usual phone chrome while adjusting brightness.
	statusBar = new PhoneStatusBar(obj);

	buildCaption();
	buildPercent();
	buildSlider();
	buildHint();

	// Bottom: SAVE on the left, BACK on the right - matches the
	// Sony-Ericsson convention for option screens (commit / discard).
	// S67 - the L caption is dirty-aware ("" until the user moves
	// the cursor away from the saved stop) and the R caption flips
	// from "BACK" -> "CANCEL" so the discard action reads correctly.
	softKeys = new PhoneSoftKeyBar(obj);
	refreshSoftKeys();

	// Initial paint: render the cells against the snapped cursor and
	// drive the LCD so the screen opens visually consistent with both
	// the persisted value and the live backlight.
	refreshSlider();
}

PhoneBrightnessScreen::~PhoneBrightnessScreen() {
	// Children (wallpaper, statusBar, softKeys, labels, cells) are all
	// parented to obj - LVGL frees them recursively when the screen's
	// obj is destroyed by the LVScreen base destructor.
}

void PhoneBrightnessScreen::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneBrightnessScreen::onStop() {
	Input::getInstance()->removeListener(this);
}

// ----- builders --------------------------------------------------------

void PhoneBrightnessScreen::buildCaption() {
	// "BRIGHTNESS" caption in pixelbasic7 cyan, just under the status
	// bar - same anchor pattern PhoneSettingsScreen uses for "SETTINGS"
	// and PhoneCallHistory uses for "CALL HISTORY". Keeps every Phase-J
	// page visually consistent.
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "BRIGHTNESS");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, 12);
}

void PhoneBrightnessScreen::buildPercent() {
	// Big pixelbasic16 percentage readout in cyan, centred horizontally
	// above the slider. Acts as the focal point of the screen so the
	// user always knows the current value at a glance. Initial text is
	// "" because refreshSlider() will fill it on the first paint.
	percentLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(percentLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(percentLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(percentLabel, "");
	lv_obj_set_align(percentLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(percentLabel, 36);
}

void PhoneBrightnessScreen::buildSlider() {
	// Frame: a thin dim-purple rounded rect that hosts the 10 segment
	// cells. Centred horizontally on the 160 px display, anchored at
	// kFrameY so it sits in a comfortable middle band beneath the
	// percentage readout.
	sliderFrame = lv_obj_create(obj);
	lv_obj_remove_style_all(sliderFrame);
	lv_obj_set_size(sliderFrame, kFrameW, kFrameH);
	lv_obj_set_align(sliderFrame, LV_ALIGN_TOP_MID);
	lv_obj_set_y(sliderFrame, kFrameY);
	lv_obj_set_scrollbar_mode(sliderFrame, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(sliderFrame, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_pad_all(sliderFrame, 0, 0);
	lv_obj_set_style_radius(sliderFrame, 2, 0);
	lv_obj_set_style_bg_opa(sliderFrame, LV_OPA_30, 0);
	lv_obj_set_style_bg_color(sliderFrame, MP_DIM, 0);
	lv_obj_set_style_border_color(sliderFrame, MP_LABEL_DIM, 0);
	lv_obj_set_style_border_opa(sliderFrame, LV_OPA_70, 0);
	lv_obj_set_style_border_width(sliderFrame, 1, 0);

	// 10 segment cells. Each cell is a tiny lv_obj rectangle; the fill
	// color is decided in refreshSlider() (sunset orange for cells <=
	// cursor, muted purple otherwise). Children of the frame so they
	// inherit the centred anchor and clip cleanly inside the rounded
	// border.
	for(uint8_t i = 0; i < 10; ++i) {
		lv_obj_t* cell = lv_obj_create(sliderFrame);
		lv_obj_remove_style_all(cell);
		lv_obj_set_size(cell, kCellW, kCellH);
		lv_obj_set_pos(cell, kFramePadX + i * (kCellW + kCellGap), kFramePadY);
		lv_obj_set_scrollbar_mode(cell, LV_SCROLLBAR_MODE_OFF);
		lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_style_radius(cell, 1, 0);
		lv_obj_set_style_border_width(cell, 0, 0);
		lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
		cells[i] = cell;
	}

	// "-" and "+" affordance icons flanking the frame. They live on the
	// screen object (not the frame) so they sit just outside the frame
	// edges; we anchor them to the centre and offset by half the frame
	// width plus a small gap so they always hug the frame symmetrically
	// regardless of the 160 px viewport.
	minusLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(minusLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(minusLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(minusLabel, "-");
	lv_obj_set_align(minusLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_pos(minusLabel, -(kFrameW / 2) - 8, kFrameY + 2);

	plusLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(plusLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(plusLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(plusLabel, "+");
	lv_obj_set_align(plusLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_pos(plusLabel, (kFrameW / 2) + 5, kFrameY + 2);
}

void PhoneBrightnessScreen::buildHint() {
	// Small dim caption under the slider explaining the input mapping.
	// Same dim-purple tone as the group headers in PhoneSettingsScreen
	// so the screens read as a family.
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(hintLabel, "LEFT / RIGHT to adjust");
	lv_obj_set_align(hintLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hintLabel, 92);
}

// ----- live updates ----------------------------------------------------

void PhoneBrightnessScreen::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	const uint8_t initialStop = (static_cast<uint16_t>(initialBrightness) +
	                             (StepSize / 2)) / StepSize;
	const bool dirty = (cursor != initialStop);
	softKeys->set(dirty ? "SAVE"   : "",
	              dirty ? "CANCEL" : "BACK");
}

void PhoneBrightnessScreen::refreshSlider() {
	// Defensive clamp - cursor should never be out of range, but the
	// step / clamp logic in stepBy() is the only writer so a bug there
	// would otherwise paint garbage. Belt-and-braces.
	if(cursor >= StopCount) cursor = StopCount - 1;

	// Cell paint pass: every cell whose 1-based position is <= cursor
	// is "lit" (sunset orange) and any beyond it is "dim" (muted
	// purple at low opacity so the empty track still reads as part of
	// the bar). cursor == 0 paints all-empty - the lowest brightness
	// stop is still visually meaningful.
	for(uint8_t i = 0; i < 10; ++i) {
		if(cells[i] == nullptr) continue;
		if(i < cursor) {
			lv_obj_set_style_bg_color(cells[i], MP_ACCENT, 0);
			lv_obj_set_style_bg_opa(cells[i], LV_OPA_COVER, 0);
		} else {
			lv_obj_set_style_bg_color(cells[i], MP_DIM, 0);
			lv_obj_set_style_bg_opa(cells[i], LV_OPA_50, 0);
		}
	}

	// Percentage readout. cursor * 10 keeps the math obvious and lands
	// on familiar tens (0%, 10%, .. 100%). snprintf into a small stack
	// buffer is plenty - lv_label_set_text copies into its internal
	// store, so the buffer is free to go out of scope immediately.
	if(percentLabel != nullptr) {
		char buf[8];
		snprintf(buf, sizeof(buf), "%u%%", static_cast<unsigned>(cursor) * 10u);
		lv_label_set_text(percentLabel, buf);
		// Recolor the readout to MP_TEXT (warm cream) at the lowest
		// stop so a 0% value reads as "low" rather than a confident
		// cyan. Anything > 0 stays cyan.
		lv_obj_set_style_text_color(percentLabel, (cursor == 0) ? MP_TEXT : MP_HIGHLIGHT, 0);
	}

	// Drive the LCD live so the brightness change is felt under the
	// user's finger. We do NOT persist on every step - that would write
	// to NVS on every keypress; instead we persist only on SAVE.
	applyPreviewBrightness();
}

void PhoneBrightnessScreen::applyPreviewBrightness() {
	Chatter.setBrightness(getPreviewBrightness());
}

uint8_t PhoneBrightnessScreen::getPreviewBrightness() const {
	uint16_t v = static_cast<uint16_t>(cursor) * StepSize;
	if(v > 250) v = 250;
	return static_cast<uint8_t>(v);
}

// ----- input handling --------------------------------------------------

void PhoneBrightnessScreen::stepBy(int8_t delta) {
	// Clamp rather than wrap - on a brightness slider, wrapping from
	// 100% straight to 0% would be a UX trap. Hitting the end of the
	// bar should feel like a hard stop.
	int16_t next = static_cast<int16_t>(cursor) + delta;
	if(next < 0)                                    next = 0;
	if(next >= static_cast<int16_t>(StopCount))     next = StopCount - 1;
	if(static_cast<uint8_t>(next) == cursor) return;
	cursor = static_cast<uint8_t>(next);
	refreshSlider();
	refreshSoftKeys();
}

void PhoneBrightnessScreen::saveAndExit() {
	// Persist the snapped value into the Settings partition. The cursor
	// is already in 0..StopCount-1 so the snap is a single multiply.
	Settings.get().screenBrightness = getPreviewBrightness();
	Settings.store();
	if(softKeys) softKeys->flashLeft();
	pop();
}

void PhoneBrightnessScreen::cancelAndExit() {
	// Revert the live backlight to whatever it was on entry so the
	// preview is non-destructive. We deliberately do NOT touch
	// Settings.get() here - if the user never pressed SAVE, the
	// persisted value is still the one we snapshotted in the ctor.
	Chatter.setBrightness(initialBrightness);
	if(softKeys) softKeys->flashRight();
	pop();
}

void PhoneBrightnessScreen::buttonPressed(uint i) {
	switch(i) {
		case BTN_LEFT:
		case BTN_4:
		case BTN_2:
			stepBy(-1);
			break;

		case BTN_RIGHT:
		case BTN_6:
		case BTN_8:
			stepBy(+1);
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
