#ifndef MAKERPHONE_PHONESTEPS_H
#define MAKERPHONE_PHONESTEPS_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneSteps — S143
 *
 * Phase-Q "Pocket Organiser" mock pedometer. Sits next to PhoneTodo
 * (S136), PhoneHabits (S137), PhonePomodoro (S138), PhoneMoodLog
 * (S139), PhoneScratchpad (S140), PhoneExpenses (S141) and
 * PhoneCountdown (S142) inside the eventual organiser-apps grid.
 * Chatter has no accelerometer, so the "step count" is a fully
 * synthetic value derived from how much wall-clock time has elapsed
 * since the start of today: a believable curve that grows during the
 * day, caps before midnight, resets at 00:00 the next day, and is
 * compared against a configurable daily goal so the streak counter
 * has something to lock onto.
 *
 *   View (always one screen — there is only one buffer):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |              STEPS                     | <- caption (cyan, pixelbasic7)
 *   |  ------------------------------------- |
 *   |             8  4  2  1                 | <- live step count (pixelbasic16, accent)
 *   |          OF 8000 GOAL                  | <- subline (dim cream)
 *   |       ===========..........  56%       | <- progress bar + percent
 *   |                                        |
 *   |  LAST 7:  | / | _ : / | _ . / | * /    | <- 7-day spark bars (oldest -> newest)
 *   |  STREAK: 03 DAYS    BEST: 12 DAYS      | <- streak strip
 *   |  ------------------------------------- |
 *   |  GOAL                           BACK   | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Step model
 *   The "live" step count is a deterministic function of today's
 *   wall-clock time:
 *
 *       todaySteps = clamp(secondsSinceMidnight / SecondsPerStep,
 *                          0, StepCap)
 *
 *   With SecondsPerStep = 5 and StepCap = 14000 the curve hits the
 *   default 8000-step goal a bit before noon (~11:07) and tops out
 *   shortly after 19:30 — a believable "active commuter" rhythm that
 *   makes the goal feel earned but reachable.
 *
 *   This also means the count is a pure function of the current
 *   wall clock: rebooting mid-day does not snap the counter back to
 *   zero. The same call to nowEpoch() always yields the same step
 *   count for that moment, which is exactly the property a
 *   non-stopwatch pedometer should have.
 *
 * Persistence (NVS namespace "mpsteps", key "s")
 *   Layout (single blob, little-endian):
 *
 *     [ 0]    magic 'M'
 *     [ 1]    magic 'P'
 *     [ 2]    version (1)
 *     [ 3]    reserved (0)
 *     [ 4..5] dailyGoal       (uint16)
 *     [ 6..9] lastDayIdx      (uint32)  // PhoneClock day index
 *     [10..11] todayPeak      (uint16)  // max steps observed today
 *     [12..13] streak         (uint16)  // consecutive goal-met days
 *     [14..15] bestStreak     (uint16)
 *     [16..29] history[7]     (7 x uint16, history[0]=newest finished day)
 *
 *   Read failure (missing blob, bad magic, NVS unavailable) is
 *   benign: we leave defaults in place (goal=8000, empty history,
 *   streak=0) and the screen runs RAM-only — same conservative
 *   behaviour every other Phase-Q screen exhibits.
 *
 *   Writes happen on every meaningful state change: GOAL bump, day
 *   rollover, BACK exit. We don't write on every tick — that would
 *   shorten flash lifetime for no user-visible benefit.
 *
 * Day rollover
 *   On every refreshFromClock() we compute the current day index. If
 *   it has advanced past lastDayIdx we:
 *     1. Decide whether yesterday's todayPeak met the goal. If yes
 *        bump streak (and bestStreak); if not, reset streak to 0.
 *     2. Push todayPeak onto history[0], shifting older entries down.
 *        If multiple days were skipped (device powered off > 1 day)
 *        we pad with zeros so the bar chart stays time-ordered.
 *     3. Reset todayPeak to 0, advance lastDayIdx to today.
 *     4. Persist.
 *
 * Controls
 *   - BTN_LEFT softkey ("GOAL")    : cycle goal through GoalPresets.
 *   - BTN_RIGHT softkey            : aliased to BACK.
 *   - BTN_BACK short or long       : pop the screen (auto-saves first).
 *   - BTN_5 / BTN_ENTER            : alias for GOAL (ergonomic shortcut).
 *   - All other buttons            : absorbed (no-op).
 *
 * Implementation notes
 *   - 100 % code-only — no SPIFFS asset growth. Reuses
 *     PhoneSynthwaveBg / PhoneStatusBar / PhoneSoftKeyBar so the
 *     screen reads as part of the MAKERphone family.
 *   - A single repeating lv_timer at TickPeriodMs cadence drives the
 *     repaint. Idle CPU cost is one label-set call per second.
 *   - The step formula is exposed as a public static helper so a
 *     host or test can introspect the curve without standing up the
 *     full screen.
 */
class PhoneSteps : public LVScreen, private InputListener {
public:
	PhoneSteps();
	virtual ~PhoneSteps() override;

	void onStart() override;
	void onStop() override;

	/** Per-second step rate divisor. todaySteps grows by 1 every
	 *  SecondsPerStep wall-clock seconds, so SecondsPerStep=5 yields
	 *  ~12 steps/min (a realistic walking cadence for a casual day
	 *  averaged over the whole 24-hour window). */
	static constexpr uint16_t SecondsPerStep = 5;

	/** Hard ceiling on the daily count. Reaching the cap before
	 *  midnight is intentional — it gives the goal-hit streak
	 *  something to clamp against, and keeps the displayed number
	 *  inside four digits at pixelbasic16 width. */
	static constexpr uint16_t StepCap        = 14000;

	/** Default daily goal. 8000 is the canonical "active without
	 *  marathon" target and corresponds to ~11:07 wall-clock time
	 *  under the curve above. */
	static constexpr uint16_t DefaultGoal    = 8000;

	/** Goal-cycle preset list, walked round-robin by the GOAL key.
	 *  Five entries keeps the cycle short enough that two presses
	 *  always reach a "near" value without ever feeling like a wheel. */
	static constexpr uint8_t  GoalPresetCount = 5;

	/** History strip length — last seven finished days, [0] newest.
	 *  Seven matches the visual sparkline width and the "weekly
	 *  rhythm" framing the rest of the organiser uses. */
	static constexpr uint8_t  HistoryDays     = 7;

	/** Repaint cadence. 1 Hz is plenty given the curve only changes
	 *  one step per five seconds — enough motion to feel alive,
	 *  cheap enough to be invisible to the loop. */
	static constexpr uint16_t TickPeriodMs    = 1000;

	/** Long-press threshold (matches the rest of the MAKERphone shell). */
	static constexpr uint16_t BackHoldMs      = 600;

	/**
	 * Clamp the supplied wall-clock seconds-into-day to the synthetic
	 * step curve described in the header doc. Pure function, side-
	 * effect-free, exposed for tests and future hosts.
	 *
	 *   secondsSinceMidnight is taken modulo 86400 internally, so
	 *   callers don't have to do that themselves.
	 */
	static uint16_t stepsForSeconds(uint32_t secondsSinceMidnight);

	/**
	 * Goal-preset accessor. Slot 0..GoalPresetCount-1 returns the
	 * canonical preset (4000 / 6000 / 8000 / 10000 / 12000); other
	 * slots return DefaultGoal so a corrupt persisted goal still
	 * resolves to a usable target. */
	static uint16_t goalPresetAt(uint8_t slot);

	/** Read-only accessors for tests / future hosts. */
	uint16_t getDailyGoal()      const { return dailyGoal; }
	uint16_t getTodaySteps()     const { return todaySteps; }
	uint16_t getStreak()         const { return streak; }
	uint16_t getBestStreak()     const { return bestStreak; }
	uint16_t getHistoryAt(uint8_t slot) const;

private:
	PhoneSynthwaveBg* wallpaper = nullptr;
	PhoneStatusBar*   statusBar = nullptr;
	PhoneSoftKeyBar*  softKeys  = nullptr;

	// View widgets
	lv_obj_t* captionLabel  = nullptr;     // "STEPS"
	lv_obj_t* topDivider    = nullptr;
	lv_obj_t* bottomDivider = nullptr;
	lv_obj_t* stepsLabel    = nullptr;     // big "8 4 2 1" count
	lv_obj_t* goalLabel     = nullptr;     // "OF 8000 GOAL"
	lv_obj_t* progressBg    = nullptr;     // empty bar background
	lv_obj_t* progressFill  = nullptr;     // filled portion of bar
	lv_obj_t* progressPct   = nullptr;     // "56%"
	lv_obj_t* historyLabel  = nullptr;     // "LAST 7:" tag
	lv_obj_t* historyBars[HistoryDays] = { nullptr }; // 7 sparkline bars
	lv_obj_t* historyBg[HistoryDays]   = { nullptr }; // dim background per bar
	lv_obj_t* streakLabel   = nullptr;     // "STREAK: 03 DAYS  BEST: 12 DAYS"

	// State
	uint16_t  dailyGoal      = DefaultGoal;
	uint16_t  todaySteps     = 0;
	uint16_t  todayPeak      = 0;
	uint32_t  lastDayIdx     = 0;
	uint16_t  streak         = 0;
	uint16_t  bestStreak     = 0;
	uint16_t  history[HistoryDays] = { 0 };

	bool      backLongFired  = false;
	lv_timer_t* tickTimer    = nullptr;

	// ---- builders ----
	void buildView();

	// ---- repainters ----
	void refreshSoftKeys();
	void refreshSteps();
	void refreshGoal();
	void refreshProgress();
	void refreshHistory();
	void refreshStreak();
	void refreshAll();

	// ---- model helpers ----
	void    refreshFromClock();         // reads wall clock, applies day rollover
	void    handleDayRollover(uint32_t newDayIdx);
	uint8_t goalPresetSlot() const;     // current goal's slot in the preset list
	void    cycleGoal();

	// ---- persistence ----
	void load();
	void save();

	// ---- timer ----
	void startTickTimer();
	void stopTickTimer();
	static void onTickStatic(lv_timer_t* timer);

	// ---- input ----
	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONESTEPS_H
