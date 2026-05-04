#ifndef MAKERPHONE_PHONEDIALERSCREEN_H
#define MAKERPHONE_PHONEDIALERSCREEN_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;
class PhoneDialerPad;

/**
 * PhoneDialerScreen
 *
 * The MAKERphone 2.0 number-entry screen (S23). First Phase-D screen and
 * the natural successor to the PhoneAppStubScreen("PHONE") that the main
 * menu wired to as a placeholder. Composes the existing Phase-A widgets
 * (PhoneSynthwaveBg, PhoneStatusBar, PhoneSoftKeyBar, PhoneDialerPad)
 * into the unmistakable Sony-Ericsson dialer silhouette:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |                                        |
 *   |              5551234_                  | <- digit-buffer label
 *   |                                        |
 *   |       +----++----++----+               |
 *   |       | 1  || 2  || 3  |               |
 *   |       +----++----++----+               | <- PhoneDialerPad
 *   |       +----++----++----+                  (3x4 keymap)
 *   |       | 4  || 5  || 6  |               |
 *   |       +----++----++----+               |
 *   |       +----++----++----+               |
 *   |       | 7  || 8  || 9  |               |
 *   |       +----++----++----+               |
 *   |       +----++----++----+               |
 *   |       | *  || 0  || #  |               |
 *   |       +----++----++----+               |
 *   |                                        |
 *   |  CALL                            BACK->| <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * S23 ships the screen *class* + wires it as the destination of both the
 * "PHONE" tile on PhoneMainMenu (replacing the stub) and the long-press-0
 * quick-dial gesture on PhoneHomeScreen / PhoneMainMenu (S22). S24-S28
 * layer the actual call screens (incoming, active, ended, history) on top
 * of this entry point - the dialer's "CALL" softkey will eventually push
 * the PhoneActiveCall screen with the typed buffer; for S23 the press
 * just flashes the softkey and does nothing else, so the screen is fully
 * driveable today without prematurely committing to a call-flow design.
 *
 * Implementation notes:
 *  - Code-only - no SPIFFS assets. Reuses PhoneDialerPad (S10) verbatim
 *    and the standard top/bottom bars so the screen feels visually part
 *    of the same MAKERphone family. Data partition cost stays zero.
 *  - 160x128 budget: 10 px status bar at top, 10 px softkey bar at the
 *    bottom, ~16 px digit buffer in between, the 88x114 pad anchored
 *    centred just under the buffer. Geometry leaves 2 px breathing room
 *    above the softkey bar so an accidental tail glyph in the typed
 *    number does not clip the bottom of the keypad.
 *  - The user can either navigate the pad with arrow keys + ENTER (the
 *    PhoneDialerPad cursor) or press the physical numpad keys
 *    (BTN_0..BTN_9) directly. PhoneDialerPad::pressGlyph() already
 *    handles both paths, including moving the cursor to the matched
 *    key so subsequent arrow-presses start from the expected spot.
 *  - BTN_BACK behaviour matches feature-phone muscle memory: a short
 *    press deletes the last digit (backspace); a long-press (>= 600 ms)
 *    pops back to whichever screen pushed us. Long-press detection is
 *    enabled exactly the same way PhoneHomeScreen / PhoneMainMenu do
 *    it (setButtonHoldTime + a guard flag so the matching short-press
 *    does not double-fire on key release).
 *  - The "CALL" softkey only fires when the buffer has at least one
 *    digit. With an empty buffer it just plays the press flash so the
 *    user gets a tactile cue without launching a call to nothing.
 *  - getBuffer() / setBuffer() / clearBuffer() are exposed up-front so
 *    a future caller (the long-press-0 quick-dial in S22) can pre-load
 *    the screen with the user's quick-dial number without subclassing.
 */
class PhoneDialerScreen : public LVScreen, private InputListener {
public:
	PhoneDialerScreen();
	virtual ~PhoneDialerScreen() override;

	void onStart() override;
	void onStop() override;

	using SoftKeyHandler = void (*)(PhoneDialerScreen* self);

	/**
	 * Bind a callback to BTN_LEFT (the "CALL" softkey). Pass nullptr to
	 * clear. The handler can call `getBuffer()` to find out which number
	 * the user dialed. With an empty buffer the handler is NOT invoked
	 * (we only flash the softkey) so call code does not have to check.
	 */
	void setOnCall(SoftKeyHandler cb);

	/**
	 * Bind a callback to a long-press of BTN_BACK ("hold to exit").
	 * Default (when nullptr) is to fall back to `pop()` so a host that
	 * pushed us still gets the screen back when the user holds Back.
	 */
	void setOnExit(SoftKeyHandler cb);

	/** Replace the visible label of the left softkey (default "CALL"). */
	void setLeftLabel(const char* label);

	/** Replace the visible label of the right softkey (default "BACK"). */
	void setRightLabel(const char* label);

	/** Currently typed buffer. Includes any '*' and '#' the user added. */
	const char* getBuffer() const { return buffer; }

	/** Number of digits currently in the buffer (0..MaxDigits). */
	uint8_t getBufferLength() const { return bufferLen; }

	/**
	 * Replace the buffer with `text` (truncated to MaxDigits). Updates
	 * the on-screen label. Useful for the long-press-0 quick-dial path
	 * which ships the user's quick-dial number into a fresh dialer.
	 */
	void setBuffer(const char* text);

	/** Clear the buffer back to empty. Updates the on-screen label. */
	void clearBuffer();

	/**
	 * S21-style press-feedback flash on the left/right softkey - exposed
	 * so a host can flash from outside (e.g. when programmatically
	 * triggering a call during a transition).
	 */
	void flashLeftSoftKey();
	void flashRightSoftKey();

	/** Hard cap on the digit buffer length (fits on the 160 px display). */
	static constexpr uint8_t MaxDigits = 18;

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;
	PhoneDialerPad*   pad;

	lv_obj_t* bufferLabel;   // pixelbasic16 typed digits
	lv_obj_t* hintLabel;     // pixelbasic7 placeholder when empty

	// Buffer is a fixed-size char array so it fits in the screen's own
	// allocation without a separate heap touch. +1 for the trailing NUL.
	char    buffer[MaxDigits + 1];
	uint8_t bufferLen = 0;

	SoftKeyHandler callCb = nullptr;
	SoftKeyHandler exitCb = nullptr;

	// Same long-press-suppression flag pattern PhoneHomeScreen / PhoneMainMenu
	// use - keeps the two short-press / long-press paths from double-firing.
	bool backLongFired = false;

	// S167 - long-press BTN_5 on the dialer is a feature-phone-style
	// quick-shortcut that pops the PhoneFlashlight utility (S134) into
	// view, mirroring the way real Sony-Ericsson handsets parked the
	// torch under a hold gesture. The short-press path still appends
	// '5' to the buffer the moment the user taps the key, so dialling
	// 555... stays snappy; on a long-hold we revert the speculative
	// '5' (or any digits the user might have queued in the meantime,
	// e.g. via auto-repeat) and push the flashlight screen.
	//
	// fivePreBufferLen   -- bufferLen snapshot taken in buttonPressed
	//                       before pad->pressGlyph('5') runs, so
	//                       buttonHeld can roll the buffer back to
	//                       exactly where it was before the press.
	// fiveLongFired      -- true once buttonHeld(BTN_5) has fired the
	//                       flashlight push, so a future short-press
	//                       handler (none today, but keeps parity
	//                       with the existing backLongFired pattern)
	//                       cannot double-fire on key release.
	uint8_t fivePreBufferLen = 0;
	bool    fiveLongFired    = false;

	/** Push a fresh PhoneFlashlight (S134) on top of the dialer.
	 *  Forward-declared in the .cpp so the header doesn't have to
	 *  pull the full flashlight screen include into every translation
	 *  unit that touches PhoneDialerScreen. */
	void launchFlashlight();

	// S172 - "Daily fortune in dialer (first open per day)".
	//
	// The classic Sony-Ericsson handsets used to drop a tiny
	// inspirational greeting on the dialer the first time you
	// opened it after midnight - "Have a beautiful day", that
	// kind of thing. We replicate the gesture here by overlaying
	// a code-only "FORTUNE OF THE DAY" strip on top of the
	// keypad whenever PhoneDialerScreen::onStart() detects the
	// wall-clock day index has rolled forward since the last
	// open. The fortune string itself is pulled from the same
	// 32-entry rotation the PhoneFortuneCookie utility (S133)
	// uses, so the toy and the morning greeting stay in lockstep.
	//
	// Lifecycle:
	//   - s_fortuneLastDayIdx is a class-static that survives
	//     across screen pushes, so re-entering the dialer on
	//     the same day does NOT re-trigger the strip. It is
	//     initialised to UINT32_MAX so the first ever open
	//     after a fresh boot always shows the fortune.
	//   - The overlay is built once in the ctor (hidden) and
	//     toggled visible inside showDailyFortune(). It carries
	//     its own auto-dismiss lv_timer which fires after
	//     FortuneAutoDismissMs and falls back into the keypad
	//     view. Any button press also dismisses the strip
	//     immediately (the press itself still propagates so
	//     a user typing the moment the dialer opens does not
	//     lose the digit).
	//   - hideDailyFortune() is idempotent and safe to call
	//     from onStop() / dtor / button handlers.
	//
	// Geometry: a 144 x 64 strip centred on the screen, sitting
	// over the top half of the keypad so the whole fortune
	// reads in one glance. Background is a high-opacity dark
	// purple so the keypad shows through faintly underneath -
	// the user gets a hint of "the dialer is still here, this
	// is just a greeting" rather than a hard takeover.
	void showDailyFortune();
	void hideDailyFortune();
	static void onFortuneAutoDismissStatic(lv_timer_t* timer);
	void buildFortuneOverlay();

	/** Auto-dismiss the fortune-of-the-day strip after this many ms.
	 *  4 s is long enough to read the longest entry in the kFortunes
	 *  table (~50 chars wraps to two lines at pixelbasic7) without
	 *  feeling like the dialer is unresponsive when the user wants
	 *  to start typing immediately. */
	static constexpr uint32_t FortuneAutoDismissMs = 4000;

	/** Day-index of the last time the dialer showed the fortune-of-
	 *  the-day strip. Persists across screen pushes via the static
	 *  storage class so re-entering the dialer on the same wall-
	 *  clock day stays quiet. UINT32_MAX = "never shown yet" so
	 *  the very first open after boot always greets the user. */
	static uint32_t s_fortuneLastDayIdx;

	void buildBufferLabel();
	void buildPad();
	void appendGlyph(char c);
	void backspace();
	void refreshBufferLabel();

	// S172 - fortune-of-the-day overlay objects + transient state.
	// Built once in the ctor and toggled visible/hidden via
	// showDailyFortune() / hideDailyFortune(). The objects are
	// parented to `obj` so LVGL frees them with the screen.
	lv_obj_t*   fortuneOverlay  = nullptr;  // dark backdrop strip
	lv_obj_t*   fortuneCaption  = nullptr;  // "FORTUNE OF THE DAY"
	lv_obj_t*   fortuneText     = nullptr;  // wrapped wisdom text
	lv_obj_t*   fortuneFooter   = nullptr;  // "ANY KEY TO DISMISS"
	lv_timer_t* fortuneTimer    = nullptr;  // auto-dismiss timer
	bool        fortuneVisible  = false;

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONEDIALERSCREEN_H
