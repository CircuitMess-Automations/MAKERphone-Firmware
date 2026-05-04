#ifndef MAKERPHONE_PHONEHOMESCREEN_H
#define MAKERPHONE_PHONEHOMESCREEN_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneClockFace;
class PhoneSoftKeyBar;
class PhoneChargingOverlay;
class PhoneChargeBars;
class PhoneOperatorBanner;
class PhoneConfettiOverlay;
class PhoneNotificationToast;
class PhoneIdleHint;

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
 * S17 ships the screen *class*. S18 promotes it to the post-LockScreen
 * default behind a build flag, S19/S20 wire MENU to PhoneMainMenu, and
 * S21 layers the home<->menu transition (slide animation + softkey press
 * flash) on top of the existing wiring.
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
 *  - S21 adds a press-feedback flash on the corresponding softkey label
 *    *before* invoking the host's handler. The handler is then free to
 *    push() with a horizontal slide; the flash and the slide overlap
 *    visually for a satisfying "click + drill in" feel.
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

	/**
	 * S22: Bind a callback to a long-press of BTN_0. The classic Sony-
	 * Ericsson "hold 0" shortcut on the homescreen jumps straight into
	 * the dialer with the user's quick-dial number pre-loaded. The
	 * dialer itself ships in S23 - until then the host wires this hook
	 * to a stub so the gesture is observable. nullptr clears.
	 */
	void setOnQuickDial(SoftKeyHandler cb);

	/**
	 * S22: Bind a callback to a long-press of BTN_BACK. The classic
	 * "hold Back to lock" gesture. Default (when nullptr) is to do
	 * nothing - the host wires this to LockScreen::activate(this) so
	 * the lock action lives next to the rest of the screen-routing
	 * logic in IntroScreen.cpp.
	 */
	void setOnLockHold(SoftKeyHandler cb);

	/**
	 * S151 — handler signature for the long-press 1..9 speed-dial
	 * gesture. The classic Sony-Ericsson "hold a digit on home dials
	 * the matching speed-dial entry" reflex; the handler receives the
	 * 1..9 digit so a single host helper can dispatch all nine slots
	 * without the screen needing to know the routing. Distinct from
	 * `SoftKeyHandler` because the digit context is essential to the
	 * caller. Slot 0 stays bound to the existing `setOnQuickDial`
	 * gesture (S22) so the home screen still opens an empty dialer on
	 * a long-press of BTN_0 the way feature-phone muscle memory
	 * expects.
	 */
	using SpeedDialHandler = void (*)(PhoneHomeScreen* self, uint8_t digit);

	/**
	 * S151 — bind a callback to a long-press of BTN_1..BTN_9. The
	 * host wires a single helper that consults Settings.speedDial[d]
	 * and either fires PhoneCallService::placeCall() on the assigned
	 * peer or falls through to opening an empty dialer when the slot
	 * has not been assigned yet. nullptr clears.
	 */
	void setOnSpeedDial(SpeedDialHandler cb);

	/** Replace the visible label of the left softkey (default "CALL"). */
	void setLeftLabel(const char* label);

	/** Replace the visible label of the right softkey (default "MENU"). */
	void setRightLabel(const char* label);

	/**
	 * S21: trigger the press-feedback flash on the left/right softkey.
	 * Already invoked internally on BTN_LEFT / BTN_RIGHT; exposed publicly
	 * so the host can also flash from outside (e.g. when programmatically
	 * simulating a press during a transition).
	 */
	void flashLeftSoftKey();
	void flashRightSoftKey();

private:
	PhoneSynthwaveBg*     wallpaper;
	PhoneStatusBar*       statusBar;
	PhoneClockFace*       clockFace;
	PhoneSoftKeyBar*      softKeys;
	PhoneChargingOverlay* chargingOverlay = nullptr;
	// S155 - wide animated charge fill-bars that sit just above the
	// charging chip while the device is plugged in. Subscribes to
	// chargingOverlay->isCharging() each loop, so visibility tracks
	// the same auto-detect heuristic the chip already uses; nullptr
	// until the constructor finishes building chargingOverlay (the
	// bars need it as their source).
	PhoneChargeBars*      chargeBars      = nullptr;
	// S147 - operator-banner widget (carrier name + 5x16 user-pixelable
	// logo). Mounted just under the status bar so the homescreen wears
	// the classic Sony-Ericsson silhouette. Hidden when both the text
	// and logo are empty (factory-cleared state) so a wiped banner
	// does not leave an empty strip on the homescreen.
	PhoneOperatorBanner*  operatorBanner  = nullptr;
	// S152 - birthday confetti overlay + matching toast. Mounted only
	// when PhoneClock::now()'s (month, day) matches a contact birthday;
	// nullptr on every other day so the homescreen pays no LVGL cost.
	// The overlay tears itself down with the screen so a freshly built
	// PhoneHomeScreen on the next session day is rebuilt from scratch.
	PhoneConfettiOverlay*    confettiOverlay  = nullptr;
	PhoneNotificationToast*  birthdayToast    = nullptr;
	// S154 - "PRESS ANY KEY" idle hint that fades in after 10 s
	// of stillness (any button = stillness reset). Mounted at
	// construction, gated off whenever PhoneChargingOverlay is
	// visible so the two never share screen space, and torn
	// down with the screen via the LVGL parent chain. The hint
	// is purely visual - it does not consume any input - so the
	// existing softkey / quick-dial / lock-hold gestures are
	// unaffected. Owned by the LVGL parent; we just keep the
	// pointer to drive setActive() each loop().
	PhoneIdleHint*           idleHint         = nullptr;

	SoftKeyHandler leftCb       = nullptr;
	SoftKeyHandler rightCb      = nullptr;
	SoftKeyHandler quickDialCb  = nullptr;
	SoftKeyHandler   lockHoldCb   = nullptr;
	SpeedDialHandler speedDialCb  = nullptr;  // S151

	// S22: track when a hold-fired action has already run for the
	// current press, so the matching short-press handler doesn't ALSO
	// fire on release. We simply suppress the next buttonReleased for
	// the same key after a long-press shortcut triggered.
	bool zeroLongFired = false;
	bool backLongFired = false;
	// S151 — one suppression flag per BTN_1..BTN_9 long-press, same
	// pattern zeroLongFired / backLongFired use for BTN_0 / BTN_BACK.
	// Sized [10] so the index matches the digit pressed; slot 0 is
	// unused (BTN_0 keeps the dedicated zeroLongFired flag the S22
	// quick-dial gesture wired). Keeps the matching short-press from
	// double-firing on key release after a long-press has already
	// triggered the speed-dial callback.
	bool digitLongFired[10] = { false, false, false, false, false, false, false, false, false, false };

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif //MAKERPHONE_PHONEHOMESCREEN_H
