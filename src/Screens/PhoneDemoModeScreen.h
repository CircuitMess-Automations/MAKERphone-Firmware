#ifndef MAKERPHONE_PHONEDEMOMODESCREEN_H
#define MAKERPHONE_PHONEDEMOMODESCREEN_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneDemoModeScreen
 *
 * S200 - the v2.0 release "auto-cycling demo mode for the marketing video"
 * surface. Launched from the new ADVANCED group at the bottom of
 * PhoneSettingsScreen ("Demo mode" row), it takes the device through a
 * scripted slideshow of the headline features the v2.0 firmware just
 * shipped. The intent is that you can prop the phone on a table, hit
 * record on a phone camera, and let the device self-narrate the v2.0
 * tour without anyone needing to drive the input pad in shot.
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |             DEMO MODE                  | <- pixelbasic7 cyan caption
 *   |                                        |
 *   |          MAKERPHONE 2.0                | <- pixelbasic16 cream title
 *   |                                        |
 *   |        retro feature phone             | <- pixelbasic7 sub-caption
 *   |        for the maker era               |
 *   |                                        |
 *   |       *  *  *  *  *  o  o  o  o        | <- pixelbasic7 progress dots
 *   |     ANY KEY: EXIT                      | <- pixelbasic7 dim hint
 *   +----------------------------------------+
 *
 * Each slide stays on screen for `kSlidePeriodMs` (3 s) and the slideshow
 * loops until any button press exits back to PhoneSettingsScreen. The
 * progress dots row visualises which slide is active so a viewer who
 * starts the video mid-cycle still gets the "X of N" cue.
 *
 * Behaviour:
 *  - On push: shows slide 0 immediately, starts an lv_timer that fires
 *    every kSlidePeriodMs ms and advances `slideIdx` modulo `kSlideCount`.
 *  - Any button press (`anyKeyPressed`) exits via `pop()` so the user
 *    is back on the settings list at the row they launched from.
 *  - The screen owns its lv_timer and tears it down in onStop() and the
 *    destructor so a screen that is destroyed without ever being started
 *    (e.g. host code that allocates and immediately deletes) does not
 *    leak the timer slot.
 *
 * Implementation notes:
 *  - Code-only, zero SPIFFS. Reuses PhoneSynthwaveBg + PhoneStatusBar +
 *    PhoneSoftKeyBar so the screen feels visually part of the rest of
 *    the MAKERphone family. Data partition cost stays zero.
 *  - The slide content is a static const table of three strings per
 *    slide (title / sub1 / sub2) plus an accent line. Lives in the .cpp
 *    so adding / re-ordering slides is a single edit and never mutates
 *    the public API.
 *  - The title is rendered in pixelbasic16 cream, the two sub-lines and
 *    the accent line in pixelbasic7 (cream and dim purple respectively),
 *    matching the typography vocabulary every other Phase-O / Phase-V
 *    screen leans on. Long titles are accommodated by the existing
 *    pixelbasic16 fallback (LV_LABEL_LONG_DOT) so a slide that overruns
 *    the 152 px content column does not spill into the softkey bar.
 *  - 160x128 budget: 10 px status bar (y = 0..10), 10 px caption strip
 *    (y = 12..20), title at y = 36 (16 px tall), sub1 at y = 56,
 *    sub2 at y = 66, accent at y = 84, progress dots at y = 100,
 *    "ANY KEY: EXIT" hint at y = 110, softkey bar at y = 118..128.
 *  - The progress-dot row is a single label re-rendered on every slide
 *    advance using a fixed buffer (one char per dot, separator spaces
 *    in between). No allocation per advance.
 *  - The lv_timer pointer is nullptr while the screen is detached so a
 *    paused screen does not consume the timer manager's slot budget.
 *  - `pop()` is the only exit; there is no SAVE / OPEN softkey because
 *    the demo carries no user-tweakable state. The right softkey is
 *    labelled "EXIT" so the soft-key strip echoes the on-screen hint.
 *  - Idle-cheap on the loop: only the lv_timer ticks at 3 s intervals,
 *    each tick is a label rewrite plus a single dots-row repaint, no
 *    LoopManager subscription, no Input subscription beyond the
 *    InputListener default that anyKeyPressed() routes through.
 */
class PhoneDemoModeScreen : public LVScreen, private InputListener {
public:
	PhoneDemoModeScreen();
	virtual ~PhoneDemoModeScreen() override;

	void onStart() override;
	void onStop() override;

	/** Slide cycle period in ms. Public for parity with PhoneAboutScreen's
	 *  refresh-period style and so a host or unit-test could in principle
	 *  introspect the cadence without standing the screen up. */
	static constexpr uint32_t kSlidePeriodMs = 3000;

	/** Total number of slides in the looping demo deck. */
	static constexpr uint8_t kSlideCount = 9;

	/**
	 * S203 - resolve the slide index the screen should open on, reading
	 * Settings.demoSlideStart and clamping it to [0..kSlideCount-1] so a
	 * stale or NVS-resize-wiped byte never crashes the constructor.
	 * Public + static so a host or unit test can introspect the resume
	 * point without having to instantiate the screen.
	 */
	static uint8_t resolveStartSlide();

private:
	PhoneSynthwaveBg*    wallpaper;
	PhoneStatusBar*      statusBar;
	PhoneSoftKeyBar*     softKeys;

	lv_obj_t*            captionLabel;     // "DEMO MODE" caption
	lv_obj_t*            titleLabel;       // pixelbasic16 cream title
	lv_obj_t*            sub1Label;        // pixelbasic7 cream sub-line 1
	lv_obj_t*            sub2Label;        // pixelbasic7 cream sub-line 2
	lv_obj_t*            accentLabel;      // pixelbasic7 dim purple accent
	lv_obj_t*            dotsLabel;        // pixelbasic7 progress-dot row
	lv_obj_t*            hintLabel;        // pixelbasic7 dim "ANY KEY: EXIT"

	lv_timer_t*          slideTimer;
	uint8_t              slideIdx;

	// Layout helpers
	void buildCaption();
	void buildBody();
	void buildHint();

	/** Re-render every label that depends on the current slideIdx. Cheap;
	 *  called on every slide advance + once on screen build. */
	void renderSlide();

	/** lv_timer callback - routes back to the owning instance via
	 *  user_data and calls advanceSlide(). Static so it can be passed
	 *  directly to lv_timer_create. */
	static void onSlideTick(lv_timer_t* timer);

	/** Tick-side helper: increments slideIdx modulo kSlideCount and
	 *  re-renders. */
	void advanceSlide();

	/**
	 * S203 - write the currently visible slide into Settings.demoSlideStart
	 * (calling Settings.store() to persist) when the user dismisses the
	 * screen. No-op when the value already matches what NVS holds, so a
	 * dismiss-on-the-same-slide path never burns a redundant store().
	 */
	void persistCurrentSlide();

	void buttonPressed(uint i) override;
};

#endif //MAKERPHONE_PHONEDEMOMODESCREEN_H
