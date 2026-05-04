#ifndef MAKERPHONE_PHONEIDLEHINT_H
#define MAKERPHONE_PHONEIDLEHINT_H

#include <Arduino.h>
#include <lvgl.h>
#include <Loop/LoopListener.h>
#include <Input/InputListener.h>
#include "../Interface/LVObject.h"

/**
 * PhoneIdleHint
 *
 * S154 - code-only "PRESS ANY KEY" idle hint that fades in after 10 s
 * of stillness on the homescreen and vanishes again the moment the
 * user touches a button. Written as a reusable element so the same
 * widget can be dropped on the lock screen, the camera viewfinder
 * (where it doubles as a "trigger to capture" prompt), or any future
 * screen that benefits from the classic feature-phone "wake me up"
 * cue.
 *
 *      +----------------------------------------+
 *      |                                        |
 *      |              12:34                     |  <- PhoneClockFace
 *      |             THU  4                     |
 *      |             MAY 2026                   |
 *      |                                        |
 *      |          PRESS ANY KEY                 |  <- PhoneIdleHint
 *      |                                        |     (fades in @10s)
 *      | <-CALL                          MENU-> |  <- PhoneSoftKeyBar
 *      +----------------------------------------+
 *
 * Behaviour summary:
 *   1. Boots invisible (caption opa = 0).
 *   2. Tracks `lastActivityMs` from `millis()`. Any button press
 *      observed via `InputListener::anyKeyPressed()` resets the clock
 *      and snaps the caption opacity back to 0.
 *   3. After `IdleMs` (10 s) of stillness, the caption ramps from 0
 *      to `PeakOpa` over `FadeMs` (600 ms), then settles into a soft
 *      triangle pulse between `PulseTroughOpa` and `PeakOpa` so the
 *      hint stays visible-but-quiet without becoming distracting.
 *   4. `setActive(false)` lets a host (e.g. the home screen while
 *      the charging overlay is showing) gate the hint without
 *      destroying it.
 *
 * Implementation notes:
 *  - 100 % code-only (one `lv_obj` slab + one centered `lv_label`).
 *    No SPIFFS assets, no canvas backing buffer.
 *  - `LV_OBJ_FLAG_IGNORE_LAYOUT` so the host's flex/column layout is
 *    untouched - the widget anchors itself to the screen explicitly.
 *  - Cleans up its `LoopManager` + `Input` registrations in the
 *    destructor so the host can `delete` the hint mid-screen-life
 *    without leaving a stale callback pointer behind.
 *  - Palette uses `MakerphoneTheme::labelDim()` / `text()` so the
 *    hint follows whichever theme (Synthwave default, Nokia 3310
 *    monochrome, ...) is active when the homescreen is built.
 */
class PhoneIdleHint : public LVObject, public LoopListener, private InputListener {
public:
	explicit PhoneIdleHint(lv_obj_t* parent);
	virtual ~PhoneIdleHint();

	void loop(uint micros) override;

	/** Pause / resume the idle clock without removing the widget. */
	void setActive(bool active);

	/** Replace the visible caption. Default is "PRESS ANY KEY". */
	void setText(const char* text);

	/** Externally pokeable activity reset (e.g. when a host-level
	 * gesture should count as activity even though no Input event
	 * fired). Cheap no-op if the hint is already hidden. */
	void resetActivity();

	/** Idle window before the hint starts to fade in, in ms. */
	static constexpr uint32_t IdleMs = 10000;

	/** Fade-in ramp duration once the idle window elapses. */
	static constexpr uint32_t FadeMs = 600;

	/** Caption opacity at peak fade-in (0..255). */
	static constexpr uint8_t  PeakOpa = 170;

	/** Soft pulse cycle period after the fade-in completes. */
	static constexpr uint32_t PulseMs = 2400;

	/** Caption opacity at the bottom of the soft pulse (0..255). */
	static constexpr uint8_t  PulseTroughOpa = 110;

	/** Slab dimensions, exposed so a host that needs to lay out
	 *  around the hint (e.g. when the home screen wants to make
	 *  room above the charging overlay) can ask. */
	static constexpr uint16_t HintWidth  = 110;
	static constexpr uint16_t HintHeight = 9;

private:
	void anyKeyPressed() override;

	void applyOpa(uint8_t opa);

	lv_obj_t* caption        = nullptr;
	uint32_t  lastActivityMs = 0;
	uint32_t  shownAtMs      = 0;     // 0 = not yet visible this idle cycle
	bool      active         = true;
	uint8_t   appliedOpa     = 0;
};

#endif // MAKERPHONE_PHONEIDLEHINT_H
