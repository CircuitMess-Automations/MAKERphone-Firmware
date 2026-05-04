#ifndef MAKERPHONE_PHONEFIRMWAREINFOSCREEN_H
#define MAKERPHONE_PHONEFIRMWAREINFOSCREEN_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneFirmwareInfoScreen
 *
 * Phase-S Easter-egg screen (S165). On a real Sony-Ericsson handset
 * the service code `*#0000#` opened a tiny "Phone information" page
 * showing the model, software version, software-build date, and
 * hardware revision -- the four facts a service technician asked the
 * customer to read off before they opened a ticket. We mirror the
 * gesture here: typing `*#0000#` on PhoneDialerScreen clears the
 * buffer and pushes this screen, which presents the same four facts
 * in the same compact "label / value" layout.
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |             FW INFO                    | <- pixelbasic7 cyan caption
 *   |                                        |
 *   |  MODEL                                 | <- pixelbasic7 dim label
 *   |  MAKERphone v0.55                      | <- pixelbasic7 cream value
 *   |  BUILD                                 |
 *   |  May  4 2026                           |
 *   |  TIME                                  |
 *   |  10:21:39                              |
 *   |  HARDWARE                              |
 *   |  Chatter rev. A                        |
 *   |                                        |
 *   |                                BACK    | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * The page is intentionally read-only, just like S55's PhoneAboutScreen
 * and S164's PhoneImeiRevealScreen -- there is nothing to commit, so
 * the only soft-key is BACK. Unlike the About page there is no live
 * tick; build / time / model / hardware are all baked in at compile
 * time, so writing them once on construction is sufficient.
 *
 * Behavior:
 *  - BTN_BACK / BTN_ENTER / BTN_RIGHT pop back to the dialer. ENTER is
 *    accepted as a friendly second way out, and BTN_RIGHT mirrors
 *    PhoneDialerScreen's "right d-pad doubles as the BACK softkey"
 *    convention so the muscle memory matches across the two screens.
 *  - There is no other input. The page does not navigate, scroll, or
 *    edit anything.
 *
 * Implementation notes:
 *  - Code-only, zero SPIFFS. Reuses PhoneSynthwaveBg / PhoneStatusBar /
 *    PhoneSoftKeyBar so the screen feels visually part of the rest of
 *    the MAKERphone family rather than a debug terminal.
 *  - Model string is `PhoneAboutScreen::kFirmwareVersion` so the two
 *    screens always agree on the firmware label without us having to
 *    keep two version constants in sync. A future bump (e.g. S200's
 *    v2.0 changelog) becomes a one-line edit in PhoneAboutScreen.h.
 *  - Build date / time are the standard `__DATE__` / `__TIME__`
 *    macros. They are expanded by the compiler into compile-time
 *    string literals (e.g. "May  4 2026" / "10:21:39"); both fit
 *    inside the 152 px label column without truncation.
 *  - Hardware revision is a single inlined kHardwareRevision string.
 *    The MAKERphone has only ever shipped on the Chatter rev. A board
 *    so the value is hard-coded today; if a future revision ever
 *    appears, this is the one place to bump.
 *  - 160x128 budget: 10 px status bar (y = 0..10), caption at y = 12,
 *    four label-pair groups starting at y = 24 with 9 px label + 9 px
 *    value + 2 px gap = 20 px per group. Four groups occupy y = 24..104,
 *    leaving 14 px of breathing room before the soft-key bar at y = 118.
 *    The geometry intentionally mirrors PhoneAboutScreen so the two
 *    "diagnostics" screens read as siblings.
 */
class PhoneFirmwareInfoScreen : public LVScreen, private InputListener {
public:
	PhoneFirmwareInfoScreen();
	virtual ~PhoneFirmwareInfoScreen() override;

	void onStart() override;
	void onStop() override;

	/**
	 * Hardware revision string baked into this build. The MAKERphone
	 * has only ever shipped on the Chatter rev. A board today; bump
	 * here if a future revision ever appears.
	 */
	static constexpr const char* kHardwareRevision = "Chatter rev. A";

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* captionLabel;

	// Group 1: MODEL.
	lv_obj_t* modelLabel;
	lv_obj_t* modelValue;

	// Group 2: BUILD (compile-time __DATE__).
	lv_obj_t* buildLabel;
	lv_obj_t* buildValue;

	// Group 3: TIME (compile-time __TIME__).
	lv_obj_t* timeLabel;
	lv_obj_t* timeValue;

	// Group 4: HARDWARE.
	lv_obj_t* hardwareLabel;
	lv_obj_t* hardwareValue;

	void buildCaption();
	void buildBody();

	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONEFIRMWAREINFOSCREEN_H
