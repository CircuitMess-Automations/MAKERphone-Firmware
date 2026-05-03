#ifndef MAKERPHONE_PHONEFLASHLIGHT_H
#define MAKERPHONE_PHONEFLASHLIGHT_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneFlashlight — S134
 *
 * Phase-P utility: turn the device into an emergency torch by
 * blasting a full-white panel at maximum backlight. Slots into the
 * utility-apps grid alongside PhoneCalculator (S60), PhoneAlarmClock
 * (S124), PhoneTimers (S125), PhoneCurrencyConverter (S126),
 * PhoneUnitConverter (S127), PhoneWorldClock (S128), PhoneVirtualPet
 * (S129), PhoneMagic8Ball (S130), PhoneDiceRoller (S131),
 * PhoneCoinFlip (S132) and PhoneFortuneCookie (S133).
 *
 * Idle (OFF) layout — synthwave wallpaper + tiny preview, so the
 * user can find the toggle in a dark room before they blind
 * themselves:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |              FLASHLIGHT                | <- pixelbasic7 cyan
 *   |                                        |
 *   |              .---------.               |
 *   |              |         |               | <- preview rect (cream
 *   |              |  TORCH  |               |    with cyan border)
 *   |              |   OFF   |               |
 *   |              `---------`               |
 *   |                                        |
 *   |        PRESS LIGHT TO TURN ON          |
 *   |   LIGHT                         BACK   | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Active (ON) layout — full-screen white panel, no chrome, the
 * status bar tucked into a high-contrast strip at the very top so
 * the user can still read the time and battery while the panel is
 * blinding bright. The hint sits at the bottom in dim grey so the
 * usable lit area is still ~95 % of the screen.
 *
 *   +----------------------------------------+
 *   |  WHITE WHITE WHITE WHITE WHITE WHITE   | <- 160 px white panel
 *   |  WHITE WHITE WHITE WHITE WHITE WHITE   |
 *   |  WHITE WHITE WHITE WHITE WHITE WHITE   |
 *   |  WHITE WHITE WHITE WHITE WHITE WHITE   |
 *   |  WHITE WHITE WHITE WHITE WHITE WHITE   |
 *   |  WHITE WHITE WHITE WHITE WHITE WHITE   |
 *   |  WHITE WHITE WHITE WHITE WHITE WHITE   |
 *   |  WHITE WHITE WHITE WHITE WHITE WHITE   |
 *   |  WHITE WHITE WHITE WHITE WHITE WHITE   |
 *   |   OFF                            BACK  | <- soft-key bar over
 *   +----------------------------------------+    a thin dim strip
 *
 * Behaviour:
 *   - Snapshots the current `Settings.screenBrightness` on construction.
 *   - On TURN-ON: writes `Chatter.setBrightness(255)` directly (does
 *     NOT mutate Settings, so the user's preferred brightness is
 *     preserved across the visit).
 *   - On TURN-OFF or BACK: restores `Chatter.setBrightness(initial)`.
 *   - While ON, ticks `IdleDim.resetActivity()` once per second so the
 *     idle-dim service can't drag the panel back to its 30 % dim
 *     state mid-emergency. The screen does no per-frame work while
 *     OFF — the keep-alive timer only runs in the lit state.
 *   - SleepService is left alone — long emergencies will still
 *     eventually trigger sleep, which is a feature, not a bug; the
 *     user can wake the phone again to keep the torch running.
 *
 * Controls:
 *   - BTN_5 / BTN_ENTER / BTN_L / left softkey ("LIGHT"/"OFF") :
 *       toggle the flashlight on / off.
 *   - BTN_0 : "panic" — instantly switches OFF and pops back to the
 *       previous screen so the user can re-enter the menu without
 *       fumbling.
 *   - BTN_R / right softkey / BTN_BACK (short or long) : pop. If the
 *       light is still ON when the user pops, the destructor restores
 *       brightness on the way out.
 *
 * Implementation notes:
 *   - 100 % code-only, no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *     PhoneStatusBar / PhoneSoftKeyBar so the OFF-state slots into
 *     the family without a visual seam.
 *   - The "white panel" is a single full-screen lv_obj with bg_color
 *     pure white (255,255,255). It is hidden in the OFF state and
 *     unhidden on toggle-ON. The status bar is *also* hidden while
 *     ON so palette-tinted chrome can't bleed into the lit area.
 *   - The keep-alive timer auto-deletes when the screen turns OFF or
 *     stops, so the screen does zero per-frame work in the idle case.
 */
class PhoneFlashlight : public LVScreen, private InputListener {
public:
	PhoneFlashlight();
	virtual ~PhoneFlashlight() override;

	void onStart() override;
	void onStop() override;

	/** True iff the flashlight panel is currently lit. */
	bool isOn() const { return on; }

	/** The brightness Chatter was sitting at when the screen was
	 *  pushed. Restored verbatim on every transition back to OFF and
	 *  on screen exit. Exposed for tests / inspection. */
	uint8_t initialBrightnessSnapshot() const { return initialBrightness; }

	/** Long-press threshold for BTN_BACK (matches the rest of the shell). */
	static constexpr uint16_t BackHoldMs   = 600;

	/** Cadence (ms) at which the lit state pokes IdleDim so the idle
	 *  dim cannot reduce the panel back to 30 %. 1000 ms is well
	 *  inside `PhoneIdleDim::IDLE_DIM_MS` (30000 ms) and costs a
	 *  single int compare per tick. */
	static constexpr uint16_t KeepAliveMs  = 1000;

	/** Brightness duty written to `Chatter.setBrightness()` when the
	 *  flashlight is lit. 255 maps to the brightest LEDC duty per
	 *  `Chatter::mapDuty`. */
	static constexpr uint8_t  MaxBrightness = 255;

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	// OFF-state chrome.
	lv_obj_t* captionLabel;
	lv_obj_t* previewBox;
	lv_obj_t* previewLabel;
	lv_obj_t* hintLabel;

	// ON-state full-screen white panel. Built once in the ctor and
	// kept hidden in the OFF state. While ON it sits above every
	// other element so the user sees pure white.
	lv_obj_t* whitePanel;

	uint8_t   initialBrightness = 0;
	bool      on               = false;
	bool      backLongFired    = false;

	lv_timer_t* keepAliveTimer = nullptr;

	void buildHud();
	void buildWhitePanel();

	/** Apply the OFF-state visuals + restore initial brightness. */
	void renderOff();

	/** Apply the ON-state visuals + write max brightness. */
	void renderOn();

	/** Toggle between OFF and ON. */
	void toggle();

	void startKeepAliveTimer();
	void stopKeepAliveTimer();
	static void onKeepAliveTickStatic(lv_timer_t* timer);

	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONEFLASHLIGHT_H
