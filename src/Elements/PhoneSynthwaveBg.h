#ifndef MAKERPHONE_PHONESYNTHWAVEBG_H
#define MAKERPHONE_PHONESYNTHWAVEBG_H

#include <Arduino.h>
#include <lvgl.h>
#include "../Interface/LVObject.h"

/**
 * PhoneSynthwaveBg
 *
 * Reusable retro-feature-phone wallpaper (160x128) for MAKERphone 2.0. It is
 * the first Phase-2 element after PhoneStatusBar, PhoneSoftKeyBar and
 * PhoneClockFace, and is intended to sit at the bottom of any screen that
 * wants the trademark synthwave look:
 *
 *      .                .                  .          <- twinkling stars
 *           ___                                          (purple -> magenta
 *          (   )                                          -> orange sky)
 *         (_____)                                       <- pulsing sun (with
 *      ====== H O R I Z O N ======                          three dark scan
 *       \    \   |   /    /                                 lines + soft halo)
 *        \    \  |  /    /                            <- vanishing-point
 *      ----------+----------                                perspective rays
 *      ------------+----------                          <- scrolling ground
 *      ----------------+----------                         horizontals
 *
 * Implementation notes:
 *  - 100% code-only - no SPIFFS assets, no canvas backing buffers - so it
 *    adds zero data partition cost and only a handful of LVGL objects.
 *  - The sun is a circular lv_obj clipped to the sky band so only the top
 *    half is visible (overlap_clip via parent bounds, no manual masking
 *    needed in LVGL 8.x).
 *  - Sky and ground use LVGL's native bg_grad_color stops, which the
 *    framework renders as a vertical gradient on flush - a single object
 *    each, no multi-stripe overdraw.
 *  - Perspective rays are lv_line widgets fanning out from a vanishing
 *    point at the center of the horizon. Each line stores its endpoints
 *    in this object's instance memory because lv_line_set_points keeps
 *    a pointer rather than copying the array.
 *  - Anchored with LV_OBJ_FLAG_IGNORE_LAYOUT (same pattern as the other
 *    phone widgets) so it cooperates with parents that already use a
 *    flex/grid layout. Constructed first on the host screen so it sits
 *    behind every other widget.
 *  - Stars twinkle. Each star runs its own infinite ping-pong opacity
 *    animation with a per-star period and delay so the field reads as
 *    organic noise rather than a synchronised pulse. The animations are
 *    owned by the star objects, so when this widget is destroyed LVGL
 *    auto-removes them on lv_obj_del - no manual teardown needed.
 *  - The sun has a soft halo ring (drawn behind the sun in sky's child
 *    list) whose opacity ping-pongs slowly to give the sun a subtle
 *    "breathing" pulse, matching real synthwave wallpapers without
 *    requiring transform/zoom support that some LVGL builds disable.
 *  - The four ground horizontal lines scroll continuously toward the
 *    viewer on independent ease-in animations, giving the synthwave
 *    grid the trademark "racing into the camera" motion. Each line
 *    fades up from a faint horizon glow to a solid cyan stroke as it
 *    approaches the bottom of the screen, faking depth without any
 *    per-pixel work.
 */
class PhoneSynthwaveBg : public LVObject {
public:
	/**
	 * S53 wallpaper variants. The default constructor reads the
	 * persisted choice from `Settings.get().wallpaperStyle` so every
	 * screen that drops a `new PhoneSynthwaveBg(obj)` automatically
	 * picks up the user's preference. Tests / hosts that want a
	 * specific look (e.g. always-Synthwave for a swatch preview) can
	 * pass the Style explicitly via the second constructor.
	 *
	 *  - Synthwave: full retro look (sun, gradient, perspective grid,
	 *    ground horizontals, twinkle stars). The original wallpaper.
	 *  - Plain:     gradient sky + ground only - calmest look, easiest
	 *               on the eyes for long settings/reading screens.
	 *  - GridOnly:  gradient + perspective rays + ground horizontals -
	 *               keeps the synthwave grid motion without the sun
	 *               drawing the eye, useful for utility screens.
	 *  - Stars:     gradient + twinkle stars - night-sky vibe, no
	 *               sun and no grid. Reads as "calm + magical".
	 */
	enum class Style : uint8_t {
		Synthwave = 0,
		Plain     = 1,
		GridOnly  = 2,
		Stars     = 3,
	};

	/** Default ctor — picks the style from `Settings.get().wallpaperStyle`. */
	PhoneSynthwaveBg(lv_obj_t* parent);

	/** Explicit-style ctor — used by previews / tests that bypass Settings. */
	PhoneSynthwaveBg(lv_obj_t* parent, Style style);

	virtual ~PhoneSynthwaveBg() = default;

	/** Resolve a raw `Settings.wallpaperStyle` byte to a clamped Style. */
	static Style styleFromByte(uint8_t raw);

	static constexpr uint16_t BgWidth   = 160;
	static constexpr uint16_t BgHeight  = 128;
	static constexpr uint16_t HorizonY  = 72;        // pixel row of the horizon
	static constexpr uint8_t  RayCount  = 7;         // perspective rays
	static constexpr uint8_t  HLineCount = 4;        // horizontal grid lines
	static constexpr uint8_t  StarCount  = 7;        // twinkle stars in the sky
	static constexpr uint8_t  ScanCount  = 3;        // dark scan lines across the sun

	// Sun "breath" period (ms). Full ping-pong cycle = 2 * SunPulsePeriod.
	// Long enough to feel like a calm pulse, short enough to be noticeable.
	static constexpr uint16_t SunPulsePeriod   = 1800;

	// Time it takes a horizontal grid line to traverse the ground from
	// the horizon to the bottom of the screen. Smaller = faster scroll.
	// 4000 ms feels close to the cadence of arcade synthwave scenes.
	static constexpr uint16_t GridScrollPeriod = 4000;

private:
	lv_obj_t* sky;          // upper half: sky gradient
	lv_obj_t* ground;       // lower half: ground gradient
	lv_obj_t* sun;          // circle clipped to sky bottom
	lv_obj_t* haloRing;     // soft halo behind the sun (opacity-pulsed)

	lv_obj_t* scanLines[ScanCount];
	lv_obj_t* hGridLines[HLineCount];
	lv_obj_t* rays[RayCount];
	lv_obj_t* stars[StarCount];

	// lv_line keeps a *pointer* to the points array instead of copying, so
	// the storage has to outlive the widget. Two points per ray.
	lv_point_t rayPoints[RayCount][2];

	void buildSky();
	void buildSun();
	void buildGround();
	void buildRays();
	void buildHorizontals();
	void buildStars();

	// Drives the per-star opacity animation. Free function semantics
	// (matches LVGL's lv_anim_exec_xcb_t signature). Defined in the .cpp
	// translation unit.
	static void twinkleExec(void* var, int32_t v);

	// Drives the halo's opacity ping-pong - the visible "sun pulse".
	static void sunPulseExec(void* var, int32_t v);

	// Drives a single horizontal grid line's y position + opacity ramp
	// as it scrolls from the horizon toward the viewer.
	static void gridScrollExec(void* var, int32_t v);
};

#endif //MAKERPHONE_PHONESYNTHWAVEBG_H
