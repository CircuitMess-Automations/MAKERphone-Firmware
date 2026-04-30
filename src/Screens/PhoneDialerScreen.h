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

	void buildBufferLabel();
	void buildPad();
	void appendGlyph(char c);
	void backspace();
	void refreshBufferLabel();

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONEDIALERSCREEN_H
