#ifndef MAKERPHONE_PHONESTOPWATCH_H
#define MAKERPHONE_PHONESTOPWATCH_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneStopwatch
 *
 * Phase-L utility app (S61): a basic feature-phone stopwatch with
 * start / stop / lap controls and a live mm:ss.cs readout. Slots in
 * next to PhoneCalculator (S60) inside the eventual utility-apps
 * grid. Same Sony-Ericsson silhouette as every other Phone* screen:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |              STOPWATCH                 | <- pixelbasic7 caption
 *   |                                        |
 *   |             02:35.42                   | <- pixelbasic16 elapsed
 *   |                                        |
 *   |   LAPS                                 | <- pixelbasic7 dim caption
 *   |   L4   00:46.50    02:35.42            |
 *   |   L3   00:15.27    01:48.92            | <- recent laps,
 *   |   L2   01:01.55    01:33.65            |    newest first
 *   |   L1   00:32.10    00:32.10            |
 *   |   START                       LAP      | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * State machine:
 *   Idle    -- no time accumulated, no laps. Display "00:00.00".
 *   Running -- timer ticking, lap recording allowed.
 *   Paused  -- timer halted (accumulator preserved); resume or reset.
 *
 * Controls:
 *   - BTN_LEFT (softkey "START"/"STOP"): toggles run state. From Idle
 *     starts the timer; from Running pauses it; from Paused resumes
 *     it (the elapsed accumulator carries over).
 *   - BTN_ENTER : friendly alias for START/STOP -- the centre A button
 *     is the muscle-memory "confirm" on every other Phone* screen.
 *   - BTN_L     : same as BTN_LEFT (bumper alias).
 *   - BTN_RIGHT (softkey "LAP"/"RESET"):
 *       * Running -> record a lap split (newest pushes older down).
 *       * Paused  -> reset the accumulator + clear lap history.
 *       * Idle    -> no-op (still flashes for tactile cue).
 *   - BTN_R     : same as BTN_RIGHT (bumper alias).
 *   - BTN_BACK  : exit the screen. State is intentionally NOT reset
 *     on exit -- a paused timer will still be paused next time the
 *     user opens the stopwatch (matches feature-phone behaviour).
 *
 * Display semantics:
 *   - mm:ss.cs format with mm clamped at 99 (i.e. 99:59.99 ~= 100 minutes)
 *     because pixelbasic16 'mm:ss.cs' fits cleanly in the 152 px display
 *     band; longer runs were never the point of a feature-phone watch.
 *   - Lap rows show split (time since previous lap or start) and total
 *     (cumulative elapsed at the moment the lap was recorded).
 *
 * Implementation notes:
 *   - 100 % code-only -- no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *     PhoneStatusBar / PhoneSoftKeyBar so the screen feels visually
 *     part of the MAKERphone family. Data partition cost stays zero.
 *   - 20 Hz refresh tick (50 ms period) drives the centisecond readout
 *     while running. The tick is a single lv_timer; no work happens
 *     while paused or idle so the screen drops back to LVGL's idle
 *     cost the moment the user pauses.
 *   - The lap stack is a fixed-size ring buffer (MaxLaps = 4) so the
 *     screen has zero per-lap allocation traffic. The 4-row visual
 *     budget is set by the available 50 px between the elapsed
 *     readout and the soft-key bar.
 *   - millis() is Arduino's monotonic ms clock. Wraps at ~49.7 days,
 *     well past any realistic stopwatch session. The diff math uses
 *     uint32_t subtraction so a wrap at runtime would still produce
 *     a correct delta as long as a single session is shorter than
 *     2^31 ms (~24.8 days).
 */
class PhoneStopwatch : public LVScreen, private InputListener {
public:
	PhoneStopwatch();
	virtual ~PhoneStopwatch() override;

	void onStart() override;
	void onStop() override;

	/** Timer tick period (ms) -- 20 Hz centisecond updates. */
	static constexpr uint16_t TickPeriodMs = 50;
	/** Cap on visible mm digits before we clamp the readout. */
	static constexpr uint32_t MaxMinutes   = 99;
	/** Fixed-size lap history stack -- newest at index 0. */
	static constexpr uint8_t  MaxLaps      = 4;
	/** Long-press threshold for BTN_BACK (matches the rest of the shell). */
	static constexpr uint16_t BackHoldMs   = 600;

	/**
	 * Format a millisecond duration as mm:ss.cs into `out`. Clamped to
	 * MaxMinutes:59.99 so the readout never overflows the 152 px display
	 * band. Static + side-effect-free so a host (or a unit test) can
	 * exercise the formatter without standing up the screen.
	 */
	static void formatElapsed(uint32_t ms, char* out, size_t outLen);

private:
	/** Internal run-state machine. See class doc for transitions. */
	enum class Mode : uint8_t {
		Idle    = 0,
		Running = 1,
		Paused  = 2
	};

	struct Lap {
		uint32_t splitMs;   ///< delta from the previous lap (or start)
		uint32_t totalMs;   ///< cumulative elapsed at the moment of capture
	};

	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	// Display widgets.
	lv_obj_t* captionLabel;       // "STOPWATCH" header
	lv_obj_t* elapsedLabel;       // big mm:ss.cs readout
	lv_obj_t* lapsCaption;        // small "LAPS" caption (hidden when no laps)
	lv_obj_t* lapRows[MaxLaps];   // 4 lap rows, newest at index 0

	// State.
	Mode      mode           = Mode::Idle;
	uint32_t  baseElapsedMs  = 0;       // accumulated ms across pauses
	uint32_t  runStartMs     = 0;       // millis() at the most-recent start
	uint32_t  lastLapTotal   = 0;       // total elapsed at the previous lap
	Lap       laps[MaxLaps]   = {};
	uint8_t   lapCount        = 0;      // number of laps currently in the buffer
	uint32_t  totalLapsTaken  = 0;      // monotonic counter for the L# label

	bool      backLongFired   = false;  // suppresses double-fire on long BACK

	// LVGL tick timer (only alive while Running).
	lv_timer_t* tickTimer = nullptr;

	void buildCaption();
	void buildElapsed();
	void buildLapList();

	/** Returns the live elapsed value in ms (running -> base + delta). */
	uint32_t currentElapsedMs() const;

	/** Repaint the big readout from currentElapsedMs(). */
	void refreshElapsed();
	/** Repaint the lap list from `laps`/`lapCount`. */
	void refreshLapList();
	/** Repaint the soft-key labels for the current mode. */
	void refreshSoftKeys();

	/** Apply a state transition (and start/stop the tick timer). */
	void enterIdle();
	void enterRunning();
	void enterPaused();

	/** Push a new lap onto the front of the ring buffer. */
	void recordLap();
	/** Drop all accumulated time + laps and return to Idle. */
	void resetAll();

	/** Toggle between Running and Paused (also handles Idle->Running). */
	void toggleRun();
	/** Right-softkey action: lap when running, reset when paused. */
	void lapOrReset();

	/** Manage the lifetime of the 20 Hz tick timer. */
	void startTickTimer();
	void stopTickTimer();
	static void onTickStatic(lv_timer_t* timer);

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONESTOPWATCH_H
