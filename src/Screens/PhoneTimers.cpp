#include "PhoneTimers.h"
#include "PhoneTimer.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <Notes.h>
#include <stdio.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../Services/PhoneRingtoneEngine.h"

// MAKERphone retro palette - kept identical to every other Phone* widget
// so the multi-timer list slots in beside PhoneTimer (S62), PhoneAlarmClock
// (S124), PhoneStopwatch (S61) without a visual seam. Inlined per the
// established pattern (see PhoneTimer.cpp / PhoneAlarmClock.cpp).
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan caption
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream readout
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim purple hint line
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange (alarm)
#define MP_ACCENT_BRIGHT   lv_color_make(255, 200,  80)  // bright sunset (alarm pulse hi)
#define MP_DIM             lv_color_make( 70,  56, 100)  // muted purple (idle slots)
#define MP_RUN             lv_color_make(122, 232, 180)  // soft mint-green for running rows

// ---------- alarm melody --------------------------------------------------
//
// Same shape as PhoneTimer's TimerAlm so a multi-timer alert is sonically
// indistinguishable from a single-timer alert - users learn one alarm
// sound, period. 4-note rising arpeggio that loops while at least one
// slot is in Ringing mode. E5 -> A5 -> E5 -> A5 reads as a familiar
// "kitchen timer" alert without being shrill.
static const PhoneRingtoneEngine::Note AlarmNotes[] = {
		{ NOTE_E5, 220 },
		{ NOTE_A5, 220 },
		{ NOTE_E5, 220 },
		{ NOTE_A5, 220 },
};

static const PhoneRingtoneEngine::Melody AlarmMelody = {
		AlarmNotes,
		sizeof(AlarmNotes) / sizeof(AlarmNotes[0]),
		60,        // gapMs
		true,      // loop until dismissed
		"TimersAlm",
};

// ---------- geometry ------------------------------------------------------
//
// Layout matches the PhoneAlarmClock list silhouette so the user feels
// the same "shape" of screen behind both apps. Status bar at y=0 (10 px),
// caption at y=12, four list rows at y=26..(26+4*22), softkey bar at y=118.

static constexpr lv_coord_t kCaptionY     = 12;
static constexpr lv_coord_t kListTopY     = 26;
static constexpr lv_coord_t kRowH         = 22;
static constexpr lv_coord_t kListLeft     = 6;
static constexpr lv_coord_t kListWidth    = 148;

static constexpr lv_coord_t kBigCaptionY  = 14;
static constexpr lv_coord_t kBigReadoutY  = 36;
static constexpr lv_coord_t kHintY        = 78;
static constexpr lv_coord_t kHintLeft     = 6;
static constexpr lv_coord_t kHintW        = 148;

// ---------- ctor / dtor ---------------------------------------------------

PhoneTimers::PhoneTimers()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  bigCaptionLabel(nullptr),
		  bigReadoutLabel(nullptr),
		  hintLabel(nullptr) {

	// Zero the entry buffer + row pointers up front so refresh* paths
	// can read them safely before any user interaction. The Slot
	// default constructor already zeroes slots[].
	for(uint8_t i = 0; i <= EntryDigits; ++i) entry[i] = 0;
	for(uint8_t i = 0; i < MaxTimers; ++i) rows[i] = nullptr;

	// Full-screen container, no scrollbars, no padding -- same blank
	// canvas pattern as PhoneTimer / PhoneAlarmClock / PhoneStopwatch.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px).
	statusBar = new PhoneStatusBar(obj);

	buildList();
	buildEditView();

	// Bottom soft-key bar; populated by refreshSoftKeys() per mode.
	// Initial labels show the cursor-row-Idle pair ("EDIT" right
	// matches the muscle memory from PhoneAlarmClock).
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("START");
	softKeys->setRight("EDIT");

	// Long-press threshold matches the rest of the MAKERphone shell
	// so the gesture feels identical from any screen.
	setButtonHoldTime(BTN_BACK, BackHoldMs);
	setButtonHoldTime(BTN_RIGHT, BackHoldMs);

	// Default mode: List. Hides the edit-view widgets via refreshAll().
	refreshAll();
}

PhoneTimers::~PhoneTimers() {
	stopTickTimer();
	stopPulseTimer();
	// Always make sure the global ringtone engine is silent on tear-down.
	// An alarm started by this instance must not keep ringing after the
	// screen is destroyed.
	Ringtone.stop();
	// All other children (wallpaper, statusBar, softKeys, labels) are
	// parented to obj and freed by the LVScreen base destructor.
}

void PhoneTimers::onStart() {
	Input::getInstance()->addListener(this);

	// Re-arm the appropriate timers on re-entry. If the user navigated
	// away while any slot was running, the countdown was paused
	// implicitly (we stop the tick on detach so a freed instance does
	// not get fired against). On re-entry we rebase runStartMs to the
	// current millis() for any still-Running slot, so the user sees no
	// time-jump from the time they were on another screen.
	const uint32_t now = (uint32_t) millis();
	for(uint8_t i = 0; i < MaxTimers; ++i) {
		Slot& s = slots[i];
		if(s.state == SlotState::Running) {
			s.runStartMs     = now;
			s.runStartRemain = s.remainingMs;
		}
	}

	if(anyRunning()) startTickTimer();
	if(anyRinging()) {
		startPulseTimer();
		// Re-fire the alarm; it was silenced by onStop() to keep the
		// engine clean while the screen was detached.
		syncRingtone();
	}

	refreshAll();
}

void PhoneTimers::onStop() {
	Input::getInstance()->removeListener(this);

	// Snapshot live remaining-ms for any Running slot, since LVGL
	// timers must not survive a screen detach (they would target a
	// stale `this` if the screen is torn down before the timer
	// fires) and we want re-entry to land on a coherent value.
	for(uint8_t i = 0; i < MaxTimers; ++i) {
		Slot& s = slots[i];
		if(s.state == SlotState::Running) {
			s.remainingMs = currentRemainingMs(s);
		}
	}

	stopTickTimer();
	stopPulseTimer();

	// Silence any active alarm. Ringing slots stay in the Ringing
	// state, so onStart() will re-fire the audio + pulse when the
	// screen is re-attached.
	Ringtone.stop();
}

// ---------- builders ------------------------------------------------------

void PhoneTimers::buildList() {
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_width(captionLabel, kListWidth);
	lv_obj_set_pos(captionLabel, kListLeft, kCaptionY);
	lv_obj_set_style_text_align(captionLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(captionLabel, "TIMERS");

	// Four list rows; each one is a single label whose text we
	// rebuild on every refresh. pixelbasic7 keeps the row legible at
	// 22 px height inside the 88 px vertical band between the caption
	// and the soft-keys.
	for(uint8_t i = 0; i < MaxTimers; ++i) {
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

void PhoneTimers::buildEditView() {
	// All three Edit widgets are created up-front and shown/hidden
	// based on mode. This avoids any LVGL teardown during a List<->Edit
	// toggle, which would interrupt the wallpaper animation.

	bigCaptionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(bigCaptionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(bigCaptionLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_width(bigCaptionLabel, kListWidth);
	lv_obj_set_pos(bigCaptionLabel, kListLeft, kBigCaptionY);
	lv_obj_set_style_text_align(bigCaptionLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(bigCaptionLabel, "");
	lv_obj_add_flag(bigCaptionLabel, LV_OBJ_FLAG_HIDDEN);

	bigReadoutLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(bigReadoutLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(bigReadoutLabel, MP_TEXT, 0);
	lv_obj_set_width(bigReadoutLabel, 160);
	lv_obj_set_pos(bigReadoutLabel, 0, kBigReadoutY);
	lv_obj_set_style_text_align(bigReadoutLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(bigReadoutLabel, "00:00");
	lv_obj_add_flag(bigReadoutLabel, LV_OBJ_FLAG_HIDDEN);

	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_label_set_long_mode(hintLabel, LV_LABEL_LONG_DOT);
	lv_obj_set_width(hintLabel, kHintW);
	lv_obj_set_style_text_align(hintLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(hintLabel, "");
	lv_obj_set_pos(hintLabel, kHintLeft, kHintY);
	lv_obj_add_flag(hintLabel, LV_OBJ_FLAG_HIDDEN);
}

// ---------- countdown math -----------------------------------------------

uint32_t PhoneTimers::currentRemainingMs(const Slot& s) const {
	if(s.state == SlotState::Running) {
		const uint32_t now     = (uint32_t) millis();
		const uint32_t elapsed = now - s.runStartMs;
		if(elapsed >= s.runStartRemain) return 0;
		return s.runStartRemain - elapsed;
	}
	return s.remainingMs;
}

void PhoneTimers::snapshotRunning(Slot& s) {
	s.remainingMs    = currentRemainingMs(s);
	s.runStartMs     = 0;
	s.runStartRemain = 0;
}

// ---------- per-slot state transitions -----------------------------------

void PhoneTimers::slotEnterIdle(uint8_t i, bool clearRemaining) {
	if(i >= MaxTimers) return;
	Slot& s = slots[i];
	s.state          = SlotState::Idle;
	s.runStartMs     = 0;
	s.runStartRemain = 0;
	if(clearRemaining) {
		s.remainingMs = 0;
	}
}

void PhoneTimers::slotEnterRunning(uint8_t i) {
	if(i >= MaxTimers) return;
	Slot& s = slots[i];
	if(s.remainingMs == 0) {
		// Defensive: never start a 0 ms countdown -- it would fire
		// the alarm immediately, which is not what the user asked for.
		s.state = SlotState::Idle;
		return;
	}
	s.runStartMs     = (uint32_t) millis();
	s.runStartRemain = s.remainingMs;
	s.state          = SlotState::Running;
	startTickTimer();
}

void PhoneTimers::slotEnterPaused(uint8_t i) {
	if(i >= MaxTimers) return;
	Slot& s = slots[i];
	// Snapshot live remaining-ms so the readout and the math agree
	// across the pause boundary.
	snapshotRunning(s);
	s.state = SlotState::Paused;
}

void PhoneTimers::slotEnterRinging(uint8_t i) {
	if(i >= MaxTimers) return;
	Slot& s = slots[i];
	s.state          = SlotState::Ringing;
	s.remainingMs    = 0;
	s.runStartMs     = 0;
	s.runStartRemain = 0;
	startPulseTimer();
	syncRingtone();
}

// ---------- top-level mode transitions -----------------------------------

void PhoneTimers::enterList() {
	mode = Mode::List;

	// Hide edit widgets, show list widgets. The wallpaper / statusBar /
	// softKeys are shared across both modes and stay visible.
	if(bigCaptionLabel) lv_obj_add_flag(bigCaptionLabel, LV_OBJ_FLAG_HIDDEN);
	if(bigReadoutLabel) lv_obj_add_flag(bigReadoutLabel, LV_OBJ_FLAG_HIDDEN);
	if(hintLabel)       lv_obj_add_flag(hintLabel,       LV_OBJ_FLAG_HIDDEN);

	if(captionLabel) lv_obj_clear_flag(captionLabel, LV_OBJ_FLAG_HIDDEN);
	for(uint8_t i = 0; i < MaxTimers; ++i) {
		if(rows[i]) lv_obj_clear_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
	}

	refreshAll();
}

void PhoneTimers::enterEdit(uint8_t slotIdx) {
	if(slotIdx >= MaxTimers) return;
	mode        = Mode::Edit;
	editingSlot = slotIdx;

	// Pre-load the entry buffer with the current preset so the user
	// can tweak rather than retype. presetMs is rounded down to a
	// whole second; pad to MMSS, strip leading zeros so the display
	// reads as the user typed it last time.
	const uint32_t totalSec = slots[slotIdx].presetMs / 1000u;
	const uint32_t mm = (totalSec / 60u) > 99u ? 99u : (totalSec / 60u);
	const uint32_t ss = totalSec % 60u;
	char padded[5];
	snprintf(padded, sizeof(padded), "%02u%02u", (unsigned) mm, (unsigned) ss);

	entryLen = 0;
	for(uint8_t i = 0; i < EntryDigits; ++i) entry[i] = '\0';
	if(strcmp(padded, "0000") != 0) {
		// Skip leading zeros so e.g. "0500" lands as "500" - the user
		// can append digits and the shift-left buffer stays consistent
		// with PhoneTimer's entry semantics.
		uint8_t idx = 0;
		while(idx < EntryDigits && padded[idx] == '0') idx++;
		while(idx < EntryDigits && entryLen < EntryDigits) {
			entry[entryLen++] = padded[idx++];
		}
		entry[entryLen] = '\0';
	}

	// Hide list widgets, show edit widgets.
	if(captionLabel) lv_obj_add_flag(captionLabel, LV_OBJ_FLAG_HIDDEN);
	for(uint8_t i = 0; i < MaxTimers; ++i) {
		if(rows[i]) lv_obj_add_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
	}
	if(bigCaptionLabel) lv_obj_clear_flag(bigCaptionLabel, LV_OBJ_FLAG_HIDDEN);
	if(bigReadoutLabel) lv_obj_clear_flag(bigReadoutLabel, LV_OBJ_FLAG_HIDDEN);
	if(hintLabel)       lv_obj_clear_flag(hintLabel,       LV_OBJ_FLAG_HIDDEN);

	refreshAll();
}

// ---------- entry buffer (Edit mode) -------------------------------------

void PhoneTimers::appendDigit(char c) {
	if(mode != Mode::Edit) return;
	if(c < '0' || c > '9') return;

	// Reject leading zero so "0" then "5" reads as "5" rather than "05"
	// (matches PhoneTimer / PhoneAlarmClock semantics).
	if(entryLen == 0 && c == '0') return;

	if(entryLen < EntryDigits) {
		entry[entryLen++] = c;
		entry[entryLen]   = '\0';
	} else {
		// Buffer full: shift left + drop the oldest digit. EntryDigits
		// is small (4) so an explicit loop is just as clear as memmove.
		for(uint8_t i = 1; i < EntryDigits; ++i) entry[i - 1] = entry[i];
		entry[EntryDigits - 1] = c;
		entry[EntryDigits]     = '\0';
	}
	refreshEdit();
}

void PhoneTimers::backspaceEntry() {
	if(mode != Mode::Edit) return;
	if(entryLen == 0) return;
	entryLen--;
	entry[entryLen] = '\0';
	refreshEdit();
}

void PhoneTimers::commitEntry() {
	if(mode != Mode::Edit) return;
	if(editingSlot >= MaxTimers) return;

	const uint32_t ms = PhoneTimer::entryToMs(entry, entryLen);
	Slot& s = slots[editingSlot];

	// Stop any current run on this slot; the new preset replaces both
	// the committed duration and the live remaining snapshot. This is
	// the kitchen-timer convention: editing the duration arms a fresh
	// countdown from zero each time the user taps START.
	if(s.state == SlotState::Running || s.state == SlotState::Ringing) {
		// Releasing the ringing engine if this was the audible slot;
		// syncRingtone() at the end re-asserts the right melody for the
		// remaining ringing-set membership.
		s.state = SlotState::Idle;
	}
	s.presetMs    = ms;
	s.remainingMs = ms;
	s.runStartMs     = 0;
	s.runStartRemain = 0;

	syncRingtone();
	if(!anyRunning()) stopTickTimer();
	if(!anyRinging()) stopPulseTimer();

	enterList();
}

// ---------- cursor (List mode) -------------------------------------------

void PhoneTimers::cursorUp() {
	if(mode != Mode::List) return;
	cursor = (cursor == 0) ? (MaxTimers - 1) : (uint8_t)(cursor - 1);
	refreshAll();
}

void PhoneTimers::cursorDown() {
	if(mode != Mode::List) return;
	cursor = (uint8_t)((cursor + 1) % MaxTimers);
	refreshAll();
}

// ---------- softkey actions ----------------------------------------------

void PhoneTimers::primaryAction() {
	if(mode == Mode::Edit) {
		commitEntry();
		return;
	}
	if(cursor >= MaxTimers) return;

	Slot& s = slots[cursor];
	switch(s.state) {
		case SlotState::Idle:
			slotEnterRunning(cursor);
			break;
		case SlotState::Running:
			slotEnterPaused(cursor);
			if(!anyRunning()) stopTickTimer();
			break;
		case SlotState::Paused:
			slotEnterRunning(cursor);
			break;
		case SlotState::Ringing:
			// Dismiss: restore the original preset so the user can
			// re-arm with a single START. Same kitchen-timer feel as
			// PhoneTimer's ringing-dismiss path.
			s.remainingMs = s.presetMs;
			slotEnterIdle(cursor, false);
			syncRingtone();
			if(!anyRinging()) stopPulseTimer();
			break;
	}
	refreshAll();
}

void PhoneTimers::secondaryAction() {
	if(mode == Mode::Edit) {
		backspaceEntry();
		return;
	}
	if(cursor >= MaxTimers) return;

	Slot& s = slots[cursor];
	switch(s.state) {
		case SlotState::Idle:
			// Right softkey in Idle is "EDIT" - drop into the digit
			// buffer to redefine the preset.
			enterEdit(cursor);
			return;
		case SlotState::Running:
		case SlotState::Paused:
			// RESET - preserve the preset so the user can re-arm
			// with a single START. Matches the feature-phone
			// convention and PhoneTimer's secondaryAction in Idle.
			s.remainingMs = s.presetMs;
			slotEnterIdle(cursor, false);
			if(!anyRunning()) stopTickTimer();
			break;
		case SlotState::Ringing:
			// Same dismiss as the left softkey in this state.
			s.remainingMs = s.presetMs;
			slotEnterIdle(cursor, false);
			syncRingtone();
			if(!anyRinging()) stopPulseTimer();
			break;
	}
	refreshAll();
}

// ---------- repaint -------------------------------------------------------

void PhoneTimers::refreshAll() {
	if(mode == Mode::List) {
		refreshList();
	} else {
		refreshEdit();
	}
	refreshSoftKeys();
}

void PhoneTimers::refreshList() {
	for(uint8_t i = 0; i < MaxTimers; ++i) {
		if(rows[i] == nullptr) continue;

		const Slot& s = slots[i];
		const char* tag = "---";
		switch(s.state) {
			case SlotState::Idle:    tag = "---";  break;
			case SlotState::Running: tag = "RUN";  break;
			case SlotState::Paused:  tag = "PSE";  break;
			case SlotState::Ringing: tag = "RING"; break;
		}

		char tbuf[8];
		if(s.state == SlotState::Idle && s.remainingMs == 0 && s.presetMs == 0) {
			// No preset configured yet - render "--:--" so the empty
			// state is visually distinct from "00:00" (a zero preset).
			snprintf(tbuf, sizeof(tbuf), "--:--");
		} else {
			const uint32_t live = (s.state == SlotState::Idle)
					? s.presetMs
					: currentRemainingMs(s);
			PhoneTimer::formatRemaining(live, tbuf, sizeof(tbuf));
		}

		// Final row format: cursor marker, slot index, state tag,
		// remaining mm:ss. Whitespace is tuned for the 148 px row
		// width at the pixelbasic7 cell pitch.
		char buf[40];
		snprintf(buf, sizeof(buf), "%s %u  %-4s  %s",
				 (i == cursor) ? ">" : " ",
				 (unsigned)(i + 1),
				 tag,
				 tbuf);
		lv_label_set_text(rows[i], buf);

		// Colour: dim purple for idle-empty, mint green for running,
		// warm cream for paused, sunset orange (pulsing) for ringing.
		lv_color_t col = MP_TEXT;
		switch(s.state) {
			case SlotState::Idle:
				col = (s.presetMs == 0) ? MP_DIM : MP_TEXT;
				break;
			case SlotState::Running:
				col = MP_RUN;
				break;
			case SlotState::Paused:
				col = MP_LABEL_DIM;
				break;
			case SlotState::Ringing:
				col = pulseHi ? MP_ACCENT_BRIGHT : MP_ACCENT;
				break;
		}
		// Highlight the cursor row regardless of slot state so the
		// user can always see which slot the soft-keys target.
		if(i == cursor && s.state != SlotState::Ringing) {
			col = MP_HIGHLIGHT;
		}
		lv_obj_set_style_text_color(rows[i], col, 0);
	}
}

void PhoneTimers::refreshEdit() {
	if(bigCaptionLabel == nullptr || bigReadoutLabel == nullptr || hintLabel == nullptr) return;

	char cap[16];
	snprintf(cap, sizeof(cap), "EDIT %u", (unsigned)(editingSlot + 1));
	lv_label_set_text(bigCaptionLabel, cap);

	char tbuf[8];
	PhoneTimer::formatRemaining(PhoneTimer::entryToMs(entry, entryLen),
								tbuf, sizeof(tbuf));
	lv_label_set_text(bigReadoutLabel, tbuf);
	lv_obj_set_style_text_color(bigReadoutLabel, MP_TEXT, 0);

	lv_label_set_text(hintLabel, "ENTER MM:SS WITH DIGITS");
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
}

void PhoneTimers::refreshSoftKeys() {
	if(softKeys == nullptr) return;

	if(mode == Mode::Edit) {
		softKeys->setLeft("SAVE");
		softKeys->setRight("CLEAR");
		return;
	}

	// List mode: labels reflect the cursor slot's current state.
	if(cursor >= MaxTimers) return;
	const Slot& s = slots[cursor];
	switch(s.state) {
		case SlotState::Idle:
			// If the slot has never been configured, START would
			// fire on a 0 ms preset and immediately drop back to
			// Idle (per slotEnterRunning's defensive zero-check).
			// The user-facing label is still "START" - the no-op is
			// a benign nudge to set a preset first.
			softKeys->setLeft(s.presetMs == 0 ? "START" : "START");
			softKeys->setRight("EDIT");
			break;
		case SlotState::Running:
			softKeys->setLeft("PAUSE");
			softKeys->setRight("RESET");
			break;
		case SlotState::Paused:
			softKeys->setLeft("RESUME");
			softKeys->setRight("RESET");
			break;
		case SlotState::Ringing:
			softKeys->setLeft("DISMISS");
			softKeys->setRight("DISMISS");
			break;
	}
}

// ---------- aggregate helpers --------------------------------------------

bool PhoneTimers::anyRunning() const {
	for(uint8_t i = 0; i < MaxTimers; ++i) {
		if(slots[i].state == SlotState::Running) return true;
	}
	return false;
}

bool PhoneTimers::anyRinging() const {
	for(uint8_t i = 0; i < MaxTimers; ++i) {
		if(slots[i].state == SlotState::Ringing) return true;
	}
	return false;
}

int8_t PhoneTimers::firstRingingSlot() const {
	for(uint8_t i = 0; i < MaxTimers; ++i) {
		if(slots[i].state == SlotState::Ringing) return (int8_t) i;
	}
	return -1;
}

void PhoneTimers::syncRingtone() {
	// One audible alarm at a time - the lowest-index ringing slot owns
	// the global Ringtone engine. If no slot is ringing, silence the
	// engine. Engine respects Settings.sound regardless, so a muted
	// device gets the visual cue without piezo output.
	if(anyRinging()) {
		// PhoneRingtoneEngine::play() is idempotent for the same melody
		// (it replaces the current playback), so calling it on every
		// tick is safe but wasteful - the public API does not expose a
		// "current melody" comparison, so we skip the noise by only
		// re-playing when transitioning from "no melody playing" to
		// "alarm playing", or when previously stopped.
		if(!Ringtone.isPlaying()) {
			Ringtone.play(AlarmMelody);
		}
	} else {
		Ringtone.stop();
	}
}

// ---------- LVGL timer plumbing ------------------------------------------

void PhoneTimers::startTickTimer() {
	if(tickTimer != nullptr) return;  // idempotent
	tickTimer = lv_timer_create(onTickStatic, TickPeriodMs, this);
}

void PhoneTimers::stopTickTimer() {
	if(tickTimer == nullptr) return;
	lv_timer_del(tickTimer);
	tickTimer = nullptr;
}

void PhoneTimers::startPulseTimer() {
	if(pulseTimer != nullptr) return;
	pulseTimer = lv_timer_create(onPulseStatic, PulsePeriodMs, this);
}

void PhoneTimers::stopPulseTimer() {
	if(pulseTimer == nullptr) return;
	lv_timer_del(pulseTimer);
	pulseTimer = nullptr;
}

void PhoneTimers::onTickStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneTimers*>(timer->user_data);
	if(self == nullptr) return;

	// Walk every Running slot; transition to Ringing if the live
	// remaining is zero. We do this BEFORE the repaint so the row
	// refreshes already reflects the new state.
	bool changed = false;
	for(uint8_t i = 0; i < MaxTimers; ++i) {
		Slot& s = self->slots[i];
		if(s.state != PhoneTimers::SlotState::Running) continue;
		if(self->currentRemainingMs(s) == 0) {
			self->slotEnterRinging(i);
			changed = true;
		}
	}

	// Repaint either way - the seconds tick on Running rows visibly
	// every 200 ms, even if no state changed.
	if(self->mode == PhoneTimers::Mode::List) {
		self->refreshList();
	}

	// If a Running -> Ringing transition happened, we may have flipped
	// soft-key labels on the cursor row.
	if(changed) {
		self->refreshSoftKeys();
		// And if no slot is still Running, free the tick.
		if(!self->anyRunning()) self->stopTickTimer();
	}
}

void PhoneTimers::onPulseStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneTimers*>(timer->user_data);
	if(self == nullptr) return;
	if(!self->anyRinging()) {
		self->stopPulseTimer();
		return;
	}
	self->pulseHi = !self->pulseHi;
	if(self->mode == PhoneTimers::Mode::List) {
		self->refreshList();
	}
}

// ---------- input ---------------------------------------------------------

void PhoneTimers::buttonPressed(uint i) {
	// In Edit mode the digit keys feed the entry buffer; everything
	// else falls through to the standard softkey handlers below.
	if(mode == Mode::Edit) {
		switch(i) {
			case BTN_0: appendDigit('0'); return;
			case BTN_1: appendDigit('1'); return;
			case BTN_2: appendDigit('2'); return;
			case BTN_3: appendDigit('3'); return;
			case BTN_4: appendDigit('4'); return;
			case BTN_5: appendDigit('5'); return;
			case BTN_6: appendDigit('6'); return;
			case BTN_7: appendDigit('7'); return;
			case BTN_8: appendDigit('8'); return;
			case BTN_9: appendDigit('9'); return;
			default: break;
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
			// Defer the actual short-press action to buttonReleased so
			// a long-press cannot double-fire on key release. The flash
			// gives instant visual feedback either way.
			if(softKeys) softKeys->flashRight();
			backLongFired = false;
			break;

		case BTN_R:
			// R bumper is a dedicated short-press alias - fire now.
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
			// slot when in List mode. Mirrors PhoneAlarmClock's
			// muscle memory.
			if(mode == Mode::List && cursor < MaxTimers) {
				enterEdit(cursor);
			}
			break;

		case BTN_BACK:
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneTimers::buttonReleased(uint i) {
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
					// Cancel - back to List, drop any partial digits.
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

void PhoneTimers::buttonHeld(uint i) {
	switch(i) {
		case BTN_BACK:
			// Long-press BACK is the same as a short tap in this
			// screen - exits the screen (or cancels Edit).
			backLongFired = true;
			if(mode == Mode::Edit) {
				enterList();
			} else {
				pop();
			}
			break;

		case BTN_RIGHT:
			// Long-press right softkey wipes the entry buffer in
			// Edit mode (a quick "AC"); in List mode it falls through
			// to the same RESET secondaryAction would have produced.
			// The flag suppresses the matching short-press release
			// fire-back.
			backLongFired = true;
			if(mode == Mode::Edit) {
				entryLen = 0;
				entry[0] = '\0';
				refreshEdit();
			} else {
				secondaryAction();
			}
			break;

		default:
			break;
	}
}
