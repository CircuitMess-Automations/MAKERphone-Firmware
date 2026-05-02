#ifndef MAKERPHONE_PHONETIMERS_H
#define MAKERPHONE_PHONETIMERS_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneTimers - S125
 *
 * Phase-P utility screen: a *multi-timer* countdown list that extends
 * the single-shot PhoneTimer (S62). Slots in next to PhoneAlarmClock
 * (S124) inside the eventual utility-apps grid; the same Sony-Ericsson
 * silhouette every other Phone* screen wears so a user navigating
 * through them feels at home immediately:
 *
 *   List view (default):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |              TIMERS                    | <- pixelbasic7 caption
 *   |   > 1   RUN    04:32                   |
 *   |     2   ---    --:--                   | <- one row per slot
 *   |     3   PSE    01:15                   |
 *   |     4   RING   00:00                   | <- pulses on Ringing
 *   |   START                       EDIT     | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 *   Edit view (typing MMSS for one slot):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### |
 *   |              EDIT 1                    |
 *   |                                        |
 *   |             05:00                      | <- pixelbasic16 entry
 *   |                                        |
 *   |   ENTER MM:SS WITH DIGITS              |
 *   |   SAVE                       CLEAR     |
 *   +----------------------------------------+
 *
 * Per-slot state machine (independent for each of MaxTimers slots):
 *   Idle    -- preset/remaining is 0 or a previously-armed value; not
 *              counting. The "---" mnemonic in the list.
 *   Running -- counting down toward zero; remainingMs derived from
 *              millis() and a snapshot taken at the most-recent start.
 *   Paused  -- countdown halted; remainingMs preserves the snapshot.
 *   Ringing -- countdown reached zero. The screen pulses the row in
 *              MP_ACCENT and asserts the global PhoneRingtoneEngine
 *              alarm melody. Only one slot rings audibly at a time
 *              (the lowest-index ringing slot owns the engine); other
 *              ringing slots still pulse visually so the user can see
 *              their queue.
 *
 * Top-level mode (whole-screen):
 *   List -- default. Cursor on a single slot; soft-keys reflect that
 *           slot's state. Pressing LEFT toggles the slot's state.
 *   Edit -- digit entry into a 4-char MMSS buffer for the slot the
 *           user pressed RIGHT/BTN_5 on. SAVE applies, BACK cancels.
 *
 * Controls (List):
 *   - BTN_2 / BTN_8 : move cursor up / down (wraps).
 *   - BTN_LEFT / BTN_L / BTN_ENTER : "primary" softkey for the
 *     highlighted slot:
 *       * Idle    -> START (no-op if preset is 0)
 *       * Running -> PAUSE
 *       * Paused  -> RESUME
 *       * Ringing -> DISMISS (re-load preset, return slot to Idle)
 *   - BTN_RIGHT / BTN_R : "secondary" softkey for the highlighted slot:
 *       * Idle    -> EDIT (enter Edit mode for this slot)
 *       * Running -> RESET (slot back to Idle, preset preserved)
 *       * Paused  -> RESET
 *       * Ringing -> DISMISS
 *   - BTN_5 : alternative EDIT shortcut for the highlighted slot.
 *   - BTN_BACK : pop the screen.
 *
 * Controls (Edit):
 *   - BTN_0..BTN_9 : append a digit to the MMSS buffer (shift-left
 *     once full, mirroring PhoneTimer's MM:SS entry).
 *   - BTN_LEFT / BTN_L / BTN_ENTER : SAVE the entered MMSS as the
 *     slot's preset. If the slot was Running/Paused, it is reset to
 *     Idle so the new preset is the next thing that will run.
 *   - BTN_RIGHT / BTN_R : CLEAR (backspace one digit). Long-press
 *     wipes the buffer.
 *   - BTN_BACK : cancel - return to List without committing.
 *
 * Implementation notes:
 *   - 100 % code-only (no SPIFFS assets). Reuses PhoneSynthwaveBg /
 *     PhoneStatusBar / PhoneSoftKeyBar so the screen drops into the
 *     family without a visual seam.
 *   - One 5 Hz tick (200 ms) drives countdowns for every Running slot;
 *     a separate 4 Hz pulse (250 ms) animates ringing rows. Both
 *     timers are owned by the screen and torn down on detach.
 *   - The ringing trigger is the global `Ringtone` engine (S39); a
 *     muted device gets the visual cue without piezo output, exactly
 *     like PhoneTimer / PhoneAlarmClock.
 *   - Reuses PhoneTimer's static formatRemaining() / entryToMs()
 *     helpers so the formatter and the shift-left buffer rules match
 *     S62's exactly - same muscle memory, shared regression surface.
 *   - millis() is Arduino's monotonic ms clock. Wraps at ~49.7 days,
 *     well past any realistic timer session. uint32_t subtraction
 *     gives a correct delta as long as a single run is shorter than
 *     2^31 ms.
 */
class PhoneTimers : public LVScreen, private InputListener {
public:
	PhoneTimers();
	virtual ~PhoneTimers() override;

	void onStart() override;
	void onStop() override;

	/** Number of independent timer slots exposed in the list. Four
	 *  matches the MaxAlarms convention from PhoneAlarmService and is
	 *  the right number to fit cleanly inside the 88 px list band on
	 *  the 160x128 display without paging. */
	static constexpr uint8_t  MaxTimers      = 4;

	/** MMSS digit-buffer length (matches PhoneTimer's EntryDigits). */
	static constexpr uint8_t  EntryDigits    = 4;

	/** Tick period (ms) -- 5 Hz seconds-resolution updates. */
	static constexpr uint16_t TickPeriodMs   = 200;

	/** Pulse cadence (ms) for Ringing-row visual pulse. */
	static constexpr uint16_t PulsePeriodMs  = 250;

	/** Long-press threshold for BTN_BACK / BTN_RIGHT, matches the
	 *  rest of the MAKERphone shell so the gesture is consistent. */
	static constexpr uint16_t BackHoldMs     = 600;

private:
	/** Per-slot state machine. Mirrors PhoneTimer::Mode. */
	enum class SlotState : uint8_t {
		Idle    = 0,
		Running = 1,
		Paused  = 2,
		Ringing = 3,
	};

	/** Top-level UI mode: list (default) vs digit-entry edit. */
	enum class Mode : uint8_t {
		List = 0,
		Edit = 1,
	};

	/** Per-slot countdown record. Compact POD; four of these live in
	 *  the screen as `slots[]`. */
	struct Slot {
		SlotState state          = SlotState::Idle;
		uint32_t  presetMs       = 0;   // committed duration
		uint32_t  remainingMs    = 0;   // preserved snapshot when Paused/Idle
		uint32_t  runStartMs     = 0;   // millis() at the most-recent start
		uint32_t  runStartRemain = 0;   // remainingMs at the most-recent start
	};

	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	// List-view widgets.
	lv_obj_t* captionLabel;
	lv_obj_t* rows[MaxTimers];

	// Edit-view widgets (re-used as the bigReadout/hint pair). Hidden
	// while in List mode.
	lv_obj_t* bigCaptionLabel;   // "EDIT 1"
	lv_obj_t* bigReadoutLabel;   // pixelbasic16 "MM:SS"
	lv_obj_t* hintLabel;         // small mode-driven caption

	Slot      slots[MaxTimers];

	Mode      mode             = Mode::List;
	uint8_t   cursor           = 0;     // 0..MaxTimers-1
	uint8_t   editingSlot      = 0;     // valid only in Edit mode
	char      entry[EntryDigits + 1];   // 4 ASCII digits + NUL
	uint8_t   entryLen         = 0;
	bool      backLongFired    = false;
	bool      pulseHi          = false;

	// LVGL tick timer (5 Hz while any slot is Running) and pulse timer
	// (4 Hz while any slot is Ringing). Both are idempotent - calling
	// startTickTimer() twice is a no-op.
	lv_timer_t* tickTimer  = nullptr;
	lv_timer_t* pulseTimer = nullptr;

	// Build helpers - invoked once from the ctor.
	void buildList();
	void buildEditView();

	/** Returns the live remaining-ms for a slot (Running -> derived
	 *  from millis(); other states -> stored). */
	uint32_t currentRemainingMs(const Slot& s) const;

	/** Clamp a slot's remainingMs to currentRemainingMs() and reset
	 *  runStart fields. Called when transitioning out of Running. */
	void snapshotRunning(Slot& s);

	/** Apply a per-slot state transition (and (re)start the relevant
	 *  global LVGL timers). */
	void slotEnterIdle(uint8_t i, bool clearRemaining);
	void slotEnterRunning(uint8_t i);
	void slotEnterPaused(uint8_t i);
	void slotEnterRinging(uint8_t i);

	// Top-level mode transitions.
	void enterList();
	void enterEdit(uint8_t slotIdx);

	// Edit-mode buffer manipulation.
	void appendDigit(char c);
	void backspaceEntry();
	void commitEntry();

	// Cursor.
	void cursorUp();
	void cursorDown();

	// Softkey actions, dispatched per current mode + cursor slot state.
	void primaryAction();
	void secondaryAction();

	// Repaint helpers - full redraws are cheap enough at this size
	// that we just rebuild whatever changed every tick.
	void refreshAll();
	void refreshList();
	void refreshEdit();
	void refreshSoftKeys();

	// Whether anything in the list is currently running / ringing -
	// drives the lifecycle of tickTimer and pulseTimer respectively.
	bool anyRunning() const;
	bool anyRinging() const;
	int8_t firstRingingSlot() const;

	// Audio control. Asserts/releases the global Ringtone engine to
	// match the current ringing-set membership.
	void syncRingtone();

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

#endif // MAKERPHONE_PHONETIMERS_H
