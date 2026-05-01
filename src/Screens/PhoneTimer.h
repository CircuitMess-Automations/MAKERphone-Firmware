#ifndef MAKERPHONE_PHONETIMER_H
#define MAKERPHONE_PHONETIMER_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneTimer
 *
 * Phase-L utility app (S62): a feature-phone-style count-down timer.
 * Slots in next to PhoneCalculator (S60) and PhoneStopwatch (S61) inside
 * the eventual utility-apps grid. Same Sony-Ericsson silhouette every
 * other Phone* screen wears:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |               TIMER                    | <- pixelbasic7 caption
 *   |                                        |
 *   |             05:00                      | <- pixelbasic16 mm:ss
 *   |                                        |
 *   |   ENTER MM:SS WITH DIGITS              | <- mode-driven hint line
 *   |                                        |
 *   |   START                       CLEAR    | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * State machine:
 *   Idle    -- user is composing the duration (digit entry buffer drives
 *              the readout). No countdown is running.
 *   Running -- duration counts down toward zero, paint refreshed at
 *              5 Hz so the seconds tick smoothly.
 *   Paused  -- countdown halted (remaining-ms is preserved); resume or
 *              reset.
 *   Ringing -- countdown reached zero. The PhoneRingtoneEngine fires a
 *              looping alert melody and the screen pulses "TIME'S UP".
 *              Any key dismisses, returning to Idle with the original
 *              preset re-loaded so the user can re-arm in one tap.
 *
 * Controls:
 *   - BTN_0..BTN_9 (Idle only): typed into a 4-digit MMSS shift buffer.
 *     The newest digit lands in the SS-low slot; older digits scroll
 *     left, falling off the front when the buffer is full. So typing
 *     "5" then "0" then "0" yields "00:05" -> "00:50" -> "05:00".
 *   - BTN_LEFT (softkey "START"/"PAUSE"/"RESUME"/"DISMISS"):
 *       * Idle    -> arm and start counting (no-op if duration is 0).
 *       * Running -> pause (remaining preserved).
 *       * Paused  -> resume from the paused remaining time.
 *       * Ringing -> dismiss the alarm, return to Idle.
 *   - BTN_ENTER (alias for BTN_LEFT): same toggle/dismiss semantics --
 *     centre A is "confirm" on every other Phone* screen.
 *   - BTN_L     (alias for BTN_LEFT): bumper alias.
 *   - BTN_RIGHT (softkey "CLEAR"/"RESET"/"DISMISS"):
 *       * Idle    -> backspace one digit from the entry buffer.
 *       * Running -> reset to the original preset, return to Idle.
 *       * Paused  -> reset to the original preset, return to Idle.
 *       * Ringing -> dismiss the alarm, return to Idle.
 *   - BTN_R     (alias for BTN_RIGHT): bumper alias.
 *   - BTN_BACK  -> exit the screen (long-press or short tap). The
 *     entry buffer is preserved across exit so the user can leave
 *     and return without re-typing the duration.
 *
 * Display semantics:
 *   - mm:ss format with mm clamped at 99 (the buffer caps at 9959 so
 *     the readout never overflows the 152 px display band).
 *   - Ringing state pulses the readout between MP_ACCENT and
 *     MP_ACCENT_BRIGHT at 4 Hz so the alarm reads as urgent without
 *     any wall-of-text overlay.
 *   - Hint line under the readout swaps between four mode-specific
 *     captions ("ENTER MM:SS WITH DIGITS", "COUNTING DOWN...",
 *     "PAUSED", "TIME'S UP").
 *
 * Implementation notes:
 *   - 100 % code-only -- no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *     PhoneStatusBar / PhoneSoftKeyBar so the screen feels visually
 *     part of the MAKERphone family. Data partition cost stays zero.
 *   - 5 Hz refresh tick (200 ms period) drives the running readout.
 *     The countdown math itself is millis()-based so we can never
 *     drift more than one tick away from real time even if LVGL
 *     drops a frame.
 *   - The alarm melody is the global PhoneRingtoneEngine (S39), which
 *     internally honours Settings.sound -- a muted device gets the
 *     visual pulse without any piezo output.
 *   - millis() is Arduino's monotonic ms clock. Wraps at ~49.7 days,
 *     well past any realistic timer session. uint32_t subtraction
 *     gives a correct delta as long as a single run is shorter than
 *     2^31 ms.
 */
class PhoneTimer : public LVScreen, private InputListener {
public:
	PhoneTimer();
	virtual ~PhoneTimer() override;

	void onStart() override;
	void onStop() override;

	/** Timer tick period (ms) -- 5 Hz seconds-resolution updates. */
	static constexpr uint16_t TickPeriodMs   = 200;
	/** Pulse cadence (ms) for the "TIME'S UP" alarm visual. */
	static constexpr uint16_t PulsePeriodMs  = 250;
	/** Cap on visible mm digits before we clamp the readout. */
	static constexpr uint32_t MaxMinutes     = 99;
	/** Long-press threshold for BTN_BACK (matches the rest of the shell). */
	static constexpr uint16_t BackHoldMs     = 600;
	/** MMSS digit-buffer length (4 chars). */
	static constexpr uint8_t  EntryDigits    = 4;

	/**
	 * Format a millisecond duration as mm:ss into `out`. Clamped to
	 * MaxMinutes:59 so the label never overflows the 152 px display
	 * band. Static + side-effect-free so a host (or a unit test) can
	 * exercise the formatter without standing up the screen.
	 */
	static void formatRemaining(uint32_t ms, char* out, size_t outLen);

	/**
	 * Convert a 4-digit MMSS buffer into total milliseconds. `digits`
	 * holds 0..EntryDigits ASCII '0'..'9' chars (no NUL); `count` is
	 * the number of valid leading digits. Excess seconds (>= 60) are
	 * clamped to 59 to keep the readout legal. Static for the same
	 * testability reason as formatRemaining().
	 */
	static uint32_t entryToMs(const char* digits, uint8_t count);

private:
	/** Internal run-state machine. See class doc for transitions. */
	enum class Mode : uint8_t {
		Idle    = 0,
		Running = 1,
		Paused  = 2,
		Ringing = 3
	};

	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	// Display widgets.
	lv_obj_t* captionLabel;       // "TIMER" header
	lv_obj_t* readoutLabel;       // big mm:ss value
	lv_obj_t* hintLabel;          // small mode-driven caption

	// State.
	Mode      mode            = Mode::Idle;
	char      entry[EntryDigits + 1]; // 4 ASCII digits + NUL
	uint8_t   entryLen        = 0;
	uint32_t  presetMs        = 0;       // committed duration at start
	uint32_t  remainingMs     = 0;       // remaining when paused; live source while running
	uint32_t  runStartMs      = 0;       // millis() at the most-recent start
	uint32_t  runStartRemain  = 0;       // remainingMs at the most-recent start

	bool      backLongFired   = false;   // suppresses double-fire on long BACK
	bool      pulseHi         = false;   // ringing-state pulse phase

	// LVGL tick timer (5 Hz while running) and pulse timer (4 Hz while ringing).
	lv_timer_t* tickTimer  = nullptr;
	lv_timer_t* pulseTimer = nullptr;

	void buildCaption();
	void buildReadout();
	void buildHint();

	/** Returns the live remaining value in ms (running -> from millis(); else stored). */
	uint32_t currentRemainingMs() const;

	/** Repaint the big readout from currentRemainingMs() / entry buffer. */
	void refreshReadout();
	/** Repaint the soft-key labels for the current mode. */
	void refreshSoftKeys();
	/** Repaint the small hint line for the current mode. */
	void refreshHint();

	/** Apply a state transition (and start/stop the appropriate timers). */
	void enterIdle(bool clearEntry);
	void enterRunning();
	void enterPaused();
	void enterRinging();

	/** Idle-only: append a single digit to the MMSS buffer (shift-left). */
	void appendDigit(char c);
	/** Idle-only: drop the most-recent digit. */
	void backspaceEntry();
	/** Discard the entry buffer + reset to a clean Idle. */
	void clearAll();

	/** Left-softkey action (start/pause/resume/dismiss). */
	void primaryAction();
	/** Right-softkey action (clear/reset/dismiss). */
	void secondaryAction();

	/** Lifetime helpers for the LVGL timers. */
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

#endif // MAKERPHONE_PHONETIMER_H
