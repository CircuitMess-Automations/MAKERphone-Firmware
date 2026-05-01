#include "PhoneStopwatch.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - kept identical to every other Phone* widget so
// the stopwatch slots in beside PhoneCalculator (S60) / PhoneAboutScreen /
// the dialer family without a visual seam. Inlined per the established
// pattern (see PhoneCalculator.cpp / PhoneAboutScreen.cpp).
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan caption
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream readout
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim purple captions
#define MP_ACCENT          lv_color_make(255, 140, 30)   // sunset (lap split)

// ---------- geometry ------------------------------------------------------

// "STOPWATCH" header in the same row PhoneSettingsScreen / PhoneAboutScreen
// use for their captions. Sits under the 10 px PhoneStatusBar.
static constexpr lv_coord_t kCaptionY      = 12;

// Big elapsed readout. pixelbasic16 glyphs are 8 px wide; "00:00.00" is
// 8 chars wide so the label is ~64 px. Centred on a 160 px display.
static constexpr lv_coord_t kElapsedY      = 24;
static constexpr lv_coord_t kElapsedH      = 16;

// Lap list. Caption + 4 rows of 9 px each, fitting between the elapsed
// readout (y = 40) and the soft-key bar (y = 118). Total budget: 78 px.
//   y = 46    "LAPS" caption
//   y = 56    L4 ...     (newest)
//   y = 66    L3 ...
//   y = 76    L2 ...
//   y = 86    L1 ...     (oldest visible)
//   y = 118   soft-key bar
// Leaves 24 px of breathing room above the soft-keys.
static constexpr lv_coord_t kLapsCaptionY  = 46;
static constexpr lv_coord_t kLapsTopY      = 56;
static constexpr lv_coord_t kLapRowH       = 10;
static constexpr lv_coord_t kLapRowX       = 6;
static constexpr lv_coord_t kLapRowW       = 148;

// ---------- ctor / dtor ---------------------------------------------------

PhoneStopwatch::PhoneStopwatch()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  elapsedLabel(nullptr),
		  lapsCaption(nullptr) {

	// Zero the row-pointer table so any early refresh before buildLapList()
	// returns is a no-op rather than a UB read. Same defensive zeroing
	// the calculator does on its `cells[]` / `cellLabels[]` arrays.
	for(uint8_t i = 0; i < MaxLaps; ++i) lapRows[i] = nullptr;

	// Full-screen container, no scrollbars, no padding -- same blank-canvas
	// pattern PhoneCalculator / PhoneAboutScreen / PhoneDialerScreen use.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper at the bottom of the z-order so the labels overlay it
	// cleanly. Synthwave keeps the screen feeling like part of the
	// MAKERphone family rather than a debug terminal.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px).
	statusBar = new PhoneStatusBar(obj);

	buildCaption();
	buildElapsed();
	buildLapList();

	// Bottom soft-key bar. Labels are mode-driven (refreshSoftKeys()
	// rewrites them on every state change) -- here we just set the
	// initial Idle pair so the user sees something useful on push.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("START");
	softKeys->setRight("");

	// Long-press detection on BTN_BACK so a hold can act differently
	// from a short tap. We don't need it here right now (BACK both
	// short and long exit) but keeping the same threshold the rest of
	// the shell uses leaves us a future hook with no behaviour change.
	setButtonHoldTime(BTN_BACK, BackHoldMs);
	setButtonHoldTime(BTN_RIGHT, BackHoldMs);

	// Initial paint -- show "00:00.00" rather than a blank readout.
	refreshElapsed();
	refreshLapList();
}

PhoneStopwatch::~PhoneStopwatch() {
	stopTickTimer();
	// All other children (wallpaper, statusBar, softKeys, labels) are
	// parented to obj and freed by the LVScreen base destructor.
}

void PhoneStopwatch::onStart() {
	Input::getInstance()->addListener(this);
	// If the user navigates away and back while paused, we want the
	// readout to still reflect the saved accumulator. If they navigated
	// away while running, time keeps advancing in the background (we
	// pin runStartMs to a wall-clock start), so resuming the tick on
	// re-entry naturally brings the display up to date.
	if(mode == Mode::Running) {
		startTickTimer();
	}
	refreshElapsed();
	refreshLapList();
	refreshSoftKeys();
}

void PhoneStopwatch::onStop() {
	Input::getInstance()->removeListener(this);
	// Leave run state intact -- a paused timer should still be paused
	// when the user comes back. We only stop the LVGL tick because
	// LVGL timers must not survive a screen detach (they would target
	// a stale `this` if the screen is torn down before the timer fires).
	stopTickTimer();
}

// ---------- builders ------------------------------------------------------

void PhoneStopwatch::buildCaption() {
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "STOPWATCH");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, kCaptionY);
}

void PhoneStopwatch::buildElapsed() {
	elapsedLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(elapsedLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(elapsedLabel, MP_TEXT, 0);
	// Centre the readout. We anchor by top-mid so the y position is
	// fixed even when the text width changes (a "00:00.00" string is
	// always 8 chars in this font, but defensive anchoring is cheap).
	lv_obj_set_align(elapsedLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(elapsedLabel, kElapsedY);
	lv_label_set_text(elapsedLabel, "00:00.00");
}

void PhoneStopwatch::buildLapList() {
	// Section caption -- shown only when at least one lap exists, so the
	// idle screen reads as a single big stopwatch rather than a half-empty
	// list.
	lapsCaption = lv_label_create(obj);
	lv_obj_set_style_text_font(lapsCaption, &pixelbasic7, 0);
	lv_obj_set_style_text_color(lapsCaption, MP_LABEL_DIM, 0);
	lv_label_set_text(lapsCaption, "LAPS");
	lv_obj_set_pos(lapsCaption, kLapRowX, kLapsCaptionY);
	lv_obj_add_flag(lapsCaption, LV_OBJ_FLAG_HIDDEN);

	// Four lap rows, top -> bottom. Newest sits at row 0 (top), older
	// laps push toward the bottom of the visible band. Each row uses
	// pixelbasic7 so all three columns ("L#  split  total") fit in
	// the 148 px row width.
	for(uint8_t i = 0; i < MaxLaps; ++i) {
		lv_obj_t* row = lv_label_create(obj);
		lv_obj_set_style_text_font(row, &pixelbasic7, 0);
		lv_obj_set_style_text_color(row, MP_TEXT, 0);
		lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
		lv_obj_set_width(row, kLapRowW);
		lv_label_set_text(row, "");
		lv_obj_set_pos(row, kLapRowX, kLapsTopY + i * kLapRowH);
		lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
		lapRows[i] = row;
	}
}

// ---------- elapsed math --------------------------------------------------

uint32_t PhoneStopwatch::currentElapsedMs() const {
	if(mode == Mode::Running) {
		// uint32_t subtraction handles a millis() rollover correctly so
		// long as the running interval is shorter than 2^31 ms (~24.8
		// days). Realistic stopwatch sessions are nowhere near that.
		const uint32_t now = (uint32_t) millis();
		return baseElapsedMs + (now - runStartMs);
	}
	return baseElapsedMs;
}

void PhoneStopwatch::formatElapsed(uint32_t ms, char* out, size_t outLen) {
	if(out == nullptr || outLen == 0) return;

	// Clamp at MaxMinutes:59.99 so the label never overflows the 152 px
	// display band. Pegging at the cap is a friendlier failure mode than
	// wrapping back to 00:00.00 (which would imply the user just hit
	// reset, which they did not).
	const uint32_t cs    = (ms / 10u) % 100u;
	const uint32_t total = ms / 1000u;
	uint32_t       mm    = total / 60u;
	const uint32_t ss    = total % 60u;
	if(mm > MaxMinutes) {
		snprintf(out, outLen, "99:59.99");
		return;
	}
	snprintf(out, outLen, "%02u:%02u.%02u",
			 (unsigned) mm, (unsigned) ss, (unsigned) cs);
}

// ---------- repaint -------------------------------------------------------

void PhoneStopwatch::refreshElapsed() {
	if(elapsedLabel == nullptr) return;
	char buf[12];
	formatElapsed(currentElapsedMs(), buf, sizeof(buf));
	lv_label_set_text(elapsedLabel, buf);
}

void PhoneStopwatch::refreshLapList() {
	// Hide the caption + every row when there are no laps, so the idle
	// screen reads as a clean watch face.
	if(lapsCaption != nullptr) {
		if(lapCount == 0) {
			lv_obj_add_flag(lapsCaption, LV_OBJ_FLAG_HIDDEN);
		} else {
			lv_obj_clear_flag(lapsCaption, LV_OBJ_FLAG_HIDDEN);
		}
	}

	for(uint8_t i = 0; i < MaxLaps; ++i) {
		if(lapRows[i] == nullptr) continue;
		if(i >= lapCount) {
			lv_obj_add_flag(lapRows[i], LV_OBJ_FLAG_HIDDEN);
			continue;
		}
		lv_obj_clear_flag(lapRows[i], LV_OBJ_FLAG_HIDDEN);

		// L# label = the lap's monotonic number from the user's POV
		// (1 = first lap they recorded). The newest entry is at i = 0
		// in the buffer, so its number is totalLapsTaken; the oldest
		// visible lap is totalLapsTaken - (lapCount - 1).
		const uint32_t labelNum = totalLapsTaken - i;

		char split[12];
		char total[12];
		formatElapsed(laps[i].splitMs, split, sizeof(split));
		formatElapsed(laps[i].totalMs, total, sizeof(total));

		// Format: "L#  SPLIT   TOTAL" -- the split column reads as the
		// payload (this lap took this long) and the total as context
		// (the watch read this much when the lap was captured). 18
		// chars max, well within the 148 px row width.
		char line[40];
		snprintf(line, sizeof(line), "L%-2u %s  %s",
				 (unsigned) labelNum, split, total);
		lv_label_set_text(lapRows[i], line);
	}
}

void PhoneStopwatch::refreshSoftKeys() {
	if(softKeys == nullptr) return;

	switch(mode) {
		case Mode::Idle:
			softKeys->setLeft("START");
			// Idle right softkey is intentionally blank -- nothing to lap,
			// nothing to reset. Pressing it still flashes for tactile
			// feedback (lapOrReset() is a no-op in Idle).
			softKeys->setRight("");
			break;
		case Mode::Running:
			softKeys->setLeft("STOP");
			softKeys->setRight("LAP");
			break;
		case Mode::Paused:
			softKeys->setLeft("START");
			softKeys->setRight("RESET");
			break;
	}
}

// ---------- state transitions --------------------------------------------

void PhoneStopwatch::enterIdle() {
	mode          = Mode::Idle;
	baseElapsedMs = 0;
	runStartMs    = 0;
	lastLapTotal  = 0;
	stopTickTimer();
	refreshElapsed();
	refreshSoftKeys();
}

void PhoneStopwatch::enterRunning() {
	// Pin the running-start to "now". From this moment, the live elapsed
	// readout is baseElapsedMs + (millis() - runStartMs).
	runStartMs = (uint32_t) millis();
	mode       = Mode::Running;
	startTickTimer();
	refreshElapsed();
	refreshSoftKeys();
}

void PhoneStopwatch::enterPaused() {
	// Freeze the accumulator at the value we're showing now, so the
	// label and the math agree. After this call millis() is no longer
	// consulted until the next start.
	baseElapsedMs = currentElapsedMs();
	mode          = Mode::Paused;
	stopTickTimer();
	refreshElapsed();
	refreshSoftKeys();
}

void PhoneStopwatch::recordLap() {
	if(mode != Mode::Running) return;

	const uint32_t total = currentElapsedMs();
	const uint32_t split = total - lastLapTotal;
	lastLapTotal         = total;
	totalLapsTaken      += 1;

	// Push onto the front of the buffer. Older entries shift down by one
	// slot; the eldest (at MaxLaps-1) falls off the visible window. The
	// monotonic totalLapsTaken counter keeps the L# labels truthful even
	// when the visible buffer wraps.
	const uint8_t copyCount = (lapCount < MaxLaps) ? lapCount : (uint8_t) (MaxLaps - 1);
	for(int8_t i = (int8_t) copyCount; i > 0; --i) {
		laps[i] = laps[i - 1];
	}
	laps[0].splitMs = split;
	laps[0].totalMs = total;
	if(lapCount < MaxLaps) lapCount += 1;

	refreshLapList();
}

void PhoneStopwatch::resetAll() {
	// Wipe both the time accumulator and the lap history. Counters that
	// drive labels (totalLapsTaken) reset too so the next L# starts at 1.
	for(uint8_t i = 0; i < MaxLaps; ++i) {
		laps[i].splitMs = 0;
		laps[i].totalMs = 0;
	}
	lapCount       = 0;
	totalLapsTaken = 0;
	enterIdle();
	refreshLapList();
}

void PhoneStopwatch::toggleRun() {
	switch(mode) {
		case Mode::Idle:    enterRunning(); break;
		case Mode::Running: enterPaused();  break;
		case Mode::Paused:  enterRunning(); break;
	}
}

void PhoneStopwatch::lapOrReset() {
	if(mode == Mode::Running) {
		recordLap();
	} else if(mode == Mode::Paused) {
		resetAll();
	}
	// Idle: no-op (still flashed by the caller for tactile cue).
}

// ---------- timer ---------------------------------------------------------

void PhoneStopwatch::startTickTimer() {
	if(tickTimer != nullptr) return;  // idempotent
	tickTimer = lv_timer_create(onTickStatic, TickPeriodMs, this);
}

void PhoneStopwatch::stopTickTimer() {
	if(tickTimer == nullptr) return;
	lv_timer_del(tickTimer);
	tickTimer = nullptr;
}

void PhoneStopwatch::onTickStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneStopwatch*>(timer->user_data);
	if(self == nullptr) return;
	self->refreshElapsed();
}

// ---------- input ---------------------------------------------------------

void PhoneStopwatch::buttonPressed(uint i) {
	switch(i) {
		case BTN_LEFT:
			if(softKeys) softKeys->flashLeft();
			toggleRun();
			break;

		case BTN_L:
			// Bumper alias for the left softkey. Same toggle behaviour;
			// the softkey flash matches so the user sees consistent
			// feedback regardless of which physical key they pressed.
			if(softKeys) softKeys->flashLeft();
			toggleRun();
			break;

		case BTN_ENTER:
			// Centre A button is the muscle-memory "confirm" on every
			// other Phone* screen, so it acts as the START/STOP toggle
			// here too.
			if(softKeys) softKeys->flashLeft();
			toggleRun();
			break;

		case BTN_RIGHT:
			// Defer the actual short-press action to buttonReleased so a
			// long-press exit cannot double-fire on key release. The
			// flash gives instant visual feedback either way.
			if(softKeys) softKeys->flashRight();
			backLongFired = false;
			break;

		case BTN_R:
			// R bumper aliases the LAP/RESET softkey. No long-press
			// branch here -- it's a dedicated action key.
			if(softKeys) softKeys->flashRight();
			lapOrReset();
			break;

		case BTN_BACK:
			// Hardware BACK button: short-press exits, long-press also
			// exits (both via buttonReleased / buttonHeld). State is
			// preserved across exit so a paused timer is still paused
			// next time the user opens the screen.
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneStopwatch::buttonReleased(uint i) {
	switch(i) {
		case BTN_RIGHT:
			if(!backLongFired) {
				lapOrReset();
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

void PhoneStopwatch::buttonHeld(uint i) {
	switch(i) {
		case BTN_BACK:
			// Long-press BACK exits the screen too. Same behaviour as a
			// short tap today; the explicit branch is here so a future
			// "hold to reset" tweak has a clean hook without restructuring
			// the input handler.
			backLongFired = true;
			pop();
			break;

		case BTN_RIGHT:
			// Long-press on the right softkey is intentionally idle: a
			// short tap already triggers LAP / RESET, and we don't want
			// a held key to spam laps. The flag suppresses the matching
			// short-press release fire-back.
			backLongFired = true;
			break;

		default:
			break;
	}
}
