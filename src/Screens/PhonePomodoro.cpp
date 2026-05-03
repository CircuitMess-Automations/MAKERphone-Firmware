#include "PhonePomodoro.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <Notes.h>
#include <stdio.h>
#include <string.h>
#include <nvs.h>
#include <esp_log.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../Services/PhoneRingtoneEngine.h"

// MAKERphone retro palette — kept identical to every other Phone* widget
// so the Pomodoro screen slots in beside PhoneTodo (S136) / PhoneHabits
// (S137) / PhoneTimer (S62) / PhoneTimers (S125) without a visual seam.
// Inlined per the established pattern (see PhoneTimer.cpp / PhoneHabits.cpp).
#define MP_HIGHLIGHT     lv_color_make(122, 232, 255)  // cyan caption / hi-pulse
#define MP_TEXT          lv_color_make(255, 220, 180)  // warm cream readout
#define MP_LABEL_DIM     lv_color_make(170, 140, 200)  // dim purple hint line
#define MP_ACCENT        lv_color_make(255, 140,  30)  // sunset orange (alarm)
#define MP_ACCENT_BRIGHT lv_color_make(255, 200,  80)  // bright sunset (alarm pulse hi)
#define MP_DIM           lv_color_make( 70,  56, 100)  // muted purple (idle / non-cursor)
#define MP_RUN           lv_color_make(122, 232, 180)  // soft mint-green (running)
#define MP_BREAK         lv_color_make(140, 200, 255)  // pale-blue (break phases)

// ---------- alarm melody --------------------------------------------------
//
// Same shape as PhoneTimer's TimerAlm so a Pomodoro phase-end alert is
// sonically continuous with the rest of the time-keeping family — users
// learn one alarm sound, period. 4-note rising arpeggio that loops while
// the screen is in Ringing state. Lives in flash because the array is
// `static const`.
static const PhoneRingtoneEngine::Note PomodoroAlarmNotes[] = {
		{ NOTE_E5, 220 },
		{ NOTE_A5, 220 },
		{ NOTE_E5, 220 },
		{ NOTE_A5, 220 },
};

static const PhoneRingtoneEngine::Melody PomodoroAlarmMelody = {
		PomodoroAlarmNotes,
		sizeof(PomodoroAlarmNotes) / sizeof(PomodoroAlarmNotes[0]),
		60,        // gapMs
		true,      // loop until dismissed
		"PomoAlm",
};

// ---------- NVS persistence ----------------------------------------------
//
// 8-byte blob in the "mppomo" namespace. Header (4 bytes) + the four
// uint8_t fields. Mirrors the PhoneVirtualPet (S129) / PhoneAlarmService
// lazy-open pattern so we never spam nvs_open() retries on a partition
// error.

namespace {

constexpr const char* kNamespace = "mppomo";
constexpr const char* kBlobKey   = "c";

constexpr uint8_t kMagic0  = 'M';
constexpr uint8_t kMagic1  = 'P';
constexpr uint8_t kVersion = 1;

constexpr size_t  kBlobSize = 8;

nvs_handle s_handle    = 0;
bool       s_attempted = false;

bool ensureOpen() {
	if(s_handle != 0) return true;
	if(s_attempted)   return false;
	s_attempted = true;
	auto err = nvs_open(kNamespace, NVS_READWRITE, &s_handle);
	if(err != ESP_OK) {
		ESP_LOGW("Pomodoro",
		         "nvs_open(%s) failed: %d -- pomodoro runs without persistence",
		         kNamespace, (int)err);
		s_handle = 0;
		return false;
	}
	return true;
}

uint8_t clampU8(uint8_t v, uint8_t lo, uint8_t hi) {
	if(v < lo) return lo;
	if(v > hi) return hi;
	return v;
}

} // namespace

// ---------- geometry ------------------------------------------------------
//
// Layout matches the other organiser apps so the user feels the same
// "shape" of screen behind every Phase-Q tool. Status bar at y=0 (10 px),
// caption at y=12, phase line at y=22, big readout at y=36, cycles
// counter at y=68, next-phase hint at y=82, mode hint at y=96, soft-key
// bar at y=118.

static constexpr lv_coord_t kCaptionY    = 12;
static constexpr lv_coord_t kPhaseY      = 22;
static constexpr lv_coord_t kReadoutY    = 36;
static constexpr lv_coord_t kCyclesY     = 70;
static constexpr lv_coord_t kNextY       = 84;
static constexpr lv_coord_t kHintY       = 100;
static constexpr lv_coord_t kHintLeft    = 6;
static constexpr lv_coord_t kHintW       = 148;

// Config view geometry — four 14 px-tall rows starting at y=28, mode
// hint just above the soft-key bar.
static constexpr lv_coord_t kCfgCaptionY = 12;
static constexpr lv_coord_t kCfgRowTopY  = 28;
static constexpr lv_coord_t kCfgRowH     = 14;
static constexpr lv_coord_t kCfgRowLeft  = 6;
static constexpr lv_coord_t kCfgRowW     = 148;
static constexpr lv_coord_t kCfgHintY    = 100;

// Config-row caption strings. Order matches PhonePomodoro::ConfigFields.
static const char* kCfgLabels[] = {
		"WORK",
		"SHORT BREAK",
		"LONG BREAK",
		"EVERY",
};

// ---------- ctor / dtor ---------------------------------------------------

PhonePomodoro::PhonePomodoro()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  phaseLabel(nullptr),
		  readoutLabel(nullptr),
		  cyclesLabel(nullptr),
		  nextLabel(nullptr),
		  hintLabel(nullptr),
		  configCaption(nullptr),
		  configHint(nullptr) {

	for(uint8_t i = 0; i < ConfigFields; ++i) configRows[i] = nullptr;

	// Full-screen container, no scrollbars, no padding — the same blank
	// canvas pattern PhoneTimer / PhoneTimers / PhoneAlarmClock use.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px).
	statusBar = new PhoneStatusBar(obj);

	// Pull persisted config (or fall back to defaults). The working
	// copy starts in sync with the committed config so opening Config
	// view never shows stale numbers.
	loadConfig();
	editConfig = config;

	buildTimerView();
	buildConfigView();

	// Initial preset matches the current phase so the readout is a
	// duration the user could actually start, rather than 00:00.
	presetMs    = phaseDurationMs(phase);
	remainingMs = presetMs;

	// Bottom soft-key bar; populated by refreshSoftKeys() per mode.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("START");
	softKeys->setRight("CONFIG");

	// Long-press threshold matches the rest of the MAKERphone shell.
	setButtonHoldTime(BTN_BACK, BackHoldMs);
	setButtonHoldTime(BTN_RIGHT, BackHoldMs);

	refreshAll();
}

PhonePomodoro::~PhonePomodoro() {
	stopTickTimer();
	stopPulseTimer();
	// Always make sure the global ringtone engine is silent on tear-down,
	// otherwise an alarm started by this instance could keep ringing
	// after the screen is destroyed.
	Ringtone.stop();
	// All other children (wallpaper, statusBar, softKeys, labels) are
	// parented to obj and freed by the LVScreen base destructor.
}

void PhonePomodoro::onStart() {
	Input::getInstance()->addListener(this);

	// Re-arm the appropriate timers on re-entry. If the user navigated
	// away while running, the countdown was paused implicitly (we stop
	// the tick on detach so a freed instance does not get fired
	// against). Treat that as a continuation rather than silently
	// losing time: rebase runStartMs to the current millis() so the
	// user sees no stale value flash up.
	if(runState == State::Running) {
		runStartMs     = (uint32_t) millis();
		runStartRemain = remainingMs;
		startTickTimer();
	} else if(runState == State::Ringing) {
		// Re-fire the alarm + pulse on re-entry. The alarm was silenced
		// by onStop() to keep the ringtone engine clean while the
		// screen was detached.
		Ringtone.play(PomodoroAlarmMelody);
		startPulseTimer();
	}

	refreshAll();
}

void PhonePomodoro::onStop() {
	Input::getInstance()->removeListener(this);

	// Snapshot remaining-ms now, since LVGL timers must not survive a
	// screen detach (they would target a stale `this` if the screen
	// is torn down before the timer fires) and we want the user to
	// come back to a coherent value.
	if(runState == State::Running) {
		remainingMs = currentRemainingMs();
	}

	stopTickTimer();
	stopPulseTimer();

	// Silence the alarm if we were ringing. The state stays Ringing so
	// onStart() can re-fire the audio + pulse on re-entry.
	Ringtone.stop();
}

// ---------- builders ------------------------------------------------------

void PhonePomodoro::buildTimerView() {
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "POMODORO");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, kCaptionY);

	phaseLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(phaseLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(phaseLabel, MP_TEXT, 0);
	lv_label_set_text(phaseLabel, "WORK 0/4");
	lv_obj_set_align(phaseLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(phaseLabel, kPhaseY);

	readoutLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(readoutLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(readoutLabel, MP_TEXT, 0);
	lv_label_set_text(readoutLabel, "25:00");
	lv_obj_set_align(readoutLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(readoutLabel, kReadoutY);

	cyclesLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(cyclesLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(cyclesLabel, MP_LABEL_DIM, 0);
	lv_obj_set_style_text_align(cyclesLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(cyclesLabel, "CYCLES TODAY: 0");
	lv_obj_set_width(cyclesLabel, kHintW);
	lv_obj_set_pos(cyclesLabel, kHintLeft, kCyclesY);

	nextLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(nextLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(nextLabel, MP_LABEL_DIM, 0);
	lv_obj_set_style_text_align(nextLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(nextLabel, "NEXT: SHORT BREAK");
	lv_obj_set_width(nextLabel, kHintW);
	lv_obj_set_pos(nextLabel, kHintLeft, kNextY);

	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_obj_set_style_text_align(hintLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_long_mode(hintLabel, LV_LABEL_LONG_DOT);
	lv_obj_set_width(hintLabel, kHintW);
	lv_label_set_text(hintLabel, "");
	lv_obj_set_pos(hintLabel, kHintLeft, kHintY);
}

void PhonePomodoro::buildConfigView() {
	configCaption = lv_label_create(obj);
	lv_obj_set_style_text_font(configCaption, &pixelbasic7, 0);
	lv_obj_set_style_text_color(configCaption, MP_HIGHLIGHT, 0);
	lv_label_set_text(configCaption, "SETTINGS");
	lv_obj_set_align(configCaption, LV_ALIGN_TOP_MID);
	lv_obj_set_y(configCaption, kCfgCaptionY);
	lv_obj_add_flag(configCaption, LV_OBJ_FLAG_HIDDEN);

	for(uint8_t i = 0; i < ConfigFields; ++i) {
		configRows[i] = lv_label_create(obj);
		lv_obj_set_style_text_font(configRows[i], &pixelbasic7, 0);
		lv_obj_set_style_text_color(configRows[i], MP_TEXT, 0);
		lv_obj_set_style_text_align(configRows[i], LV_TEXT_ALIGN_LEFT, 0);
		lv_label_set_long_mode(configRows[i], LV_LABEL_LONG_DOT);
		lv_obj_set_width(configRows[i], kCfgRowW);
		lv_obj_set_pos(configRows[i], kCfgRowLeft,
		               (lv_coord_t)(kCfgRowTopY + i * kCfgRowH));
		lv_label_set_text(configRows[i], "");
		lv_obj_add_flag(configRows[i], LV_OBJ_FLAG_HIDDEN);
	}

	configHint = lv_label_create(obj);
	lv_obj_set_style_text_font(configHint, &pixelbasic7, 0);
	lv_obj_set_style_text_color(configHint, MP_LABEL_DIM, 0);
	lv_obj_set_style_text_align(configHint, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(configHint, kHintW);
	lv_label_set_text(configHint, "4/6 ADJUST   2/8 MOVE");
	lv_obj_set_pos(configHint, kHintLeft, kCfgHintY);
	lv_obj_add_flag(configHint, LV_OBJ_FLAG_HIDDEN);
}

// ---------- formatting / pure helpers ------------------------------------

void PhonePomodoro::formatRemaining(uint32_t ms, char* out, size_t outLen) {
	if(out == nullptr || outLen == 0) return;

	// Round up to the next whole second so a count-down never lingers
	// on "00:00" before triggering the alarm — same rule PhoneTimer
	// uses, so the family stays consistent.
	const uint32_t totalSec = (ms + 999u) / 1000u;
	uint32_t mm = totalSec / 60u;
	const uint32_t ss = totalSec % 60u;
	if(mm > 99u) {
		snprintf(out, outLen, "99:59");
		return;
	}
	snprintf(out, outLen, "%02u:%02u", (unsigned) mm, (unsigned) ss);
}

const char* PhonePomodoro::phaseName(Phase p) {
	switch(p) {
		case Phase::Work:       return "WORK";
		case Phase::ShortBreak: return "SHORT BREAK";
		case Phase::LongBreak:  return "LONG BREAK";
	}
	return "WORK";
}

PhonePomodoro::Phase PhonePomodoro::nextPhase(Phase ending,
                                              uint8_t cyclesDone,
                                              uint8_t cyclesPerLong) {
	// Defensive: a 0 long-break interval would divide-by-zero. Treat
	// that as "every cycle gets a long break", which keeps the math
	// well-defined and is a sensible behaviour given the user
	// somehow forced cyclesPerLong = 0.
	if(cyclesPerLong == 0) return Phase::LongBreak;

	if(ending == Phase::Work) {
		// Long break every Nth cycle. Caller has already credited
		// cyclesDone (i.e. it includes the cycle that just ended).
		if(cyclesDone > 0 && (cyclesDone % cyclesPerLong) == 0) {
			return Phase::LongBreak;
		}
		return Phase::ShortBreak;
	}
	// After any break we go back to Work.
	return Phase::Work;
}

uint32_t PhonePomodoro::phaseDurationMs(Phase p) const {
	uint32_t mins = 0;
	switch(p) {
		case Phase::Work:       mins = config.workMin;       break;
		case Phase::ShortBreak: mins = config.shortBreakMin; break;
		case Phase::LongBreak:  mins = config.longBreakMin;  break;
	}
	return mins * 60u * 1000u;
}

uint32_t PhonePomodoro::currentRemainingMs() const {
	if(runState == State::Running) {
		const uint32_t now     = (uint32_t) millis();
		const uint32_t elapsed = now - runStartMs;
		if(elapsed >= runStartRemain) return 0;
		return runStartRemain - elapsed;
	}
	return remainingMs;
}

// ---------- repaint -------------------------------------------------------

void PhonePomodoro::refreshAll() {
	const bool timerView = (mode == Mode::Timer);

	// Toggle visibility wholesale. LVGL has no group, so we flip each
	// child individually — cheap at this size.
	auto setVisible = [&](lv_obj_t* o, bool show) {
		if(o == nullptr) return;
		if(show) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
		else     lv_obj_add_flag(o,   LV_OBJ_FLAG_HIDDEN);
	};

	setVisible(captionLabel, timerView);
	setVisible(phaseLabel,   timerView);
	setVisible(readoutLabel, timerView);
	setVisible(cyclesLabel,  timerView);
	setVisible(nextLabel,    timerView);
	setVisible(hintLabel,    timerView);

	setVisible(configCaption, !timerView);
	for(uint8_t i = 0; i < ConfigFields; ++i) {
		setVisible(configRows[i], !timerView);
	}
	setVisible(configHint, !timerView);

	if(timerView) {
		refreshTimerView();
	} else {
		refreshConfigView();
	}
	refreshSoftKeys();
}

void PhonePomodoro::refreshTimerView() {
	if(phaseLabel == nullptr || readoutLabel == nullptr) return;

	// "WORK 1/4" — the dot-progress shows where the user is in the
	// current "set" of work cycles before the long break. The numerator
	// is "completed cycles in this set + 1 if Work is active", which
	// reads naturally as a 1-based progress indicator.
	char phaseBuf[24] = { 0 };
	const uint8_t cyclesPerLong = (config.cyclesPerLong == 0) ? 1 : config.cyclesPerLong;
	if(phase == Phase::Work) {
		uint8_t inSet = (uint8_t)(cyclesDone % cyclesPerLong) + 1u;
		if(inSet > cyclesPerLong) inSet = cyclesPerLong;
		snprintf(phaseBuf, sizeof(phaseBuf), "%s %u/%u",
		         phaseName(phase), (unsigned) inSet, (unsigned) cyclesPerLong);
	} else {
		snprintf(phaseBuf, sizeof(phaseBuf), "%s", phaseName(phase));
	}
	lv_label_set_text(phaseLabel, phaseBuf);

	// Phase label colour cues the user to the kind of phase they are
	// in: orange for Work, pale-blue for either break.
	lv_color_t phaseCol = (phase == Phase::Work) ? MP_ACCENT : MP_BREAK;
	lv_obj_set_style_text_color(phaseLabel, phaseCol, 0);

	// Big mm:ss readout.
	char readout[8];
	const uint32_t live = (runState == State::Running)
		? currentRemainingMs()
		: remainingMs;
	formatRemaining(live, readout, sizeof(readout));
	lv_label_set_text(readoutLabel, readout);

	lv_color_t readoutCol = MP_TEXT;
	switch(runState) {
		case State::Idle:    readoutCol = MP_TEXT;            break;
		case State::Running:
			readoutCol = (phase == Phase::Work) ? MP_RUN : MP_BREAK;
			break;
		case State::Paused:  readoutCol = MP_LABEL_DIM;       break;
		case State::Ringing:
			readoutCol = pulseHi ? MP_ACCENT_BRIGHT : MP_ACCENT;
			break;
	}
	lv_obj_set_style_text_color(readoutLabel, readoutCol, 0);

	// Cycles + next-phase hint.
	char cyclesBuf[24];
	snprintf(cyclesBuf, sizeof(cyclesBuf), "CYCLES TODAY: %u",
	         (unsigned) cyclesDone);
	lv_label_set_text(cyclesLabel, cyclesBuf);

	char nextBuf[28];
	if(runState == State::Ringing) {
		// In Ringing the "next" line is what dismissing will start.
		// Use the same nextPhase rule the dismiss path will run.
		Phase np = nextPhase(phase, cyclesDone, cyclesPerLong);
		snprintf(nextBuf, sizeof(nextBuf), "NEXT: %s", phaseName(np));
	} else {
		// Not ringing: preview what comes after the current phase ends.
		// For Work we have to predict cyclesDone+1 to call nextPhase
		// correctly, since the credit-then-pick rule wants the post-
		// credit count.
		uint8_t hypotheticalCycles = cyclesDone;
		if(phase == Phase::Work) hypotheticalCycles = (uint8_t)(cyclesDone + 1u);
		Phase np = nextPhase(phase, hypotheticalCycles, cyclesPerLong);
		snprintf(nextBuf, sizeof(nextBuf), "NEXT: %s", phaseName(np));
	}
	lv_label_set_text(nextLabel, nextBuf);

	// Mode-driven hint line.
	const char* hint = "";
	switch(runState) {
		case State::Idle:
			hint = (phase == Phase::Work) ? "FOCUS — STAY ON TASK"
			                              : "RELAX — TAKE A BREATH";
			break;
		case State::Running:
			hint = (phase == Phase::Work) ? "WORKING..." : "ON BREAK...";
			break;
		case State::Paused:
			hint = "PAUSED";
			break;
		case State::Ringing:
			hint = (phase == Phase::Work) ? "TIME FOR A BREAK"
			                              : "BREAK OVER";
			break;
	}
	lv_label_set_text(hintLabel, hint);

	// In Ringing the hint also flips with the pulse so the whole
	// screen reads as urgent. Other modes keep the dim caption colour.
	lv_color_t hintCol = MP_LABEL_DIM;
	if(runState == State::Ringing) {
		hintCol = pulseHi ? MP_ACCENT_BRIGHT : MP_ACCENT;
	}
	lv_obj_set_style_text_color(hintLabel, hintCol, 0);
}

void PhonePomodoro::refreshConfigView() {
	for(uint8_t i = 0; i < ConfigFields; ++i) {
		if(configRows[i] == nullptr) continue;

		char buf[32];
		const uint8_t v = fieldValue(i);
		const char prefix = (i == cursor) ? '>' : ' ';

		if(i == 3) {
			// "EVERY  N CYCLES" — the long-break frequency reads more
			// naturally as a sentence than as a bare number.
			snprintf(buf, sizeof(buf), "%c %-12s %2u CYCLES",
			         prefix, kCfgLabels[i], (unsigned) v);
		} else {
			snprintf(buf, sizeof(buf), "%c %-12s %2u MIN",
			         prefix, kCfgLabels[i], (unsigned) v);
		}
		lv_label_set_text(configRows[i], buf);

		// Cursor row glows accent; non-cursor rows are dim.
		lv_color_t col = (i == cursor) ? MP_ACCENT : MP_LABEL_DIM;
		lv_obj_set_style_text_color(configRows[i], col, 0);
	}
}

void PhonePomodoro::refreshSoftKeys() {
	if(softKeys == nullptr) return;

	if(mode == Mode::Config) {
		softKeys->setLeft("SAVE");
		softKeys->setRight("BACK");
		return;
	}

	switch(runState) {
		case State::Idle:
			softKeys->setLeft("START");
			softKeys->setRight("CONFIG");
			break;
		case State::Running:
			softKeys->setLeft("PAUSE");
			softKeys->setRight("RESET");
			break;
		case State::Paused:
			softKeys->setLeft("RESUME");
			softKeys->setRight("RESET");
			break;
		case State::Ringing:
			// Both softkeys advance the phase, just like PhoneTimer's
			// "DISMISS" pair — gives the user two ways to clear the
			// alarm and matches the PhoneIncomingCall (S24) "any key
			// dismisses" feel.
			softKeys->setLeft("NEXT");
			softKeys->setRight("NEXT");
			break;
	}
}

// ---------- run-state transitions ----------------------------------------

void PhonePomodoro::enterIdle() {
	runState       = State::Idle;
	pulseHi        = false;
	runStartMs     = 0;
	runStartRemain = 0;
	stopTickTimer();
	stopPulseTimer();
	Ringtone.stop();

	// Idle reloads the current phase preset so the readout shows a
	// startable duration. Re-entering Idle from a RESET path will
	// have already restored the preset; the assignment is harmless.
	presetMs    = phaseDurationMs(phase);
	remainingMs = presetMs;

	refreshTimerView();
	refreshSoftKeys();
}

void PhonePomodoro::enterRunning() {
	if(remainingMs == 0) {
		// Defensive: never start a 0 ms phase. Treat as a no-op rather
		// than firing the alarm immediately.
		enterIdle();
		return;
	}
	runStartMs     = (uint32_t) millis();
	runStartRemain = remainingMs;
	runState       = State::Running;
	startTickTimer();
	refreshTimerView();
	refreshSoftKeys();
}

void PhonePomodoro::enterPaused() {
	// Snapshot the live remaining-ms so the readout and the math agree
	// across the pause boundary.
	remainingMs = currentRemainingMs();
	runState    = State::Paused;
	stopTickTimer();
	refreshTimerView();
	refreshSoftKeys();
}

void PhonePomodoro::enterRinging() {
	runState    = State::Ringing;
	remainingMs = 0;
	stopTickTimer();
	pulseHi     = false;
	startPulseTimer();
	// Engine respects Settings.sound — a muted device gets the visual
	// pulse without any piezo output.
	Ringtone.play(PomodoroAlarmMelody);
	refreshTimerView();
	refreshSoftKeys();
}

void PhonePomodoro::advancePhase() {
	// Credit a completed Work cycle BEFORE picking the next phase so
	// nextPhase() gets the post-credit count it expects.
	if(phase == Phase::Work) {
		// Cap at 99 — cyclesDone is uint8_t and a Pomodoro session
		// long enough to overflow it has bigger problems than a
		// truncated counter, but saturating at a 2-digit value keeps
		// the readout legal.
		if(cyclesDone < 99) cyclesDone++;
	}

	const uint8_t cyclesPerLong =
		(config.cyclesPerLong == 0) ? 1 : config.cyclesPerLong;
	phase = nextPhase(phase, cyclesDone, cyclesPerLong);

	enterIdle();        // user pressed NEXT — we land in Idle on the new phase
}

// ---------- top-level mode transitions -----------------------------------

void PhonePomodoro::enterTimerMode() {
	mode = Mode::Timer;
	refreshAll();
}

void PhonePomodoro::enterConfigMode() {
	// Snapshot current config into the working copy. Any in-flight
	// session is implicitly paused on entering Config: a Running
	// phase becomes Paused so the user does not "lose" countdown
	// while editing.
	if(runState == State::Running) {
		enterPaused();
	}
	editConfig = config;
	cursor     = 0;
	mode       = Mode::Config;
	refreshAll();
}

// ---------- config nav ---------------------------------------------------

uint8_t PhonePomodoro::fieldValue(uint8_t idx) const {
	switch(idx) {
		case 0: return editConfig.workMin;
		case 1: return editConfig.shortBreakMin;
		case 2: return editConfig.longBreakMin;
		case 3: return editConfig.cyclesPerLong;
		default: return 0;
	}
}

void PhonePomodoro::setFieldValue(uint8_t idx, uint8_t v) {
	switch(idx) {
		case 0: editConfig.workMin       = v; break;
		case 1: editConfig.shortBreakMin = v; break;
		case 2: editConfig.longBreakMin  = v; break;
		case 3: editConfig.cyclesPerLong = v; break;
		default: break;
	}
}

uint8_t PhonePomodoro::fieldMin(uint8_t idx) const {
	switch(idx) {
		case 0: return WorkMinMin;
		case 1: return ShortBreakMinMin;
		case 2: return LongBreakMinMin;
		case 3: return CyclesPerLongMin;
		default: return 0;
	}
}

uint8_t PhonePomodoro::fieldMax(uint8_t idx) const {
	switch(idx) {
		case 0: return WorkMinMax;
		case 1: return ShortBreakMinMax;
		case 2: return LongBreakMinMax;
		case 3: return CyclesPerLongMax;
		default: return 0;
	}
}

void PhonePomodoro::cursorUp() {
	if(cursor == 0) cursor = (uint8_t)(ConfigFields - 1);
	else            cursor--;
	refreshConfigView();
}

void PhonePomodoro::cursorDown() {
	cursor = (uint8_t)((cursor + 1u) % ConfigFields);
	refreshConfigView();
}

void PhonePomodoro::adjustCurrent(int8_t delta) {
	if(cursor >= ConfigFields) return;
	const uint8_t lo = fieldMin(cursor);
	const uint8_t hi = fieldMax(cursor);
	int16_t v = (int16_t) fieldValue(cursor) + (int16_t) delta;
	if(v < (int16_t) lo) v = (int16_t) hi;     // wrap to top on underflow
	if(v > (int16_t) hi) v = (int16_t) lo;     // wrap to bottom on overflow
	setFieldValue(cursor, (uint8_t) v);
	refreshConfigView();
}

void PhonePomodoro::saveConfig() {
	// Belt-and-braces clamp before commit so a future edit path can
	// never let an out-of-range value into the persisted blob.
	editConfig.workMin       = clampU8(editConfig.workMin,
	                                   WorkMinMin, WorkMinMax);
	editConfig.shortBreakMin = clampU8(editConfig.shortBreakMin,
	                                   ShortBreakMinMin, ShortBreakMinMax);
	editConfig.longBreakMin  = clampU8(editConfig.longBreakMin,
	                                   LongBreakMinMin, LongBreakMinMax);
	editConfig.cyclesPerLong = clampU8(editConfig.cyclesPerLong,
	                                   CyclesPerLongMin, CyclesPerLongMax);

	config = editConfig;
	persistConfig();

	// Saving new durations resets any in-flight session: cyclesDone
	// stays (we don't want to throw away the user's progress for
	// the day) but the current phase rolls back to Work + Idle and
	// the readout reloads with the freshly-saved preset.
	phase    = Phase::Work;
	runState = State::Idle;
	stopTickTimer();
	stopPulseTimer();
	Ringtone.stop();
	presetMs    = phaseDurationMs(phase);
	remainingMs = presetMs;

	enterTimerMode();
}

void PhonePomodoro::loadConfig() {
	// Defaults first; NVS overrides on a successful read. Fail-soft:
	// any error path leaves the defaults intact.
	config = Config{};

	if(!ensureOpen()) return;

	uint8_t blob[kBlobSize] = {};
	size_t  size = sizeof(blob);
	auto err = nvs_get_blob(s_handle, kBlobKey, blob, &size);
	if(err != ESP_OK)        return;
	if(size < kBlobSize)     return;
	if(blob[0] != kMagic0)   return;
	if(blob[1] != kMagic1)   return;
	if(blob[2] != kVersion)  return;

	// Clamp on read so a corrupt blob can never give us out-of-range
	// durations.
	config.workMin       = clampU8(blob[4], WorkMinMin,       WorkMinMax);
	config.shortBreakMin = clampU8(blob[5], ShortBreakMinMin, ShortBreakMinMax);
	config.longBreakMin  = clampU8(blob[6], LongBreakMinMin,  LongBreakMinMax);
	config.cyclesPerLong = clampU8(blob[7], CyclesPerLongMin, CyclesPerLongMax);
}

void PhonePomodoro::persistConfig() const {
	if(!ensureOpen()) return;

	uint8_t blob[kBlobSize] = {};
	blob[0] = kMagic0;
	blob[1] = kMagic1;
	blob[2] = kVersion;
	blob[3] = 0;
	blob[4] = config.workMin;
	blob[5] = config.shortBreakMin;
	blob[6] = config.longBreakMin;
	blob[7] = config.cyclesPerLong;

	auto err = nvs_set_blob(s_handle, kBlobKey, blob, sizeof(blob));
	if(err != ESP_OK) {
		ESP_LOGW("Pomodoro", "nvs_set_blob failed: %d", (int)err);
		return;
	}
	nvs_commit(s_handle);
}

// ---------- softkey actions ----------------------------------------------

void PhonePomodoro::primaryAction() {
	if(mode == Mode::Config) {
		saveConfig();
		return;
	}

	switch(runState) {
		case State::Idle: {
			// Always (re)load the preset from the current phase so the
			// user gets the durations they would expect after a config
			// change or a reset.
			presetMs    = phaseDurationMs(phase);
			remainingMs = presetMs;
			enterRunning();
			break;
		}
		case State::Running:
			enterPaused();
			break;
		case State::Paused:
			enterRunning();
			break;
		case State::Ringing:
			advancePhase();
			break;
	}
}

void PhonePomodoro::secondaryAction() {
	if(mode == Mode::Config) {
		// "BACK" — discard working copy.
		editConfig = config;
		enterTimerMode();
		return;
	}

	switch(runState) {
		case State::Idle:
			enterConfigMode();
			break;
		case State::Running:
		case State::Paused:
			// RESET — back to Work-Idle with cyclesDone wiped. This is
			// the closest analogue to PhoneTimer's RESET semantics
			// (which also discards an in-flight countdown). The user
			// can restart a fresh session with one tap.
			cyclesDone = 0;
			phase      = Phase::Work;
			enterIdle();
			break;
		case State::Ringing:
			advancePhase();
			break;
	}
}

// ---------- timers --------------------------------------------------------

void PhonePomodoro::startTickTimer() {
	if(tickTimer != nullptr) return;
	tickTimer = lv_timer_create(onTickStatic, TickPeriodMs, this);
}

void PhonePomodoro::stopTickTimer() {
	if(tickTimer == nullptr) return;
	lv_timer_del(tickTimer);
	tickTimer = nullptr;
}

void PhonePomodoro::startPulseTimer() {
	if(pulseTimer != nullptr) return;
	pulseTimer = lv_timer_create(onPulseStatic, PulsePeriodMs, this);
}

void PhonePomodoro::stopPulseTimer() {
	if(pulseTimer == nullptr) return;
	lv_timer_del(pulseTimer);
	pulseTimer = nullptr;
}

void PhonePomodoro::onTickStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhonePomodoro*>(timer->user_data);
	if(self == nullptr) return;
	if(self->runState != State::Running) return;

	const uint32_t remain = self->currentRemainingMs();
	self->refreshTimerView();
	if(remain == 0) {
		self->enterRinging();
	}
}

void PhonePomodoro::onPulseStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhonePomodoro*>(timer->user_data);
	if(self == nullptr) return;
	if(self->runState != State::Ringing) return;

	self->pulseHi = !self->pulseHi;
	self->refreshTimerView();
}

// ---------- input ---------------------------------------------------------

void PhonePomodoro::buttonPressed(uint i) {
	if(mode == Mode::Config) {
		switch(i) {
			case BTN_2: cursorUp();   return;
			case BTN_8: cursorDown(); return;
			case BTN_4: adjustCurrent(-1); return;
			case BTN_6: adjustCurrent(+1); return;
			case BTN_LEFT:
			case BTN_L:
			case BTN_ENTER:
				if(softKeys) softKeys->flashLeft();
				primaryAction();   // SAVE
				return;
			case BTN_RIGHT:
				if(softKeys) softKeys->flashRight();
				backLongFired = false;
				return;
			case BTN_R:
				if(softKeys) softKeys->flashRight();
				secondaryAction(); // BACK
				return;
			case BTN_BACK:
				backLongFired = false;
				return;
			default:
				return;
		}
	}

	// Timer mode below.

	// In Ringing, ANY non-BACK / non-Config-toggle key dismisses to
	// the next phase. Mirrors the PhoneIncomingCall convention.
	if(runState == State::Ringing) {
		if(i == BTN_BACK) {
			backLongFired = false;
			return;
		}
		if(softKeys) {
			if(i == BTN_LEFT || i == BTN_L || i == BTN_ENTER) softKeys->flashLeft();
			else if(i == BTN_RIGHT || i == BTN_R)             softKeys->flashRight();
		}
		advancePhase();
		return;
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
			// long-press cannot double-fire on key release.
			if(softKeys) softKeys->flashRight();
			backLongFired = false;
			break;

		case BTN_R:
			// R bumper is a dedicated short-press alias.
			if(softKeys) softKeys->flashRight();
			secondaryAction();
			break;

		case BTN_5:
			// SKIP — advance to the next phase from any state.
			advancePhase();
			break;

		case BTN_BACK:
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhonePomodoro::buttonReleased(uint i) {
	switch(i) {
		case BTN_RIGHT:
			if(!backLongFired) {
				secondaryAction();
			}
			backLongFired = false;
			break;

		case BTN_BACK:
			if(!backLongFired) {
				if(mode == Mode::Config) {
					// In Config short-BACK == BACK softkey (discard +
					// return to Timer view).
					editConfig = config;
					enterTimerMode();
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

void PhonePomodoro::buttonHeld(uint i) {
	switch(i) {
		case BTN_BACK:
			// Long-press BACK exits the screen from any mode.
			backLongFired = true;
			pop();
			break;

		case BTN_RIGHT:
			// Long-press on the right softkey falls through to the same
			// secondaryAction() the short-press would produce. The flag
			// suppresses the matching short-press release fire-back.
			backLongFired = true;
			secondaryAction();
			break;

		default:
			break;
	}
}
