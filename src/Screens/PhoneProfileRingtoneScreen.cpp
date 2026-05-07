#include "PhoneProfileRingtoneScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <Settings.h>
#include <stdio.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../Services/PhoneContactRingtone.h"
#include "../Services/PhoneRingtoneEngine.h"

// =====================================================================
// PhoneProfileRingtoneScreen (S160)
//
// Two-mode list/pick screen that lets the user assign a different
// stock or composed ringtone to each of the five PhoneProfileScreen
// profiles. Mode-switch fires LVGL hide/show on the two child
// containers (listContainer / pickContainer) so the cursor highlight
// rect always lives inside whichever view is active and the layout
// math stays trivial. Persistence is immediate on PICK -- no
// "discard changes" state to track because the picker only ever
// commits the focused id, not a working-copy table.
// =====================================================================

// MAKERphone retro palette -- inlined per the established pattern
// (see PhoneSpeedDialScreen.cpp, PhoneSettingsScreen.cpp,
// PhoneContactRingtonePicker.cpp). The screen uses cyan for the
// list-mode caption (informational), sunset orange for the pick-mode
// caption (live edit), warm cream for row labels, dim purple for
// inactive secondary text, and a translucent sunset-orange rect for
// the focused-row highlight.
#define MP_BG_DARK      lv_color_make( 20,  12,  36)
#define MP_ACCENT       lv_color_make(255, 140,  30)
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)
#define MP_DIM          lv_color_make( 70,  56, 100)
#define MP_TEXT         lv_color_make(255, 220, 180)
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)

namespace {

// Profile names, parallel to Settings.phoneProfile / PhoneProfileScreen
// ::Profile order. Kept inline literal so the screen has no SPIFFS
// dependency and no allocation cost at construction time.
const char* const kProfileNames[PhoneProfileRingtoneScreen::ProfileCount] = {
	"GENERAL",
	"SILENT",
	"MEETING",
	"OUTDOOR",
	"HEADSET",
};

// Caption strings. Pick-mode caption is rebuilt on every mode entry
// to include the active profile name ("PROFILE - MEETING").
const char* const kListCaption    = "PROFILE RING";
const char* const kListLeftLabel  = "EDIT";
const char* const kPickLeftLabel  = "PICK";
const char* const kBackLabel      = "BACK";

// Long-press cadence shared with the rest of the MAKERphone shell.
constexpr uint32_t   kBackHoldMs   = 600;

// Inset where the row name + tune labels paint inside the body band.
constexpr lv_coord_t kRowNameX     = 6;
constexpr lv_coord_t kRowTuneX     = 70;
constexpr lv_coord_t kRowDotX      = 2;
constexpr lv_coord_t kPickRowX     = 12;

// Caption strip y -- matches PhoneContactRingtonePicker so the screen
// family stays visually aligned.
constexpr lv_coord_t kCaptionY     = 12;

} // namespace

// ---------- ctor / dtor ----------

PhoneProfileRingtoneScreen::PhoneProfileRingtoneScreen()
		: LVScreen() {

	// Full-screen container, no scrollbars, no padding -- same blank
	// canvas every other Phone* screen uses.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper + status bar (top of z-order).
	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildLayout();
	buildCaption();
	buildListView();
	buildPickView();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft(kListLeftLabel);
	softKeys->setRight(kBackLabel);

	// Open in list mode with cursor on profile 0 (General). Mirrors
	// PhoneSpeedDialScreen / PhoneSettingsScreen which always boot to
	// a defined cursor position so a screenshot test reads the same
	// pixels every run.
	enterListMode();

	setButtonHoldTime(BTN_BACK, kBackHoldMs);
}

PhoneProfileRingtoneScreen::~PhoneProfileRingtoneScreen() {
	// Ringtone.stop() is a no-op when nothing is playing, but we
	// still guard so a destructor path that bypasses onStop()
	// (very rare) does not leave the engine driving the piezo
	// after the screen has gone.
	if(previewing) {
		Ringtone.stop();
	}
}

// ---------- lifecycle ----------

void PhoneProfileRingtoneScreen::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneProfileRingtoneScreen::onStop() {
	Input::getInstance()->removeListener(this);
	stopPreview();
}

// ---------- builders ----------

void PhoneProfileRingtoneScreen::buildLayout() {
	// Nothing to do at the LVScreen root level beyond the size /
	// scrollbar setup the ctor already did. Kept as its own
	// function so future additions (a header divider line, a
	// hint label) have an obvious home that doesn't pollute the
	// ctor.
}

void PhoneProfileRingtoneScreen::buildCaption() {
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, kListCaption);
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, kCaptionY);
}

void PhoneProfileRingtoneScreen::buildListView() {
	listContainer = lv_obj_create(obj);
	lv_obj_remove_style_all(listContainer);
	lv_obj_set_size(listContainer, BodyW, VisibleRows * RowHeight);
	lv_obj_set_pos(listContainer, BodyX, BodyY);
	lv_obj_set_scrollbar_mode(listContainer, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(listContainer, 0, 0);
	lv_obj_set_style_radius(listContainer, 2, 0);
	lv_obj_set_style_bg_color(listContainer, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(listContainer, LV_OPA_40, 0);
	lv_obj_set_style_border_width(listContainer, 1, 0);
	lv_obj_set_style_border_color(listContainer, MP_DIM, 0);
	lv_obj_set_style_border_opa(listContainer, LV_OPA_60, 0);

	listHighlight = lv_obj_create(listContainer);
	lv_obj_remove_style_all(listHighlight);
	lv_obj_set_size(listHighlight, BodyW - 4, RowHeight);
	lv_obj_set_pos(listHighlight, 2, 0);
	lv_obj_set_style_bg_color(listHighlight, MP_ACCENT, 0);
	lv_obj_set_style_bg_opa(listHighlight, LV_OPA_30, 0);
	lv_obj_set_style_radius(listHighlight, 2, 0);
	lv_obj_set_style_border_width(listHighlight, 1, 0);
	lv_obj_set_style_border_color(listHighlight, MP_ACCENT, 0);
	lv_obj_set_style_border_opa(listHighlight, LV_OPA_70, 0);

	for(uint8_t i = 0; i < ProfileCount; ++i) {
		const lv_coord_t y = (lv_coord_t)(i * RowHeight + 2);

		nameLabels[i] = lv_label_create(listContainer);
		lv_obj_set_style_text_font(nameLabels[i], &pixelbasic7, 0);
		lv_obj_set_style_text_color(nameLabels[i], MP_TEXT, 0);
		lv_label_set_long_mode(nameLabels[i], LV_LABEL_LONG_DOT);
		lv_obj_set_width(nameLabels[i], kRowTuneX - kRowNameX - 2);
		lv_obj_set_pos(nameLabels[i], kRowNameX, y);
		lv_label_set_text(nameLabels[i], kProfileNames[i]);

		tuneLabels[i] = lv_label_create(listContainer);
		lv_obj_set_style_text_font(tuneLabels[i], &pixelbasic7, 0);
		lv_obj_set_style_text_color(tuneLabels[i], MP_LABEL_DIM, 0);
		lv_label_set_long_mode(tuneLabels[i], LV_LABEL_LONG_DOT);
		lv_obj_set_width(tuneLabels[i], BodyW - kRowTuneX - 4);
		lv_obj_set_pos(tuneLabels[i], kRowTuneX, y);
		lv_label_set_text(tuneLabels[i], "");
	}
}

void PhoneProfileRingtoneScreen::buildPickView() {
	pickContainer = lv_obj_create(obj);
	lv_obj_remove_style_all(pickContainer);
	lv_obj_set_size(pickContainer, BodyW, VisibleRows * RowHeight);
	lv_obj_set_pos(pickContainer, BodyX, BodyY);
	lv_obj_set_scrollbar_mode(pickContainer, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(pickContainer, 0, 0);
	lv_obj_set_style_radius(pickContainer, 2, 0);
	lv_obj_set_style_bg_color(pickContainer, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(pickContainer, LV_OPA_40, 0);
	lv_obj_set_style_border_width(pickContainer, 1, 0);
	lv_obj_set_style_border_color(pickContainer, MP_ACCENT, 0);
	lv_obj_set_style_border_opa(pickContainer, LV_OPA_60, 0);

	pickHighlight = lv_obj_create(pickContainer);
	lv_obj_remove_style_all(pickHighlight);
	lv_obj_set_size(pickHighlight, BodyW - 4, RowHeight);
	lv_obj_set_pos(pickHighlight, 2, 0);
	lv_obj_set_style_bg_color(pickHighlight, MP_ACCENT, 0);
	lv_obj_set_style_bg_opa(pickHighlight, LV_OPA_30, 0);
	lv_obj_set_style_radius(pickHighlight, 2, 0);
	lv_obj_set_style_border_width(pickHighlight, 1, 0);
	lv_obj_set_style_border_color(pickHighlight, MP_ACCENT, 0);
	lv_obj_set_style_border_opa(pickHighlight, LV_OPA_70, 0);

	// Pick rows + saved-dot labels are created lazily by
	// rebuildPickEntries() because the count varies with how many
	// composer slots are populated.
	for(uint8_t i = 0; i < MaxPickEntries; ++i) {
		pickRows[i]      = nullptr;
		pickSavedDots[i] = nullptr;
		pickIds[i]       = 0;
	}

	lv_obj_add_flag(pickContainer, LV_OBJ_FLAG_HIDDEN);
}

// ---------- mode transitions ----------

void PhoneProfileRingtoneScreen::enterListMode() {
	stopPreview();

	mode = Mode::List;

	if(pickContainer) lv_obj_add_flag(pickContainer, LV_OBJ_FLAG_HIDDEN);
	if(listContainer) lv_obj_clear_flag(listContainer, LV_OBJ_FLAG_HIDDEN);

	if(softKeys) {
		softKeys->setLeft(kListLeftLabel);
		softKeys->setRight(kBackLabel);
	}

	refreshCaption();
	refreshListLabels();
	refreshListHighlight();
}

void PhoneProfileRingtoneScreen::enterPickMode() {
	stopPreview();

	mode = Mode::Pick;

	if(listContainer) lv_obj_add_flag(listContainer, LV_OBJ_FLAG_HIDDEN);
	if(pickContainer) lv_obj_clear_flag(pickContainer, LV_OBJ_FLAG_HIDDEN);

	if(softKeys) {
		softKeys->setLeft(kPickLeftLabel);
		softKeys->setRight(kBackLabel);
	}

	rebuildPickEntries();
	refreshCaption();
	refreshPickRows();
	refreshPickHighlight();
	refreshPickSavedMarks();
}

// ---------- caption ----------

void PhoneProfileRingtoneScreen::refreshCaption() {
	if(captionLabel == nullptr) return;
	if(mode == Mode::List) {
		lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
		lv_label_set_text(captionLabel, kListCaption);
	} else {
		lv_obj_set_style_text_color(captionLabel, MP_ACCENT, 0);
		const char* name = (profileCursor < ProfileCount)
				? kProfileNames[profileCursor]
				: "PROFILE";
		char buf[32];
		snprintf(buf, sizeof(buf), "PROFILE - %s", name);
		lv_label_set_text(captionLabel, buf);
	}
}

// ---------- list view ----------

void PhoneProfileRingtoneScreen::refreshListLabels() {
	for(uint8_t i = 0; i < ProfileCount; ++i) {
		if(tuneLabels[i] == nullptr) continue;
		const uint8_t id = PhoneContactRingtone::profileRingtoneId(i);
		char buf[PhoneContactRingtone::NameBufferSize];
		PhoneContactRingtone::nameOf(id, buf, sizeof(buf));
		lv_label_set_text(tuneLabels[i], buf);
	}
}

void PhoneProfileRingtoneScreen::refreshListHighlight() {
	if(listHighlight == nullptr) return;
	const lv_coord_t y = (lv_coord_t)(profileCursor * RowHeight);
	lv_obj_set_y(listHighlight, y);
}

void PhoneProfileRingtoneScreen::moveProfileCursor(int8_t dir) {
	if(dir < 0) {
		if(profileCursor == 0) return;
		--profileCursor;
	} else if(dir > 0) {
		if(profileCursor + 1 >= ProfileCount) return;
		++profileCursor;
	}
	refreshListHighlight();
}

// ---------- pick view ----------

void PhoneProfileRingtoneScreen::rebuildPickEntries() {
	// Tear down any prior row labels -- rebuildPickEntries is called
	// on every entry into pick mode because the composer-slot
	// population can change between visits (the user may have saved
	// a slot from PhoneComposer mid-session).
	for(uint8_t i = 0; i < MaxPickEntries; ++i) {
		if(pickRows[i]) {
			lv_obj_del(pickRows[i]);
			pickRows[i] = nullptr;
		}
		if(pickSavedDots[i]) {
			lv_obj_del(pickSavedDots[i]);
			pickSavedDots[i] = nullptr;
		}
		pickIds[i] = 0;
	}

	const uint8_t available = PhoneContactRingtone::pickerCount();
	pickEntryCount = (available <= MaxPickEntries) ? available : MaxPickEntries;

	pickSavedId = PhoneContactRingtone::profileRingtoneId(profileCursor);
	uint8_t cursorIdx = PhoneContactRingtone::pickerIndexOf(pickSavedId);
	if(cursorIdx >= pickEntryCount) cursorIdx = 0;
	pickCursor    = cursorIdx;
	pickTopVisible = 0;

	for(uint8_t i = 0; i < pickEntryCount; ++i) {
		pickIds[i] = PhoneContactRingtone::pickerIdAt(i);

		pickRows[i] = lv_label_create(pickContainer);
		lv_obj_set_style_text_font(pickRows[i], &pixelbasic7, 0);
		lv_obj_set_style_text_color(pickRows[i], MP_TEXT, 0);
		lv_label_set_long_mode(pickRows[i], LV_LABEL_LONG_DOT);
		lv_obj_set_width(pickRows[i], BodyW - kPickRowX - 4);
		lv_obj_set_pos(pickRows[i], kPickRowX, 2);

		char buf[PhoneContactRingtone::NameBufferSize];
		PhoneContactRingtone::nameOf(pickIds[i], buf, sizeof(buf));
		lv_label_set_text(pickRows[i], buf);

		// "saved" dot pinned to the row's left margin.
		pickSavedDots[i] = lv_label_create(pickContainer);
		lv_obj_set_style_text_font(pickSavedDots[i], &pixelbasic7, 0);
		lv_obj_set_style_text_color(pickSavedDots[i], MP_DIM, 0);
		lv_label_set_text(pickSavedDots[i], "*");
		lv_obj_set_pos(pickSavedDots[i], 2, 2);
	}
}

void PhoneProfileRingtoneScreen::refreshPickRows() {
	if(pickEntryCount == 0) return;

	// Slide topVisible so cursor sits inside the visible band.
	if(pickCursor < pickTopVisible) {
		pickTopVisible = pickCursor;
	} else if(pickCursor >= (uint8_t)(pickTopVisible + VisibleRows)) {
		pickTopVisible = (uint8_t)(pickCursor + 1 - VisibleRows);
	}

	for(uint8_t i = 0; i < pickEntryCount; ++i) {
		if(pickRows[i] == nullptr) continue;
		const int8_t relRow = (int8_t)i - (int8_t)pickTopVisible;
		if(relRow < 0 || relRow >= (int8_t)VisibleRows) {
			lv_obj_add_flag(pickRows[i], LV_OBJ_FLAG_HIDDEN);
			if(pickSavedDots[i]) lv_obj_add_flag(pickSavedDots[i], LV_OBJ_FLAG_HIDDEN);
			continue;
		}
		lv_obj_clear_flag(pickRows[i], LV_OBJ_FLAG_HIDDEN);
		if(pickSavedDots[i]) lv_obj_clear_flag(pickSavedDots[i], LV_OBJ_FLAG_HIDDEN);
		const lv_coord_t y = (lv_coord_t)(relRow * RowHeight + 2);
		lv_obj_set_y(pickRows[i], y);
		if(pickSavedDots[i]) lv_obj_set_y(pickSavedDots[i], y);
	}
}

void PhoneProfileRingtoneScreen::refreshPickHighlight() {
	if(pickHighlight == nullptr) return;
	if(pickEntryCount == 0) {
		lv_obj_add_flag(pickHighlight, LV_OBJ_FLAG_HIDDEN);
		return;
	}
	lv_obj_clear_flag(pickHighlight, LV_OBJ_FLAG_HIDDEN);
	const int8_t relCursor = (int8_t)pickCursor - (int8_t)pickTopVisible;
	const lv_coord_t y = (lv_coord_t)(relCursor * RowHeight);
	lv_obj_set_y(pickHighlight, y);
}

void PhoneProfileRingtoneScreen::refreshPickSavedMarks() {
	for(uint8_t i = 0; i < pickEntryCount; ++i) {
		if(pickSavedDots[i] == nullptr) continue;
		const bool isSaved = (pickIds[i] == pickSavedId);
		lv_obj_set_style_text_color(pickSavedDots[i],
				isSaved ? MP_HIGHLIGHT : MP_DIM, 0);
	}
}

void PhoneProfileRingtoneScreen::movePickCursor(int8_t dir) {
	if(pickEntryCount == 0) return;
	if(dir < 0) {
		if(pickCursor == 0) return;
		--pickCursor;
	} else if(dir > 0) {
		if(pickCursor + 1 >= pickEntryCount) return;
		++pickCursor;
	}
	stopPreview();
	refreshPickRows();
	refreshPickHighlight();
}

// ---------- preview ----------

// S223 -- SILENT-profile preview gate. The engine self-mutes per-loop
// via Settings.get().sound, but the loop listener still attaches to
// LoopManager for the millisecond between play() and the engine's
// first mute tick (audible click on some Chatter units). Reading the
// legacy bool covers every "should the picker drive the piezo right
// now" case without dragging the five-state PhoneProfileScreen enum
// into this screen. Static so the engine-skip check in startPreview()
// can call it without indirecting through a live picker instance.
// PhoneProfileScreen (S159) writes Settings.get().sound = false for
// SILENT and MEETING and true for GENERAL / OUTDOOR / HEADSET, so the
// helper covers every silenced profile in one read.
bool PhoneProfileRingtoneScreen::isSilenced() {
	return !Settings.get().sound;
}

void PhoneProfileRingtoneScreen::setMutedCaption(bool muted) {
	if(captionLabel == nullptr) return;
	// S223 -- repurpose the per-mode caption strip ("PROFILE RING" in
	// list mode, "PROFILE - <NAME>" in pick mode) as a "MUTED --
	// SOUND OFF" badge while a silenced preview is "live", painted in
	// the same MP_HIGHLIGHT cyan the list-mode caption already uses
	// so the badge reads as a deliberate state change rather than a
	// stuck label. Delegates to refreshCaption() on the un-mute path
	// so the regular per-mode caption (with its sunset-orange pick-
	// mode color and live profile name) is restored verbatim, and a
	// profile flip mid-preview cannot leave the badge dragging onto
	// the next preview attempt.
	if(muted) {
		lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
		lv_label_set_text(captionLabel, "MUTED -- SOUND OFF");
	} else {
		refreshCaption();
	}
}

void PhoneProfileRingtoneScreen::startPreview() {
	if(pickCursor >= pickEntryCount) return;
	const uint8_t id = pickIds[pickCursor];
	const PhoneRingtoneEngine::Melody* m = PhoneContactRingtone::resolve(id);
	if(m == nullptr) return;
	if(isSilenced()) {
		// S223 -- SILENT / MEETING profile is active: do NOT call
		// Ringtone.play(). Skipping the call entirely keeps the loop
		// listener off the LoopManager queue and prevents even the
		// micro-interval of audible piezo that can leak in between
		// play() and the engine's first mute tick. Defensive
		// Ringtone.stop() in case a stale engine playhead is still
		// ticking from a profile flip mid-preview. The cursor still
		// flips to "previewing" so a second BTN_ENTER tap stops
		// cleanly through the regular stopPreview() path.
		Ringtone.stop();
		previewing = true;
		setMutedCaption(true);
		if(softKeys) softKeys->flashLeft();
		return;
	}
	Ringtone.play(*m);
	previewing = true;
	setMutedCaption(false);
	if(softKeys) softKeys->flashLeft();
}

void PhoneProfileRingtoneScreen::stopPreview() {
	if(!previewing) return;
	previewing = false;
	Ringtone.stop();
	setMutedCaption(false);
}

// ---------- pick / back ----------

void PhoneProfileRingtoneScreen::confirmPick() {
	if(softKeys) softKeys->flashLeft();
	stopPreview();

	if(mode != Mode::Pick) return;
	if(pickCursor >= pickEntryCount) return;

	const uint8_t pickedId = pickIds[pickCursor];
	PhoneContactRingtone::setProfileRingtoneId(profileCursor, pickedId);

	// Refresh saved indicators so the user briefly sees the new
	// "*" land on the row before the screen returns to list mode.
	pickSavedId = PhoneContactRingtone::profileRingtoneId(profileCursor);
	refreshPickSavedMarks();

	enterListMode();
}

void PhoneProfileRingtoneScreen::invokeBack() {
	if(softKeys) softKeys->flashRight();
	stopPreview();

	if(mode == Mode::Pick) {
		// Cancel the pick and return to list mode without persisting.
		enterListMode();
		return;
	}

	// List mode: pop back to PhoneSettingsScreen.
	pop();
}

// ---------- input ----------

void PhoneProfileRingtoneScreen::buttonPressed(uint i) {
	switch(i) {
		case BTN_2:
		case BTN_LEFT:
			if(mode == Mode::List) moveProfileCursor(-1);
			else                    movePickCursor(-1);
			break;

		case BTN_8:
		case BTN_RIGHT:
			if(mode == Mode::List) moveProfileCursor(+1);
			else                    movePickCursor(+1);
			break;

		case BTN_ENTER:
			if(mode == Mode::List) {
				enterPickMode();
			} else {
				// Toggle preview.
				if(previewing) stopPreview();
				else            startPreview();
			}
			break;

		case BTN_L:
			// EDIT in list mode, PICK in pick mode -- both routed
			// through the right helper for the active mode.
			if(mode == Mode::List) enterPickMode();
			else                    confirmPick();
			break;

		case BTN_R:
			// Right bumper = quick BACK. Standard MAKERphone affordance.
			invokeBack();
			break;

		case BTN_BACK:
			// Defer short-press to buttonReleased so a long-press
			// can pre-empt it (matches the rest of the screen family).
			backHoldFired = false;
			break;

		default:
			break;
	}
}

void PhoneProfileRingtoneScreen::buttonHeld(uint i) {
	if(i == BTN_BACK) {
		// Hold-BACK = bail to the parent (PhoneSettingsScreen ->
		// homescreen continues the unwind chain).
		backHoldFired = true;
		stopPreview();
		pop();
	}
}

void PhoneProfileRingtoneScreen::buttonReleased(uint i) {
	if(i == BTN_BACK) {
		if(backHoldFired) return;
		invokeBack();
	}
}
