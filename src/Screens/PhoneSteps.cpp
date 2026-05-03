#include "PhoneSteps.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>

#include <nvs.h>
#include <esp_log.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../Services/PhoneClock.h"

// MAKERphone retro palette — kept identical to every other Phone* widget
// so the steps screen slots in beside PhoneTodo (S136), PhoneHabits (S137),
// PhonePomodoro (S138), PhoneMoodLog (S139), PhoneScratchpad (S140),
// PhoneExpenses (S141) and PhoneCountdown (S142) without a visual seam.
// Same inline-#define convention every other Phone* screen .cpp uses.
#define MP_BG_DARK         lv_color_make( 20,  12,  36)  // deep purple
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan caption
#define MP_DIM             lv_color_make( 70,  56, 100)  // muted purple
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim cream

// =====================================================================
// S143 — PhoneSteps — geometry
//
// 160 x 128 layout:
//
//   y =   0..  9  PhoneStatusBar (10 px)
//   y =  12       caption "STEPS" (pixelbasic7, ~7 px tall)
//   y =  21       top divider rule (1 px)
//   y =  28       big step count (pixelbasic16, ~16 px tall)
//   y =  47       "OF NNNN GOAL" subline (pixelbasic7)
//   y =  58       progress bar background (124 x 6 px)
//   y =  58       progress fill (clamped to bar background width)
//   y =  56       "NN%" tag right of the bar (pixelbasic7)
//   y =  74       "LAST 7:" tag (pixelbasic7)
//   y =  74..104  seven sparkline columns (12 px wide x ~24 px tall)
//   y = 108       streak strip "STREAK: NN  BEST: NN" (pixelbasic7)
//   y = 117       bottom divider rule (1 px)
//   y = 118..127  PhoneSoftKeyBar (10 px)
//
// The 6 px progress bar + 24 px sparkline window keep the layout
// breathable at pixelbasic7 (~8 px line height) and the cap label
// always lands on a non-clipping y so the screen reads cleanly even
// when the user dials the goal up to 12000.
// =====================================================================

static constexpr lv_coord_t kCaptionY      = 12;
static constexpr lv_coord_t kTopDividerY   = 21;
static constexpr lv_coord_t kStepsY        = 28;
static constexpr lv_coord_t kGoalY         = 47;
static constexpr lv_coord_t kProgressY     = 58;
static constexpr lv_coord_t kProgressPctY  = 56;
static constexpr lv_coord_t kHistoryTagY   = 74;
static constexpr lv_coord_t kHistoryAreaY  = 74;
static constexpr lv_coord_t kHistoryAreaH  = 24;
static constexpr lv_coord_t kStreakY       = 108;
static constexpr lv_coord_t kBotDividerY   = 117;
static constexpr lv_coord_t kRowLeftX      = 6;
static constexpr lv_coord_t kRowWidth      = 148;

static constexpr lv_coord_t kProgressBgW   = 100;
static constexpr lv_coord_t kProgressBgH   = 6;
static constexpr lv_coord_t kProgressBgX   = 8;
static constexpr lv_coord_t kProgressPctX  = 116;

static constexpr lv_coord_t kHistTagX      = 6;
static constexpr lv_coord_t kHistTagW      = 42;
static constexpr lv_coord_t kHistColX0     = 52;     // first bar's left edge
static constexpr lv_coord_t kHistColW      = 12;     // bar column width
static constexpr lv_coord_t kHistColGap    = 3;      // gap between columns
static constexpr lv_coord_t kHistColMaxH   = 22;     // tallest fill (in px)
static constexpr lv_coord_t kHistColBaseY  = kHistoryAreaY + kHistoryAreaH;

// ---------- NVS persistence ----------------------------------------------

namespace {

constexpr const char* kNamespace = "mpsteps";
constexpr const char* kBlobKey   = "s";

constexpr uint8_t kMagic0  = 'M';
constexpr uint8_t kMagic1  = 'P';
constexpr uint8_t kVersion = 1;

// Layout offsets into the persisted blob. Mirrors the doc in PhoneSteps.h.
constexpr size_t kOffMagic0    = 0;
constexpr size_t kOffMagic1    = 1;
constexpr size_t kOffVersion   = 2;
constexpr size_t kOffReserved  = 3;
constexpr size_t kOffGoal      = 4;
constexpr size_t kOffLastDay   = 6;
constexpr size_t kOffTodayPeak = 10;
constexpr size_t kOffStreak    = 12;
constexpr size_t kOffBest      = 14;
constexpr size_t kOffHistory   = 16;
constexpr size_t kBlobSize     = kOffHistory + (size_t) PhoneSteps::HistoryDays * 2u;

// Single shared NVS handle, lazy-open. Mirrors PhoneTodo / PhoneHabits /
// PhoneMoodLog / PhoneScratchpad / PhoneCountdown so we never spam
// nvs_open() retries when the partition is unavailable.
nvs_handle s_handle    = 0;
bool       s_attempted = false;

bool ensureOpen() {
	if(s_handle != 0) return true;
	if(s_attempted)   return false;
	s_attempted = true;
	auto err = nvs_open(kNamespace, NVS_READWRITE, &s_handle);
	if(err != ESP_OK) {
		ESP_LOGW("PhoneSteps",
		         "nvs_open(%s) failed: %d -- steps run without persistence",
		         kNamespace, (int)err);
		s_handle = 0;
		return false;
	}
	return true;
}

inline void put_u16(uint8_t* buf, size_t off, uint16_t v) {
	buf[off]     = (uint8_t)(v & 0xFF);
	buf[off + 1] = (uint8_t)((v >> 8) & 0xFF);
}

inline void put_u32(uint8_t* buf, size_t off, uint32_t v) {
	buf[off]     = (uint8_t)(v & 0xFF);
	buf[off + 1] = (uint8_t)((v >> 8) & 0xFF);
	buf[off + 2] = (uint8_t)((v >> 16) & 0xFF);
	buf[off + 3] = (uint8_t)((v >> 24) & 0xFF);
}

inline uint16_t get_u16(const uint8_t* buf, size_t off) {
	return (uint16_t)(buf[off]) | ((uint16_t)(buf[off + 1]) << 8);
}

inline uint32_t get_u32(const uint8_t* buf, size_t off) {
	return (uint32_t)(buf[off])
	     | ((uint32_t)(buf[off + 1]) << 8)
	     | ((uint32_t)(buf[off + 2]) << 16)
	     | ((uint32_t)(buf[off + 3]) << 24);
}

// Goal preset list, walked round-robin by the GOAL softkey. Five entries
// keeps the cycle short enough that two presses always reach a "near"
// value without ever feeling like a wheel.
constexpr uint16_t kGoalPresets[PhoneSteps::GoalPresetCount] = {
	4000, 6000, 8000, 10000, 12000,
};

} // namespace

// ---------- static helpers ----------------------------------------------

uint16_t PhoneSteps::stepsForSeconds(uint32_t secondsSinceMidnight) {
	// Fold any overshoot back into the 24-hour window so the function
	// is well-defined even if the caller hands us raw nowEpoch().
	const uint32_t s = secondsSinceMidnight % 86400u;
	const uint32_t raw = s / SecondsPerStep;
	if(raw >= (uint32_t) StepCap) return StepCap;
	return (uint16_t) raw;
}

uint16_t PhoneSteps::goalPresetAt(uint8_t slot) {
	if(slot >= GoalPresetCount) return DefaultGoal;
	return kGoalPresets[slot];
}

uint16_t PhoneSteps::getHistoryAt(uint8_t slot) const {
	if(slot >= HistoryDays) return 0;
	return history[slot];
}

uint8_t PhoneSteps::goalPresetSlot() const {
	for(uint8_t i = 0; i < GoalPresetCount; ++i) {
		if(kGoalPresets[i] == dailyGoal) return i;
	}
	// Persisted goal that doesn't match any preset — pretend we're
	// sitting on the closest preset so the next GOAL tap snaps us
	// back into the canonical cycle.
	uint8_t  bestSlot = 0;
	uint16_t bestDiff = 0xFFFF;
	for(uint8_t i = 0; i < GoalPresetCount; ++i) {
		const uint16_t d = (kGoalPresets[i] >= dailyGoal)
				? (uint16_t)(kGoalPresets[i] - dailyGoal)
				: (uint16_t)(dailyGoal     - kGoalPresets[i]);
		if(d < bestDiff) {
			bestDiff = d;
			bestSlot = i;
		}
	}
	return bestSlot;
}

// ---------- ctor / dtor --------------------------------------------------

PhoneSteps::PhoneSteps() : LVScreen() {
	// Zero history so an early refresh sees stable empty state.
	for(uint8_t i = 0; i < HistoryDays; ++i) {
		history[i] = 0;
	}

	// Load persisted state. If load() fails we keep the constructor
	// defaults (DefaultGoal, empty history, streak=0) and run RAM-only.
	load();

	// Full-screen blank canvas, same pattern every Phone* screen uses.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px).
	statusBar = new PhoneStatusBar(obj);

	// Bottom soft-key bar.
	softKeys = new PhoneSoftKeyBar(obj);

	buildView();

	// Long-press threshold matches the rest of the MAKERphone shell so
	// the gesture feels identical from any screen.
	setButtonHoldTime(BTN_BACK, BackHoldMs);

	// Apply the wall clock once so the very first paint shows live
	// values rather than the persisted snapshot.
	refreshFromClock();
	refreshAll();
}

PhoneSteps::~PhoneSteps() {
	stopTickTimer();
	// LVGL children parented to obj are freed by the LVScreen base
	// destructor.
}

// ---------- lifecycle ----------------------------------------------------

void PhoneSteps::onStart() {
	Input::getInstance()->addListener(this);
	// Re-snapshot the clock — if the user came back the next morning
	// the history strip should advance without needing to re-push.
	refreshFromClock();
	refreshAll();
	startTickTimer();
}

void PhoneSteps::onStop() {
	stopTickTimer();
	// Persist on exit so a power cycle preserves the latest peak +
	// streak. Cheap (~30 byte blob) and only happens on screen tear-down.
	save();
	Input::getInstance()->removeListener(this);
}

// ---------- builders -----------------------------------------------------

void PhoneSteps::buildView() {
	// Caption — "STEPS". Cyan, matches every other Phase-Q caption.
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, kCaptionY);
	lv_label_set_text(captionLabel, "STEPS");

	// Top divider rule below the caption.
	topDivider = lv_obj_create(obj);
	lv_obj_remove_style_all(topDivider);
	lv_obj_set_size(topDivider, kRowWidth, 1);
	lv_obj_set_style_bg_color(topDivider, MP_DIM, 0);
	lv_obj_set_style_bg_opa(topDivider, LV_OPA_COVER, 0);
	lv_obj_set_pos(topDivider, kRowLeftX, kTopDividerY);

	// Big step count — pixelbasic16 in sunset orange so it's the
	// visual focal point of the screen.
	stepsLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(stepsLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(stepsLabel, MP_ACCENT, 0);
	lv_obj_set_width(stepsLabel, 160);
	lv_obj_set_pos(stepsLabel, 0, kStepsY);
	lv_obj_set_style_text_align(stepsLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(stepsLabel, "0");

	// Goal sub-line — "OF 8000 GOAL", dim cream.
	goalLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(goalLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(goalLabel, MP_LABEL_DIM, 0);
	lv_obj_set_width(goalLabel, 160);
	lv_obj_set_pos(goalLabel, 0, kGoalY);
	lv_obj_set_style_text_align(goalLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(goalLabel, "");

	// Progress bar background — desaturated purple so the cyan fill
	// reads clearly even on the synthwave wallpaper.
	progressBg = lv_obj_create(obj);
	lv_obj_remove_style_all(progressBg);
	lv_obj_set_size(progressBg, kProgressBgW, kProgressBgH);
	lv_obj_set_pos(progressBg, kProgressBgX, kProgressY);
	lv_obj_set_style_radius(progressBg, kProgressBgH / 2, 0);
	lv_obj_set_style_bg_opa(progressBg, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(progressBg, MP_DIM, 0);
	lv_obj_set_style_border_width(progressBg, 0, 0);
	lv_obj_set_style_pad_all(progressBg, 0, 0);
	lv_obj_clear_flag(progressBg, LV_OBJ_FLAG_SCROLLABLE);

	// Progress fill — child of progressBg so the rounded corners
	// clip naturally.
	progressFill = lv_obj_create(progressBg);
	lv_obj_remove_style_all(progressFill);
	lv_obj_set_size(progressFill, 0, kProgressBgH);
	lv_obj_set_pos(progressFill, 0, 0);
	lv_obj_set_style_radius(progressFill, kProgressBgH / 2, 0);
	lv_obj_set_style_bg_opa(progressFill, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(progressFill, MP_HIGHLIGHT, 0);
	lv_obj_set_style_border_width(progressFill, 0, 0);
	lv_obj_set_style_pad_all(progressFill, 0, 0);
	lv_obj_clear_flag(progressFill, LV_OBJ_FLAG_SCROLLABLE);

	// Percent tag right of the bar.
	progressPct = lv_label_create(obj);
	lv_obj_set_style_text_font(progressPct, &pixelbasic7, 0);
	lv_obj_set_style_text_color(progressPct, MP_TEXT, 0);
	lv_obj_set_pos(progressPct, kProgressPctX, kProgressPctY);
	lv_label_set_text(progressPct, "0%");

	// "LAST 7:" tag.
	historyLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(historyLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(historyLabel, MP_LABEL_DIM, 0);
	lv_obj_set_pos(historyLabel, kHistTagX, kHistoryAreaY + 12);
	lv_obj_set_width(historyLabel, kHistTagW);
	lv_label_set_text(historyLabel, "LAST 7:");

	// Seven sparkline columns. We build a dim background column for
	// every slot (so the layout is visible even with empty history)
	// and a foreground "fill" rect that's resized per refresh.
	for(uint8_t i = 0; i < HistoryDays; ++i) {
		const lv_coord_t colX = kHistColX0 + (lv_coord_t) i * (kHistColW + kHistColGap);

		historyBg[i] = lv_obj_create(obj);
		lv_obj_remove_style_all(historyBg[i]);
		lv_obj_set_size(historyBg[i], kHistColW, kHistColMaxH);
		lv_obj_set_pos(historyBg[i],
		               colX,
		               (lv_coord_t)(kHistColBaseY - kHistColMaxH));
		lv_obj_set_style_radius(historyBg[i], 1, 0);
		lv_obj_set_style_bg_opa(historyBg[i], LV_OPA_COVER, 0);
		lv_obj_set_style_bg_color(historyBg[i], MP_DIM, 0);
		lv_obj_set_style_border_width(historyBg[i], 0, 0);
		lv_obj_set_style_pad_all(historyBg[i], 0, 0);
		lv_obj_clear_flag(historyBg[i], LV_OBJ_FLAG_SCROLLABLE);

		historyBars[i] = lv_obj_create(historyBg[i]);
		lv_obj_remove_style_all(historyBars[i]);
		// Default to zero-height; refreshHistory() rewrites this.
		lv_obj_set_size(historyBars[i], kHistColW, 0);
		lv_obj_set_align(historyBars[i], LV_ALIGN_BOTTOM_LEFT);
		lv_obj_set_pos(historyBars[i], 0, 0);
		lv_obj_set_style_radius(historyBars[i], 1, 0);
		lv_obj_set_style_bg_opa(historyBars[i], LV_OPA_COVER, 0);
		lv_obj_set_style_bg_color(historyBars[i], MP_HIGHLIGHT, 0);
		lv_obj_set_style_border_width(historyBars[i], 0, 0);
		lv_obj_set_style_pad_all(historyBars[i], 0, 0);
		lv_obj_clear_flag(historyBars[i], LV_OBJ_FLAG_SCROLLABLE);
	}

	// Streak strip — single line, "STREAK: NN  BEST: NN".
	streakLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(streakLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(streakLabel, MP_TEXT, 0);
	lv_obj_set_width(streakLabel, kRowWidth);
	lv_obj_set_pos(streakLabel, kRowLeftX, kStreakY);
	lv_obj_set_style_text_align(streakLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(streakLabel, "");

	// Bottom divider rule above the soft-keys.
	bottomDivider = lv_obj_create(obj);
	lv_obj_remove_style_all(bottomDivider);
	lv_obj_set_size(bottomDivider, kRowWidth, 1);
	lv_obj_set_style_bg_color(bottomDivider, MP_DIM, 0);
	lv_obj_set_style_bg_opa(bottomDivider, LV_OPA_COVER, 0);
	lv_obj_set_pos(bottomDivider, kRowLeftX, kBotDividerY);
}

// ---------- repainters ---------------------------------------------------

void PhoneSteps::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	softKeys->set("GOAL", "BACK");
}

void PhoneSteps::refreshSteps() {
	if(stepsLabel == nullptr) return;
	char buf[8];
	// Format with a single space between thousands and the rest of the
	// digits so "8 421" reads at a glance even at pixelbasic16's 16 px
	// glyphs. Plain decimal for sub-thousand counts.
	if(todaySteps >= 1000) {
		snprintf(buf, sizeof(buf), "%u %03u",
		         (unsigned)(todaySteps / 1000u),
		         (unsigned)(todaySteps % 1000u));
	} else {
		snprintf(buf, sizeof(buf), "%u", (unsigned) todaySteps);
	}
	lv_label_set_text(stepsLabel, buf);
}

void PhoneSteps::refreshGoal() {
	if(goalLabel == nullptr) return;
	char buf[24];
	snprintf(buf, sizeof(buf), "OF %u GOAL", (unsigned) dailyGoal);
	lv_label_set_text(goalLabel, buf);
}

void PhoneSteps::refreshProgress() {
	if(progressBg == nullptr || progressFill == nullptr || progressPct == nullptr) return;

	uint32_t pct = 0;
	if(dailyGoal > 0) {
		pct = (uint32_t) todaySteps * 100u / (uint32_t) dailyGoal;
	}
	if(pct > 999u) pct = 999u; // clamp text to three digits

	uint32_t fillW = 0;
	if(dailyGoal > 0) {
		fillW = (uint32_t) todaySteps * (uint32_t) kProgressBgW / (uint32_t) dailyGoal;
	}
	if(fillW > (uint32_t) kProgressBgW) fillW = (uint32_t) kProgressBgW;
	lv_obj_set_size(progressFill, (lv_coord_t) fillW, kProgressBgH);

	// Once the goal is met, flip the bar fill to sunset orange so the
	// "I made it" milestone is visually loud.
	const lv_color_t fillCol = (todaySteps >= dailyGoal) ? MP_ACCENT : MP_HIGHLIGHT;
	lv_obj_set_style_bg_color(progressFill, fillCol, 0);

	char buf[8];
	snprintf(buf, sizeof(buf), "%u%%", (unsigned) pct);
	lv_label_set_text(progressPct, buf);
}

void PhoneSteps::refreshHistory() {
	// Each column maps history[i] / StepCap into the [0, kHistColMaxH]
	// pixel range, with a 1 px floor for nonzero days so a tiny day
	// still shows up as a sliver instead of disappearing entirely.
	for(uint8_t i = 0; i < HistoryDays; ++i) {
		if(historyBars[i] == nullptr) continue;

		uint32_t h = 0;
		if(history[i] > 0) {
			h = (uint32_t) history[i] * (uint32_t) kHistColMaxH / (uint32_t) StepCap;
			if(h == 0) h = 1; // minimum visible sliver
			if(h > (uint32_t) kHistColMaxH) h = (uint32_t) kHistColMaxH;
		}
		lv_obj_set_size(historyBars[i], kHistColW, (lv_coord_t) h);

		// A day that hit the goal lights up sunset orange; a day that
		// missed stays cyan. The dim background column stays purple
		// regardless so the strip reads as a 7-cell frame.
		const bool met = (history[i] >= dailyGoal && history[i] > 0);
		lv_obj_set_style_bg_color(historyBars[i],
		                          met ? MP_ACCENT : MP_HIGHLIGHT, 0);
	}
}

void PhoneSteps::refreshStreak() {
	if(streakLabel == nullptr) return;
	char buf[40];
	const char* dayWord  = (streak     == 1) ? "DAY" : "DAYS";
	const char* dayWord2 = (bestStreak == 1) ? "DAY" : "DAYS";
	snprintf(buf, sizeof(buf),
	         "STREAK: %u %s   BEST: %u %s",
	         (unsigned) streak, dayWord,
	         (unsigned) bestStreak, dayWord2);
	lv_label_set_text(streakLabel, buf);
}

void PhoneSteps::refreshAll() {
	refreshSoftKeys();
	refreshSteps();
	refreshGoal();
	refreshProgress();
	refreshHistory();
	refreshStreak();
}

// ---------- model helpers -----------------------------------------------

void PhoneSteps::refreshFromClock() {
	uint16_t y; uint8_t m, d, hh, mm, ss, wd;
	const uint32_t epoch = PhoneClock::now(y, m, d, hh, mm, ss, wd);

	const uint32_t dayIdx = epoch / 86400u;
	const uint32_t secsInDay = epoch % 86400u;

	if(lastDayIdx == 0) {
		// Fresh-install state: adopt today as the anchor, no rollover.
		lastDayIdx = dayIdx;
	}
	if(dayIdx > lastDayIdx) {
		handleDayRollover(dayIdx);
	}

	todaySteps = stepsForSeconds(secsInDay);
	if(todaySteps > todayPeak) {
		todayPeak = todaySteps;
	}
}

void PhoneSteps::handleDayRollover(uint32_t newDayIdx) {
	// How many days advanced since we last updated. Clamp at HistoryDays
	// so a "device off for two weeks" gap doesn't underflow our shifts.
	uint32_t advanced = newDayIdx - lastDayIdx;
	if(advanced > (uint32_t) HistoryDays) advanced = (uint32_t) HistoryDays;

	// Yesterday's final value is whatever we last peaked at. If we
	// advanced multiple days the intermediate ones are recorded as
	// zeros (device was off).
	for(uint32_t step = 0; step < advanced; ++step) {
		// Shift older days down: history[6] is dropped, history[5] -> [6],
		// ..., history[0] -> [1].
		for(int8_t i = (int8_t) HistoryDays - 1; i > 0; --i) {
			history[i] = history[i - 1];
		}
		// New entry at slot 0. The first iteration (most recent finished
		// day) uses todayPeak; subsequent skipped days are zero.
		history[0] = (step == 0) ? todayPeak : 0;
	}

	// Evaluate streak for the day that just finished (our previous
	// "today"). A skipped day breaks the streak even if today's peak
	// would have been enough.
	const bool yesterdayMet = (todayPeak >= dailyGoal && dailyGoal > 0);
	if(yesterdayMet && advanced == 1) {
		if(streak < 0xFFFFu) ++streak;
		if(streak > bestStreak) bestStreak = streak;
	} else {
		streak = 0;
	}

	// Reset today.
	lastDayIdx = newDayIdx;
	todayPeak  = 0;

	// Persist immediately so a power loss right after midnight doesn't
	// re-fire the rollover and double-count.
	save();
}

void PhoneSteps::cycleGoal() {
	const uint8_t cur = goalPresetSlot();
	const uint8_t nxt = (uint8_t)((cur + 1u) % GoalPresetCount);
	dailyGoal = kGoalPresets[nxt];
	save();
	refreshGoal();
	refreshProgress();
	refreshHistory();   // bar colors depend on whether each day met the goal
	refreshStreak();    // streak text doesn't change but stays in sync
}

// ---------- persistence -------------------------------------------------

void PhoneSteps::load() {
	if(!ensureOpen()) return;

	uint8_t buf[kBlobSize] = {};
	size_t  readLen = sizeof(buf);
	auto err = nvs_get_blob(s_handle, kBlobKey, buf, &readLen);
	if(err != ESP_OK || readLen < kBlobSize) return;
	if(buf[kOffMagic0] != kMagic0)   return;
	if(buf[kOffMagic1] != kMagic1)   return;
	if(buf[kOffVersion] != kVersion) return;

	uint16_t goal = get_u16(buf, kOffGoal);
	if(goal == 0 || goal > 30000u) goal = DefaultGoal;
	dailyGoal = goal;

	lastDayIdx = get_u32(buf, kOffLastDay);

	uint16_t peak = get_u16(buf, kOffTodayPeak);
	if(peak > StepCap) peak = StepCap;
	todayPeak = peak;

	streak     = get_u16(buf, kOffStreak);
	bestStreak = get_u16(buf, kOffBest);
	if(bestStreak < streak) bestStreak = streak;

	for(uint8_t i = 0; i < HistoryDays; ++i) {
		uint16_t v = get_u16(buf, kOffHistory + (size_t) i * 2u);
		if(v > StepCap) v = StepCap;
		history[i] = v;
	}
}

void PhoneSteps::save() {
	if(!ensureOpen()) return;

	uint8_t buf[kBlobSize] = {};
	buf[kOffMagic0]   = kMagic0;
	buf[kOffMagic1]   = kMagic1;
	buf[kOffVersion]  = kVersion;
	buf[kOffReserved] = 0;
	put_u16(buf, kOffGoal,      dailyGoal);
	put_u32(buf, kOffLastDay,   lastDayIdx);
	put_u16(buf, kOffTodayPeak, todayPeak);
	put_u16(buf, kOffStreak,    streak);
	put_u16(buf, kOffBest,      bestStreak);
	for(uint8_t i = 0; i < HistoryDays; ++i) {
		put_u16(buf, kOffHistory + (size_t) i * 2u, history[i]);
	}

	auto err = nvs_set_blob(s_handle, kBlobKey, buf, kBlobSize);
	if(err != ESP_OK) {
		ESP_LOGW("PhoneSteps", "nvs_set_blob failed: %d", (int)err);
		return;
	}
	err = nvs_commit(s_handle);
	if(err != ESP_OK) {
		ESP_LOGW("PhoneSteps", "nvs_commit failed: %d", (int)err);
	}
}

// ---------- timer -------------------------------------------------------

void PhoneSteps::startTickTimer() {
	if(tickTimer != nullptr) return;
	tickTimer = lv_timer_create(&PhoneSteps::onTickStatic,
	                            (uint32_t) TickPeriodMs, this);
}

void PhoneSteps::stopTickTimer() {
	if(tickTimer == nullptr) return;
	lv_timer_del(tickTimer);
	tickTimer = nullptr;
}

void PhoneSteps::onTickStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneSteps*>(timer->user_data);
	if(self == nullptr) return;
	self->refreshFromClock();
	self->refreshSteps();
	self->refreshProgress();
	// Day rollover (handled in refreshFromClock) may also have updated
	// the streak and history rows, so repaint those defensively. Both
	// paths are O(HistoryDays) and amount to a handful of label sets,
	// so the every-second cost is invisible.
	self->refreshHistory();
	self->refreshStreak();
}

// ---------- input -------------------------------------------------------

void PhoneSteps::buttonPressed(uint i) {
	switch(i) {
		case BTN_LEFT:
			if(softKeys) softKeys->flashLeft();
			cycleGoal();
			break;

		case BTN_5:
		case BTN_ENTER:
			// Ergonomic alias — center / D-pad enter mirrors the GOAL
			// softkey, matching the muscle memory of every other
			// Phase-Q app's main toggle.
			if(softKeys) softKeys->flashLeft();
			cycleGoal();
			break;

		case BTN_RIGHT:
			if(softKeys) softKeys->flashRight();
			pop();
			break;

		case BTN_BACK:
			// Defer the actual pop to release so a long-press exit
			// path cannot double-fire alongside buttonHeld().
			backLongFired = false;
			break;

		default:
			// Absorb every other key. The screen has no cursor or text
			// entry — falling through could leak into a parent.
			break;
	}
}

void PhoneSteps::buttonReleased(uint i) {
	switch(i) {
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

void PhoneSteps::buttonHeld(uint i) {
	switch(i) {
		case BTN_BACK:
			// Long-press BACK == short tap == exit. The flag suppresses
			// the matching short-press fire-back on release.
			backLongFired = true;
			pop();
			break;

		default:
			break;
	}
}
