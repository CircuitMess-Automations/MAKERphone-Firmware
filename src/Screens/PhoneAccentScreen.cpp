#include "PhoneAccentScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <Settings.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette -- inlined per the established pattern in this
// codebase (see PhoneBrightnessScreen.cpp / PhoneSettingsScreen.cpp).
// Cyan for the caption + focused channel border (informational), sunset
// orange for the un-focused-channel filled cells (the legacy bar look so
// the screen reads as part of the brightness-slider family), muted
// purple for the empty cells (track) and dim purple for the frame +
// numeric readouts. The chosen-RGB live preview is painted dynamically
// so it bypasses these constants entirely.
#define MP_ACCENT       lv_color_make(255, 140,  30)
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)
#define MP_TEXT         lv_color_make(255, 220, 180)
#define MP_DIM          lv_color_make( 70,  56, 100)
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)

// Slider geometry. Each channel bar is 16 cells * 5 px wide + 15 gaps *
// 1 px = 95 px of bar surface; the surrounding frame adds 2 px padding
// on each side -> 99 px. Three bars stack vertically with 4 px between
// them so the whole slider region is compact enough to leave room for
// the preview slab below on the 128 px display.
static constexpr lv_coord_t kCellW       = 5;
static constexpr lv_coord_t kCellH       = 5;
static constexpr lv_coord_t kCellGap     = 1;
static constexpr lv_coord_t kFramePadX   = 2;
static constexpr lv_coord_t kFramePadY   = 2;
static constexpr lv_coord_t kFrameW      =
		(16 * kCellW) + (15 * kCellGap) + (2 * kFramePadX);   // 99 px
static constexpr lv_coord_t kFrameH      = kCellH + (2 * kFramePadY); // 9 px
static constexpr lv_coord_t kSliderTopY  = 22;                        // first channel y inside obj
static constexpr lv_coord_t kSliderRowH  = kFrameH + 4;               // 13 px row pitch

// Preview slab geometry. Sits below the three channels in the central
// "what does it look like" band, big enough to host the chosen-RGB
// chip + a chat-bubble pill so the user can preview the accent in two
// representative roles at once.
static constexpr lv_coord_t kPreviewW    = 110;
static constexpr lv_coord_t kPreviewH    = 16;
static constexpr lv_coord_t kPreviewY    = kSliderTopY + 3 * kSliderRowH + 2; // ~63

PhoneAccentScreen::PhoneAccentScreen()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  previewSlab(nullptr),
		  previewLabel(nullptr),
		  toggleLabel(nullptr),
		  hintLabel(nullptr),
		  overrideEnabled(0),
		  initialOverride(0),
		  focused(Channel::R) {

	for(uint8_t c = 0; c < 3; ++c) {
		chLabels[c] = nullptr;
		chFrames[c] = nullptr;
		chValues[c] = nullptr;
		for(uint8_t i = 0; i < 16; ++i) chCells[c][i] = nullptr;
	}

	// Snapshot the values the screen opens with so BACK can revert the
	// preview without committing -- the standard Sony-Ericsson "discard
	// changes" flow byte-identical to PhoneBrightnessScreen.
	channels[0] = Settings.get().customAccentR;
	channels[1] = Settings.get().customAccentG;
	channels[2] = Settings.get().customAccentB;
	overrideEnabled = (Settings.get().customAccentEnabled != 0) ? 1 : 0;

	initialChannels[0] = channels[0];
	initialChannels[1] = channels[1];
	initialChannels[2] = channels[2];
	initialOverride    = overrideEnabled;

	// Full-screen container, no scrollbars, no padding -- same blank-
	// canvas pattern PhoneBrightnessScreen / PhoneSettingsScreen use.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Synthwave wallpaper at the bottom of the z-order so the rest of
	// the screen reads on top of the gradient like every other Phase-J
	// settings page.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Standard signal | clock | battery bar so the user can still see
	// the usual phone chrome while dialling in their accent.
	statusBar = new PhoneStatusBar(obj);

	buildCaption();
	buildSliders();
	buildPreview();
	buildToggle();
	buildHint();

	// Bottom: SAVE on the left, BACK on the right -- matches the
	// Sony-Ericsson option-screen convention. The caption is dirty-
	// aware via refreshSoftKeys() (pristine: "" / "BACK"; dirty:
	// "SAVE" / "CANCEL"), same pattern PhoneBrightnessScreen uses.
	softKeys = new PhoneSoftKeyBar(obj);
	refreshSoftKeys();

	// Initial paint: render every channel + the preview + the toggle
	// pill against the snapshot so the screen opens visually
	// consistent with the persisted state.
	refreshSliders();
	refreshPreview();
	refreshToggle();
}

PhoneAccentScreen::~PhoneAccentScreen() {
	// Children (wallpaper, statusBar, softKeys, sliders, cells, ...)
	// are all parented to obj -- LVGL frees them recursively when the
	// LVScreen base destructor tears obj down. Nothing manual.
}

void PhoneAccentScreen::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneAccentScreen::onStop() {
	Input::getInstance()->removeListener(this);
}

uint8_t PhoneAccentScreen::getChannelValue(Channel c) const {
	const uint8_t i = static_cast<uint8_t>(c);
	if(i >= 3) return 0;
	return channels[i];
}

// ----- builders --------------------------------------------------------

void PhoneAccentScreen::buildCaption() {
	// "ACCENT" caption in pixelbasic7 cyan, just under the status bar
	// at y=12 -- same anchor pattern PhoneBrightnessScreen /
	// PhoneSettingsScreen / PhoneCallHistory use, so the screen feels
	// visually consistent with the rest of the Phase-J family.
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "ACCENT");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, 12);
}

void PhoneAccentScreen::buildSliders() {
	// Three channels stacked vertically. Each channel:
	//   - a single-character label ("R" / "G" / "B") on the left
	//   - a 16-cell segment-bar frame in the middle
	//   - a 3-character numeric readout on the right ("255")
	// Layout x columns: label at x=8, frame centred, readout at x=144.
	const char* names[3] = { "R", "G", "B" };

	for(uint8_t c = 0; c < 3; ++c) {
		const lv_coord_t y = kSliderTopY + c * kSliderRowH;

		// Channel label (left column).
		chLabels[c] = lv_label_create(obj);
		lv_obj_set_style_text_font(chLabels[c], &pixelbasic7, 0);
		lv_obj_set_style_text_color(chLabels[c], MP_TEXT, 0);
		lv_label_set_text(chLabels[c], names[c]);
		lv_obj_set_pos(chLabels[c], 4, y + 1);

		// Frame: dim-purple rounded rect centred horizontally, sized to
		// host 16 cells. Cyan border on the focused frame is applied
		// in refreshSliders() so the focused channel reads as "the
		// one your sliders are aimed at".
		chFrames[c] = lv_obj_create(obj);
		lv_obj_remove_style_all(chFrames[c]);
		lv_obj_set_size(chFrames[c], kFrameW, kFrameH);
		lv_obj_set_pos(chFrames[c], 14, y);
		lv_obj_set_scrollbar_mode(chFrames[c], LV_SCROLLBAR_MODE_OFF);
		lv_obj_clear_flag(chFrames[c], LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_style_pad_all(chFrames[c], 0, 0);
		lv_obj_set_style_radius(chFrames[c], 2, 0);
		lv_obj_set_style_bg_opa(chFrames[c], LV_OPA_30, 0);
		lv_obj_set_style_bg_color(chFrames[c], MP_DIM, 0);
		lv_obj_set_style_border_color(chFrames[c], MP_LABEL_DIM, 0);
		lv_obj_set_style_border_opa(chFrames[c], LV_OPA_70, 0);
		lv_obj_set_style_border_width(chFrames[c], 1, 0);

		// 16 cells inside the frame. Each cell is a plain lv_obj rect;
		// the fill colour is decided in refreshSliders() so the per-
		// step repaint stays cheap (16 style calls per channel max).
		for(uint8_t i = 0; i < 16; ++i) {
			lv_obj_t* cell = lv_obj_create(chFrames[c]);
			lv_obj_remove_style_all(cell);
			lv_obj_set_size(cell, kCellW, kCellH);
			lv_obj_set_pos(cell, kFramePadX + i * (kCellW + kCellGap), kFramePadY);
			lv_obj_set_scrollbar_mode(cell, LV_SCROLLBAR_MODE_OFF);
			lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_set_style_radius(cell, 1, 0);
			lv_obj_set_style_border_width(cell, 0, 0);
			lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
			chCells[c][i] = cell;
		}

		// Numeric readout (right column). pixelbasic7 dim purple by
		// default; recoloured to cream on the focused channel inside
		// refreshSliders() so the focus state reads at a glance.
		chValues[c] = lv_label_create(obj);
		lv_obj_set_style_text_font(chValues[c], &pixelbasic7, 0);
		lv_obj_set_style_text_color(chValues[c], MP_LABEL_DIM, 0);
		lv_label_set_text(chValues[c], "0");
		lv_obj_set_pos(chValues[c], 14 + kFrameW + 4, y + 1);
	}
}

void PhoneAccentScreen::buildPreview() {
	// Preview slab -- a single rounded rect filled with the chosen
	// RGB so the user can see exactly what their accent will look
	// like under any theme. The fill colour is set live in
	// refreshPreview() on every channel step.
	previewSlab = lv_obj_create(obj);
	lv_obj_remove_style_all(previewSlab);
	lv_obj_set_size(previewSlab, kPreviewW, kPreviewH);
	lv_obj_set_align(previewSlab, LV_ALIGN_TOP_MID);
	lv_obj_set_y(previewSlab, kPreviewY);
	lv_obj_set_scrollbar_mode(previewSlab, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(previewSlab, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_pad_all(previewSlab, 0, 0);
	lv_obj_set_style_radius(previewSlab, 4, 0);
	lv_obj_set_style_border_width(previewSlab, 1, 0);
	lv_obj_set_style_border_color(previewSlab, MP_LABEL_DIM, 0);
	lv_obj_set_style_border_opa(previewSlab, LV_OPA_50, 0);
	lv_obj_set_style_bg_opa(previewSlab, LV_OPA_COVER, 0);

	// "PREVIEW" caption centred inside the slab. Painted in the deep-
	// purple BG_DARK so the cream / orange / cyan accents stay legible
	// regardless of the chosen RGB; refreshPreview() flips the caption
	// colour to MP_TEXT cream when the chosen RGB is dark enough that
	// a dark-on-dark caption would vanish.
	previewLabel = lv_label_create(previewSlab);
	lv_obj_set_style_text_font(previewLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(previewLabel, lv_color_make(20, 12, 36), 0);
	lv_label_set_text(previewLabel, "PREVIEW");
	lv_obj_set_align(previewLabel, LV_ALIGN_CENTER);
	lv_obj_set_y(previewLabel, 0);
}

void PhoneAccentScreen::buildToggle() {
	// Override toggle pill -- a single label that flips between
	// "[ ON ]" and "[ OFF ]" so the user can A/B their custom accent
	// against the per-theme accent without leaving the screen.
	toggleLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(toggleLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(toggleLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(toggleLabel, "[ OFF ]");
	lv_obj_set_align(toggleLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(toggleLabel, kPreviewY + kPreviewH + 3);
}

void PhoneAccentScreen::buildHint() {
	// Compact dim caption explaining the L/R bumper + 5 toggle. Same
	// dim-purple tone as PhoneBrightnessScreen's hint label so the
	// screens read as a family. Anchored just above the soft-key bar.
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(hintLabel, "L/R chan  5 toggle");
	lv_obj_set_align(hintLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hintLabel, 108);
}

// ----- live updates ----------------------------------------------------

void PhoneAccentScreen::refreshSliders() {
	// Per-channel paint pass: a cell is "lit" when its 1-based index
	// is <= channels[c]/StepSize. The fill colour for lit cells uses
	// the chosen channel value mapped onto a synthwave-orange tint
	// for un-focused channels (so the screen still reads as part of
	// the orange-bar family) and the LIVE chosen RGB chip for the
	// focused channel (so the focused bar feels "live"). Empty cells
	// are muted purple at low opacity, same as PhoneBrightnessScreen.
	for(uint8_t c = 0; c < 3; ++c) {
		const uint8_t lit = channels[c] / StepSize;   // 0..16
		const bool focusedRow = (static_cast<uint8_t>(focused) == c);

		// Frame border -- cyan on the focused row, dim purple
		// elsewhere.
		if(chFrames[c] != nullptr) {
			lv_obj_set_style_border_color(
				chFrames[c],
				focusedRow ? MP_HIGHLIGHT : MP_LABEL_DIM,
				0);
			lv_obj_set_style_border_opa(
				chFrames[c],
				focusedRow ? LV_OPA_COVER : LV_OPA_70,
				0);
		}

		// Channel-name label -- cream on the focused row, label-dim
		// elsewhere.
		if(chLabels[c] != nullptr) {
			lv_obj_set_style_text_color(
				chLabels[c],
				focusedRow ? MP_TEXT : MP_LABEL_DIM,
				0);
		}

		// Numeric readout -- cream on the focused row, dim elsewhere
		// + always shows the live channel value. snprintf into a
		// stack buffer is plenty; lv_label_set_text copies into LVGL
		// so the buffer can go out of scope immediately.
		if(chValues[c] != nullptr) {
			char buf[8];
			snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(channels[c]));
			lv_label_set_text(chValues[c], buf);
			lv_obj_set_style_text_color(
				chValues[c],
				focusedRow ? MP_TEXT : MP_LABEL_DIM,
				0);
		}

		// Cell paint loop. We tint lit cells with the legacy MP_ACCENT
		// sunset orange so the slider reads as part of the existing
		// brightness-slider family even when the user has dialled
		// their custom accent away from orange. The preview slab
		// below carries the live "what does it actually look like"
		// signal, so doubling that up on the slider would make the
		// bar harder to read at extreme RGB values.
		for(uint8_t i = 0; i < 16; ++i) {
			lv_obj_t* cell = chCells[c][i];
			if(cell == nullptr) continue;
			if(i < lit) {
				lv_obj_set_style_bg_color(cell, MP_ACCENT, 0);
				lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
			} else {
				lv_obj_set_style_bg_color(cell, MP_DIM, 0);
				lv_obj_set_style_bg_opa(cell, LV_OPA_50, 0);
			}
		}
	}
}

void PhoneAccentScreen::refreshPreview() {
	if(previewSlab == nullptr) return;

	const lv_color_t chosen = lv_color_make(channels[0], channels[1], channels[2]);
	lv_obj_set_style_bg_color(previewSlab, chosen, 0);

	// Toggle the inner caption colour based on perceived brightness so
	// the "PREVIEW" word stays legible regardless of the chosen RGB.
	// Standard ITU-R BT.601 luma weighting (R*0.299 + G*0.587 + B*0.114)
	// captures the human-perceived brightness of the chosen colour.
	// Threshold 128 lands cleanly in the middle of the 0..255 range so
	// vivid neons land on the dark caption (which keeps the slab
	// looking "lit") while deep / muted values flip to cream.
	const uint16_t luma = (static_cast<uint16_t>(channels[0]) * 299u
	                     + static_cast<uint16_t>(channels[1]) * 587u
	                     + static_cast<uint16_t>(channels[2]) * 114u) / 1000u;
	if(previewLabel != nullptr) {
		lv_obj_set_style_text_color(
			previewLabel,
			(luma < 128) ? MP_TEXT : lv_color_make(20, 12, 36),
			0);
	}
}

void PhoneAccentScreen::refreshToggle() {
	if(toggleLabel == nullptr) return;
	if(overrideEnabled != 0) {
		lv_label_set_text(toggleLabel, "[ ON ]");
		lv_obj_set_style_text_color(toggleLabel, MP_HIGHLIGHT, 0);
	} else {
		lv_label_set_text(toggleLabel, "[ OFF ]");
		lv_obj_set_style_text_color(toggleLabel, MP_LABEL_DIM, 0);
	}
}

void PhoneAccentScreen::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	const bool dirty =
			(channels[0]      != initialChannels[0]) ||
			(channels[1]      != initialChannels[1]) ||
			(channels[2]      != initialChannels[2]) ||
			(overrideEnabled  != initialOverride);
	softKeys->set(dirty ? "SAVE"   : "",
	              dirty ? "CANCEL" : "BACK");
}

// ----- input handling --------------------------------------------------

void PhoneAccentScreen::stepFocusedBy(int16_t delta) {
	const uint8_t i = static_cast<uint8_t>(focused);
	if(i >= 3) return;

	int16_t next = static_cast<int16_t>(channels[i]) + delta;
	if(next < 0)   next = 0;
	if(next > 255) next = 255;
	if(static_cast<uint8_t>(next) == channels[i]) return;
	channels[i] = static_cast<uint8_t>(next);

	refreshSliders();
	refreshPreview();
	refreshSoftKeys();
}

void PhoneAccentScreen::cycleChannel(int8_t delta) {
	int8_t next = static_cast<int8_t>(focused) + delta;
	while(next < 0) next += 3;
	while(next > 2) next -= 3;
	focused = static_cast<Channel>(next);
	refreshSliders();
}

void PhoneAccentScreen::saveAndExit() {
	// Persist the live preview state into the Settings partition. The
	// channels are already in 0..255 space and overrideEnabled is
	// strict 0/1, so the writes are a direct assignment.
	Settings.get().customAccentEnabled = overrideEnabled;
	Settings.get().customAccentR       = channels[0];
	Settings.get().customAccentG       = channels[1];
	Settings.get().customAccentB       = channels[2];
	Settings.store();
	if(softKeys) softKeys->flashLeft();
	pop();
}

void PhoneAccentScreen::cancelAndExit() {
	// Revert the preview-only edits and pop. We deliberately do NOT
	// touch Settings.get() here -- the persisted values are still the
	// ones we snapshotted in the ctor, so the next screen rebuild
	// reads the user's pre-edit accent through MakerphoneTheme.
	channels[0]     = initialChannels[0];
	channels[1]     = initialChannels[1];
	channels[2]     = initialChannels[2];
	overrideEnabled = initialOverride;
	if(softKeys) softKeys->flashRight();
	pop();
}

void PhoneAccentScreen::buttonPressed(uint i) {
	switch(i) {
		case BTN_LEFT:
		case BTN_4:
		case BTN_2:
			stepFocusedBy(-StepSize);
			break;

		case BTN_RIGHT:
		case BTN_6:
		case BTN_8:
			stepFocusedBy(+StepSize);
			break;

		case BTN_L:
			cycleChannel(-1);
			break;
		case BTN_R:
			cycleChannel(+1);
			break;

		case BTN_5:
			// Toggle the override flag live so the user can A/B the
			// custom accent against the per-theme accent without
			// having to SAVE first. The preview slab keeps painting
			// the chosen RGB regardless of the toggle -- the toggle
			// only affects what *every other* screen will look like
			// after SAVE.
			overrideEnabled = (overrideEnabled != 0) ? 0 : 1;
			refreshToggle();
			refreshSoftKeys();
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
