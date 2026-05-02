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
		// S101 — Nokia 3310 Monochrome theme override. Not part of the
		// 4-style PhoneWallpaperScreen pager (StyleCount stays at 4
		// there); this value is selected exclusively by
		// `resolveStyleFromSettings()` when Settings.themeId picks the
		// Nokia 3310 theme. Renders a flat pea-green LCD panel with a
		// faint scanline pattern + a small pixel-art antenna motif,
		// bypassing every Synthwave builder (sky/ground/sun/rays/grid
		// /stars) so the wallpaper reads as a real Nokia 3310 idle
		// screen rather than a tinted Synthwave.
		Nokia3310 = 4,

		// S103 - Game Boy DMG (Dot Matrix Game) theme override.
		// Same dispatch pattern as Nokia3310: not part of the 4-style
		// PhoneWallpaperScreen pager (StyleCount stays at 4 there);
		// this value is only ever returned by
		// resolveStyleFromSettings() when Settings.themeId picks the
		// Game Boy DMG theme. Renders a flat 4-shade pea-mint LCD
		// panel with a faint pixel-dither pattern + a small 8-bit
		// 'boy' silhouette anchored bottom-right, bypassing every
		// Synthwave builder so the wallpaper reads as a real DMG-01
		// idle screen rather than a tinted Synthwave.
		GameBoyDMG = 5,

		// S105 - Amber CRT phosphor theme override. Same dispatch
		// pattern as Nokia3310 / GameBoyDMG: not part of the 4-style
		// PhoneWallpaperScreen pager (StyleCount stays at 4 there);
		// this value is only ever returned by
		// resolveStyleFromSettings() when Settings.themeId picks the
		// Amber CRT theme. Renders a flat near-black warm-brown CRT
		// panel with horizontal scanlines + a subtle vignette + a
		// small ">_" terminal prompt motif anchored bottom-right,
		// bypassing every Synthwave builder so the wallpaper reads
		// as a real 1980s amber phosphor terminal rather than a
		// tinted Synthwave.
		AmberCRT  = 6,
	};

	/** Default ctor — picks the style from `Settings.get().wallpaperStyle`. */
	PhoneSynthwaveBg(lv_obj_t* parent);

	/** Explicit-style ctor — used by previews / tests that bypass Settings. */
	PhoneSynthwaveBg(lv_obj_t* parent, Style style);

	virtual ~PhoneSynthwaveBg() = default;

	/** Resolve a raw `Settings.wallpaperStyle` byte to a clamped Style. */
	static Style styleFromByte(uint8_t raw);

	/**
	 * S101 — resolve the *effective* Style by consulting both the
	 * global theme (Settings.themeId) and the per-theme wallpaper
	 * choice (Settings.wallpaperStyle).
	 *
	 * Returns Style::Nokia3310 whenever the user has the Nokia 3310
	 * Monochrome theme selected, Style::GameBoyDMG when the Game Boy
	 * DMG theme is selected (S103); in every other case it falls through
	 * to styleFromByte(Settings.wallpaperStyle), preserving the
	 * existing per-Synthwave-variant behaviour. The default
	 * constructor uses this resolver, so dropping `new
	 * PhoneSynthwaveBg(obj)` in any screen automatically picks up
	 * the user's current theme without callers having to know the
	 * wiring.
	 */
	static Style resolveStyleFromSettings();

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

	// S101 — Nokia 3310 Monochrome wallpaper builder. Bypasses
	// buildSky / buildGround entirely (those paint the Synthwave
	// gradient) and instead lays down a single pea-green LCD panel
	// with a faint scanline pattern + a tiny pixel-art antenna motif
	// anchored bottom-right. Uses the Nokia palette from
	// MakerphoneTheme.h. ~12 LVGL primitives total, no animations -
	// the Nokia 3310's idle screen is famously static, so a still
	// preview is the authentic look.
	void buildNokia3310Wallpaper();

	// S103 - Game Boy DMG (Dot Matrix Game) wallpaper builder. Same
	// pattern as buildNokia3310Wallpaper: paints a flat 4-shade
	// pea-mint LCD panel covering the full 160x128 area, a faint
	// 2-shade pixel-dither pattern across the panel to suggest the
	// LCD's row structure, and a tiny pixel-art 'boy' silhouette
	// (the DMG-01's iconic startup animation reduced to its idle
	// resting pose) anchored bottom-right in the patch the soft-key
	// bar covers anyway. Uses the GBDMG_* palette from
	// MakerphoneTheme.h. ~22 LVGL primitives total, no animations -
	// matches the Nokia variant's still-image philosophy.
	void buildGameBoyDMGWallpaper();

	// S105 - Amber CRT phosphor wallpaper builder. Same dispatch
	// pattern as buildNokia3310Wallpaper / buildGameBoyDMGWallpaper:
	// paints a flat near-black warm-brown CRT panel covering the
	// full 160x128 area, a faint horizontal scanline pattern across
	// the panel (every 2nd row, AMBER_CRT_DIM at low opacity) for
	// the iconic phosphor scanline cue, and a small pixel-art
	// '>_' terminal prompt motif anchored bottom-right in the patch
	// the soft-key bar covers anyway. Uses the AMBER_CRT_* palette
	// from MakerphoneTheme.h. ~70 LVGL primitives total (mostly
	// scanline rects), no animations - matches the Nokia / DMG
	// variants' still-image philosophy.
	void buildAmberCRTWallpaper();

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
