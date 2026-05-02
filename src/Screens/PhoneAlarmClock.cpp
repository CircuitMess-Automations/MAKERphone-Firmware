#include "PhoneAlarmClock.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../Services/PhoneAlarmService.h"

// MAKERphone retro palette - kept identical to every other Phone* widget so
// the alarm clock slots in beside PhoneStopwatch (S61) / PhoneTimer (S62)
// without a visual seam. Same inline-#define convention every other Phone*
// screen .cpp uses (see PhoneTimer.cpp / PhoneStopwatch.cpp).
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan caption
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream readout
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim purple hint line
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange (alarm)
#define MP_ACCENT_BRIGHT   lv_color_make(255, 200,  80)  // bright sunset (alarm pulse hi)
#define MP_DIM             lv_color_make( 70,  56, 100)  // muted purple (disabled rows)

// ---------- geometry ------------------------------------------------------
//
// Layout matches the PhoneTimer silhouette so the user feels the same
// "shape" of screen behind both apps. Status bar at y=0 (10 px), caption
// at y=12, list rows at y=26..(26+4*16), softkey bar at y=118.

static constexpr lv_coord_t kCaptionY     = 12;
static constexpr lv_coord_t kListTopY     = 26;
static constexpr lv_coord_t kRowH         = 18;
static constexpr lv_coord_t kListLeft     = 6;
static constexpr lv_coord_t kListWidth    = 148;

static constexpr lv_coord_t kBigCaptionY  = 14;
static constexpr lv_coord_t kBigReadoutY  = 36;
static constexpr lv_coord_t kHintY        = 78;
static constexpr lv_coord_t kHintLeft     = 6;
static constexpr lv_coord_t kHintW        = 148;

// ---------- ctor / dtor ---------------------------------------------------

PhoneAlarmClock::PhoneAlarmClock()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  bigLabel(nullptr),
		  hintLabel(nullptr) {

	// Zero the entry buffer + row pointers up front so refresh* paths
	// can read them before any user interaction.
	for(uint8_t i = 0; i <= EntryDigits; ++i) entry[i] = 0;
	for(uint8_t i = 0; i < PhoneAlarmService::MaxAlarms; ++i) rows[i] = nullptr;

	// Full-screen container, no scrollbars, no padding -- same blank
	// canvas pattern as PhoneTimer / PhoneStopwatch / PhoneAboutScreen.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px).
	statusBar = new PhoneStatusBar(obj);

	buildList();
	buildBigReadout();

	// Bottom soft-key bar; populated by refreshSoftKeys() per mode.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("TOGGLE");
	softKeys->setRight("EDIT");
}

PhoneAlarmClock::~PhoneAlarmClock() {
	stopPollTimer();
	stopPulseTimer();
}

// ---------- build helpers -------------------------------------------------

void PhoneAlarmClock::buildList() {
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_width(captionLabel, kListWidth);
	lv_obj_set_pos(captionLabel, kListLeft, kCaptionY);
	lv_obj_set_style_text_align(captionLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(captionLabel, "ALARMS");

	// Four list rows; each one is a single label whose text we
	// rebuild on every refresh. pixelbasic7 keeps the row legible at
	// 18 px height inside the 88 px vertical band between caption and
	// soft-keys.
	for(uint8_t i = 0; i < PhoneAlarmService::MaxAlarms; ++i) {
		lv_obj_t* row = lv_label_create(obj);
		lv_obj_set_style_text_font(row, &pixelbasic7, 0);
		lv_obj_set_style_text_color(row, MP_TEXT, 0);
		lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
		lv_obj_set_width(row, kListWidth);
		lv_label_set_text(row, "");
		lv_obj_set_pos(row, kListLeft, kListTopY + i * kRowH);
		rows[i] = row;
	}
}

void PhoneAlarmClock::buildBigReadout() {
	// Both Edit and Firing modes reuse this single big-readout label
	// so a transition between them is a text + colour swap rather
	// than an LVGL teardown. List mode hides it.
	bigLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(bigLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(bigLabel, MP_TEXT, 0);
	lv_obj_set_width(bigLabel, 160);
	lv_obj_set_pos(bigLabel, 0, kBigReadoutY);
	lv_obj_set_style_text_align(bigLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(bigLabel, "00:00");
	lv_obj_add_flag(bigLabel, LV_OBJ_FLAG_HIDDEN);

	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_obj_set_width(hintLabel, kHintW);
	lv_obj_set_pos(hintLabel, kHintLeft, kHintY);
	lv_obj_set_style_text_align(hintLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(hintLabel, "");
	lv_obj_add_flag(hintLabel, LV_OBJ_FLAG_HIDDEN);
}

// ---------- lifecycle -----------------------------------------------------

void PhoneAlarmClock::onStart() {
	Input::getInstance()->addListener(this);
	// Start in whichever mode the world is in: if the alarm service is
	// already firing (e.g. the user opened this screen from a missed-
	// notification jump), drop straight into the firing modal.
	if(Alarms.isFiring()) {
		enterFiring();
	} else {
		enterList();
	}
	startPollTimer();
}

void PhoneAlarmClock::onStop() {
	Input::getInstance()->removeListener(this);
	stopPollTimer();
	stopPulseTimer();
}

// ---------- repaint -------------------------------------------------------

void PhoneAlarmClock::refreshAll() {
	switch(mode) {
		case Mode::List:   refreshList();   break;
		case Mode::Edit:   refreshEdit();   break;
		case Mode::Firing: refreshFiring(); break;
	}
	refreshSoftKeys();
	refreshHint();
}

void PhoneAlarmClock::refreshList() {
	if(captionLabel) lv_obj_clear_flag(captionLabel, LV_OBJ_FLAG_HIDDEN);
	if(bigLabel)     lv_obj_add_flag(bigLabel,    LV_OBJ_FLAG_HIDDEN);

	for(uint8_t i = 0; i < PhoneAlarmService::MaxAlarms; ++i) {
		if(rows[i] == nullptr) continue;
		lv_obj_clear_flag(rows[i], LV_OBJ_FLAG_HIDDEN);

		const PhoneAlarmService::Alarm a = Alarms.getAlarm(i);
		const char* mark    = (i == cursor)  ? ">" : " ";
		const char* status  = a.enabled      ? "ON" : "--";

		// Single label per row: "> 1 ON  07:00". Wide spacing keeps
		// the columns visually distinct even at pixelbasic7.
		char buf[24];
		snprintf(buf, sizeof(buf), "%s %u  %-3s  %02u:%02u",
		         mark, (unsigned)(i + 1), status,
		         (unsigned)a.hour, (unsigned)a.minute);
		lv_label_set_text(rows[i], buf);

		// Highlighted row gets the cream text colour, others get the
		// dim accent so the cursor is visually obvious without an
		// extra widget.
		lv_color_t col = (i == cursor)
				? (a.enabled ? MP_TEXT : MP_LABEL_DIM)
				: (a.enabled ? MP_LABEL_DIM : MP_DIM);
		lv_obj_set_style_text_color(rows[i], col, 0);
	}

	// Hide the firing/edit mode big readout while the list is showing.
	if(hintLabel) lv_obj_add_flag(hintLabel, LV_OBJ_FLAG_HIDDEN);
}

void PhoneAlarmClock::refreshEdit() {
	for(uint8_t i = 0; i < PhoneAlarmService::MaxAlarms; ++i) {
		if(rows[i]) lv_obj_add_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
	}

	// Caption: "EDIT N" so the user knows which slot they're modifying.
	if(captionLabel) {
		char buf[16];
		snprintf(buf, sizeof(buf), "EDIT %u", (unsigned)(editingSlot + 1));
		lv_label_set_text(captionLabel, buf);
		lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
		lv_obj_clear_flag(captionLabel, LV_OBJ_FLAG_HIDDEN);
	}

	// Big readout shows the live HHMM buffer (or the slot's current
	// value if the user has not typed anything yet).
	if(bigLabel) {
		char hh[3] = "--";
		char mm[3] = "--";
		if(entryLen >= 1) hh[0] = entry[0];
		if(entryLen >= 2) hh[1] = entry[1];
		if(entryLen >= 1 && entryLen < 2) hh[1] = '_';
		if(entryLen == 0) { hh[0] = '_'; hh[1] = '_'; }
		if(entryLen >= 3) mm[0] = entry[2];
		if(entryLen >= 4) mm[1] = entry[3];
		if(entryLen >= 3 && entryLen < 4) mm[1] = '_';
		if(entryLen < 3)  { mm[0] = '_'; mm[1] = '_'; }

		char buf[8];
		snprintf(buf, sizeof(buf), "%c%c:%c%c", hh[0], hh[1], mm[0], mm[1]);
		lv_obj_set_style_text_color(bigLabel, MP_TEXT, 0);
		lv_label_set_text(bigLabel, buf);
		lv_obj_clear_flag(bigLabel, LV_OBJ_FLAG_HIDDEN);
	}
}

void PhoneAlarmClock::refreshFiring() {
	for(uint8_t i = 0; i < PhoneAlarmService::MaxAlarms; ++i) {
		if(rows[i]) lv_obj_add_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
	}

	if(captionLabel) {
		// Pulse the caption colour with the modal pulse so the screen
		// reads as urgent at a glance.
		const lv_color_t col = pulseHi ? MP_ACCENT_BRIGHT : MP_ACCENT;
		lv_obj_set_style_text_color(captionLabel, col, 0);
		lv_label_set_text(captionLabel, "ALARM!");
		lv_obj_clear_flag(captionLabel, LV_OBJ_FLAG_HIDDEN);
	}

	if(bigLabel) {
		const int8_t slot = Alarms.firingSlot();
		PhoneAlarmService::Alarm a;
		if(slot >= 0) a = Alarms.getAlarm((uint8_t)slot);

		char buf[8];
		snprintf(buf, sizeof(buf), "%02u:%02u",
		         (unsigned)a.hour, (unsigned)a.minute);
		lv_obj_set_style_text_color(bigLabel,
		                            pulseHi ? MP_ACCENT_BRIGHT : MP_TEXT,
		                            0);
		lv_label_set_text(bigLabel, buf);
		lv_obj_clear_flag(bigLabel, LV_OBJ_FLAG_HIDDEN);
	}
}

void PhoneAlarmClock::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	switch(mode) {
		case Mode::List:
			softKeys->setLeft("TOGGLE");
			softKeys->setRight("EDIT");
			break;
		case Mode::Edit:
			softKeys->setLeft("SAVE");
			softKeys->setRight("CLEAR");
			break;
		case Mode::Firing:
			softKeys->setLeft("SNOOZE");
			softKeys->setRight("DISMISS");
			break;
	}
}

void PhoneAlarmClock::refreshHint() {
	if(hintLabel == nullptr) return;
	const char* text = nullptr;
	lv_color_t  col  = MP_LABEL_DIM;
	switch(mode) {
		case Mode::List:
			text = nullptr;
			break;
		case Mode::Edit:
			text = "ENTER HHMM WITH DIGITS";
			break;
		case Mode::Firing:
			text = "ANY KEY TO DISMISS";
			col  = pulseHi ? MP_ACCENT_BRIGHT : MP_ACCENT;
			break;
	}

	if(text == nullptr) {
		lv_obj_add_flag(hintLabel, LV_OBJ_FLAG_HIDDEN);
		return;
	}
	lv_obj_set_style_text_color(hintLabel, col, 0);
	lv_label_set_text(hintLabel, text);
	lv_obj_clear_flag(hintLabel, LV_OBJ_FLAG_HIDDEN);
}

// ---------- mode transitions ---------------------------------------------

void PhoneAlarmClock::enterList() {
	mode = Mode::List;
	stopPulseTimer();
	pulseHi = false;
	if(captionLabel) {
		// Restore the cyan caption colour the firing pulse may have
		// stomped while we were in the alarm modal.
		lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
		lv_label_set_text(captionLabel, "ALARMS");
	}
	refreshAll();
}

void PhoneAlarmClock::enterEdit(uint8_t slot) {
	if(slot >= PhoneAlarmService::MaxAlarms) return;
	editingSlot = slot;
	// Pre-fill the buffer with the slot's current HHMM so the user can
	// confirm an existing value with a single SAVE press.
	const PhoneAlarmService::Alarm a = Alarms.getAlarm(slot);
	entry[0] = '0' + (a.hour   / 10) % 10;
	entry[1] = '0' + (a.hour   % 10);
	entry[2] = '0' + (a.minute / 10) % 10;
	entry[3] = '0' + (a.minute % 10);
	entry[4] = '\0';
	entryLen = EntryDigits;
	mode = Mode::Edit;
	stopPulseTimer();
	pulseHi = false;
	refreshAll();
}

void PhoneAlarmClock::enterFiring() {
	mode = Mode::Firing;
	pulseHi = false;
	startPulseTimer();
	refreshAll();
}

// ---------- entry buffer --------------------------------------------------

void PhoneAlarmClock::appendDigit(char c) {
	if(mode != Mode::Edit) return;
	if(c < '0' || c > '9') return;

	if(entryLen < EntryDigits) {
		entry[entryLen++] = c;
		entry[entryLen]   = '\0';
	} else {
		// Buffer full: shift left, drop oldest. Same scrolling-entry
		// behaviour the user already has from PhoneTimer's MM:SS.
		for(uint8_t i = 1; i < EntryDigits; ++i) entry[i - 1] = entry[i];
		entry[EntryDigits - 1] = c;
		entry[EntryDigits]     = '\0';
	}
	refreshEdit();
}

void PhoneAlarmClock::backspaceEntry() {
	if(mode != Mode::Edit) return;
	if(entryLen == 0) return;
	entryLen--;
	entry[entryLen] = '\0';
	refreshEdit();
}

void PhoneAlarmClock::commitEntry() {
	if(mode != Mode::Edit) return;

	uint8_t hh = 0, mm = 0;
	if(entryLen >= 1) hh += (entry[0] - '0') * 10;
	if(entryLen >= 2) hh += (entry[1] - '0');
	if(entryLen >= 3) mm += (entry[2] - '0') * 10;
	if(entryLen >= 4) mm += (entry[3] - '0');
	if(hh > 23) hh = 23;
	if(mm > 59) mm = 59;

	// Saving an HHMM auto-enables the slot — the most common reason a
	// user touches an alarm is to set a new time and arm it. They can
	// still toggle off explicitly from the list afterwards.
	Alarms.setAlarm(editingSlot, hh, mm, true);
	enterList();
}

// ---------- list cursor ---------------------------------------------------

void PhoneAlarmClock::cursorUp() {
	if(mode != Mode::List) return;
	cursor = (cursor == 0)
			? (PhoneAlarmService::MaxAlarms - 1)
			: (cursor - 1);
	refreshList();
}

void PhoneAlarmClock::cursorDown() {
	if(mode != Mode::List) return;
	cursor = (cursor + 1) % PhoneAlarmService::MaxAlarms;
	refreshList();
}

// ---------- soft-key actions ---------------------------------------------

void PhoneAlarmClock::primaryAction() {
	switch(mode) {
		case Mode::List: {
			// TOGGLE the highlighted alarm.
			const PhoneAlarmService::Alarm a = Alarms.getAlarm(cursor);
			Alarms.setAlarm(cursor, a.hour, a.minute, !a.enabled);
			refreshList();
			break;
		}
		case Mode::Edit:
			commitEntry();
			break;
		case Mode::Firing:
			Alarms.snooze(PhoneAlarmService::DefaultSnoozeMin);
			enterList();
			break;
	}
}

void PhoneAlarmClock::secondaryAction() {
	switch(mode) {
		case Mode::List:
			enterEdit(cursor);
			break;
		case Mode::Edit:
			backspaceEntry();
			break;
		case Mode::Firing:
			Alarms.dismiss();
			enterList();
			break;
	}
}

// ---------- LVGL timers --------------------------------------------------

void PhoneAlarmClock::startPollTimer() {
	if(pollTimer != nullptr) return;
	pollTimer = lv_timer_create(&PhoneAlarmClock::onPollStatic,
	                            PollPeriodMs, this);
}

void PhoneAlarmClock::stopPollTimer() {
	if(pollTimer == nullptr) return;
	lv_timer_del(pollTimer);
	pollTimer = nullptr;
}

void PhoneAlarmClock::startPulseTimer() {
	if(pulseTimer != nullptr) return;
	pulseTimer = lv_timer_create(&PhoneAlarmClock::onPulseStatic,
	                             PulsePeriodMs, this);
}

void PhoneAlarmClock::stopPulseTimer() {
	if(pulseTimer == nullptr) return;
	lv_timer_del(pulseTimer);
	pulseTimer = nullptr;
}

void PhoneAlarmClock::onPollStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneAlarmClock*>(timer->user_data);
	if(self == nullptr) return;
	// Sync the screen state with the global service. The service is
	// the source of truth; this poll just lets the UI flip into the
	// firing modal promptly when the alarm triggers in the
	// background, and back to list on dismiss/snooze.
	const bool firing = Alarms.isFiring();
	if(firing && self->mode != Mode::Firing) {
		self->enterFiring();
	} else if(!firing && self->mode == Mode::Firing) {
		self->enterList();
	} else if(self->mode == Mode::List) {
		// Re-paint the list every 200 ms so a slot that just flipped
		// (e.g. the snooze rearmed) reflects the right state without
		// a full mode bounce.
		self->refreshList();
	}
}

void PhoneAlarmClock::onPulseStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneAlarmClock*>(timer->user_data);
	if(self == nullptr) return;
	self->pulseHi = !self->pulseHi;
	self->refreshFiring();
	self->refreshHint();
}

// ---------- input ---------------------------------------------------------

void PhoneAlarmClock::buttonPressed(uint i) {
	// In Firing mode, almost any key dismisses or snoozes — feature
	// phones from the era are not subtle here. Left/Enter -> snooze,
	// Right/Back -> dismiss, anything else also dismisses by default
	// so a panicked user can stop the noise without thinking.
	if(mode == Mode::Firing) {
		switch(i) {
			case BTN_LEFT:
			case BTN_L:
			case BTN_ENTER:
				if(softKeys) softKeys->flashLeft();
				primaryAction();
				return;
			case BTN_RIGHT:
			case BTN_R:
				if(softKeys) softKeys->flashRight();
				secondaryAction();
				return;
			case BTN_BACK:
				backLongFired = false;
				return;
			default:
				// Any other key also dismisses (the muscle-memory
				// "stop the noise" gesture). Flash the right softkey
				// so the user gets visual feedback for whichever key
				// they hit.
				if(softKeys) softKeys->flashRight();
				secondaryAction();
				return;
		}
	}

	switch(i) {
		case BTN_LEFT:
		case BTN_L:
		case BTN_ENTER:
			if(softKeys) softKeys->flashLeft();
			primaryAction();
			break;

		case BTN_RIGHT:
			// Defer the actual action to release so a long-press exit
			// path (List->Back) cannot double-fire.
			if(softKeys) softKeys->flashRight();
			backLongFired = false;
			break;

		case BTN_R:
			if(softKeys) softKeys->flashRight();
			secondaryAction();
			break;

		case BTN_2:
			cursorUp();
			break;
		case BTN_8:
			cursorDown();
			break;

		case BTN_5:
			// Centre dialer key acts as "open" for the highlighted
			// slot when in List mode.
			if(mode == Mode::List) {
				enterEdit(cursor);
			}
			break;

		case BTN_0: case BTN_1: case BTN_3: case BTN_4:
		case BTN_6: case BTN_7: case BTN_9:
			if(mode == Mode::Edit) {
				appendDigit('0' + (i - BTN_0));
			}
			break;

		case BTN_BACK:
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneAlarmClock::buttonReleased(uint i) {
	switch(i) {
		case BTN_RIGHT:
			if(!backLongFired) {
				secondaryAction();
			}
			backLongFired = false;
			break;

		case BTN_BACK:
			if(!backLongFired) {
				if(mode == Mode::Edit) {
					// Cancel back to the list view, drop any partial
					// digits the user typed.
					enterList();
				} else if(mode == Mode::Firing) {
					Alarms.dismiss();
					enterList();
				} else {
					pop();
				}
			}
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneAlarmClock::buttonHeld(uint i) {
	switch(i) {
		case BTN_BACK:
			// Long-press BACK is the same as a short tap — exit the
			// screen. The flag suppresses the matching short-press
			// fire-back on release.
			backLongFired = true;
			if(mode == Mode::Edit) {
				enterList();
			} else if(mode == Mode::Firing) {
				Alarms.dismiss();
				enterList();
			} else {
				pop();
			}
			break;

		case BTN_RIGHT:
			// Long-press right softkey is a no-op; the flag stops the
			// matching release-fire from firing again.
			backLongFired = true;
			break;

		default:
			break;
	}
}
