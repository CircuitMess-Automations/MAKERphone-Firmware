#ifndef MAKERPHONE_PHONECAMERASCREEN_H
#define MAKERPHONE_PHONECAMERASCREEN_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"
#include "../Services/PhoneRingtoneEngine.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneCameraScreen
 *
 * S44 - the MAKERphone in-app camera viewfinder. First Phase-H screen and
 * the natural successor to the PhoneAppStubScreen("CAMERA") that the main
 * menu wired to as a placeholder. Composes the existing Phase-A widgets
 * (PhoneSynthwaveBg, PhoneStatusBar, PhoneSoftKeyBar) with a custom
 * viewfinder overlay (corner brackets, dotted edge ticks, centre
 * crosshair, mode label, frame counter, "live" REC dot) and a
 * non-blocking shutter sound delegated to the global
 * PhoneRingtoneEngine into a feature-phone camera-app silhouette:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |  L---  -  -  -  -  -  -  -  -  -  ---J|   corner brackets +
 *   |  *                                  *  |   dotted edge ticks
 *   |  |                                  |  |
 *   |  |              |                   |  |
 *   |  |            - + -                 |  |   centre crosshair / reticle
 *   |  |              |                   |  |
 *   |  *                                  *  |
 *   |  |---  -  -  -  -  -  -  -  -  -  ---|  |
 *   |  PHOTO                       0/24      | <- mode label + frame counter
 *   |  CAPTURE                         EXIT->| <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Input contract - feature-phone muscle memory:
 *   - BTN_ENTER (BTN_A) : trigger the shutter (flash + click + counter++)
 *   - BTN_BACK         : pop back to the main menu
 *   - BTN_L            : previous mode (Photo <- Effect <- Selfie, wraps)
 *   - BTN_R            : next mode (Photo -> Effect -> Selfie, wraps)
 *
 * Mode-switching (PHOTO / EFFECT / SELFIE) ships in S45 - the L/R
 * bumpers (BTN_L / BTN_R) cycle the mode, the mode label recolours
 * to the per-mode accent (cyan/orange/cream), the small REC dot in
 * the top-left of the viewfinder switches to the same accent so the
 * mode is visible at a glance even with the label half-occluded by
 * the soft-key bar, and a single soft "tick" plays through the
 * Ringtone engine so the cycle has audio feedback. Photo / Effect /
 * Selfie are still placeholder behaviours - the actual capture path
 * is identical for all three modes; S46+ will diverge them.
 *
 * Implementation notes:
 *  - Code-only (no SPIFFS assets). Every primitive is a tiny lv_obj
 *    rectangle styled with a flat colour - no canvases, no allocated
 *    buffers, no fonts beyond pixelbasic7. Cost is a fixed handful of
 *    LVGL objects (4 corners x 2 arms each, ~10 dotted edge ticks per
 *    side, 3 crosshair pieces, 1 REC dot, 1 flash overlay, 2 labels).
 *  - The shutter sound is a 3-note "click" played through Ringtone
 *    (S39) using a static const Melody so we do not allocate on every
 *    capture. Respects Settings.sound just like the ringtone does.
 *  - On capture the entire viewfinder briefly inverts to a bright cyan
 *    (the "flash"), then fades back over ~150 ms via an LVGL animation
 *    on the flash overlay's opacity. While the flash is fading, the
 *    frame-counter label increments and the soft-key caption flicks
 *    from "CAPTURE" to "SAVED" for the same duration so the user gets
 *    explicit feedback that the (simulated) photo was taken.
 *  - Leaving the screen always stops Ringtone playback so the click
 *    cannot leak a residual tone into a parent screen.
 *
 * No SPIFFS writes happen - the "frame counter" is in-memory only and
 * resets every time the screen is opened. S46 (PhoneGalleryScreen)
 * wires up persistent storage; for S44 the counter exists purely as
 * UI feedback that the shutter actually fired.
 */
class PhoneCameraScreen : public LVScreen, private InputListener {
public:
	enum class Mode : uint8_t {
		Photo  = 0,    // default - plain viewfinder + click
		Effect,        // future S45 - placeholder enum entry, not exposed yet
		Selfie,        // future S45 - placeholder enum entry, not exposed yet
	};

	PhoneCameraScreen();
	virtual ~PhoneCameraScreen() override;

	void onStart() override;
	void onStop() override;

	/** Switch to the given mode. Refreshes the mode label, recolours
	 *  the live REC dot to the per-mode accent so the change is visible
	 *  at a glance, and is safe to call before / after onStart(). */
	void setMode(Mode m);
	Mode getMode() const { return mode; }

	/** Cycle modes by +1 (next) or -1 (prev) with wrap. Plays a soft
	 *  "tick" through the Ringtone engine so the user gets audio
	 *  feedback on the bumper press. Any other dir is treated as +1. */
	void cycleMode(int8_t dir);

	/** Trigger the shutter: flash overlay + click sound + frame++. Public
	 *  so a host or future test harness can simulate captures. */
	void shoot();

	/** Number of (simulated) photos captured this session. */
	uint8_t getFrameCount() const { return frameCount; }

	/** UI animation period for the flash overlay fade-out. */
	static constexpr uint16_t FlashFadeMs = 150;

	/** Maximum simulated frame budget - shows up in the "X/24" caption. */
	static constexpr uint8_t  FrameBudget = 24;

	/** Number of dotted ticks drawn along each viewfinder edge. */
	static constexpr uint8_t  EdgeTicksHoriz = 9;
	static constexpr uint8_t  EdgeTicksVert  = 5;

private:
	// ----- visual children -----
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	// HUD labels.
	lv_obj_t* modeLabel;      // "PHOTO" - cyan, pixelbasic7
	lv_obj_t* frameLabel;     // "0/24" - dim purple, pixelbasic7

	// Small "live" REC dot in the top-left of the viewfinder. Held as
	// a member (S45) so cycleMode can recolour it on mode change. Built
	// in buildViewfinder() so the rest of the corner-bracket / tick /
	// crosshair primitives stay anonymous (we never need them again
	// after construction).

	lv_obj_t* recDot;         // sunset-orange (Photo) / cyan (Effect) / cream (Selfie)

	// Flash overlay (cyan rect over the viewfinder, opaque on shoot()).
	lv_obj_t* flash;

	// ----- state -----
	Mode    mode;
	uint8_t frameCount;       // 0..FrameBudget
	bool    flashActive;      // true while the flash overlay is visible
	lv_timer_t* clickResetTimer;  // resets soft-key caption "SAVED" -> "CAPTURE"

	// ----- builders -----
	void buildViewfinder();
	void buildHud();
	void buildFlash();

	// Helper for buildViewfinder() - creates a coloured rectangle with no
	// border / no padding, parented at obj, positioned with IGNORE_LAYOUT.
	lv_obj_t* makeRect(lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, lv_color_t c);

	// ----- helpers -----
	void refreshModeLabel();
	void refreshFrameLabel();
	void playShutterSound();

	// S45: tiny non-blocking "tick" played on every mode cycle so the
	// bumper press has audible feedback alongside the label change.
	void playModeTickSound();

	// S45: per-mode accent colour. Cyan for Photo, sunset orange for
	// Effect, warm cream for Selfie. Used by setMode to retint the
	// mode label and the REC dot together.
	static lv_color_t modeAccent(Mode m);

	// LVGL animation / timer callbacks. They cast user_data back to the
	// screen instance.
	static void onFlashAnim(void* var, int32_t v);
	static void onClickResetTick(lv_timer_t* t);

	// ----- input -----
	void buttonPressed(uint i) override;

	/** Geometry / layout constants - pinned to the 160x128 display. */
	static constexpr uint16_t ScreenW    = 160;
	static constexpr uint16_t ScreenH    = 128;
	static constexpr uint16_t VfX        = 14;    // viewfinder top-left x (rel. to screen)
	static constexpr uint16_t VfY        = 14;    // viewfinder top-left y (just below status bar)
	static constexpr uint16_t VfW        = 132;   // viewfinder width  (160 - 14 - 14)
	static constexpr uint16_t VfH        =  78;   // viewfinder height (just above HUD line)
	static constexpr uint16_t HudY       =  96;   // HUD label baseline y (between viewfinder and softkeys)
	static constexpr uint16_t CornerLen  = 6;     // bracket arm length in px
	static constexpr uint16_t CornerThk  = 1;     // bracket stroke thickness in px
	static constexpr uint16_t CrossArm   = 4;     // crosshair arm length in px
};

#endif // MAKERPHONE_PHONECAMERASCREEN_H
