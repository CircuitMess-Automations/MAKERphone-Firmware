#ifndef MAKERPHONE_PHONEPOWEROFFMESSAGESCREEN_H
#define MAKERPHONE_PHONEPOWEROFFMESSAGESCREEN_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;
class PhoneT9Input;

/**
 * PhonePowerOffMessageScreen - S146
 *
 * SYSTEM-section sub-screen that lets the user T9-type the "power-off
 * message" painted over the phosphor plate during the PhonePowerDown
 * CRT-shrink animation. Reached from PhoneSettingsScreen's "Power-off
 * msg" row (S146 wires it just above About inside the SYSTEM group,
 * one row below S144's "Owner name"). The buffer is persisted into
 * Settings.powerOffMessage (the new S146 fixed-width 24-byte field on
 * SettingsData) so PhonePowerDown can read it on every push and
 * append a preamble phase that holds the plate at full brightness
 * while the message is centred on it in deep-purple pixelbasic16 --
 * the classic Sony-Ericsson "Bye!" flourish.
 *
 *   View (always edit-mode -- there is exactly one buffer):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |          POWER-OFF MSG                 | <- caption (cyan, pixelbasic7)
 *   |  ------------------------------------- |
 *   |  +----------------------------------+ |
 *   |  | BYE ALBERT|                      | | <- PhoneT9Input (S32)
 *   |  | abc                          Abc | |
 *   |  +----------------------------------+ |
 *   |                                        |
 *   |  10 of 23 chars                        | <- live char counter
 *   |  * UNSAVED                             | <- dirty marker
 *   |  ------------------------------------- |
 *   |  CLEAR                          DONE   | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Persistence
 *   - Backed directly by Settings.powerOffMessage (the S146 fixed-width
 *     24-byte field on SettingsData), persisted via the existing
 *     SettingsImpl::store() call. No separate NVS namespace -- this
 *     value is part of the same blob that already holds
 *     soundProfile / wallpaperStyle / themeId / keyTicks / ownerName.
 *   - The buffer is auto-saved on the way out (DONE softkey, BACK
 *     short, BACK long) so an instant edit never loses content. CLEAR
 *     wipes the live buffer and the persisted value in one shot,
 *     which makes the PhonePowerDown overlay skip its preamble on
 *     subsequent power-offs (factory behaviour).
 *
 * Controls (single edit view, mirrors PhoneOwnerNameScreen one-to-one
 * so the two SYSTEM-group T9-entry sub-screens read as a single visual
 * family):
 *   - BTN_0..BTN_9                : T9 multi-tap (PhoneT9Input).
 *   - BTN_L                       : T9 backspace (forwards '*').
 *   - BTN_R                       : T9 case toggle (forwards '#').
 *   - BTN_ENTER                   : commit the in-flight pending letter.
 *   - BTN_LEFT softkey ("CLEAR")  : wipe the buffer + persist.
 *   - BTN_RIGHT softkey ("DONE")  : commit pending, persist, pop screen.
 *   - BTN_BACK short              : same as DONE (auto-save on exit).
 *   - BTN_BACK long               : same as DONE (auto-save on exit).
 *
 * Implementation notes
 *   - 100 % code-only -- no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *     PhoneStatusBar / PhoneSoftKeyBar / PhoneT9Input so the screen
 *     reads as part of the MAKERphone family. Data partition cost
 *     stays zero.
 *   - The PhoneT9Input lives for the entire screen lifetime, since
 *     this app is "always editing". Its caret / commit timers stop
 *     in the destructor via PhoneT9Input's standard tear-down so a
 *     screen pop mid-cycle never leaves a stale callback pointing
 *     into freed memory.
 *   - MaxLen is 23 (fits in the 24-byte powerOffMessage buffer with
 *     one byte reserved for the nul terminator). PhonePowerDown
 *     further dot-truncates the string to whatever the 156 px wide
 *     phosphor plate can paint cleanly so an over-long entry never
 *     wraps or clips into the plate edge.
 */
class PhonePowerOffMessageScreen : public LVScreen, private InputListener {
public:
	PhonePowerOffMessageScreen();
	virtual ~PhonePowerOffMessageScreen() override;

	void onStart() override;
	void onStop() override;

	/** Hard cap on the editable buffer (matches the 24-byte
	 *  Settings.powerOffMessage field minus the trailing nul). */
	static constexpr uint16_t MaxLen     = 23;

	/** Long-press threshold (matches the rest of the MAKERphone shell). */
	static constexpr uint16_t BackHoldMs = 600;

	/** Read-only accessors useful for tests and future hosts. */
	uint16_t getLength() const;
	bool     isDirty()   const { return dirty; }
	const char* getText() const;

	/**
	 * Force-flush the live buffer into Settings.powerOffMessage +
	 * persist. Public so a future shell (e.g. low-battery shutdown
	 * hook) can persist the in-flight message without ripping the
	 * user out of the screen.
	 */
	void persist();

	/** Wipe the buffer and persist. Public for the same reason as
	 *  persist() -- a host or test can drive it without synthesising
	 *  a softkey press. Re-skips the PhonePowerDown preamble on
	 *  subsequent shutdowns by leaving the persisted slot empty. */
	void clearBuffer();

private:
	PhoneSynthwaveBg* wallpaper = nullptr;
	PhoneStatusBar*   statusBar = nullptr;
	PhoneSoftKeyBar*  softKeys  = nullptr;

	// View widgets (single edit view -- always mounted).
	lv_obj_t*     captionLabel = nullptr;     // "POWER-OFF MSG"
	lv_obj_t*     topDivider   = nullptr;     // 1 px line under caption
	lv_obj_t*     bottomDivider= nullptr;     // 1 px line above softkeys
	lv_obj_t*     charCounter  = nullptr;     // "X of 23 chars"
	lv_obj_t*     dirtyLabel   = nullptr;     // "* UNSAVED" / "SAVED"
	PhoneT9Input* t9Input      = nullptr;     // S32 multi-tap entry

	// State
	bool dirty           = false;     // buffer differs from persisted-snapshot
	bool backLongFired   = false;     // suppresses double-fire on hold

	// ---- builders ----
	void buildView();

	// ---- repainters ----
	void refreshCaption();
	void refreshSoftKeys();
	void refreshCharCounter();
	void refreshDirty();

	// ---- input ----
	void onClearPressed();
	void onDonePressed();

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONEPOWEROFFMESSAGESCREEN_H
