#ifndef MAKERPHONE_PHONEYAWNOVERLAY_H
#define MAKERPHONE_PHONEYAWNOVERLAY_H

#include <Arduino.h>
#include <lvgl.h>
#include <Loop/LoopListener.h>
#include <Input/InputListener.h>
#include "../Interface/LVObject.h"

/**
 * PhoneYawnOverlay
 *
 * S163 - code-only "phone yawns" idle animation. After 5 minutes of
 * stillness on the homescreen, a pair of pixel-art eyes fades in
 * over the synthwave wallpaper and starts blinking at a slow,
 * breathing pace, so the device reads as a sleepy companion that
 * has nodded off rather than as a bricked screen the user has
 * forgotten about. Any button press resets the idle clock and
 * snaps the overlay back to invisible.
 *
 *      +----------------------------------------+
 *      |  ||||      12:34                ##### | <- PhoneStatusBar
 *      |                                        |
 *      |              12:34                     |  <- PhoneClockFace
 *      |             THU  4                     |
 *      |             MAY 2026                   |
 *      |                                        |
 *      |             ___    ___                 |  <- PhoneYawnOverlay
 *      |            ( o )  ( o )                |     (fades in @5 min)
 *      |             ___    ___                 |
 *      |          PRESS ANY KEY                 |  <- PhoneIdleHint
 *      |                                        |
 *      | <-CALL                          MENU-> |  <- PhoneSoftKeyBar
 *      +----------------------------------------+
 *
 * Behaviour summary:
 *   1. Boots invisible (overlay opa = 0, eyes parked open).
 *   2. Tracks `lastActivityMs` from `millis()`. Any button press
 *      observed via `InputListener::anyKeyPressed()` resets the
 *      clock and snaps the overlay opacity back to 0.
 *   3. After `IdleMs` (5 min) of stillness, the overlay ramps
 *      from 0 to `PeakOpa` over `FadeMs` (800 ms), then settles
 *      into a slow blink loop:
 *        - `BlinkPeriodMs` (3.2 s) full cycle.
 *        - `BlinkDownMs`   (130 ms) inside that cycle the eyes
 *           are closed (eye-white height squeezes to 1 px and
 *           pupils fade to 0). Otherwise the eyes are open.
 *   4. Every `YawnEvery` blinks the loop fires a longer
 *      `YawnDownMs` (520 ms) "yawn" close, so the animation feels
 *      alive rather than mechanical. The yawn is a longer eyes-
 *      shut hold, not a different animation - one extra branch
 *      in `loop()` keeps the cost ~zero.
 *   5. `setActive(false)` lets a host (e.g. the home screen while
 *      the charging overlay is showing) gate the overlay without
 *      destroying it.
 *
 * Implementation notes:
 *  - 100 % code-only (four `lv_obj` rectangles - two eye whites,
 *    two pupils). No SPIFFS assets, no canvas backing buffer.
 *  - `LV_OBJ_FLAG_IGNORE_LAYOUT` so the host's layout is
 *    untouched - the overlay anchors itself to the screen
 *    explicitly. Hit-testing is disabled so the host's input
 *    listeners keep receiving every key press while the overlay
 *    is on screen.
 *  - Cleans up its `LoopManager` + `Input` registrations in the
 *    destructor so the host can `delete` the overlay mid-screen-
 *    life without leaving a stale callback pointer behind.
 *  - Palette uses `MakerphoneTheme::text()` for the eye whites
 *    and `MakerphoneTheme::bgDark()` for the pupils, so the eyes
 *    follow whichever skin (Synthwave default, Nokia 3310
 *    monochrome, ...) is active when the homescreen is built.
 */
class PhoneYawnOverlay : public LVObject, public LoopListener, private InputListener {
public:
	explicit PhoneYawnOverlay(lv_obj_t* parent);
	virtual ~PhoneYawnOverlay();

	void loop(uint micros) override;

	/** Pause / resume the idle clock without removing the widget. */
	void setActive(bool active);

	/** Externally pokeable activity reset (e.g. when a host-level
	 * gesture should count as activity even though no Input event
	 * fired). Cheap no-op if the overlay is already hidden. */
	void resetActivity();

	/** Idle window before the eyes start to fade in, in ms. */
	static constexpr uint32_t IdleMs = 5UL * 60UL * 1000UL;  // 5 minutes

	/** Fade-in ramp duration once the idle window elapses. */
	static constexpr uint32_t FadeMs = 800;

	/** Eye opacity at peak fade-in (0..255). */
	static constexpr uint8_t  PeakOpa = 200;

	/** Full blink cycle period once the overlay has fully faded in. */
	static constexpr uint32_t BlinkPeriodMs = 3200;

	/** Fraction of the blink cycle that the eyes spend closed. */
	static constexpr uint32_t BlinkDownMs = 130;

	/** Every Nth blink is held longer, reading as a slow "yawn"
	 *  rather than a regular blink. Keeps the loop feeling alive. */
	static constexpr uint8_t  YawnEvery = 5;

	/** Hold length of the longer "yawn" close, in ms. */
	static constexpr uint32_t YawnDownMs = 520;

	/** Eye dimensions, exposed so a host that needs to lay out
	 *  around the overlay can ask. */
	static constexpr uint16_t EyeWidth   = 11;
	static constexpr uint16_t EyeHeight  = 7;
	static constexpr uint16_t EyeGap     = 8;       // gap between the two eye whites
	static constexpr uint16_t PupilSize  = 3;
	static constexpr int16_t  EyeYOffset = 60;      // y of the eye top-left, from the home screen top

private:
	void anyKeyPressed() override;

	void applyOpa(uint8_t opa);
	void setEyesClosed(bool closed);

	lv_obj_t* eyeL    = nullptr;
	lv_obj_t* eyeR    = nullptr;
	lv_obj_t* pupilL  = nullptr;
	lv_obj_t* pupilR  = nullptr;

	uint32_t lastActivityMs = 0;
	uint32_t shownAtMs      = 0;     // 0 = not yet visible this idle cycle
	bool     active         = true;
	bool     eyesClosed     = false;
	uint8_t  appliedOpa     = 0;
	uint16_t blinkCount     = 0;     // # of complete blink cycles since fade-in finished
};

#endif // MAKERPHONE_PHONEYAWNOVERLAY_H
