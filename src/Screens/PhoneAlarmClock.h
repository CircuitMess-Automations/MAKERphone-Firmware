#ifndef MAKERPHONE_PHONEALARMCLOCK_H
#define MAKERPHONE_PHONEALARMCLOCK_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"
#include "../Services/PhoneAlarmService.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneAlarmClock — S124
 *
 * Phase-P utility screen: multi-alarm clock with snooze. Slots in
 * next to PhoneCalculator (S60) / PhoneStopwatch (S61) / PhoneTimer
 * (S62) inside the eventual utility-apps grid. Same Sony-Ericsson
 * silhouette every other Phone* screen wears so a user navigating
 * through them feels at home immediately:
 *
 *   List view (default):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |               ALARMS                   | <- pixelbasic7 caption
 *   |   > 1   ON     07:00                   |
 *   |     2   --     12:30                   | <- PhoneAlarmService rows
 *   |     3   --     18:00                   |
 *   |     4   --     22:30                   |
 *   |   TOGGLE                       EDIT    | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 *   Edit view (typing HHMM):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### |
 *   |              EDIT 1                    |
 *   |                                        |
 *   |             07:30                      | <- pixelbasic16 entry
 *   |                                        |
 *   |   ENTER HHMM WITH DIGITS               |
 *   |   SAVE                       CLEAR     |
 *   +----------------------------------------+
 *
 *   Firing view (ringtone alerting the user):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### |
 *   |              ALARM!                    | <- pulsing accent
 *   |             07:00                      | <- pixelbasic16
 *   |                                        |
 *   |   SNOOZE                     DISMISS   |
 *   +----------------------------------------+
 *
 * State machine:
 *   List   -- read-only overview, cursor on a single slot.
 *   Edit   -- digit entry into a 4-char HHMM buffer; commit applies.
 *   Firing -- the global PhoneAlarmService is in `firing` state. The
 *             screen overrides the layout with the alarm modal and
 *             the soft-keys become SNOOZE / DISMISS.
 *
 * Controls (List):
 *   - BTN_2 / BTN_8 : move cursor up / down (wraps at the ends).
 *   - BTN_LEFT / BTN_L / BTN_ENTER : toggle the highlighted alarm
 *     between enabled and disabled.
 *   - BTN_RIGHT / BTN_R : enter Edit mode for the highlighted slot.
 *   - BTN_5 : alternative "edit" shortcut (the 5 key is the centre
 *     of the dialer pad and the Sony Ericsson "open" muscle memory).
 *   - BTN_BACK : pop the screen.
 *
 * Controls (Edit):
 *   - BTN_0..BTN_9 : append a digit to the HHMM buffer (shift-left
 *     once the buffer is full, mirroring PhoneTimer's MM:SS entry).
 *   - BTN_LEFT / BTN_L / BTN_ENTER : SAVE the entered HHMM into the
 *     slot, return to List. An empty / partial buffer commits the
 *     current value (so a user who reopens edit just to confirm has
 *     to type nothing).
 *   - BTN_RIGHT / BTN_R : CLEAR — backspace one digit.
 *   - BTN_BACK : cancel — return to List without committing.
 *
 * Controls (Firing):
 *   - BTN_LEFT / BTN_L / BTN_ENTER : SNOOZE for 5 minutes, return
 *     to List view. The ringtone stops.
 *   - BTN_RIGHT / BTN_R / BTN_BACK : DISMISS — stop the ringtone,
 *     return to List view. Snooze state is cleared.
 *
 * Implementation notes:
 *   - 100 % code-only, no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *     PhoneStatusBar / PhoneSoftKeyBar so the screen drops into the
 *     family without a visual seam.
 *   - The ringing UI is a repaint of the same labels (visibility +
 *     text), not a separate child screen. This avoids any LVGL screen
 *     swap during the pulse, which would interrupt the wallpaper
 *     animation under the alarm modal.
 *   - The screen is not the source of truth — PhoneAlarmService
 *     persists the slots and runs the firing detector regardless of
 *     whether this screen is open. The screen just renders + edits.
 */
class PhoneAlarmClock : public LVScreen, private InputListener {
public:
	PhoneAlarmClock();
	virtual ~PhoneAlarmClock() override;

	void onStart() override;
	void onStop() override;

	/** Number of HHMM digits the entry buffer accepts. */
	static constexpr uint8_t  EntryDigits     = 4;

	/** Pulse cadence (ms) for the firing-state visual pulse. */
	static constexpr uint16_t PulsePeriodMs   = 250;

	/** Long-press threshold for BTN_BACK (matches the rest of the shell). */
	static constexpr uint16_t BackHoldMs      = 600;

	/** UI poll cadence (ms) so the firing modal lights up promptly
	 *  once the background service decides to ring. */
	static constexpr uint16_t PollPeriodMs    = 200;

private:
	/** Internal state machine. See class doc for transitions. */
	enum class Mode : uint8_t {
		List   = 0,
		Edit   = 1,
		Firing = 2,
	};

	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	// List view widgets.
	lv_obj_t* captionLabel;
	lv_obj_t* rows[PhoneAlarmService::MaxAlarms];

	// Edit / Firing big-readout widgets (re-used between modes).
	lv_obj_t* bigLabel;
	lv_obj_t* hintLabel;

	Mode      mode      = Mode::List;
	uint8_t   cursor    = 0;
	uint8_t   editingSlot = 0;

	// HHMM digit-entry buffer. Mirrors the PhoneTimer entry pattern so
	// users have the same muscle memory across utility screens.
	char      entry[EntryDigits + 1];
	uint8_t   entryLen = 0;

	bool      backLongFired = false;
	bool      pulseHi       = false;

	lv_timer_t* pollTimer  = nullptr;
	lv_timer_t* pulseTimer = nullptr;

	void buildList();
	void buildBigReadout();
	void refreshAll();
	void refreshList();
	void refreshEdit();
	void refreshFiring();
	void refreshSoftKeys();
	void refreshHint();

	void enterList();
	void enterEdit(uint8_t slot);
	void enterFiring();

	void appendDigit(char c);
	void backspaceEntry();
	void commitEntry();

	void cursorUp();
	void cursorDown();

	void primaryAction();    // left softkey
	void secondaryAction();  // right softkey

	void startPollTimer();
	void stopPollTimer();
	void startPulseTimer();
	void stopPulseTimer();

	static void onPollStatic(lv_timer_t* timer);
	static void onPulseStatic(lv_timer_t* timer);

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONEALARMCLOCK_H
