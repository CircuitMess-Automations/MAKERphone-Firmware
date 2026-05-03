#ifndef MAKERPHONE_PHONEOWNERNAMESCREEN_H
#define MAKERPHONE_PHONEOWNERNAMESCREEN_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;
class PhoneT9Input;

/**
 * PhoneOwnerNameScreen - S144
 *
 * SYSTEM-section sub-screen that lets the user T9-type the "owner
 * name" shown on the lock screen. Reached from PhoneSettingsScreen's
 * "Owner name" row (S144 wires it just above About inside the SYSTEM
 * group). The buffer is persisted into Settings.ownerName (the new
 * S144 field on SettingsData) so the LockScreen can read it on every
 * push and tuck a retro "ALBERT"-style greeting between the status
 * bar and the clock face.
 *
 *   View (always edit-mode -- there is exactly one buffer):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |             OWNER NAME                 | <- caption (cyan, pixelbasic7)
 *   |  ------------------------------------- |
 *   |  +----------------------------------+ |
 *   |  | ALBERT|                          | | <- PhoneT9Input (S32)
 *   |  | abc                          Abc | |
 *   |  +----------------------------------+ |
 *   |                                        |
 *   |  6 of 23 chars                         | <- live char counter
 *   |  * UNSAVED                             | <- dirty marker
 *   |  ------------------------------------- |
 *   |  CLEAR                          DONE   | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Persistence
 *   - Backed directly by Settings.ownerName (the S144 fixed-width
 *     24-byte field on SettingsData), persisted via the existing
 *     SettingsImpl::store() call. No separate NVS namespace -- this
 *     value is part of the same blob that already holds
 *     soundProfile / wallpaperStyle / themeId / keyTicks / etc.
 *   - The buffer is auto-saved on the way out (DONE softkey, BACK
 *     short, BACK long) so an instant edit never loses content. CLEAR
 *     wipes the live buffer and the persisted value in one shot.
 *
 * Controls (single edit view, mirrors PhoneScratchpad / PhoneNotepad):
 *   - BTN_0..BTN_9                : T9 multi-tap (PhoneT9Input).
 *   - BTN_L                       : T9 backspace (forwards '*').
 *   - BTN_R                       : T9 case toggle (forwards '#').
 *   - BTN_ENTER                   : commit the in-flight pending letter.
 *   - BTN_LEFT softkey ("CLEAR")  : wipe the buffer + persist + flash.
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
 *   - MaxLen is 23 (fits in the 24-byte ownerName buffer with one
 *     byte reserved for the nul terminator). The lock screen further
 *     dot-truncates the string to the available 156 px wide strip
 *     so an over-long entry never wraps or clips into the clock face.
 */
class PhoneOwnerNameScreen : public LVScreen, private InputListener {
public:
	PhoneOwnerNameScreen();
	virtual ~PhoneOwnerNameScreen() override;

	void onStart() override;
	void onStop() override;

	/** Hard cap on the editable buffer (matches the 24-byte
	 *  Settings.ownerName field minus the trailing nul). */
	static constexpr uint16_t MaxLen     = 23;

	/** Long-press threshold (matches the rest of the MAKERphone shell). */
	static constexpr uint16_t BackHoldMs = 600;

	/** Read-only accessors useful for tests and future hosts. */
	uint16_t getLength() const;
	bool     isDirty()   const { return dirty; }
	const char* getText() const;

	/**
	 * Force-flush the live buffer into Settings.ownerName + persist.
	 * Public so a future shell (e.g. low-battery shutdown hook) can
	 * persist the in-flight name without ripping the user out of
	 * the screen.
	 */
	void persist();

	/** Wipe the buffer and persist. Public for the same reason as
	 *  persist() -- a host or test can drive it without synthesising
	 *  a softkey press. */
	void clearBuffer();

private:
	PhoneSynthwaveBg* wallpaper = nullptr;
	PhoneStatusBar*   statusBar = nullptr;
	PhoneSoftKeyBar*  softKeys  = nullptr;

	// View widgets (single edit view -- always mounted).
	lv_obj_t*     captionLabel = nullptr;     // "OWNER NAME"
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

#endif // MAKERPHONE_PHONEOWNERNAMESCREEN_H
