#ifndef MAKERPHONE_PHONEWELCOMESCREEN_H
#define MAKERPHONE_PHONEWELCOMESCREEN_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;

/**
 * PhoneWelcomeScreen (S145)
 *
 * Sony-Ericsson-style "Hello, $NAME!" greeting that flashes briefly
 * between the boot intro animation and the LockScreen. Reads the owner
 * string from Settings.ownerName (the field S144 added to SettingsData).
 * If the owner name is empty (factory default) the screen is skipped
 * entirely so a freshly-flashed Chatter still boots straight from the
 * intro into the lock screen with no visible delta.
 *
 *   +----------------------------------------+
 *   |                                        |  <- synthwave wallpaper
 *   |                                        |
 *   |                Hello,                  |  <- pixelbasic16 cream prefix
 *   |                ALBERT                  |  <- pixelbasic16 sunset-orange
 *   |                                        |     name in upper-case
 *   |                                        |
 *   |          press any key                 |  <- pixelbasic7 dim hint
 *   +----------------------------------------+
 *
 * Implementation notes
 *  - 100% code-only - no SPIFFS asset cost, no canvas, only LVGL labels
 *    on top of the existing PhoneSynthwaveBg wallpaper. Same "build
 *    from scratch each time" pattern PhoneBootSplash (S56) and
 *    PhoneCallEnded (S26) already use.
 *  - The greeting holds for DefaultDurationMs (1500 ms) on a single
 *    one-shot lv_timer started in onStart(), so the visible window
 *    begins when the screen actually appears - not when it is
 *    constructed by IntroScreen's READY handler. Pressing any
 *    hardware key short-circuits the timer and dismisses immediately,
 *    which mirrors the "press any key to skip" behaviour the rest of
 *    the boot-flow overlays use.
 *  - On dismiss the screen tears itself down (stop, lv_obj_del) and
 *    invokes the host-supplied DismissHandler; the handler is what
 *    pushes the LockScreen on top of the freshly-built PhoneHomeScreen.
 *    Boot-flow lifetime mirrors PhoneBootSplash::fireDismiss exactly so
 *    the two splashes read as one family.
 *  - The screen does NOT use a PhoneStatusBar - the boot flow has not
 *    yet reached the home/lock surface where the status strip belongs,
 *    and a stripped-down full-bleed greeting reads more like the
 *    Sony-Ericsson "T610 hello" screen than a chrome-shrouded modal.
 *  - Owner-name is upper-cased on render (pixelbasic16 only ships an
 *    upper-case glyph table) and dot-truncated to MaxRenderLen so an
 *    over-long name never wraps off the 160-px screen. The persisted
 *    Settings.ownerName field stays unchanged - upper-casing is purely
 *    a render-time choice.
 *  - The dismiss callback is a plain `void (*)()` rather than a
 *    std::function so the screen stays POD-callback-compatible with
 *    the existing PhoneBootSplash/IntroScreen wiring style.
 *
 * Layout (160x128 budget):
 *   y =  0..127  PhoneSynthwaveBg fills the whole screen.
 *   y = 44       "Hello," prefix label, pixelbasic16 cream, centered.
 *   y = 64       owner-name label, pixelbasic16 sunset-orange, centered.
 *   y = 110      "press any key" hint, pixelbasic7 dim, centered.
 *
 * Construction is cheap (a wallpaper element + three labels + one
 * timer), so a host that wants to greet the user at later moments
 * (e.g. on charge-complete in S150) can reuse the same screen with a
 * different DismissHandler without paying any persistent cost.
 */
class PhoneWelcomeScreen : public LVScreen, private InputListener {
public:
	using DismissHandler = void (*)();

	/** Default greeting hold time, in milliseconds. Sized to feel
	 *  "blink-and-you-miss-it" rather than ceremonial - the user is
	 *  trying to use their phone, not watch a brand intro. */
	static constexpr uint32_t DefaultDurationMs = 1500;

	/** Hard cap on the rendered name length. The persisted buffer is
	 *  23 chars (MaxLen on PhoneOwnerNameScreen); we cap the on-screen
	 *  render at the same number so the dot-truncated label can never
	 *  wrap into the hint row below. */
	static constexpr uint16_t MaxRenderLen = 23;

	/**
	 * Build the greeting screen. The DismissHandler is invoked exactly
	 * once after the timer fires or any key is pressed. It is up to
	 * the handler to instantiate / activate the next screen (typically
	 * LockScreen::activate(home) for the boot flow). Pass nullptr to
	 * make the screen a self-tearing-down no-op overlay (mostly useful
	 * from tests).
	 *
	 * `durationMs` defaults to 1500 to match the roadmap spec; tests
	 * can pass a smaller value to keep their wall-clock time short.
	 * A duration of 0 disables the auto-dismiss timer entirely - the
	 * screen will then hold until the user presses a key.
	 */
	explicit PhoneWelcomeScreen(DismissHandler onDismiss = nullptr,
								uint32_t       durationMs = DefaultDurationMs);

	~PhoneWelcomeScreen() override;

	void onStart() override;
	void onStop() override;

	/** From InputListener - any key short-circuits the auto-dismiss. */
	void buttonPressed(uint i) override;

	/**
	 * Static probe: returns true if Settings.ownerName is non-empty,
	 * i.e. there is actually a name to greet. Hosts can use this in
	 * the boot flow to skip constructing the screen on a fresh device.
	 * Safe to call before LVScreen has been initialised - it only
	 * touches the Settings singleton.
	 */
	static bool isEnabled();

private:
	DismissHandler dismissCb;
	uint32_t       durationMs;
	bool           dismissedAlready;

	PhoneSynthwaveBg* wallpaper;     // bottom-of-stack synthwave look
	lv_obj_t*         greetLabel;    // "Hello,"
	lv_obj_t*         nameLabel;     // upper-cased owner name
	lv_obj_t*         hintLabel;     // "press any key"

	lv_timer_t*       dismissTimer;

	void buildGreeting();
	void buildName();
	void buildHint();

	void startDismissTimer();
	void stopDismissTimer();
	void fireDismiss();

	static void onDismissTimer(lv_timer_t* timer);
};

#endif // MAKERPHONE_PHONEWELCOMESCREEN_H
