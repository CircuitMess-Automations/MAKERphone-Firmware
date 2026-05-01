#ifndef MAKERPHONE_PHONEBRIGHTNESSSCREEN_H
#define MAKERPHONE_PHONEBRIGHTNESSSCREEN_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneBrightnessScreen
 *
 * Phase-J sub-screen (S51): the first real settings sub-page reachable
 * from PhoneSettingsScreen (S50). Replaces the BRIGHTNESS placeholder
 * stub with a horizontal "segment-bar" slider that adjusts the LCD
 * backlight in real time and persists the chosen value into the
 * SettingsData partition so the brightness sticks across reboots.
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |             BRIGHTNESS                 | <- pixelbasic7 cyan caption
 *   |                                        |
 *   |                  80%                   | <- pixelbasic16 cyan readout
 *   |                                        |
 *   |   - [##########         ] +            | <- segment-bar slider
 *   |                                        |
 *   |        LEFT / RIGHT to adjust          | <- pixelbasic7 dim hint
 *   |                                        |
 *   |   SAVE                          BACK   | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * The slider has 11 stops (0..10) so the user lands on familiar 10%
 * increments rather than fiddling with arbitrary values. Internally
 * the stop index maps to the underlying 0..250 brightness scale
 * (Chatter::setBrightness internally maps 0..255 to backlight duty
 * 51..255, so 0 is the minimum visible brightness, NOT off). The
 * default Settings.screenBrightness of 200 maps to stop 8 (80%) so a
 * factory-fresh phone opens this screen with the bar 80% full.
 *
 * Behavior:
 *  - LEFT / 4 step the bar one stop down (with audible-visible feedback
 *    via Chatter.setBrightness on every change so the screen actually
 *    dims under the user's finger).
 *  - RIGHT / 6 step the bar one stop up. No wrap-around - clamping to
 *    [0, 10] is the right feature-phone affordance for a slider.
 *  - 2 / 8 mirror LEFT / RIGHT for users who navigate with the numpad.
 *  - ENTER (SAVE softkey) writes Settings.screenBrightness =
 *    currentStop * StepSize, calls Settings.store(), and pop()s.
 *  - BACK reverts the live brightness back to the value the screen
 *    opened with (so the user can preview without committing) and
 *    pop()s. This is the standard Sony-Ericsson "discard changes"
 *    affordance for option screens.
 *
 * Implementation notes:
 *  - Code-only, zero SPIFFS. Reuses PhoneSynthwaveBg / PhoneStatusBar /
 *    PhoneSoftKeyBar so the screen feels visually part of the rest of
 *    the MAKERphone family.
 *  - The "segment-bar" slider is a 110x10 frame with 10 internal
 *    segment cells. Filled cells are sunset orange, empty cells are
 *    muted purple, the frame is dim purple. One label per cell would
 *    be wasteful; we draw the cells as plain LVGL rects (cheap, no
 *    text shaping). The slider frame is right-aligned with a "-" on
 *    the left and "+" on the right so the affordance reads as a
 *    classic horizontal slider.
 *  - The percentage readout is one pixelbasic16 label that we
 *    snprintf into on every step; the bar repaint loops over 10 cells
 *    only, so a step is a constant-time UI update.
 *  - No animation on the bar - feature-phone sliders should feel
 *    instantaneous. Animations would also stack frames if the user
 *    holds the LEFT/RIGHT button down.
 */
class PhoneBrightnessScreen : public LVScreen, private InputListener {
public:
	PhoneBrightnessScreen();
	virtual ~PhoneBrightnessScreen() override;

	void onStart() override;
	void onStop() override;

	/** Number of slider stops (so the visible bar reads as 11 ticks: 0..10). */
	static constexpr uint8_t StopCount = 11;

	/**
	 * Mapping from a stop index (0..10) to the raw brightness value
	 * (0..250) stored in SettingsData::screenBrightness. 25 keeps the
	 * full range walkable in 11 keypresses while still landing on the
	 * Settings default (200) at exactly stop 8.
	 */
	static constexpr uint8_t StepSize = 25;

	/** Current focused stop index. Public so tests can read state cleanly. */
	uint8_t getCurrentStop() const { return cursor; }

	/** The raw brightness value (0..250) currently previewed on hardware. */
	uint8_t getPreviewBrightness() const;

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* captionLabel;     // "BRIGHTNESS"
	lv_obj_t* percentLabel;     // big "80%" readout
	lv_obj_t* sliderFrame;      // dim purple frame around the cells
	lv_obj_t* cells[10];        // 10 segment cells inside the frame
	lv_obj_t* minusLabel;       // "-" left of the frame
	lv_obj_t* plusLabel;        // "+" right of the frame
	lv_obj_t* hintLabel;        // "LEFT / RIGHT to adjust"

	uint8_t  cursor = 0;        // 0..StopCount-1
	uint8_t  initialBrightness; // value the screen opened with (used by BACK)

	void buildCaption();
	void buildSlider();
	void buildPercent();
	void buildHint();

	void refreshSlider();
	void applyPreviewBrightness();

	void stepBy(int8_t delta);
	void saveAndExit();
	void cancelAndExit();

	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONEBRIGHTNESSSCREEN_H
