#ifndef MAKERPHONE_PHONETIPBANNER_H
#define MAKERPHONE_PHONETIPBANNER_H

#include <Arduino.h>
#include <lvgl.h>
#include <Loop/LoopListener.h>
#include <Input/InputListener.h>
#include "../Interface/LVObject.h"

/**
 * PhoneTipBanner
 *
 * S169 - code-only "random tip-of-the-day" banner that fades in below
 * the home-screen clock face after IdleMs of stillness, picks a fresh
 * random tip from an internal pool, and snaps invisible the moment
 * the user touches a button. The classic Sony-Ericsson screensaver-
 * tip habit, condensed into a one-line strip that lives inside the
 * homescreen's empty wallpaper band rather than as a full-screen
 * takeover.
 *
 *      +----------------------------------------+
 *      |  ||||      12:34                ##### | <- PhoneStatusBar
 *      |                                        |
 *      |              12:34                     |  <- PhoneClockFace
 *      |             THU  4                     |
 *      |             MAY 2026                   |
 *      |       _____________________            |  <- PhoneTipBanner
 *      |      | TIP: Hold 0 = dial  |           |     (fades in @25s)
 *      |       ---------------------            |
 *      |             ___    ___                 |  <- PhoneYawnOverlay
 *      |            ( o )  ( o )                |
 *      |          PRESS ANY KEY                 |  <- PhoneIdleHint
 *      |                                        |
 *      | <-CALL                          MENU-> |  <- PhoneSoftKeyBar
 *      +----------------------------------------+
 *
 * Behaviour summary:
 *   1. Boots invisible (slab opa = 0, caption opa = 0).
 *   2. Tracks `lastActivityMs` from `millis()`. Any button press
 *      observed via `InputListener::anyKeyPressed()` resets the
 *      clock and snaps the banner opacity back to 0.
 *   3. After `IdleMs` (25 s) of stillness, the banner picks a
 *      fresh random tip and ramps from 0 to `PeakOpa` over
 *      `FadeMs` (700 ms), then settles at `PeakOpa` until the
 *      user touches a button.
 *   4. Two consecutive idle cycles never hit the same tip slot,
 *      so a user staring at the homescreen for a long-stillness
 *      bake gets a steady drip of variety.
 *   5. `setActive(false)` lets a host (e.g. the home screen
 *      while the charging overlay is showing) gate the banner
 *      without destroying it, mirroring the PhoneIdleHint and
 *      PhoneYawnOverlay gating contracts.
 *
 * Implementation notes:
 *  - 100 % code-only (one `lv_obj` slab + one centered `lv_label`).
 *    No SPIFFS assets, no canvas backing buffer.
 *  - `LV_OBJ_FLAG_IGNORE_LAYOUT` so the host's flex/column layout
 *    is untouched - the banner anchors itself to TOP_MID at a
 *    fixed y. Hit-testing is disabled so the host's existing
 *    softkey / quick-dial / lock-hold gestures keep firing the
 *    moment the user touches a button.
 *  - Cleans up its `LoopManager` + `Input` registrations in the
 *    destructor so the host can `delete` the banner mid-screen-
 *    life without leaving a stale callback pointer behind.
 *  - Palette uses `MakerphoneTheme::text()` / `dim()` so the
 *    banner follows whichever theme (Synthwave default, Nokia
 *    3310 monochrome, ...) is active when the homescreen is
 *    built. The slab background hugs ~24 % of the caption opa
 *    so the strip reads as a softly-lit chip rather than a
 *    hard chrome bubble that fights the synthwave wallpaper.
 *
 * Idle timeline on the homescreen:
 *      t = 0          - boot / activity reset, every overlay invisible.
 *      t = 10 s       - PhoneIdleHint   "PRESS ANY KEY" fades in.
 *      t = 25 s       - PhoneTipBanner  random-tip strip fades in.
 *      t = 5 min      - PhoneYawnOverlay sleepy-eyes fade in + blink.
 * The three are designed to coexist - each owns a distinct y-band
 * (clock area, mid-wallpaper strip, lower wallpaper, just above
 * the soft-key bar) so a fully-idle homescreen reads as a layered
 * "phone is napping with reminders pinned to its sweater" rather
 * than as three widgets fighting for the same pixels.
 */
class PhoneTipBanner : public LVObject, public LoopListener, private InputListener {
public:
	explicit PhoneTipBanner(lv_obj_t* parent);
	virtual ~PhoneTipBanner();

	void loop(uint micros) override;

	/** Pause / resume the idle clock without removing the widget. */
	void setActive(bool active);

	/** Externally pokeable activity reset (e.g. when a host-level
	 * gesture should count as activity even though no Input event
	 * fired). Cheap no-op if the banner is already hidden. */
	void resetActivity();

	/** Idle window before the banner starts to fade in, in ms. Sits
	 * 15 s after the PhoneIdleHint cue so the user reads the "PRESS
	 * ANY KEY" prompt first, then the tip arrives as a second wave. */
	static constexpr uint32_t IdleMs = 25000;

	/** Fade-in ramp duration once the idle window elapses. */
	static constexpr uint32_t FadeMs = 700;

	/** Caption opacity at the steady-state plateau (0..255). */
	static constexpr uint8_t  PeakOpa = 220;

	/** Slab dimensions, exposed so a host that needs to lay out
	 *  around the banner (e.g. when adding a future widget that
	 *  must avoid the same y-band) can ask. */
	static constexpr uint16_t BannerWidth   = 140;
	static constexpr uint16_t BannerHeight  = 13;

	/** Anchor y, measured from the top edge of the 160x128 home
	 *  screen. Lands the banner at y = 46..58 - clear of the
	 *  clock face (y = 11..43, or y = 19..51 with the operator
	 *  banner mounted) and the yawn-overlay eyes (y = 60..66).
	 *  Two-pixel breathing gap above the eyes keeps the trio
	 *  reading as distinct strips rather than a stacked block. */
	static constexpr int16_t  BannerYOffset = 46;

	/** Number of tips in the rotating pool. */
	static constexpr uint8_t TipCount = 12;

private:
	void anyKeyPressed() override;

	void applyOpa(uint8_t opa);
	void pickNewTip();

	lv_obj_t* caption        = nullptr;
	uint32_t  lastActivityMs = 0;
	uint32_t  shownAtMs      = 0;     // 0 = not yet visible this idle cycle
	bool      active         = true;
	uint8_t   appliedOpa     = 0;
	uint8_t   currentTip     = 0;
};

#endif // MAKERPHONE_PHONETIPBANNER_H
