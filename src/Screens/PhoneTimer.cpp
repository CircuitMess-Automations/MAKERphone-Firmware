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

// MAKERphone retro palette - kept identical to every other Phone* widget so
// the timer slots in beside PhoneCalculator (S60) / PhoneStopwatch (S61) /
// PhoneAboutScreen without a visual seam. Inlined per the established
// pattern (see PhoneCalculator.cpp / PhoneStopwatch.cpp / PhoneAboutScreen.cpp).
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan caption
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream readout
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim purple hint line
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange (alarm)
#define MP_ACCENT_BRIGHT   lv_color_make(255, 200,  80)  // bright sunset (alarm pulse hi)

// ---------- alarm melody --------------------------------------------------
//
// 4-note rising arpeggio that loops while the timer is in Ringing mode.
// E5 -> A5 -> E5 -> A5 reads as a familiar "kitchen-timer" alert without
// being shrill enough to be annoying. The 220 ms note + 60 ms gap gives
// a 1.12 s loop; combined with the 250 ms pulse animation, the audio
// and visual cues stay roughly in phase. Lives in flash because the
// array is `static const`.
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
		"TimerAlm",
};

// ---------- geometry ------------------------------------------------------
//
// Layout matches the PhoneStopwatch silhouette so the user feels the same
// "shape" of screen behind both apps. Status bar at y=0 (10 px), caption
// at y=12, big readout at y=32, hint line at y=64, softkey bar at y=118.

static constexpr lv_coord_t kCaptionY  = 12;
static constexpr lv_coord_t kReadoutY  = 32;
static constexpr lv_coord_t kHintY     = 70;
static constexpr lv_coord_t kHintLeft  = 6;
static constexpr lv_coord_t kHintW     = 148;

// ---------- ctor / dtor ---------------------------------------------------

PhoneTimer::PhoneTimer()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  readoutLabel(nullptr),
		  hintLabel(nullptr) {

	// Zero the entry buffer up front so refreshReadout() can read it
	// safely before the user types anything (an empty buffer renders
	// as "00:00").
	for(uint8_t i = 0; i <= EntryDigits; ++i) entry[i] = 0;

	// Full-screen container, no scrollbars, no padding -- same blank
	// canvas pattern PhoneCalculator / PhoneStopwatch / PhoneAboutScreen
	// use. Children below either pin themselves with IGNORE_LAYOUT or
	// are LVGL primitives that we anchor manually on the 160x128 display.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px).
	statusBar = new PhoneStatusBar(obj);

	buildCaption();
	buildReadout();
	buildHint();

	// Bottom soft-key bar. Initial Idle pair.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("START");
	softKeys->setRight("CLEAR");

	// Long-press threshold matches the rest of the MAKERphone shell so
	// the gesture feels identical from any screen.
	setButtonHoldTime(BTN_BACK, BackHoldMs);
	setButtonHoldTime(BTN_RIGHT, BackHoldMs);

	// Initial paint -- show "00:00" and the idle hint rather than a
	// blank panel.
	refreshReadout();
	refreshHint();
}

PhoneTimer::~PhoneTimer() {
	stopTickTimer();
	stopPulseTimer();
	// Always make sure the global ringtone engine is silent on tear-down,
	// otherwise an alarm started by this instance could keep ringing
	// after the screen is destroyed.
	Ringtone.stop();
	// All other children (wallpaper, statusBar, softKeys, labels) are
	// parented to obj and freed by the LVScreen base destructor.
}

void PhoneTimer::onStart() {
	Input::getInstance()->addListener(this);

	// Re-arm the appropriate timers on re-entry. If the user navigated
	// away while running, the countdown was paused implicitly (we stop
	// the tick on detach so a freed instance does not get fired against)
	// -- treat that as a paused state rather than silently losing time.
	if(mode == Mode::Running) {
		// Resume from where we left off, but reload the live remaining
		// snapshot so the user does not see a stale value flash up.
		runStartMs     = (uint32_t) millis();
		runStartRemain = remainingMs;
		startTickTimer();
	} else if(mode == Mode::Ringing) {
		// Re-fire the alarm + pulse on re-entry. The alarm was silenced
		// by onStop() to keep the ringtone engine clean while the
		// screen is detached.
		Ringtone.play(AlarmMelody);
		startPulseTimer();
	}

	refreshReadout();
	refreshSoftKeys();
	refreshHint();
}

void PhoneTimer::onStop() {
	Input::getInstance()->removeListener(this);

	// Snapshot remaining-ms now, since LVGL timers must not survive a
	// screen detach (they would target a stale `this` if the screen is
	// torn down before the timer fires) and we want the user to come
	// back to a coherent value.
	if(mode == Mode::Running) {
		remainingMs = currentRemainingMs();
	}

	stopTickTimer();
	stopPulseTimer();

	// Silence the alarm if we were ringing. The state stays Ringing so
	// onStart() can re-fire the audio + pulse on re-entry.
	Ringtone.stop();
}

// ---------- builders ------------------------------------------------------

void PhoneTimer::buildCaption() {
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "TIMER");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, kCaptionY);
}

void PhoneTimer::buildReadout() {
	readoutLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(readoutLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(readoutLabel, MP_TEXT, 0);
	lv_obj_set_align(readoutLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(readoutLabel, kReadoutY);
	lv_label_set_text(readoutLabel, "00:00");
}

void PhoneTimer::buildHint() {
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_label_set_long_mode(hintLabel, LV_LABEL_LONG_DOT);
	lv_obj_set_width(hintLabel, kHintW);
	lv_obj_set_style_text_align(hintLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(hintLabel, "");
	lv_obj_set_pos(hintLabel, kHintLeft, kHintY);
}

// ---------- formatting / math --------------------------------------------

void PhoneTimer::formatRemaining(uint32_t ms, char* out, size_t outLen) {
	if(out == nullptr || outLen == 0) return;

	// Round up to the next whole second so a count-down never lingers
	// on "00:00" before triggering the alarm -- the user expects the
	// display to tick to zero exactly when the alarm fires.
	const uint32_t totalSec = (ms + 999u) / 1000u;
	uint32_t mm = totalSec / 60u;
	const uint32_t ss = totalSec % 60u;
	if(mm > MaxMinutes) {
		snprintf(out, outLen, "99:59");
		return;
	}
	snprintf(out, outLen, "%02u:%02u", (unsigned) mm, (unsigned) ss);
}

uint32_t PhoneTimer::entryToMs(const char* digits, uint8_t count) {
	if(digits == nullptr || count == 0) return 0;
	if(count > EntryDigits) count = EntryDigits;

	// Pad with leading zeros into a fixed 4-char "MMSS" view so we can
	// pick the columns out without index-juggling. Bytes outside the
	// valid range get '0'.
	char padded[5] = {'0','0','0','0','\0'};
	const uint8_t offset = (uint8_t) (EntryDigits - count);
	for(uint8_t i = 0; i < count; ++i) {
		const char c = digits[i];
		padded[offset + i] = (c >= '0' && c <= '9') ? c : '0';
	}

	const uint32_t mm = (uint32_t)((padded[0] - '0') * 10 + (padded[1] - '0'));
	uint32_t ss       = (uint32_t)((padded[2] - '0') * 10 + (padded[3] - '0'));
	if(ss > 59) ss = 59;        // clamp invalid SS so the readout stays legal

	uint32_t total = mm * 60u + ss;
	if(total > MaxMinutes * 60u + 59u) total = MaxMinutes * 60u + 59u;
	return total * 1000u;
}

uint32_t PhoneTimer::currentRemainingMs() const {
	if(mode == Mode::Running) {
		const uint32_t now     = (uint32_t) millis();
		const uint32_t elapsed = now - runStartMs;
		if(elapsed >= runStartRemain) return 0;
		return runStartRemain - elapsed;
	}
	return remainingMs;
}

// ---------- repaint -------------------------------------------------------

void PhoneTimer::refreshReadout() {
	if(readoutLabel == nullptr) return;
	char buf[8];

	if(mode == Mode::Idle) {
		// In Idle the readout reflects the entry buffer in real time --
		// what the user has typed so far, formatted as MM:SS. An empty
		// buffer reads as "00:00".
		formatRemaining(entryToMs(entry, entryLen), buf, sizeof(buf));
	} else {
		formatRemaining(currentRemainingMs(), buf, sizeof(buf));
	}

	lv_label_set_text(readoutLabel, buf);

	// Colour: cream in Idle/Running/Paused; pulses sunset in Ringing.
	lv_color_t col = MP_TEXT;
	if(mode == Mode::Ringing) {
		col = pulseHi ? MP_ACCENT_BRIGHT : MP_ACCENT;
	}
	lv_obj_set_style_text_color(readoutLabel, col, 0);
}

void PhoneTimer::refreshSoftKeys() {
	if(softKeys == nullptr) return;

	switch(mode) {
		case Mode::Idle:
			softKeys->setLeft("START");
			// Right softkey doubles as a backspace in Idle so the user
			// can correct a mistyped digit without switching screens.
			softKeys->setRight("CLEAR");
			break;
		case Mode::Running:
			softKeys->setLeft("PAUSE");
			softKeys->setRight("RESET");
			break;
		case Mode::Paused:
			softKeys->setLeft("RESUME");
			softKeys->setRight("RESET");
			break;
		case Mode::Ringing:
			// In Ringing both softkeys map to "DISMISS". Same label on
			// both sides gives the user two ways to silence the alarm
			// (matches the PhoneIncomingCall S24 pattern where any key
			// answers/declines a ring).
			softKeys->setLeft("DISMISS");
			softKeys->setRight("DISMISS");
			break;
	}
}

void PhoneTimer::refreshHint() {
	if(hintLabel == nullptr) return;
	const char* text = "";
	switch(mode) {
		case Mode::Idle:    text = "ENTER MM:SS WITH DIGITS"; break;
		case Mode::Running: text = "COUNTING DOWN...";        break;
		case Mode::Paused:  text = "PAUSED";                  break;
		case Mode::Ringing: text = "TIME'S UP";               break;
	}
	lv_label_set_text(hintLabel, text);

	// In Ringing the hint also flips with the pulse so the whole screen
	// reads as urgent. The other modes keep the dim caption colour.
	lv_color_t col = MP_LABEL_DIM;
	if(mode == Mode::Ringing) {
		col = pulseHi ? MP_ACCENT_BRIGHT : MP_ACCENT;
	}
	lv_obj_set_style_text_color(hintLabel, col, 0);
}

// ---------- state transitions --------------------------------------------

void PhoneTimer::enterIdle(bool clearEntry) {
	if(clearEntry) {
		for(uint8_t i = 0; i <= EntryDigits; ++i) entry[i] = 0;
		entryLen = 0;
	}
	mode           = Mode::Idle;
	remainingMs    = 0;
	runStartRemain = 0;
	runStartMs     = 0;
	pulseHi        = false;
	stopTickTimer();
	stopPulseTimer();
	Ringtone.stop();
	refreshReadout();
	refreshSoftKeys();
	refreshHint();
}

void PhoneTimer::enterRunning() {
	// remainingMs / presetMs must already be set by the caller.
	if(remainingMs == 0) {
		// Defensive: never start a 0 ms countdown -- it would fire the
		// alarm immediately, which is not what the user asked for.
		enterIdle(false);
		return;
	}
	runStartMs     = (uint32_t) millis();
	runStartRemain = remainingMs;
	mode           = Mode::Running;
	startTickTimer();
	refreshReadout();
	refreshSoftKeys();
	refreshHint();
}

void PhoneTimer::enterPaused() {
	// Snapshot the live remaining-ms so the readout and the math agree
	// across the pause boundary.
	remainingMs = currentRemainingMs();
	mode        = Mode::Paused;
	stopTickTimer();
	refreshReadout();
	refreshSoftKeys();
	refreshHint();
}

void PhoneTimer::enterRinging() {
	mode        = Mode::Ringing;
	remainingMs = 0;
	stopTickTimer();
	pulseHi = false;
	startPulseTimer();
	// Engine respects Settings.sound -- a muted device gets the visual
	// pulse without any piezo output.
	Ringtone.play(AlarmMelody);
	refreshReadout();
	refreshSoftKeys();
	refreshHint();
}

// ---------- entry buffer --------------------------------------------------

void PhoneTimer::appendDigit(char c) {
	if(mode != Mode::Idle) return;
	if(c < '0' || c > '9') return;

	// Reject leading zeros so "0" then "5" reads as "5" rather than "05".
	if(entryLen == 0 && c == '0') return;

	if(entryLen < EntryDigits) {
		entry[entryLen++] = c;
		entry[entryLen]   = '\0';
	} else {
		// Buffer is full: shift left and drop the oldest digit. This
		// gives the user a feature-phone-style "scrolling" entry where
		// new digits push off the front. EntryDigits is small (4) so a
		// memmove is overkill; an explicit loop is just as clear.
		for(uint8_t i = 1; i < EntryDigits; ++i) entry[i - 1] = entry[i];
		entry[EntryDigits - 1] = c;
		entry[EntryDigits]     = '\0';
	}
	refreshReadout();
}

void PhoneTimer::backspaceEntry() {
	if(mode != Mode::Idle) return;
	if(entryLen == 0) return;
	entryLen--;
	entry[entryLen] = '\0';
	refreshReadout();
}

void PhoneTimer::clearAll() {
	enterIdle(true);
}

// ---------- softkey actions ----------------------------------------------

void PhoneTimer::primaryAction() {
	switch(mode) {
		case Mode::Idle: {
			const uint32_t ms = entryToMs(entry, entryLen);
			if(ms == 0) return;        // nothing to start
			presetMs    = ms;
			remainingMs = ms;
			enterRunning();
			break;
		}
		case Mode::Running:
			enterPaused();
			break;
		case Mode::Paused:
			enterRunning();
			break;
		case Mode::Ringing:
			// Dismiss -- restore the original preset so the user can
			// re-arm with a single START. This matches every kitchen
			// timer the author has ever used.
			remainingMs = presetMs;
			enterIdle(false);
			break;
	}
}

void PhoneTimer::secondaryAction() {
	switch(mode) {
		case Mode::Idle:
			// Right softkey in Idle is a backspace (one digit at a time).
			// An explicit clear-all is exposed via long-press if the user
			// really wants it -- the common case is "I mistyped the last
			// digit".
			backspaceEntry();
			break;
		case Mode::Running:
		case Mode::Paused:
			// RESET -- preserve the entry buffer so the user can re-fire
			// the same preset. This is the feature-phone convention: the
			// preset survives a reset so re-arming is instant.
			remainingMs = 0;
			enterIdle(false);
			break;
		case Mode::Ringing:
			// Same dismiss behaviour as the left softkey in this mode.
			remainingMs = presetMs;
			enterIdle(false);
			break;
	}
}

// ---------- timers --------------------------------------------------------

void PhoneTimer::startTickTimer() {
	if(tickTimer != nullptr) return;  // idempotent
	tickTimer = lv_timer_create(onTickStatic, TickPeriodMs, this);
}

void PhoneTimer::stopTickTimer() {
	if(tickTimer == nullptr) return;
	lv_timer_del(tickTimer);
	tickTimer = nullptr;
}

void PhoneTimer::startPulseTimer() {
	if(pulseTimer != nullptr) return;
	pulseTimer = lv_timer_create(onPulseStatic, PulsePeriodMs, this);
}

void PhoneTimer::stopPulseTimer() {
	if(pulseTimer == nullptr) return;
	lv_timer_del(pulseTimer);
	pulseTimer = nullptr;
}

void PhoneTimer::onTickStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneTimer*>(timer->user_data);
	if(self == nullptr) return;
	if(self->mode != Mode::Running) return;

	const uint32_t remain = self->currentRemainingMs();
	self->refreshReadout();
	if(remain == 0) {
		self->enterRinging();
	}
}

void PhoneTimer::onPulseStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneTimer*>(timer->user_data);
	if(self == nullptr) return;
	if(self->mode != Mode::Ringing) return;

	self->pulseHi = !self->pulseHi;
	self->refreshReadout();
	self->refreshHint();
}

// ---------- input ---------------------------------------------------------

void PhoneTimer::buttonPressed(uint i) {
	// Digit keys: only consumed in Idle mode (to compose the duration).
	// In every other mode they are ignored so a stray digit press during
	// a countdown does not retroactively rewrite the preset.
	if(mode == Mode::Idle) {
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

	// In Ringing, ANY non-BACK key dismisses. This matches the
	// PhoneIncomingCall convention and stops a panicking user from
	// having to hunt for the right softkey.
	if(mode == Mode::Ringing) {
		if(i != BTN_BACK) {
			if(softKeys) {
				if(i == BTN_LEFT || i == BTN_L || i == BTN_ENTER) softKeys->flashLeft();
				else if(i == BTN_RIGHT || i == BTN_R)             softKeys->flashRight();
			}
			primaryAction();   // dismiss
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
			// Defer the actual short-press action to buttonReleased so a
			// long-press cannot double-fire on key release. The flash
			// gives instant visual feedback either way.
			if(softKeys) softKeys->flashRight();
			backLongFired = false;
			break;

		case BTN_R:
			// R bumper is a dedicated short-press alias -- no long-press
			// branch on the bumpers, so we fire immediately.
			if(softKeys) softKeys->flashRight();
			secondaryAction();
			break;

		case BTN_BACK:
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneTimer::buttonReleased(uint i) {
	switch(i) {
		case BTN_RIGHT:
			if(!backLongFired) {
				secondaryAction();
			}
			backLongFired = false;
			break;

		case BTN_BACK:
			if(!backLongFired) {
				pop();
			}
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneTimer::buttonHeld(uint i) {
	switch(i) {
		case BTN_BACK:
			// Long-press BACK exits the screen too. State is preserved
			// so the user comes back to whatever they last had typed.
			backLongFired = true;
			pop();
			break;

		case BTN_RIGHT:
			// Long-press on the right softkey clears the entire entry
			// buffer in Idle mode (a quick "AC"), while in any active
			// mode it falls through to the same RESET secondaryAction
			// would have produced. The flag suppresses the matching
			// short-press release fire-back.
			backLongFired = true;
			if(mode == Mode::Idle) {
				clearAll();
			} else {
				secondaryAction();
			}
			break;

		default:
			break;
	}
}
