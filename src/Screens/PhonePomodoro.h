#ifndef MAKERPHONE_PHONEPOMODORO_H
#define MAKERPHONE_PHONEPOMODORO_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhonePomodoro — S138
 *
 * Phase-Q "Pocket Organiser" work/break cycle timer. Sits next to
 * PhoneTodo (S136) and PhoneHabits (S137) inside the eventual
 * organiser-apps grid. Same Sony-Ericsson silhouette every other
 * Phone* screen wears so the user navigates between focus tools
 * without re-learning a UI:
 *
 *   Timer view (default):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |             POMODORO                   | <- pixelbasic7 cyan caption
 *   |             WORK 1/4                   | <- phase + dot-progress
 *   |                                        |
 *   |             24:35                      | <- pixelbasic16 mm:ss
 *   |                                        |
 *   |       CYCLES TODAY: 5                  | <- session counter
 *   |       NEXT: SHORT BREAK                | <- preview of next phase
 *   |       FOCUS — STAY ON TASK             | <- mode-driven hint
 *   |   START                       CONFIG   | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 *   Config view (after CONFIG / Settings entry):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### |
 *   |             SETTINGS                   |
 *   |   > WORK             25 MIN            | <- accent on cursor row
 *   |     SHORT BREAK       5 MIN            |
 *   |     LONG BREAK       15 MIN            |
 *   |     EVERY             4 CYCLES         |
 *   |                                        |
 *   |   4/6 ADJUST   2/8 MOVE                |
 *   |   SAVE                          BACK   |
 *   +----------------------------------------+
 *
 * Phase loop:
 *   Work          ── ringing ──► ShortBreak (or LongBreak when
 *                                cyclesDone is a non-zero multiple
 *                                of cyclesPerLong)
 *   ShortBreak    ── ringing ──► Work (cyclesDone unchanged)
 *   LongBreak     ── ringing ──► Work (cyclesDone unchanged — the
 *                                long-break selector hinges on a
 *                                running tally rather than a
 *                                rolled-over one, mirroring the
 *                                original Cirillo recipe)
 *
 *   cyclesDone increments at the end of every Work phase (i.e.
 *   when Work transitions to Ringing). The rule for "is the next
 *   break a long one?" is exactly:
 *
 *       (cyclesDone % cyclesPerLong) == 0  &&  cyclesDone > 0
 *
 *   so a fresh session always starts with a Short break after the
 *   first Work, and the user gets a Long break every Nth cycle.
 *
 * Per-phase run state:
 *   Idle    -- not counting; Start fires the current phase. The
 *              readout shows the phase preset; a fresh app launch
 *              lands here with phase=Work.
 *   Running -- counting toward zero, derived from millis() and a
 *              snapshot taken at the most-recent start so we can
 *              never drift more than one tick away from real time.
 *   Paused  -- countdown halted; remainingMs preserves the
 *              snapshot.
 *   Ringing -- countdown reached zero. The screen pulses the
 *              readout in MP_ACCENT and asserts the global
 *              PhoneRingtoneEngine alarm melody. Dismissing
 *              advances to the next phase and lands the user in
 *              Idle so they can pace the transition.
 *
 * Top-level mode (whole-screen):
 *   Timer  -- the default countdown view (above).
 *   Config -- four-row settings editor for the durations + cycles
 *             count; a working copy of the config is mutated and
 *             only persisted on SAVE.
 *
 * Controls (Timer view):
 *   - BTN_LEFT / BTN_L / BTN_ENTER  : "primary" softkey for the
 *     current run state:
 *       * Idle    -> START   (begin the current phase)
 *       * Running -> PAUSE
 *       * Paused  -> RESUME
 *       * Ringing -> NEXT    (advance to the next phase + Idle)
 *   - BTN_RIGHT / BTN_R             : "secondary" softkey for
 *     the current run state:
 *       * Idle    -> CONFIG  (enter the settings editor)
 *       * Running -> RESET   (zero cyclesDone + back to Idle Work)
 *       * Paused  -> RESET
 *       * Ringing -> NEXT    (same as primary)
 *   - BTN_5                          : SKIP — advance to the next
 *                                      phase from any state. Lets
 *                                      the user fast-forward a
 *                                      phase that already feels
 *                                      done.
 *   - BTN_BACK                       : pop the screen.
 *
 * Controls (Config view):
 *   - BTN_2 / BTN_8                  : cursor up / down (wraps).
 *   - BTN_4 / BTN_6                  : decrement / increment the
 *                                      current field by 1 (clamped
 *                                      to the field's [min, max]).
 *   - BTN_LEFT softkey ("SAVE")      : commit the working copy
 *                                      to NVS, return to Timer
 *                                      and reset the in-flight
 *                                      session so the next START
 *                                      uses the new durations.
 *   - BTN_RIGHT softkey ("BACK") /
 *     BTN_BACK short                 : discard the working copy
 *                                      and return to Timer.
 *   - BTN_BACK long                  : exit the entire screen.
 *
 * Persistence:
 *   One 8-byte NVS blob in namespace "mppomo" / key "c". Layout:
 *
 *     [0]  magic 'M'
 *     [1]  magic 'P'
 *     [2]  version (1)
 *     [3]  reserved (0)
 *     [4]  workMin
 *     [5]  shortBreakMin
 *     [6]  longBreakMin
 *     [7]  cyclesPerLong
 *
 *   On read failure (missing blob, bad magic, NVS error) the screen
 *   falls back to the Default* constants. Writes are best-effort
 *   and fail-soft, exactly the way PhoneVirtualPet (S129) and
 *   PhoneAlarmService degrade.
 *
 *   The cycle counter (cyclesDone) and the in-flight phase live in
 *   RAM only. A reboot mid-session resets cyclesDone to 0, which
 *   matches the way every kitchen-pomodoro the author has used
 *   behaves on power-cycle.
 *
 * Implementation notes:
 *   - 100 % code-only — no SPIFFS asset growth. Reuses
 *     PhoneSynthwaveBg / PhoneStatusBar / PhoneSoftKeyBar so the
 *     screen reads as part of the MAKERphone family.
 *   - 5 Hz tick (200 ms) drives the running readout; a 4 Hz pulse
 *     (250 ms) animates the ringing readout. Both timers are
 *     owned by the screen and torn down on detach.
 *   - The alarm melody is the global PhoneRingtoneEngine (S39),
 *     which internally honours Settings.sound — a muted device
 *     gets the visual pulse without any piezo output.
 *   - millis() is Arduino's monotonic ms clock. Wraps at ~49.7
 *     days, well past any realistic Pomodoro session.
 */
class PhonePomodoro : public LVScreen, private InputListener {
public:
	PhonePomodoro();
	virtual ~PhonePomodoro() override;

	void onStart() override;
	void onStop() override;

	/** Default Pomodoro recipe — Cirillo's original numbers. */
	static constexpr uint8_t  DefaultWorkMin       = 25;
	static constexpr uint8_t  DefaultShortBreakMin = 5;
	static constexpr uint8_t  DefaultLongBreakMin  = 15;
	static constexpr uint8_t  DefaultCyclesPerLong = 4;

	/** Editable ranges for the settings UI. Clamped on read so a
	 *  corrupt NVS blob can never give us a 200-minute work phase. */
	static constexpr uint8_t  WorkMinMin           = 1;
	static constexpr uint8_t  WorkMinMax           = 90;
	static constexpr uint8_t  ShortBreakMinMin     = 1;
	static constexpr uint8_t  ShortBreakMinMax     = 30;
	static constexpr uint8_t  LongBreakMinMin      = 1;
	static constexpr uint8_t  LongBreakMinMax      = 60;
	static constexpr uint8_t  CyclesPerLongMin     = 2;
	static constexpr uint8_t  CyclesPerLongMax     = 8;

	/** 5 Hz tick — the same cadence PhoneTimer / PhoneTimers use so
	 *  the seconds-resolution feel is identical across the family. */
	static constexpr uint16_t TickPeriodMs   = 200;

	/** 4 Hz pulse for the ringing readout. */
	static constexpr uint16_t PulsePeriodMs  = 250;

	/** Long-press threshold for BTN_BACK / BTN_RIGHT, matches the
	 *  rest of the MAKERphone shell so the gesture is consistent. */
	static constexpr uint16_t BackHoldMs     = 600;

	/** Number of editable config fields exposed in the Config view. */
	static constexpr uint8_t  ConfigFields   = 4;

	/** The three Pomodoro phases. */
	enum class Phase : uint8_t {
		Work       = 0,
		ShortBreak = 1,
		LongBreak  = 2,
	};

	/** Per-phase run-state machine. */
	enum class State : uint8_t {
		Idle    = 0,
		Running = 1,
		Paused  = 2,
		Ringing = 3,
	};

	/** Top-level UI mode. */
	enum class Mode : uint8_t {
		Timer  = 0,
		Config = 1,
	};

	/** Persisted-config struct. Whole thing fits in 4 bytes. */
	struct Config {
		uint8_t workMin       = DefaultWorkMin;
		uint8_t shortBreakMin = DefaultShortBreakMin;
		uint8_t longBreakMin  = DefaultLongBreakMin;
		uint8_t cyclesPerLong = DefaultCyclesPerLong;
	};

	/** Read-only accessors for tests / future hosts. */
	Mode    getMode()       const { return mode; }
	State   getState()      const { return runState; }
	Phase   getPhase()      const { return phase; }
	uint8_t getCyclesDone() const { return cyclesDone; }
	const Config& getConfig() const { return config; }

	/** Format a millisecond duration as mm:ss into `out`. Static +
	 *  side-effect-free so a host (or a unit test) can exercise the
	 *  formatter without standing up the screen. */
	static void formatRemaining(uint32_t ms, char* out, size_t outLen);

	/** Short ("WORK" / "SHORT BREAK" / "LONG BREAK") name for the
	 *  given phase. Returns a static const C-string; never nullptr. */
	static const char* phaseName(Phase p);

	/** Pure phase-transition rule used by advancePhase() and exposed
	 *  for tests. Given the phase that just ended, the cyclesDone
	 *  count after that phase has been credited (see advancePhase
	 *  for the credit-then-pick ordering), and the cyclesPerLong
	 *  setting, return the next phase. */
	static Phase nextPhase(Phase ending, uint8_t cyclesDone,
	                       uint8_t cyclesPerLong);

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	// Timer-view widgets.
	lv_obj_t* captionLabel;
	lv_obj_t* phaseLabel;
	lv_obj_t* readoutLabel;
	lv_obj_t* cyclesLabel;
	lv_obj_t* nextLabel;
	lv_obj_t* hintLabel;

	// Config-view widgets. Hidden while in Timer mode.
	lv_obj_t* configCaption;
	lv_obj_t* configRows[ConfigFields];
	lv_obj_t* configHint;

	Config   config;            // committed
	Config   editConfig;        // working copy in Config mode

	Mode     mode             = Mode::Timer;
	State    runState         = State::Idle;
	Phase    phase            = Phase::Work;

	uint8_t  cyclesDone       = 0;     // completed Work phases this session
	uint8_t  cursor           = 0;     // 0..ConfigFields-1 in Config mode

	uint32_t presetMs         = 0;
	uint32_t remainingMs      = 0;
	uint32_t runStartMs       = 0;
	uint32_t runStartRemain   = 0;

	bool     backLongFired    = false;
	bool     pulseHi          = false;

	// LVGL tick timer (5 Hz while Running) and pulse timer (4 Hz
	// while Ringing). Both idempotent.
	lv_timer_t* tickTimer  = nullptr;
	lv_timer_t* pulseTimer = nullptr;

	// Build helpers.
	void buildTimerView();
	void buildConfigView();

	// Repaint helpers — full redraws are cheap at this size.
	void refreshAll();
	void refreshTimerView();
	void refreshConfigView();
	void refreshSoftKeys();

	// Phase / run-state.
	uint32_t currentRemainingMs() const;
	uint32_t phaseDurationMs(Phase p) const;
	void enterIdle();
	void enterRunning();
	void enterPaused();
	void enterRinging();
	void advancePhase();        // ringing -> idle on the next phase

	// Top-level mode transitions.
	void enterTimerMode();
	void enterConfigMode();

	// Config nav.
	void cursorUp();
	void cursorDown();
	void adjustCurrent(int8_t delta);
	void saveConfig();          // commit editConfig + persist + back
	void persistConfig() const;
	void loadConfig();
	uint8_t  fieldValue(uint8_t idx) const;
	void     setFieldValue(uint8_t idx, uint8_t v);
	uint8_t  fieldMin(uint8_t idx) const;
	uint8_t  fieldMax(uint8_t idx) const;

	// Softkey actions.
	void primaryAction();
	void secondaryAction();

	// LVGL-timer plumbing.
	void startTickTimer();
	void stopTickTimer();
	void startPulseTimer();
	void stopPulseTimer();
	static void onTickStatic(lv_timer_t* timer);
	static void onPulseStatic(lv_timer_t* timer);

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONEPOMODORO_H
