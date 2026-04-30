#ifndef MAKERPHONE_PHONEAPPSTUBSCREEN_H
#define MAKERPHONE_PHONEAPPSTUBSCREEN_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneAppStubScreen
 *
 * "Not yet built" placeholder used by S20 to stand in for MAKERphone apps
 * that the roadmap has not yet implemented (Phone dialer / Music player /
 * Camera at the time of S20). Lets the user navigate to every PhoneMainMenu
 * tile and confirm the dispatch is wired correctly without crashing on a
 * non-existent app:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |                                        |
 *   |              MUSIC                     | <- pixelbasic16 app name
 *   |                                        |
 *   |          NOT YET BUILT                 | <- pixelbasic7 caption
 *   |                                        |
 *   |    coming in a future session          | <- pixelbasic7 sub-caption
 *   |                                        |
 *   |                                  BACK->| <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Kept deliberately bare: a single subtle wallpaper, the standard top/
 * bottom bars, three centred labels. No animations, no input apart from
 * BTN_BACK, no per-app branching. When a real screen is delivered for
 * one of these icons (e.g. S23 ships PhoneDialerScreen), the call site
 * in IntroScreen.cpp swaps the stub for the real screen and this class
 * keeps serving the remaining placeholders unchanged.
 *
 * Implementation notes:
 *  - Constructor takes the user-visible app name (e.g. "MUSIC", "CAMERA")
 *    so the same class can be reused for every not-yet-built tile without
 *    subclassing.
 *  - Code-only - reuses PhoneSynthwaveBg / PhoneStatusBar / PhoneSoftKeyBar
 *    so the placeholder feels visually part of the MAKERphone family.
 *  - BTN_BACK pops to whoever pushed us. The expected parent is the
 *    PhoneMainMenu instance, so back returns the user to the icon grid.
 */
class PhoneAppStubScreen : public LVScreen, private InputListener {
public:
	/**
	 * @param appName Uppercase app name displayed in the centre. Caller
	 *                owns the string; it must outlive the screen (typical
	 *                callers pass a string literal, which is fine).
	 */
	explicit PhoneAppStubScreen(const char* appName);
	virtual ~PhoneAppStubScreen() override;

	void onStart() override;
	void onStop() override;

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* titleLabel;     // big app name, pixelbasic16
	lv_obj_t* statusLabel;    // "NOT YET BUILT", pixelbasic7
	lv_obj_t* hintLabel;      // "coming in a future session", pixelbasic7

	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONEAPPSTUBSCREEN_H
