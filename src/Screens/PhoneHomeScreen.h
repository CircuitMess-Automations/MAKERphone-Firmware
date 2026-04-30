#ifndef MAKERPHONE_PHONEHOMESCREEN_H
#define MAKERPHONE_PHONEHOMESCREEN_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneClockFace;
class PhoneSoftKeyBar;

/**
 * PhoneHomeScreen
 *
 * The MAKERphone 2.0 homescreen skeleton (S17). It is the first end-user
 * Phase-C screen and composes the four foundational Phase-A/B widgets into
 * the unmistakable Sony-Ericsson silhouette:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |                                        |
 *   |              12:34                     | <- PhoneClockFace
 *   |             THU 30                     |
 *   |             APR 2026                   |
 *   |                                        |
 *   |       . synthwave wallpaper .          | <- PhoneSynthwaveBg (back)
 *   |          (sun + grid + stars)          |
 *   |                                        |
 *   | <-CALL                          MENU-> | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * This session (S17) only ships the screen *class*. It is intentionally not
 * routed into the boot flow yet - the existing IntroScreen -> MainMenu ->
 * LockScreen path is preserved untouched. S18 then promotes this screen as
 * the post-LockScreen default behind a build flag, and S19+ wire CALL/MENU
 * to their real destinations (dialer / phone-style main menu).
 *
 * Implementation notes:
 *  - Code-only (no SPIFFS assets) so the data partition stays small.
 *  - The synthwave background is constructed *first* so it is at the
 *    bottom of LVGL's draw stack and every other widget overlays it.
 *  - The screen owns the widgets but does not delete them manually -
 *    LVGL recursively deletes children when obj is freed at screen
 *    teardown, matching the pattern in LockScreen / MainMenu.
 *  - Stub button bindings are wired so that already at S17 the screen
 *    is *driveable*: BTN_BACK pops to parent (so when an upstream
 *    screen does push(homescreen), Back returns to it). The actual
 *    routing of CALL / MENU softkeys is the job of S18 - S20 sessions.
 *  - Hooks setOnLeftSoftKey / setOnRightSoftKey are exposed up-front so
 *    a future caller (e.g. main.ino once we wire S18) can route CALL
 *    and MENU without subclassing.
 */
class PhoneHomeScreen : public LVScreen, private InputListener {
public:
	PhoneHomeScreen();
	virtual ~PhoneHomeScreen();

	void onStart() override;
	void onStop() override;

	using SoftKeyHandler = void (*)(PhoneHomeScreen* self);

	/** Bind a callback to BTN_LEFT (the "CALL" softkey). nullptr clears. */
	void setOnLeftSoftKey(SoftKeyHandler cb);

	/** Bind a callback to BTN_RIGHT (the "MENU" softkey). nullptr clears. */
	void setOnRightSoftKey(SoftKeyHandler cb);

	/** Replace the visible label of the left softkey (default "CALL"). */
	void setLeftLabel(const char* label);

	/** Replace the visible label of the right softkey (default "MENU"). */
	void setRightLabel(const char* label);

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneClockFace*   clockFace;
	PhoneSoftKeyBar*  softKeys;

	SoftKeyHandler leftCb  = nullptr;
	SoftKeyHandler rightCb = nullptr;

	void buttonPressed(uint i) override;
};

#endif //MAKERPHONE_PHONEHOMESCREEN_H
