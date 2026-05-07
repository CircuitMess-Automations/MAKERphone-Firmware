#include "PhoneAlarmTonePicker.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../Services/PhoneAlarmTone.h"
#include "../Services/PhoneRingtoneEngine.h"
#include <Settings.h>

// MAKERphone retro palette - kept identical to every other Phone* widget
// so the picker reads visually as part of the same family. Inlined here
// per the established pattern.
#define MP_BG_DARK      lv_color_make( 20,  12,  36)
#define MP_ACCENT       lv_color_make(255, 140,  30)
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)
#define MP_DIM          lv_color_make( 70,  56, 100)
#define MP_TEXT         lv_color_make(255, 220, 180)
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)

// 160 x 128 layout. Vertical budget mirrors PhoneContactRingtonePicker so
// the two pickers feel mechanically identical when the user crosses
// between SOUND-cluster sub-screens:
//
//   y =   0 ..  9   PhoneStatusBar (10 px)
//   y =  12 .. 19   "ALARM TONE" caption strip (8 px tall, pixelbasic7)
//   y =  22 .. 93   list body band (72 px = 6 visible rows of 12 px)
//   y =  96 ..115   slack
//   y = 118 ..127   PhoneSoftKeyBar (10 px)
//
// The cursor is a translucent sunset-orange rectangle that slides
// between rows. Rows are simple labels - each row's text is the
// alarm-tone name, with a small saved dot pinned to the row's left
// margin coloured in cyan when this row is the persisted choice.
namespace {
constexpr lv_coord_t kCaptionY  = 12;
constexpr lv_coord_t kRowInsetX = 8;
} // namespace

PhoneAlarmTonePicker::PhoneAlarmTonePicker()
		: LVScreen() {
	buildLayout();
	buildList();
	rebuildEntries();
}

PhoneAlarmTonePicker::~PhoneAlarmTonePicker() {
	// Belt-and-braces: ensure no preview is left driving the engine
	// after destruction. onStop normally handles this, but a
	// destructor path that bypasses onStop (rare) still needs the
	// safety. Ringtone.stop() is a no-op when nothing is playing.
	if(previewing) {
		Ringtone.stop();
	}
}

void PhoneAlarmTonePicker::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneAlarmTonePicker::onStop() {
	Input::getInstance()->removeListener(this);
	stopPreview();
}

// ----- builders --------------------------------------------------------

void PhoneAlarmTonePicker::buildLayout() {
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "ALARM TONE");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, kCaptionY);

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("PICK");
	softKeys->setRight("BACK");

	setButtonHoldTime(BTN_BACK, BackHoldMs);
}

void PhoneAlarmTonePicker::buildList() {
	listContainer = lv_obj_create(obj);
	lv_obj_remove_style_all(listContainer);
	lv_obj_set_size(listContainer, BodyW, VisibleRows * RowHeight);
	lv_obj_set_pos(listContainer, BodyX, BodyY);
	lv_obj_set_scrollbar_mode(listContainer, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(listContainer, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_pad_all(listContainer, 0, 0);
	lv_obj_set_style_radius(listContainer, 2, 0);
	lv_obj_set_style_bg_color(listContainer, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(listContainer, LV_OPA_40, 0);
	lv_obj_set_style_border_width(listContainer, 1, 0);
	lv_obj_set_style_border_color(listContainer, MP_DIM, 0);
	lv_obj_set_style_border_opa(listContainer, LV_OPA_60, 0);

	cursorRect = lv_obj_create(listContainer);
	lv_obj_remove_style_all(cursorRect);
	lv_obj_set_size(cursorRect, BodyW - 4, RowHeight);
	lv_obj_set_pos(cursorRect, 2, 0);
	lv_obj_set_style_bg_color(cursorRect, MP_ACCENT, 0);
	lv_obj_set_style_bg_opa(cursorRect, LV_OPA_30, 0);
	lv_obj_set_style_radius(cursorRect, 2, 0);
	lv_obj_set_style_border_width(cursorRect, 1, 0);
	lv_obj_set_style_border_color(cursorRect, MP_ACCENT, 0);
	lv_obj_set_style_border_opa(cursorRect, LV_OPA_70, 0);
}

// ----- entry table -----------------------------------------------------

void PhoneAlarmTonePicker::rebuildEntries() {
	// Tear down any prior row labels (rebuildEntries is only called
	// in the ctor today, but the helper stays idempotent so a future
	// "refresh after composer save" path can call it again).
	for(uint8_t i = 0; i < MaxEntries; ++i) {
		if(rows[i] != nullptr) {
			lv_obj_del(rows[i]);
			rows[i]      = nullptr;
			savedDots[i] = nullptr;
		}
		ids[i] = 0;
	}

	const uint8_t available = PhoneAlarmTone::pickerCount();
	entryCount = (available <= MaxEntries) ? available : MaxEntries;

	// Resolve the persisted alarm-tone id so the cursor lands on it
	// on first paint and the saved dot lights up next to the right row.
	savedId = PhoneAlarmTone::getActiveId();

	uint8_t cursorIdx = PhoneAlarmTone::pickerIndexOf(savedId);
	if(cursorIdx >= entryCount) cursorIdx = 0;
	cursor = cursorIdx;

	for(uint8_t i = 0; i < entryCount; ++i) {
		ids[i] = PhoneAlarmTone::pickerIdAt(i);

		rows[i] = lv_label_create(listContainer);
		lv_obj_set_style_text_font(rows[i], &pixelbasic7, 0);
		lv_obj_set_style_text_color(rows[i], MP_TEXT, 0);
		lv_label_set_long_mode(rows[i], LV_LABEL_LONG_DOT);
		lv_obj_set_width(rows[i], BodyW - kRowInsetX - 6);
		lv_obj_set_pos(rows[i], kRowInsetX, 2);  // y set in scrollIntoView()

		char nameBuf[PhoneAlarmTone::NameBufferSize];
		PhoneAlarmTone::nameOf(ids[i], nameBuf, sizeof(nameBuf));
		lv_label_set_text(rows[i], nameBuf);

		// "saved" dot - a single pixelbasic7 asterisk pinned to the
		// row's left margin. Created visible so refreshSavedMarks()
		// only needs to flip its colour.
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

// ----- cursor ----------------------------------------------------------

void PhoneAlarmTonePicker::moveCursor(int8_t dir) {
	if(entryCount == 0) return;

	if(dir < 0) {
		if(cursor == 0) return;
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

void PhoneAlarmTonePicker::scrollIntoView() {
	if(entryCount == 0) return;

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

void PhoneAlarmTonePicker::refreshCursor() {
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

void PhoneAlarmTonePicker::refreshSavedMarks() {
	for(uint8_t i = 0; i < entryCount; ++i) {
		if(savedDots[i] == nullptr) continue;
		const bool isSaved = (ids[i] == savedId);
		lv_obj_set_style_text_color(savedDots[i],
				isSaved ? MP_HIGHLIGHT : MP_DIM, 0);
	}
}

uint8_t PhoneAlarmTonePicker::getFocusedId() const {
	if(cursor >= entryCount) return PhoneAlarmTone::DefaultId;
	return ids[cursor];
}

// ----- preview ---------------------------------------------------------

// S221 -- gate the alarm-tone preview against the SILENT / MEETING
// phone profiles. The underlying truth is `Settings.get().sound`:
// `PhoneProfileScreen` (S159) writes `sound = false` for both Silent
// and Meeting and `sound = true` for General / Outdoor / Headset, so
// reading the legacy bool covers every "should the picker drive the
// piezo right now" case without dragging the five-state enum into
// this screen. Static so the engine-skip check in startPreview()
// can call it without indirecting through a live picker instance.
bool PhoneAlarmTonePicker::isSilenced() {
	return !Settings.get().sound;
}

void PhoneAlarmTonePicker::setMutedCaption(bool muted) {
	if(captionLabel == nullptr) return;
	// S221 -- repurpose the "ALARM TONE" caption strip as a
	// "MUTED -- SOUND OFF" badge while a silenced preview is "live"
	// so the user reads the silence as deliberate rather than
	// wondering whether the picked tone happens to be near-silent.
	// Restored to the regular caption on stopPreview() / on every
	// non-silenced path so a profile flip mid-preview does not leave
	// the badge stuck.
	lv_label_set_text(captionLabel, muted ? "MUTED -- SOUND OFF" : "ALARM TONE");
}

void PhoneAlarmTonePicker::startPreview() {
	if(cursor >= entryCount) return;
	const uint8_t id = ids[cursor];
	const PhoneRingtoneEngine::Melody* m = PhoneAlarmTone::resolve(id);
	if(m == nullptr) return;
	if(isSilenced()){
		// S221 -- SILENT / MEETING profile is active: do NOT call
		// Ringtone.play(). The engine self-mutes per-loop via
		// `Settings.get().sound`, but skipping the call entirely keeps
		// the loop listener off the LoopManager queue and prevents
		// even the micro-interval of audible piezo that can leak in
		// between play() and the engine's first mute tick. Defensive
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

void PhoneAlarmTonePicker::stopPreview() {
	if(!previewing) return;
	previewing = false;
	Ringtone.stop();
	setMutedCaption(false);
}

// ----- pick / back -----------------------------------------------------

void PhoneAlarmTonePicker::confirmPick() {
	if(softKeys) softKeys->flashLeft();
	stopPreview();

	const uint8_t pickedId = (cursor < entryCount)
			? ids[cursor]
			: PhoneAlarmTone::DefaultId;

	PhoneAlarmTone::setActiveId(pickedId);
	savedId = PhoneAlarmTone::getActiveId();   // re-read in case validatedOrDefault clamped
	refreshSavedMarks();
	pop();
}

void PhoneAlarmTonePicker::invokeBack() {
	if(softKeys) softKeys->flashRight();
	stopPreview();
	pop();
}

// ----- input -----------------------------------------------------------

void PhoneAlarmTonePicker::buttonPressed(uint i) {
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
			// Toggle preview - second tap stops the playback so the
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

void PhoneAlarmTonePicker::buttonHeld(uint i) {
	if(i == BTN_BACK) {
		// Hold-BACK = bail. We pop our own screen here; the parent
		// screen continues the unwind chain when the user holds again.
		backHoldFired = true;
		invokeBack();
	}
}

void PhoneAlarmTonePicker::buttonReleased(uint i) {
	if(i == BTN_BACK) {
		if(backHoldFired) return;
		invokeBack();
	}
}
