#ifndef MAKERPHONE_PHONESCRATCHPAD_H
#define MAKERPHONE_PHONESCRATCHPAD_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;
class PhoneT9Input;

/**
 * PhoneScratchpad - S140
 *
 * Phase-Q "Pocket Organiser" instant quick-jot pad. Sits next to
 * PhoneTodo (S136), PhoneHabits (S137), PhonePomodoro (S138) and
 * PhoneMoodLog (S139) inside the eventual organiser-apps grid. The
 * difference from PhoneNotepad (S64) is intentional: Notepad keeps a
 * list of up to MaxNotes saved notes you navigate between, while the
 * Scratchpad holds exactly one persistent buffer that you drop into,
 * jot a thought, and walk away from. No list view, no slot indices,
 * no "NEW vs OPEN" picker -- the screen always boots straight into
 * the same single buffer the user left behind on the previous visit.
 *
 *   View (always edit-mode -- there is only one buffer):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |             SCRATCHPAD                 | <- caption (cyan, pixelbasic7)
 *   |  ------------------------------------- |
 *   |  +----------------------------------+ |
 *   |  | hello wo|                        | | <- PhoneT9Input (S32)
 *   |  | abc                          Abc | |
 *   |  +----------------------------------+ |
 *   |                                        |
 *   |  8 of 200 chars                        | <- live char counter
 *   |  * UNSAVED                             | <- dirty marker
 *   |  ------------------------------------- |
 *   |  CLEAR                          DONE   | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Persistence
 *   - One blob in the "mpscratch" NVS namespace -- same lazy-open
 *     pattern as PhoneTodo (S136), PhoneHabits (S137), PhoneMoodLog
 *     (S139) and PhoneVirtualPet (S129). Layout is:
 *
 *         offset 0 : 'M'  (magic 0)
 *         offset 1 : 'P'  (magic 1)
 *         offset 2 : version (1)
 *         offset 3 : reserved (0)
 *         offset 4 : length low byte
 *         offset 5 : length high byte
 *         offset 6.. : `length` UTF-8 bytes (no terminator on disk)
 *
 *     Loading is best-effort: if the magic / version mismatches or
 *     the partition is unavailable we silently fall back to an
 *     empty buffer so the app still works (matches the rest of the
 *     organiser apps' "soft offline" behaviour).
 *
 *   - The buffer is auto-saved when the user exits the screen via
 *     the DONE softkey or BACK (short or long). It is also saved
 *     on CLEAR. There is intentionally no auto-save on every
 *     keystroke -- a flush-on-leave matches the "instant quick-jot"
 *     framing in the roadmap (S140) and keeps NVS write traffic
 *     minimal so we don't shorten flash lifetime on heavy editors.
 *
 * Controls (single edit view):
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
 *   - The dirty marker repaints from the T9 widget's onTextChanged
 *     callback so the user gets immediate feedback while typing,
 *     and again from the persistence path so saving turns it off.
 */
class PhoneScratchpad : public LVScreen, private InputListener {
public:
	PhoneScratchpad();
	virtual ~PhoneScratchpad() override;

	void onStart() override;
	void onStop() override;

	/** Hard cap on the buffer length. 200 keeps the Scratchpad
	 *  noticeably roomier than PhoneNotepad's 120-char slots so it
	 *  reads as a "longer one-shot" pad rather than a duplicate of
	 *  Notepad. Still well under the SMS-payload ceiling so a future
	 *  "send as SMS" hook would fit cleanly.
	 */
	static constexpr uint16_t MaxLen     = 200;

	/** Long-press threshold (matches the rest of the MAKERphone shell). */
	static constexpr uint16_t BackHoldMs = 600;

	/** Read-only accessors useful for tests and future hosts. */
	uint16_t getLength() const;
	bool     isDirty()   const { return dirty; }
	const char* getText() const;

	/**
	 * Trim leading + trailing ASCII whitespace from `in` into `out`.
	 * Static + side-effect-free so a host (or a test) can sanity-
	 * check the trim semantics without standing up the screen.
	 * Always nul-terminates `out` when `outLen > 0`.
	 */
	static void trimText(const char* in, char* out, size_t outLen);

	/**
	 * Force-flush the live buffer to NVS. Public so a future shell
	 * (e.g. low-battery shutdown hook) can persist the in-flight
	 * scratch text without ripping the user out of the screen.
	 */
	void persist();

	/** Wipe the buffer and persist. Public for the same reason as
	 *  persist() -- a host or test can drive it without synthesising
	 *  a softkey press.
	 */
	void clearBuffer();

private:
	PhoneSynthwaveBg* wallpaper = nullptr;
	PhoneStatusBar*   statusBar = nullptr;
	PhoneSoftKeyBar*  softKeys  = nullptr;

	// View widgets (single edit view -- always mounted).
	lv_obj_t*     captionLabel = nullptr;     // "SCRATCHPAD"
	lv_obj_t*     topDivider   = nullptr;     // 1 px line under caption
	lv_obj_t*     bottomDivider= nullptr;     // 1 px line above softkeys
	lv_obj_t*     charCounter  = nullptr;     // "X of 200 chars"
	lv_obj_t*     dirtyLabel   = nullptr;     // "* UNSAVED" / "SAVED"
	PhoneT9Input* t9Input      = nullptr;     // S32 multi-tap entry

	// State
	bool dirty           = false;     // buffer differs from disk-snapshot
	bool backLongFired   = false;     // suppresses double-fire on hold

	// ---- builders ----
	void buildView();

	// ---- repainters ----
	void refreshCaption();
	void refreshSoftKeys();
	void refreshCharCounter();
	void refreshDirty();

	// ---- persistence ----
	bool loadFromNvs(char* outBuf, uint16_t outBufLen, uint16_t& lengthOut);
	bool saveToNvs(const char* text, uint16_t length);

	// ---- input ----
	void onClearPressed();
	void onDonePressed();

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONESCRATCHPAD_H
