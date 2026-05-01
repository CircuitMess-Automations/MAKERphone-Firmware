#ifndef MAKERPHONE_PHONEBOOTSPLASH_H
#define MAKERPHONE_PHONEBOOTSPLASH_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

/**
 * PhoneBootSplash (S56)
 *
 * The very first screen the MAKERphone shows on boot. A code-only
 * "MAKERphone wordmark + sunset" splash that holds for 3 seconds and
 * then auto-advances into the legacy IntroScreen (which keeps the
 * existing post-intro routing into LockScreen -> PhoneHomeScreen
 * intact). Pressing any hardware key short-circuits the auto-advance
 * timer and dismisses immediately, so a user who has seen the splash a
 * thousand times can blast straight into the home screen.
 *
 *   +----------------------------------------+
 *   |                                        |  <- deep-purple sky band
 *   |                                        |
 *   |             MAKERphone                 |  <- pixelbasic16 wordmark
 *   |               2 . 0                    |  <- pixelbasic7 cyan tagline
 *   |                                        |
 *   |  ............... ___ ................. |  <- horizon line + dotted ticks
 *   |                /     \                 |  <- half-sun rising at the
 *   |              /         \               |     bottom-centre, clipped
 *   |            /             \             |     by the screen edge
 *   |     press any key                      |  <- dim cream hint, dimmed in
 *   +----------------------------------------+
 *
 * Implementation notes:
 *  - 100% code-only. No SPIFFS asset cost. The sky is a single LVGL
 *    object with a vertical bg gradient (deep purple -> magenta), and a
 *    second "ground" object overlays the lower band with magenta ->
 *    sunset orange so the eye reads a smooth 3-stop sunset across the
 *    whole height. Same trick PhoneSynthwaveBg uses to fake a 3-stop
 *    gradient out of LVGL 8.x's 2-stop primitive.
 *  - The sun is a circular lv_obj radius=W/2 anchored at the bottom of
 *    the screen with its centre below the screen edge, so only the top
 *    half of the disc shows. A soft outline (LVGL outline) gives it a
 *    halo without needing canvas.
 *  - The MAKERphone wordmark is a pixelbasic16 label tinted in warm
 *    cream (MP_TEXT). The "2.0" tagline below uses pixelbasic7 cyan
 *    (MP_HIGHLIGHT) so the version reads as a hot-key-style accent
 *    rather than competing with the wordmark.
 *  - The "press any key" hint at the bottom is rendered in dim purple
 *    (MP_LABEL_DIM) so it sits as a quiet affordance under the sun.
 *  - Auto-dismiss after DefaultDurationMs ms (3000). Driven by a single
 *    one-shot lv_timer started in onStart() so the 3 s window begins
 *    when the splash actually appears, not when it was constructed.
 *  - Any hardware key (BTN_BACK, BTN_ENTER, BTN_LEFT, BTN_RIGHT, any
 *    keypad digit, L/R bumpers) short-circuits the timer. Same
 *    "press any key to skip" pattern PhoneCallEnded (S26) uses for its
 *    auto-dismiss overlay.
 *  - Dismissal does NOT pop() like the rest of the LVScreen overlays:
 *    the splash is the FIRST screen of the boot flow, so there is no
 *    parent to pop back to. Instead the screen calls a host-supplied
 *    DismissHandler that owns starting the next screen (the existing
 *    IntroScreen) and then frees this splash via its own callback - a
 *    direct mirror of the IntroScreen -> LockScreen -> Home pattern
 *    already used by MAKERphone-Firmware.ino. The splash deletes its
 *    own lv_obj before invoking the handler so a slow handler can not
 *    redraw a half-torn-down screen.
 *  - Guarded against double-fire: a hardware key press and the
 *    auto-dismiss timer can race within a single LVGL tick. The
 *    `dismissedAlready` flag collapses both paths to a single dispatch.
 */
class PhoneBootSplash : public LVScreen, private InputListener {
public:
	using DismissHandler = void (*)();

	/** Default splash hold time, in milliseconds. */
	static constexpr uint32_t DefaultDurationMs = 3000;

	/**
	 * Build the splash. The DismissHandler is invoked exactly once
	 * after either the timer fires or the user presses any key. The
	 * handler is responsible for instantiating + starting the next
	 * screen (typically a freshly-allocated IntroScreen). Pass nullptr
	 * to make the splash a no-op overlay (mostly useful from tests).
	 *
	 * `durationMs` defaults to 3000 to match the roadmap spec; tests
	 * can pass a smaller value to keep their wall-clock time short.
	 */
	explicit PhoneBootSplash(DismissHandler onDismiss = nullptr,
							 uint32_t       durationMs = DefaultDurationMs);

	~PhoneBootSplash() override;

	void onStart() override;
	void onStop() override;

	/** From InputListener - any key short-circuits the auto-dismiss. */
	void buttonPressed(uint i) override;

private:
	DismissHandler dismissCb;
	uint32_t       durationMs;
	bool           dismissedAlready;

	lv_obj_t* sky;          // upper gradient band
	lv_obj_t* ground;       // lower gradient band
	lv_obj_t* sun;          // half-disc clipped by the screen edge
	lv_obj_t* horizonLine;  // thin orange line where sky meets ground
	lv_obj_t* wordmark;     // "MAKERphone" pixelbasic16 label
	lv_obj_t* tagline;      // "2.0" pixelbasic7 cyan accent
	lv_obj_t* hint;         // "press any key" dim hint

	lv_timer_t* dismissTimer;

	void buildSky();
	void buildGround();
	void buildHorizon();
	void buildSun();
	void buildWordmark();
	void buildTagline();
	void buildHint();

	void startDismissTimer();
	void stopDismissTimer();
	void fireDismiss();

	static void onDismissTimer(lv_timer_t* timer);
};

#endif //MAKERPHONE_PHONEBOOTSPLASH_H
