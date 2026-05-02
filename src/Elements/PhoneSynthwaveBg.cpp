#include "PhoneSynthwaveBg.h"
#include <stdio.h>
#include <Settings.h>
#include "../MakerphoneTheme.h"

// MAKERphone retro palette (kept consistent with PhoneStatusBar /
// PhoneSoftKeyBar / PhoneClockFace).
#define MP_BG_DARK     lv_color_make( 20,  12,  36)   // deep purple
#define MP_PURPLE_MID  lv_color_make( 70,  40, 110)   // mid purple band
#define MP_MAGENTA     lv_color_make(180,  40, 140)   // magenta near horizon
#define MP_ACCENT      lv_color_make(255, 140,  30)   // sunset orange
#define MP_SUN         lv_color_make(255, 170,  50)   // bright sun fill
#define MP_HORIZON     lv_color_make(255, 200,  90)   // hot orange-yellow horizon line
#define MP_GROUND_TOP  lv_color_make( 80,  20,  90)   // ground near horizon
#define MP_GROUND_BOT  lv_color_make( 10,   5,  25)   // ground deep distance
#define MP_GRID        lv_color_make(122, 232, 255)   // cyan grid
#define MP_STAR        lv_color_make(255, 240, 220)   // warm white stars

PhoneSynthwaveBg::Style PhoneSynthwaveBg::styleFromByte(uint8_t raw){
	// Defensive clamp - any persisted byte outside the known enum range
	// falls back to Synthwave (the original look) so a corrupted NVS
	// page can never render a blank screen.
	switch(raw){
		case static_cast<uint8_t>(Style::Plain):     return Style::Plain;
		case static_cast<uint8_t>(Style::GridOnly):  return Style::GridOnly;
		case static_cast<uint8_t>(Style::Stars):     return Style::Stars;
		case static_cast<uint8_t>(Style::Synthwave):
		default:                                     return Style::Synthwave;
	}
}

PhoneSynthwaveBg::Style PhoneSynthwaveBg::resolveStyleFromSettings(){
	// S101 - the global theme overrides the per-Synthwave wallpaperStyle:
	// if the user has selected a non-default theme (Nokia 3310 today),
	// the wallpaper is the theme's variant regardless of which Synthwave
	// style the user previously picked. Their wallpaperStyle byte stays
	// persisted, so flipping the theme back to Default restores the
	// previously chosen Synthwave variant without resetting it.
	if(MakerphoneTheme::getCurrent() == MakerphoneTheme::Theme::Nokia3310){
		return Style::Nokia3310;
	}
	// S103 - dispatch the DMG style override the same way. The
	// wallpaperStyle byte stays persisted underneath so flipping the
	// theme back to Default restores the previously chosen Synthwave
	// variant unchanged, exactly like the Nokia branch.
	if(MakerphoneTheme::getCurrent() == MakerphoneTheme::Theme::GameBoyDMG){
		return Style::GameBoyDMG;
	}
	// S105 - dispatch the Amber CRT style override the same way.
	// The wallpaperStyle byte stays persisted underneath so flipping
	// the theme back to Default / Nokia / DMG restores the previously
	// chosen Synthwave variant unchanged, exactly like the Nokia and
	// DMG branches above.
	if(MakerphoneTheme::getCurrent() == MakerphoneTheme::Theme::AmberCRT){
		return Style::AmberCRT;
	}
	// S107 - dispatch the Sony Ericsson Aqua style override the same
	// way. The wallpaperStyle byte stays persisted underneath so
	// flipping the theme back to Default / Nokia / DMG / Amber CRT
	// restores the previously chosen Synthwave variant unchanged,
	// exactly like the prior branches above.
	if(MakerphoneTheme::getCurrent() == MakerphoneTheme::Theme::SonyEricssonAqua){
		return Style::SonyEricssonAqua;
	}
	// S109 - dispatch the RAZR Hot Pink style override the same way.
	// The wallpaperStyle byte stays persisted underneath so flipping
	// the theme back to any prior theme restores the previously
	// chosen Synthwave variant unchanged, exactly like the prior
	// branches above.
	if(MakerphoneTheme::getCurrent() == MakerphoneTheme::Theme::RazrHotPink){
		return Style::RazrHotPink;
	}
	return styleFromByte(Settings.get().wallpaperStyle);
}

PhoneSynthwaveBg::PhoneSynthwaveBg(lv_obj_t* parent)
		: PhoneSynthwaveBg(parent, resolveStyleFromSettings()) {
	// Delegating ctor - reads both the persisted theme and the
	// per-Synthwave wallpaperStyle on every drop, so every new screen
	// picks up the user's current preference without each caller
	// having to know the wiring. The resolver short-circuits to
	// Style::Nokia3310 whenever Settings.themeId picks the Nokia 3310
	// theme (S101) so the screen renders the LCD-green wallpaper
	// instead of a tinted Synthwave.
}

PhoneSynthwaveBg::PhoneSynthwaveBg(lv_obj_t* parent, Style style) : LVObject(parent){
	// Default-init member pointers that may be left null by a non-Synthwave
	// style. Keeps the destructor / future code defensive against null
	// dereferences if a follow-up commit references e.g. sun directly.
	sky      = nullptr;
	ground   = nullptr;
	sun      = nullptr;
	haloRing = nullptr;
	for(uint8_t i = 0; i < ScanCount;  ++i) scanLines[i]  = nullptr;
	for(uint8_t i = 0; i < HLineCount; ++i) hGridLines[i] = nullptr;
	for(uint8_t i = 0; i < RayCount;   ++i) rays[i]       = nullptr;
	for(uint8_t i = 0; i < StarCount;  ++i) stars[i]      = nullptr;

	// Anchor to the full 160x128 display regardless of parent layout. Using
	// IGNORE_LAYOUT keeps this widget a pure "wallpaper" - it does not get
	// re-flowed by a flex/grid parent.
	lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(obj, BgWidth, BgHeight);
	lv_obj_set_pos(obj, 0, 0);

	// Container itself is transparent - the sky and ground children paint
	// the actual background. This keeps the wallpaper trivially layerable
	// on top of the screen's default bg without inheriting odd theme tints.
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_border_width(obj, 0, 0);
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

	// S101 - the Nokia 3310 Monochrome theme owns its wallpaper end
	// to end: it bypasses every Synthwave builder (sky/ground/sun/rays
	// /grid/stars) and paints a flat pea-green LCD panel instead.
	// Returns early so none of the Synthwave palette ever touches the
	// canvas while the Nokia theme is active.
	if(style == Style::Nokia3310){
		buildNokia3310Wallpaper();
		return;
	}

	// S103 - the Game Boy DMG theme owns its wallpaper end to end
	// the same way: it bypasses every Synthwave builder and paints a
	// flat 4-shade pea-mint LCD panel instead. Returning early keeps
	// the GBDMG palette out of every Synthwave hot-path.
	if(style == Style::GameBoyDMG){
		buildGameBoyDMGWallpaper();
		return;
	}

	// S105 - the Amber CRT theme owns its wallpaper end to end the
	// same way: it bypasses every Synthwave builder and paints a flat
	// near-black warm-brown CRT panel with horizontal scanlines + a
	// '>_' terminal prompt motif instead. Returning early keeps the
	// AMBER_CRT palette out of every Synthwave hot-path.
	if(style == Style::AmberCRT){
		buildAmberCRTWallpaper();
		return;
	}

	// S107 - the Sony Ericsson Aqua theme owns its wallpaper end to
	// end the same way: it bypasses every Synthwave builder and paints
	// a flat ocean-gradient panel with foam currents + bubble specks +
	// a water-droplet motif instead. Returning early keeps the AQUA_*
	// palette out of every Synthwave hot-path.
	if(style == Style::SonyEricssonAqua){
		buildSonyEricssonAquaWallpaper();
		return;
	}

	// S109 - the RAZR Hot Pink theme owns its wallpaper end to end
	// the same way: it bypasses every Synthwave builder and paints a
	// flat dark-magenta gradient panel with anodised-aluminium
	// striations + LED-backlight specks + a Z-shaped lightning-bolt
	// motif instead. Returning early keeps the RAZR_* palette out of
	// every Synthwave hot-path.
	if(style == Style::RazrHotPink){
		buildRazrHotPinkWallpaper();
		return;
	}

	// Sky + ground are the foundation of every Synthwave variant -
	// they always run so the screen never falls back to the default
	// LVGL theme color.
	buildSky();
	buildGround();

	// The sun (and its halo) live only in the full Synthwave variant.
	// Plain / GridOnly / Stars deliberately omit it to read calmer.
	if(style == Style::Synthwave){
		buildSun();
	}

	// Perspective rays + scrolling horizontals belong to the synthwave
	// "racing grid" motif. They appear in Synthwave and GridOnly so a
	// user who wants the grid feel without the sun can still get it.
	if(style == Style::Synthwave || style == Style::GridOnly){
		buildRays();
		buildHorizontals();
	}

	// Twinkle stars are the night-sky cue. They appear in Synthwave and
	// Stars - the Stars-only variant keeps the gradient + stars and
	// drops everything else so the wallpaper reads as a calm sky.
	if(style == Style::Synthwave || style == Style::Stars){
		buildStars();
	}
}

// ----- builders -----

void PhoneSynthwaveBg::buildSky(){
	sky = lv_obj_create(obj);
	lv_obj_remove_style_all(sky);
	lv_obj_clear_flag(sky, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(sky, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(sky, BgWidth, HorizonY);
	lv_obj_set_pos(sky, 0, 0);

	// Vertical gradient: deep purple at top -> magenta at the horizon.
	// LVGL renders this in one pass on flush, no multi-stripe overdraw.
	lv_obj_set_style_bg_color(sky, MP_BG_DARK, 0);
	lv_obj_set_style_bg_grad_color(sky, MP_MAGENTA, 0);
	lv_obj_set_style_bg_grad_dir(sky, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_bg_opa(sky, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(sky, 0, 0);
	lv_obj_set_style_pad_all(sky, 0, 0);
	lv_obj_set_style_border_width(sky, 0, 0);
}

void PhoneSynthwaveBg::buildSun(){
	// Sun is a child of the sky band, so the parent's bounds clip the
	// bottom half automatically - we get a perfect semicircle resting on
	// the horizon without any manual masking work.
	const uint16_t diameter = 56;
	const int16_t  cx = (BgWidth - diameter) / 2;          // center horizontally
	const int16_t  cy = HorizonY - (diameter / 2);         // half above horizon

	// ---- HALO (created BEFORE the sun so it draws underneath) ----
	// A soft warm ring that breathes in opacity, giving the sun a subtle
	// "sun pulse" without touching the sun itself. Sized 12 px wider than
	// the sun in every direction; the parent sky's bottom edge auto-clips
	// the lower half just like with the sun.
	const uint16_t haloDiameter = diameter + 24;
	const int16_t  hx = (BgWidth - haloDiameter) / 2;
	const int16_t  hy = HorizonY - (haloDiameter / 2);
	haloRing = lv_obj_create(sky);
	lv_obj_remove_style_all(haloRing);
	lv_obj_clear_flag(haloRing, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(haloRing, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(haloRing, haloDiameter, haloDiameter);
	lv_obj_set_pos(haloRing, hx, hy);
	lv_obj_set_style_radius(haloRing, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_color(haloRing, MP_SUN, 0);
	lv_obj_set_style_bg_opa(haloRing, LV_OPA_30, 0);
	lv_obj_set_style_border_width(haloRing, 0, 0);
	lv_obj_set_style_pad_all(haloRing, 0, 0);

	// Phase-3 follow-up to the twinkle stars: animate the halo opacity
	// in a slow ping-pong. Path is ease_in_out so the brightening and
	// dimming feel breathy rather than mechanical. The animation is
	// owned by the halo object, so destroying the wallpaper auto-cleans
	// it up via lv_obj_del - no explicit teardown needed.
	lv_anim_t pulse;
	lv_anim_init(&pulse);
	lv_anim_set_var(&pulse, haloRing);
	lv_anim_set_values(&pulse, LV_OPA_10, LV_OPA_50);
	lv_anim_set_time(&pulse, SunPulsePeriod);
	lv_anim_set_playback_time(&pulse, SunPulsePeriod);
	lv_anim_set_path_cb(&pulse, lv_anim_path_ease_in_out);
	lv_anim_set_repeat_count(&pulse, LV_ANIM_REPEAT_INFINITE);
	lv_anim_set_exec_cb(&pulse, sunPulseExec);
	lv_anim_start(&pulse);

	// ---- SUN ----
	sun = lv_obj_create(sky);
	lv_obj_remove_style_all(sun);
	lv_obj_clear_flag(sun, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(sun, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(sun, diameter, diameter);
	lv_obj_set_pos(sun, cx, cy);
	lv_obj_set_style_radius(sun, LV_RADIUS_CIRCLE, 0);

	// Vertical gradient on the sun: bright yellow at top -> hot orange at
	// horizon, gives the sun a glow without any extra widgets.
	lv_obj_set_style_bg_color(sun, MP_SUN, 0);
	lv_obj_set_style_bg_grad_color(sun, MP_ACCENT, 0);
	lv_obj_set_style_bg_grad_dir(sun, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_bg_opa(sun, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(sun, 0, 0);
	lv_obj_set_style_pad_all(sun, 0, 0);

	// Three classic dark scan lines across the sun. Each is a thin
	// magenta-colored rectangle the same color as the surrounding sky
	// gradient near the horizon, so they read as "missing" pixels rather
	// than as overlay objects. Rows in screen-space terms are at y = 50,
	// 60, 68 (i.e. progressively closer to the horizon and tighter).
	const int16_t scanScreenY[ScanCount] = { 50, 60, 68 };
	const uint8_t scanH[ScanCount]       = {  2,  2,  2 };
	for(uint8_t i = 0; i < ScanCount; i++){
		lv_obj_t* line = lv_obj_create(sun);
		lv_obj_remove_style_all(line);
		lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_flag(line, LV_OBJ_FLAG_IGNORE_LAYOUT);
		// Scan line spans the full sun width; the round parent bounds
		// auto-clip the ends to a chord of the circle, which is exactly
		// the look we want.
		lv_obj_set_size(line, diameter, scanH[i]);
		lv_obj_set_pos(line, 0, scanScreenY[i] - cy);
		lv_obj_set_style_bg_color(line, MP_MAGENTA, 0);
		lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
		lv_obj_set_style_radius(line, 0, 0);
		lv_obj_set_style_border_width(line, 0, 0);
		scanLines[i] = line;
	}

	// Bright 1 px horizon line. Painted as a child of the sky (rather than
	// of the parent obj) so it is unconditionally drawn on top of the sun
	// even if the sun's circle math drifts by a pixel.
	lv_obj_t* horizon = lv_obj_create(sky);
	lv_obj_remove_style_all(horizon);
	lv_obj_clear_flag(horizon, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(horizon, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(horizon, BgWidth, 1);
	lv_obj_set_pos(horizon, 0, HorizonY - 1);
	lv_obj_set_style_bg_color(horizon, MP_HORIZON, 0);
	lv_obj_set_style_bg_opa(horizon, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(horizon, 0, 0);
	lv_obj_set_style_border_width(horizon, 0, 0);
}

void PhoneSynthwaveBg::buildGround(){
	const uint16_t groundH = BgHeight - HorizonY;

	ground = lv_obj_create(obj);
	lv_obj_remove_style_all(ground);
	lv_obj_clear_flag(ground, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(ground, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(ground, BgWidth, groundH);
	lv_obj_set_pos(ground, 0, HorizonY);

	// Vertical gradient: warm purple at the horizon -> near-black at the
	// bottom of the screen. Sells the depth of the perspective grid without
	// requiring any per-pixel work.
	lv_obj_set_style_bg_color(ground, MP_GROUND_TOP, 0);
	lv_obj_set_style_bg_grad_color(ground, MP_GROUND_BOT, 0);
	lv_obj_set_style_bg_grad_dir(ground, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_bg_opa(ground, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(ground, 0, 0);
	lv_obj_set_style_pad_all(ground, 0, 0);
	lv_obj_set_style_border_width(ground, 0, 0);
}

void PhoneSynthwaveBg::buildRays(){
	const uint16_t groundH = BgHeight - HorizonY;
	// Vanishing point in ground-local coords: top center.
	const int16_t vx = BgWidth / 2;
	const int16_t vy = 0;

	// Bottom-edge x positions for each ray. Symmetric around the screen
	// center; some endpoints are deliberately off-screen so the parent
	// clipping keeps the rays parallel-feeling at the edges.
	static const int16_t bottomX[RayCount] = { -50, -10, 30, 80, 130, 170, 210 };

	for(uint8_t i = 0; i < RayCount; i++){
		rayPoints[i][0].x = vx;
		rayPoints[i][0].y = vy;
		rayPoints[i][1].x = bottomX[i];
		rayPoints[i][1].y = groundH;

		lv_obj_t* line = lv_line_create(ground);
		lv_line_set_points(line, rayPoints[i], 2);
		lv_obj_set_style_line_color(line, MP_GRID, 0);
		lv_obj_set_style_line_width(line, 1, 0);
		lv_obj_set_style_line_opa(line, LV_OPA_70, 0);
		// The center ray is the only one that perfectly aligns with the
		// horizon's vanishing pixel; brighten it a touch.
		if(bottomX[i] == vx){
			lv_obj_set_style_line_opa(line, LV_OPA_COVER, 0);
		}
		rays[i] = line;
	}
}

void PhoneSynthwaveBg::buildHorizontals(){
	const uint16_t groundH = BgHeight - HorizonY;

	// Phase-3 follow-up to the static grid: every horizontal line now
	// scrolls continuously from the horizon (y=0) toward the viewer
	// (y=groundH-1) on its own ease-in animation. Stagger the start times
	// across the period so HLineCount lines are evenly spaced at any
	// instant - the field always looks "full" without ever showing two
	// lines on top of each other.
	for(uint8_t i = 0; i < HLineCount; i++){
		lv_obj_t* h = lv_obj_create(ground);
		lv_obj_remove_style_all(h);
		lv_obj_clear_flag(h, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_flag(h, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_set_size(h, BgWidth, 1);
		// Initial y; the animation immediately overwrites it.
		lv_obj_set_pos(h, 0, 0);
		lv_obj_set_style_bg_color(h, MP_GRID, 0);
		// Initial opacity matches the brightness-at-horizon that the
		// gridScrollExec will set in its first tick.
		lv_obj_set_style_bg_opa(h, LV_OPA_30, 0);
		lv_obj_set_style_radius(h, 0, 0);
		lv_obj_set_style_border_width(h, 0, 0);
		hGridLines[i] = h;

		// Per-line scroll animation. ease_in fakes perspective: the line
		// crawls slowly near the horizon (where small screen-space deltas
		// represent large world-space distance) and accelerates as it
		// approaches the viewer. Repeat infinite, so the line wraps back
		// to y=0 at the end of each period.
		lv_anim_t a;
		lv_anim_init(&a);
		lv_anim_set_var(&a, h);
		lv_anim_set_values(&a, 0, groundH - 1);
		lv_anim_set_time(&a, GridScrollPeriod);
		lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
		lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
		lv_anim_set_exec_cb(&a, gridScrollExec);
		// Stagger start times so the four lines look evenly spread.
		lv_anim_set_delay(&a, (GridScrollPeriod / HLineCount) * i);
		lv_anim_start(&a);
	}
}

void PhoneSynthwaveBg::buildStars(){
	// Twinkling stars in the upper half of the sky. Positions are hand-picked
	// to read well on a 160x72 sky band - clustered toward the top so they do
	// not compete with the sun's halo.
	//
	// Each star carries its own peak opacity, period and delay so the field
	// reads as organic noise. Periods are intentionally coprime-ish (in the
	// 0.9 - 2.1 s range) and delays are spread across the full cycle so no
	// two stars fade in together. The dim end is anchored at LV_OPA_10 for
	// every star, which keeps even the brightest stars (LV_OPA_COVER) from
	// flickering all the way to fully solid black - feels more like a real
	// night-sky shimmer than a square-wave blink.
	struct Star {
		int16_t  x;
		int16_t  y;
		uint8_t  size;
		lv_opa_t peak;     // brightest point of the twinkle cycle
		uint16_t period;   // ms for fade-in (full ping-pong cycle = 2*period)
		uint16_t delay;    // ms before this star starts animating
	};
	static const Star starList[StarCount] = {
			{  12,  6, 1, LV_OPA_COVER, 1100,    0 },
			{  34, 14, 1, LV_OPA_70,    1700,  400 },
			{  58,  4, 2, LV_OPA_COVER, 1300,  200 },
			{ 102, 10, 1, LV_OPA_50,    2100,  800 },
			{ 124,  3, 1, LV_OPA_COVER,  900, 1000 },
			{ 144, 18, 2, LV_OPA_70,    1500,  600 },
			{  82, 24, 1, LV_OPA_30,    1900,  300 },
	};

	for(uint8_t i = 0; i < StarCount; i++){
		lv_obj_t* s = lv_obj_create(sky);
		lv_obj_remove_style_all(s);
		lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_flag(s, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_set_size(s, starList[i].size, starList[i].size);
		lv_obj_set_pos(s, starList[i].x, starList[i].y);
		lv_obj_set_style_bg_color(s, MP_STAR, 0);
		// Initial bg_opa is set to peak so the star is visible during the
		// pre-animation delay; the anim immediately overwrites it once it
		// starts running.
		lv_obj_set_style_bg_opa(s, starList[i].peak, 0);
		lv_obj_set_style_radius(s, 0, 0);
		lv_obj_set_style_border_width(s, 0, 0);
		stars[i] = s;

		// Per-star ping-pong opacity animation (Phase-2.5 follow-up to the
		// initial PhoneSynthwaveBg drop). LVGL auto-removes the anim when
		// the star object is deleted, so the widget destructor needs no
		// extra teardown here.
		lv_anim_t a;
		lv_anim_init(&a);
		lv_anim_set_var(&a, s);
		lv_anim_set_values(&a, LV_OPA_10, starList[i].peak);
		lv_anim_set_time(&a, starList[i].period);
		lv_anim_set_playback_time(&a, starList[i].period);
		lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
		lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
		lv_anim_set_exec_cb(&a, twinkleExec);
		lv_anim_set_delay(&a, starList[i].delay);
		lv_anim_start(&a);
	}
}

void PhoneSynthwaveBg::twinkleExec(void* var, int32_t v){
	// Cast back to the lv_obj_t* given to lv_anim_set_var. v is the current
	// interpolated value in the [LV_OPA_10, peak] range, which we map
	// straight onto the star's bg_opa. Using a free static here keeps the
	// callback ABI-compatible with lv_anim_exec_xcb_t.
	lv_obj_set_style_bg_opa((lv_obj_t*) var, (lv_opa_t) v, 0);
}

void PhoneSynthwaveBg::sunPulseExec(void* var, int32_t v){
	// Halo opacity ping-pong. v is in the [LV_OPA_10, LV_OPA_50] range -
	// a soft band that reads as "breathing" rather than "blinking" against
	// the magenta sky behind. ABI-compatible with lv_anim_exec_xcb_t so it
	// can be passed straight to lv_anim_set_exec_cb.
	lv_obj_set_style_bg_opa((lv_obj_t*) var, (lv_opa_t) v, 0);
}

void PhoneSynthwaveBg::gridScrollExec(void* var, int32_t v){
	// v is in the [0, groundH-1] range. Drive both the line's y position
	// (so it slides toward the viewer) and its opacity (so it fades in
	// from the horizon). The opacity ramp keeps the freshly-spawned line
	// from popping into existence at full brightness right under the
	// horizon, which would otherwise read as a glitch.
	const int32_t  groundHm1 = (PhoneSynthwaveBg::BgHeight - PhoneSynthwaveBg::HorizonY) - 1; // 55
	lv_obj_t* line = (lv_obj_t*) var;
	lv_obj_set_y(line, (lv_coord_t) v);

	// Map y in [0, groundHm1] to opacity in [0x30, 0xFF]. Linear ramp; the
	// motion is already non-linear (ease_in path), which gives the field
	// the perspective compression near the horizon.
	int32_t opa = 0x30 + (v * (0xFF - 0x30)) / (groundHm1 > 0 ? groundHm1 : 1);
	if(opa < 0)    opa = 0;
	if(opa > 0xFF) opa = 0xFF;
	lv_obj_set_style_bg_opa(line, (lv_opa_t) opa, 0);
}

// ----- Nokia 3310 Monochrome wallpaper (S101) -------------------------
//
// The Nokia 3310's idle screen is a flat pea-green LCD panel with the
// operator name banded across the top and (sometimes) a tiny
// pixel-art motif. Our PhoneStatusBar already owns the top 10 px and
// PhoneSoftKeyBar the bottom 10 px, so the wallpaper's job here is to
// paint:
//
//   1. A solid LCD-green vertical gradient covering the full 160×128
//      area. The gradient is intentionally subtle - the LCD panel
//      itself is more or less flat - but a touch of saturation tilt
//      from top to bottom keeps it from reading as a flat green fill
//      and helps the foreground text feel anchored to a real surface.
//
//   2. A faint horizontal scanline pattern (8 dim olive 1 px lines
//      spaced ~16 px apart) to suggest the LCD's row structure
//      without overwhelming foreground content. Same trick the real
//      PhoneSynthwaveBg uses for its scan lines across the sun, just
//      stretched across the whole panel.
//
//   3. A small pixel-art antenna+wave motif anchored at the bottom-
//      right. The motif sits in the corner the soft-key bar covers
//      anyway, so it does not compete with foreground content during
//      normal use - but on screens that omit the soft-key bar (boot
//      splash, lock screen) it shows through, giving the LCD panel
//      the iconic "phone idle" silhouette. About a dozen 1-2 px
//      rectangles in N3310_PIXEL with descending opacity to fake the
//      receding "wave" arcs.
//
// Everything runs static - no animations - because the Nokia 3310's
// idle screen is famously calm. ~12 LVGL primitives total, well below
// the budget the Synthwave variant's animated stars + grid need.
void PhoneSynthwaveBg::buildNokia3310Wallpaper(){
	// ----- LCD panel: solid pea-green vertical gradient -----
	//
	// Painted on the same `sky` member pointer the Synthwave variant
	// uses, so that a future caller iterating wallpaper children can
	// rely on a single named root regardless of theme. The container
	// covers the entire 160×128 area - there is no horizon to split.
	sky = lv_obj_create(obj);
	lv_obj_remove_style_all(sky);
	lv_obj_clear_flag(sky, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(sky, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(sky, BgWidth, BgHeight);
	lv_obj_set_pos(sky, 0, 0);
	lv_obj_set_style_bg_color(sky, N3310_BG_LIGHT, 0);
	lv_obj_set_style_bg_grad_color(sky, N3310_BG_DEEP, 0);
	lv_obj_set_style_bg_grad_dir(sky, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_bg_opa(sky, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(sky, 0, 0);
	lv_obj_set_style_pad_all(sky, 0, 0);
	lv_obj_set_style_border_width(sky, 0, 0);

	// ----- LCD scanline pattern: 8 dim 1 px horizontal lines -----
	//
	// Spaced ~16 px apart starting at y=7. Drawn as N3310_PIXEL_DIM at
	// LV_OPA_30 so the scanlines are visible but never compete with
	// foreground text. Each line is a 1 px tall rect rather than an
	// lv_line widget because lv_line requires an external point array
	// that has to outlive the widget; rects are cheaper and don't
	// need the storage.
	const lv_coord_t scanlineSpacing = 16;
	const lv_coord_t scanlineY0      = 7;
	const uint8_t    scanlineCount   = 8;
	for(uint8_t i = 0; i < scanlineCount; i++){
		lv_obj_t* sl = lv_obj_create(sky);
		lv_obj_remove_style_all(sl);
		lv_obj_clear_flag(sl, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_flag(sl, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_set_size(sl, BgWidth, 1);
		lv_obj_set_pos(sl, 0, scanlineY0 + scanlineSpacing * i);
		lv_obj_set_style_bg_color(sl, N3310_PIXEL_DIM, 0);
		lv_obj_set_style_bg_opa(sl, LV_OPA_30, 0);
		lv_obj_set_style_radius(sl, 0, 0);
		lv_obj_set_style_border_width(sl, 0, 0);
	}

	// ----- Pixel-art antenna + waves motif (bottom-right corner) -----
	//
	// Anchored ~6 px clear of the right edge and ~6 px clear of the
	// bottom edge, in the patch the soft-key bar will cover during
	// normal use. The mast is a 1×10 dark olive bar; a 5×2 base sits
	// underneath it; six tiny "wave" rectangles flank the mast at
	// descending opacity for the receding-arc effect. The whole motif
	// fits in a 16×18 bounding box, so the soft-key bar's 10 px height
	// fully covers it on screens that draw one.
	const lv_coord_t motifAnchorX = BgWidth  - 22;   // 138
	const lv_coord_t motifAnchorY = BgHeight - 24;   // 104

	// Antenna mast (1×10 vertical bar)
	{
		lv_obj_t* mast = lv_obj_create(sky);
		lv_obj_remove_style_all(mast);
		lv_obj_clear_flag(mast, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_flag(mast, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_set_size(mast, 1, 10);
		lv_obj_set_pos(mast, motifAnchorX + 8, motifAnchorY + 6);
		lv_obj_set_style_bg_color(mast, N3310_PIXEL, 0);
		lv_obj_set_style_bg_opa(mast, LV_OPA_70, 0);
		lv_obj_set_style_radius(mast, 0, 0);
		lv_obj_set_style_border_width(mast, 0, 0);
	}
	// Antenna base (5×2 dark olive plate)
	{
		lv_obj_t* base = lv_obj_create(sky);
		lv_obj_remove_style_all(base);
		lv_obj_clear_flag(base, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_flag(base, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_set_size(base, 5, 2);
		lv_obj_set_pos(base, motifAnchorX + 6, motifAnchorY + 16);
		lv_obj_set_style_bg_color(base, N3310_PIXEL, 0);
		lv_obj_set_style_bg_opa(base, LV_OPA_70, 0);
		lv_obj_set_style_radius(base, 0, 0);
		lv_obj_set_style_border_width(base, 0, 0);
	}
	// Three pairs of "wave" rects flanking the mast at descending
	// opacity. Closest pair (1 px tall, ~LV_OPA_70) sits right next
	// to the mast; mid pair (3 px tall, ~LV_OPA_50) one column out;
	// outer pair (5 px tall, ~LV_OPA_30) two columns out. Reads as
	// concentric arcs receding away from the antenna.
	struct Wave {
		int8_t   dx;
		int8_t   dy;
		uint8_t  w;
		uint8_t  h;
		lv_opa_t opa;
	};
	const Wave waves[] = {
			// left side (closer -> farther)
			{ -2,  6, 1, 1, LV_OPA_70 },
			{ -4,  4, 1, 3, LV_OPA_50 },
			{ -6,  2, 1, 5, LV_OPA_30 },
			// right side (mirrored)
			{  2,  6, 1, 1, LV_OPA_70 },
			{  4,  4, 1, 3, LV_OPA_50 },
			{  6,  2, 1, 5, LV_OPA_30 },
	};
	const uint8_t waveCount = sizeof(waves) / sizeof(waves[0]);
	for(uint8_t i = 0; i < waveCount; i++){
		lv_obj_t* w = lv_obj_create(sky);
		lv_obj_remove_style_all(w);
		lv_obj_clear_flag(w, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_flag(w, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_set_size(w, waves[i].w, waves[i].h);
		lv_obj_set_pos(w, motifAnchorX + 8 + waves[i].dx, motifAnchorY + waves[i].dy);
		lv_obj_set_style_bg_color(w, N3310_PIXEL, 0);
		lv_obj_set_style_bg_opa(w, waves[i].opa, 0);
		lv_obj_set_style_radius(w, 0, 0);
		lv_obj_set_style_border_width(w, 0, 0);
	}
}

// ----- Game Boy DMG (Dot Matrix Game) wallpaper (S103) ----------------
//
// The DMG-01's idle/menu screen is a flat 4-shade pea-mint LCD panel,
// usually with a small pixel-art glyph in one corner and a hairline
// dither pattern visible across the rest of the panel. Same constraint
// as the Nokia variant: PhoneStatusBar already owns the top 10 px and
// PhoneSoftKeyBar the bottom 10 px, so this wallpaper paints
//
//   1. A 4-shade LCD panel covering the full 160x128 area. Top half
//      uses GBDMG_LCD_LIGHT, bottom half tilts toward GBDMG_LCD_DEEP
//      via a vertical gradient. The tilt is intentionally subtle -
//      the DMG panel is more or less flat - but enough saturation
//      shift to keep it from reading as a flat fill.
//
//   2. A faint 2-shade dither pattern: every 4th row (y % 4 == 2) gets
//      a 1 px wide strip of the mid-tone GBDMG_LCD_MID at LV_OPA_30.
//      Same trick as the Nokia variant's scanlines, but tuned to the
//      DMG's denser dither grid. The dither suggests the LCD's
//      sub-pixel structure without overwhelming foreground content.
//
//   3. A small pixel-art "boy" silhouette anchored at the bottom-
//      right - the DMG-01's iconic startup-animation character
//      reduced to its idle resting pose. The glyph is built from
//      ~14 1-2 px rectangles in GBDMG_INK and GBDMG_INK_MID. The
//      whole motif fits in a 12x16 bounding box, fully covered by
//      the soft-key bar's 10 px height during normal use; on screens
//      that omit the soft-key bar (boot splash, lock screen) it
//      peeks through as the theme's signature glyph.
//
// Everything runs static - no animations - matching the Nokia variant's
// still-image philosophy. The DMG-01's idle screen is famously calm
// (the original boot animation aside), so a still preview is the
// authentic look. ~22 LVGL primitives total.
void PhoneSynthwaveBg::buildGameBoyDMGWallpaper(){
	// ----- LCD panel: solid pea-mint vertical gradient -----
	//
	// Painted on the same `sky` member pointer the Synthwave variant
	// uses, mirroring the Nokia builder so a future caller iterating
	// wallpaper children can rely on a single named root regardless
	// of theme.
	sky = lv_obj_create(obj);
	lv_obj_remove_style_all(sky);
	lv_obj_clear_flag(sky, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(sky, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(sky, BgWidth, BgHeight);
	lv_obj_set_pos(sky, 0, 0);
	lv_obj_set_style_bg_color(sky, GBDMG_LCD_LIGHT, 0);
	lv_obj_set_style_bg_grad_color(sky, GBDMG_LCD_DEEP, 0);
	lv_obj_set_style_bg_grad_dir(sky, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_bg_opa(sky, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(sky, 0, 0);
	lv_obj_set_style_pad_all(sky, 0, 0);
	lv_obj_set_style_border_width(sky, 0, 0);

	// ----- LCD dither pattern: faint horizontal strips -----
	//
	// One 1 px tall strip every 4 rows, in GBDMG_LCD_MID at LV_OPA_30.
	// Reads as the LCD's sub-pixel structure without competing with
	// foreground text. The strips are skipped over the rows where the
	// status bar / soft-key bar paint to avoid double-painting that
	// area at boot.
	const lv_coord_t ditherSpacing = 4;
	const lv_coord_t ditherY0      = 12;          // clear of status bar
	const lv_coord_t ditherYEnd    = BgHeight - 12; // clear of soft-key bar
	for(lv_coord_t y = ditherY0; y < ditherYEnd; y += ditherSpacing){
		lv_obj_t* strip = lv_obj_create(sky);
		lv_obj_remove_style_all(strip);
		lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_flag(strip, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_set_size(strip, BgWidth, 1);
		lv_obj_set_pos(strip, 0, y);
		lv_obj_set_style_bg_color(strip, GBDMG_LCD_MID, 0);
		lv_obj_set_style_bg_opa(strip, LV_OPA_30, 0);
		lv_obj_set_style_radius(strip, 0, 0);
		lv_obj_set_style_border_width(strip, 0, 0);
	}

	// ----- Pixel-art "boy" silhouette (bottom-right corner) -----
	//
	// The DMG-01's startup animation features a small marching figure
	// that drops in from the top of the screen. Reduced to a 9x12
	// idle silhouette here: a 5 px round head, a 7 px wide torso, and
	// two 2 px legs - all built out of GBDMG_INK rectangles with a
	// GBDMG_INK_MID shadow column for shading. The whole motif fits
	// in a 12x16 box anchored to (138, 104) so the soft-key bar's
	// 10 px height fully covers it during normal use.
	const lv_coord_t boyX = BgWidth  - 22;   // 138
	const lv_coord_t boyY = BgHeight - 28;   // 100

	struct PixelRect {
		int8_t   dx;
		int8_t   dy;
		uint8_t  w;
		uint8_t  h;
		bool     mid;       // true -> GBDMG_INK_MID, false -> GBDMG_INK
		lv_opa_t opa;
	};

	// 14 rectangles - rough approximation of the DMG mascot's idle
	// pose: head (top), arms-by-sides torso, two stubby legs.
	// dx/dy are local to (boyX, boyY); w/h are pixel sizes.
	const PixelRect boyParts[] = {
			// Head: 5x4 cap at top, with mid-tone shadow on the right side.
			{  3, 0, 5, 1, false, LV_OPA_COVER },
			{  2, 1, 7, 1, false, LV_OPA_COVER },
			{  2, 2, 7, 1, false, LV_OPA_COVER },
			{  3, 3, 5, 1, false, LV_OPA_COVER },
			// One mid-tone column on the right side of the head as shading.
			{  8, 1, 1, 3, true,  LV_OPA_70 },

			// Neck: 3 px wide, 1 px tall.
			{  4, 4, 3, 1, false, LV_OPA_COVER },

			// Torso: 5 wide, 4 tall, with a 1 px highlight notch on the
			// left for shape definition.
			{  3, 5, 5, 4, false, LV_OPA_COVER },
			{  3, 5, 1, 4, true,  LV_OPA_70 },

			// Arms: 1 px out from each torso side, 2 tall.
			{  2, 5, 1, 3, false, LV_OPA_COVER },
			{  8, 5, 1, 3, false, LV_OPA_COVER },

			// Legs: two 2 px wide bars under the torso, separated by 1 px.
			{  3, 9, 2, 2, false, LV_OPA_COVER },
			{  6, 9, 2, 2, false, LV_OPA_COVER },

			// Feet: tiny 2 px shoe at the base of each leg.
			{  2, 11, 3, 1, false, LV_OPA_COVER },
			{  6, 11, 3, 1, false, LV_OPA_COVER },
	};
	const uint8_t boyPartCount = sizeof(boyParts) / sizeof(boyParts[0]);
	for(uint8_t i = 0; i < boyPartCount; i++){
		lv_obj_t* px = lv_obj_create(sky);
		lv_obj_remove_style_all(px);
		lv_obj_clear_flag(px, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_flag(px, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_set_size(px, boyParts[i].w, boyParts[i].h);
		lv_obj_set_pos(px, boyX + boyParts[i].dx, boyY + boyParts[i].dy);
		lv_obj_set_style_bg_color(px,
		                          boyParts[i].mid ? GBDMG_INK_MID : GBDMG_INK, 0);
		lv_obj_set_style_bg_opa(px, boyParts[i].opa, 0);
		lv_obj_set_style_radius(px, 0, 0);
		lv_obj_set_style_border_width(px, 0, 0);
	}
}


// ----- Amber CRT phosphor wallpaper (S105) ---------------------------
//
// The 1980s amber-phosphor monochrome CRT (Apple ///, IBM 5151, Wyse
// 50, DEC VT320 amber, etc.) is the visual opposite of the Nokia /
// DMG LCDs: bright amber pixels burning on a near-black warm-brown
// panel, with the iconic horizontal-scanline structure visible across
// the entire panel (every CRT row is a discrete electron-beam pass).
// Same constraint as the Nokia + DMG variants: PhoneStatusBar already
// owns the top 10 px and PhoneSoftKeyBar the bottom 10 px, so this
// wallpaper paints
//
//   1. A near-black CRT panel covering the full 160x128 area. Top half
//      uses AMBER_CRT_BG_DARK, bottom tilts toward AMBER_CRT_BG_DEEP
//      via a vertical gradient. The tilt is intentionally subtle - a
//      real CRT panel reads almost flat at idle - but enough warm-
//      brown saturation shift to keep it from rendering as a
//      pure-black void.
//
//   2. A horizontal scanline pattern: every 2nd row (y % 2 == 1) gets
//      a 1 px wide AMBER_CRT_DIM strip at LV_OPA_30. Reads as the
//      authentic 'every CRT row is a separate electron-beam pass'
//      texture without overwhelming foreground content. The strips
//      are skipped over the rows where the status bar / soft-key bar
//      paint to avoid double-painting that area at boot.
//
//   3. A small pixel-art '>_' terminal-prompt motif anchored at the
//      bottom-right corner - the classic '1980s monochrome terminal
//      idle scene'. The motif is built from ~12 1-2 px rectangles in
//      AMBER_CRT_GLOW (the prompt chevron + underscore cursor) and
//      AMBER_CRT_HOT (the brightest pixel of the cursor, mid-blink).
//      The whole motif fits in a 14x10 bounding box, fully covered by
//      the soft-key bar's 10 px height during normal use; on screens
//      that omit the soft-key bar (boot splash, lock screen) it peeks
//      through as the theme's signature glyph.
//
// Everything runs static - no animations - matching the Nokia / DMG
// variants' still-image philosophy. Real CRT terminals had a slowly
// blinking cursor, but pulsing it would tie the wallpaper into the
// LVGL animation timeline, which the Nokia + DMG variants
// deliberately stay out of for power-budget reasons. ~70 LVGL
// primitives total (mostly the scanline rects).
void PhoneSynthwaveBg::buildAmberCRTWallpaper(){
	// ----- CRT panel: near-black warm-brown vertical gradient -----
	//
	// Painted on the same `sky` member pointer the Synthwave variant
	// uses, mirroring the Nokia + DMG builders so a future caller
	// iterating wallpaper children can rely on a single named root
	// regardless of theme. The container covers the entire 160x128
	// area - there is no horizon to split.
	sky = lv_obj_create(obj);
	lv_obj_remove_style_all(sky);
	lv_obj_clear_flag(sky, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(sky, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(sky, BgWidth, BgHeight);
	lv_obj_set_pos(sky, 0, 0);
	lv_obj_set_style_bg_color(sky, AMBER_CRT_BG_DARK, 0);
	lv_obj_set_style_bg_grad_color(sky, AMBER_CRT_BG_DEEP, 0);
	lv_obj_set_style_bg_grad_dir(sky, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_bg_opa(sky, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(sky, 0, 0);
	lv_obj_set_style_pad_all(sky, 0, 0);
	lv_obj_set_style_border_width(sky, 0, 0);

	// ----- CRT scanline pattern: dim amber 1 px strips every 2 rows -----
	//
	// One 1 px tall strip every 2 rows starting at y=12 and stopping
	// at y=BgHeight-12 (clear of the status bar / soft-key bar
	// rectangles). Drawn as AMBER_CRT_DIM at LV_OPA_30. This is the
	// signature visual cue of the theme - without it the wallpaper
	// would read as 'plain dark' rather than 'amber phosphor CRT'.
	//
	// Rect-per-strip rather than lv_line widget: lv_line requires an
	// external point array that has to outlive the widget; rects are
	// cheaper at this density (58 strips) and need no storage.
	const lv_coord_t scanlineSpacing = 2;
	const lv_coord_t scanlineY0      = 12;          // clear of status bar
	const lv_coord_t scanlineYEnd    = BgHeight - 12; // clear of soft-key bar
	for(lv_coord_t y = scanlineY0; y < scanlineYEnd; y += scanlineSpacing){
		lv_obj_t* sl = lv_obj_create(sky);
		lv_obj_remove_style_all(sl);
		lv_obj_clear_flag(sl, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_flag(sl, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_set_size(sl, BgWidth, 1);
		lv_obj_set_pos(sl, 0, y);
		lv_obj_set_style_bg_color(sl, AMBER_CRT_DIM, 0);
		lv_obj_set_style_bg_opa(sl, LV_OPA_30, 0);
		lv_obj_set_style_radius(sl, 0, 0);
		lv_obj_set_style_border_width(sl, 0, 0);
	}

	// ----- '>_' terminal prompt motif (bottom-right corner) -----
	//
	// Anchored ~6 px clear of the right edge and ~6 px clear of the
	// bottom edge, in the patch the soft-key bar will cover during
	// normal use. The chevron is a 5-pixel '>' shape; the cursor is
	// a 4x1 underscore burning slightly hotter than the chevron. The
	// whole motif fits in a 14x10 bounding box, so the soft-key bar's
	// 10 px height fully covers it on screens that draw one.
	const lv_coord_t promptX = BgWidth  - 22;   // 138
	const lv_coord_t promptY = BgHeight - 22;   // 106

	struct PixelRect {
		int8_t   dx;
		int8_t   dy;
		uint8_t  w;
		uint8_t  h;
		bool     hot;       // true -> AMBER_CRT_HOT, false -> AMBER_CRT_GLOW
		lv_opa_t opa;
	};

	// 12 rectangles - approximation of '>_' rendered in a 5x7 cell
	// (chevron) followed by a 1-cell gap and a 4x1 cursor underline.
	// dx/dy are local to (promptX, promptY); w/h are pixel sizes.
	// Chevron pixel layout (each # is one rect):
	//   #..
	//   .#.
	//   ..#
	//   .#.
	//   #..
	const PixelRect promptParts[] = {
			// Chevron '>' - 5 stacked diagonal pixels
			{ 0, 0, 1, 1, false, LV_OPA_COVER },
			{ 1, 1, 1, 1, false, LV_OPA_COVER },
			{ 2, 2, 1, 1, false, LV_OPA_COVER },
			{ 1, 3, 1, 1, false, LV_OPA_COVER },
			{ 0, 4, 1, 1, false, LV_OPA_COVER },
			// Faint mid-tone glow inside the chevron's 'mouth' so it
			// reads as a phosphor-rendered glyph rather than a flat
			// pixel cluster.
			{ 1, 2, 1, 1, false, LV_OPA_30 },
			// Cursor underscore '_' to the right of the chevron - 4 px
			// wide, brightest amber (the active prompt cursor on a
			// real terminal would be the hottest pixel on the panel).
			{ 6, 5, 1, 1, true,  LV_OPA_COVER },
			{ 7, 5, 1, 1, true,  LV_OPA_COVER },
			{ 8, 5, 1, 1, true,  LV_OPA_COVER },
			{ 9, 5, 1, 1, true,  LV_OPA_COVER },
			// Faint dim trail under the cursor, suggesting the
			// phosphor's slow decay after the beam has passed (every
			// CRT does this; it's part of the look).
			{ 6, 6, 4, 1, false, LV_OPA_30 },
			// Tiny baseline pixel under the chevron to anchor the
			// glyph to the same line as the cursor underscore.
			{ 0, 5, 1, 1, false, LV_OPA_30 },
	};
	const uint8_t promptPartCount = sizeof(promptParts) / sizeof(promptParts[0]);
	for(uint8_t i = 0; i < promptPartCount; i++){
		lv_obj_t* px = lv_obj_create(sky);
		lv_obj_remove_style_all(px);
		lv_obj_clear_flag(px, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_flag(px, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_set_size(px, promptParts[i].w, promptParts[i].h);
		lv_obj_set_pos(px, promptX + promptParts[i].dx, promptY + promptParts[i].dy);
		lv_obj_set_style_bg_color(px,
		                          promptParts[i].hot ? AMBER_CRT_HOT : AMBER_CRT_GLOW, 0);
		lv_obj_set_style_bg_opa(px, promptParts[i].opa, 0);
		lv_obj_set_style_radius(px, 0, 0);
		lv_obj_set_style_border_width(px, 0, 0);
	}
}


void PhoneSynthwaveBg::buildSonyEricssonAquaWallpaper(){
	// ----- Aqua panel: deep navy -> mid-ocean blue vertical gradient -----
	//
	// Painted on the same `sky` member pointer the Synthwave variant
	// uses, mirroring the Nokia / DMG / Amber CRT builders so a
	// future caller iterating wallpaper children can rely on a
	// single named root regardless of theme. The container covers
	// the entire 160x128 area - the Aqua menu screen, like the LCD
	// and CRT panels, is a single flat surface with no horizon.
	sky = lv_obj_create(obj);
	lv_obj_remove_style_all(sky);
	lv_obj_clear_flag(sky, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(sky, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(sky, BgWidth, BgHeight);
	lv_obj_set_pos(sky, 0, 0);
	lv_obj_set_style_bg_color(sky, AQUA_BG_DEEP, 0);
	lv_obj_set_style_bg_grad_color(sky, AQUA_BG_MID, 0);
	lv_obj_set_style_bg_grad_dir(sky, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_bg_opa(sky, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(sky, 0, 0);
	lv_obj_set_style_pad_all(sky, 0, 0);
	lv_obj_set_style_border_width(sky, 0, 0);

	// ----- Foam current streaks: 1 px tall horizontal ripples -----
	//
	// Six short low-opacity ripples scattered across the panel in
	// AQUA_FOAM. They suggest the iconic Sony Ericsson Aqua "ocean
	// current" cue without animating - the visible whitespace gap
	// between streaks reads as the spaces between waves. Each rect
	// is parented to `sky` so when the wallpaper is destroyed LVGL
	// auto-removes them. y-positions are spaced so a streak never
	// sits behind the status bar (y < 12) or the soft-key bar
	// (y > BgHeight - 12).
	struct Ripple { lv_coord_t y; lv_coord_t x; lv_coord_t w; lv_opa_t opa; };
	const Ripple ripples[] = {
			{ 22,  16, 36, LV_OPA_20 },
			{ 38,  88, 50, LV_OPA_20 },
			{ 56,  10, 70, LV_OPA_30 },
			{ 74,  68, 60, LV_OPA_20 },
			{ 92,  20, 50, LV_OPA_20 },
			{ 106, 96, 44, LV_OPA_20 },
	};
	const uint8_t rippleCount = sizeof(ripples) / sizeof(ripples[0]);
	for(uint8_t i = 0; i < rippleCount; i++){
		lv_obj_t* rp = lv_obj_create(sky);
		lv_obj_remove_style_all(rp);
		lv_obj_clear_flag(rp, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_flag(rp, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_set_size(rp, ripples[i].w, 1);
		lv_obj_set_pos(rp, ripples[i].x, ripples[i].y);
		lv_obj_set_style_bg_color(rp, AQUA_FOAM, 0);
		lv_obj_set_style_bg_opa(rp, ripples[i].opa, 0);
		lv_obj_set_style_radius(rp, 0, 0);
		lv_obj_set_style_border_width(rp, 0, 0);
	}

	// ----- Bubble specks: tiny 1-2 px dots scattered for upward cue -----
	//
	// Eight foam-coloured dots; the larger ones (2 px) tend to sit
	// closer to the top of the panel, the smaller ones near the
	// bottom, a subtle 'rising bubble depth' cue. Drawn as flat
	// rects (no LV_RADIUS_CIRCLE - at this size LVGL rounds to a
	// pixel rect anyway).
	struct Bubble { lv_coord_t x; lv_coord_t y; uint8_t s; lv_opa_t opa; };
	const Bubble bubbles[] = {
			{  20,  44, 1, LV_OPA_60 },
			{  64,  28, 2, LV_OPA_60 },
			{ 110,  62, 1, LV_OPA_60 },
			{ 138,  44, 2, LV_OPA_60 },
			{  44,  84, 1, LV_OPA_50 },
			{  88,  90, 2, LV_OPA_60 },
			{ 122,  98, 1, LV_OPA_50 },
			{  16, 100, 1, LV_OPA_50 },
	};
	const uint8_t bubbleCount = sizeof(bubbles) / sizeof(bubbles[0]);
	for(uint8_t i = 0; i < bubbleCount; i++){
		lv_obj_t* bb = lv_obj_create(sky);
		lv_obj_remove_style_all(bb);
		lv_obj_clear_flag(bb, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_flag(bb, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_set_size(bb, bubbles[i].s, bubbles[i].s);
		lv_obj_set_pos(bb, bubbles[i].x, bubbles[i].y);
		lv_obj_set_style_bg_color(bb, AQUA_FOAM, 0);
		lv_obj_set_style_bg_opa(bb, bubbles[i].opa, 0);
		lv_obj_set_style_radius(bb, 0, 0);
		lv_obj_set_style_border_width(bb, 0, 0);
	}

	// ----- Water-droplet motif (bottom-right corner) -----
	//
	// Anchored ~14 px clear of the right edge and ~22 px clear of the
	// bottom edge, in the patch the soft-key bar will cover during
	// normal use. The droplet is a 7x9 pixel-art shape with a
	// pointed top tapering into a rounded base - the canonical
	// "water drop" silhouette every late-2000s Aqua-skinned phone
	// shipped on its idle / lock screen. A 2-pixel chrome shine
	// runs down the upper-right of the body, suggesting reflected
	// light - without it the glyph reads as a flat blob rather than
	// a 3D droplet. The whole motif fits in a 7x10 bounding box; the
	// soft-key bar's 10 px height fully covers it on screens that
	// draw one, on screens that omit the soft-key bar (boot splash,
	// lock screen) it peeks through as the theme's signature glyph.
	const lv_coord_t dropX = BgWidth  - 14;   // 146
	const lv_coord_t dropY = BgHeight - 22;   // 106

	struct PixelRect {
		int8_t   dx;
		int8_t   dy;
		uint8_t  w;
		uint8_t  h;
		bool     shine;     // true -> AQUA_CHROME, false -> AQUA_GLOW
		lv_opa_t opa;
	};

	// Pixel layout (7 wide x 9 tall):
	//    ...#...    row 0  - point top
	//    ..###..    row 1
	//    ..###..    row 2
	//    .#####.    row 3
	//    .#####.    row 4
	//    #######    row 5  - widest
	//    #######    row 6
	//    .#####.    row 7  - bottom rounding
	//    ..###..    row 8
	const PixelRect dropletParts[] = {
			{ 3, 0, 1, 1, false, LV_OPA_COVER },          // tip
			{ 2, 1, 3, 1, false, LV_OPA_COVER },
			{ 2, 2, 3, 1, false, LV_OPA_COVER },
			{ 1, 3, 5, 1, false, LV_OPA_COVER },
			{ 1, 4, 5, 1, false, LV_OPA_COVER },
			{ 0, 5, 7, 1, false, LV_OPA_COVER },
			{ 0, 6, 7, 1, false, LV_OPA_COVER },
			{ 1, 7, 5, 1, false, LV_OPA_COVER },
			{ 2, 8, 3, 1, false, LV_OPA_COVER },
			// Chrome shine pixels on the upper-right of the body
			// (suggesting a light source from above-right, the
			// standard 2007 Sony Ericsson skin convention).
			{ 4, 3, 1, 1, true,  LV_OPA_COVER },
			{ 4, 4, 1, 1, true,  LV_OPA_COVER },
			// One faint phosphor-style afterglow underneath the
			// bottom rounding so the droplet reads as 'sitting on
			// the water surface' rather than 'floating in space'.
			{ 1, 9, 5, 1, false, LV_OPA_30 },
	};
	const uint8_t dropletCount = sizeof(dropletParts) / sizeof(dropletParts[0]);
	for(uint8_t i = 0; i < dropletCount; i++){
		lv_obj_t* px = lv_obj_create(sky);
		lv_obj_remove_style_all(px);
		lv_obj_clear_flag(px, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_flag(px, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_set_size(px, dropletParts[i].w, dropletParts[i].h);
		lv_obj_set_pos(px, dropX + dropletParts[i].dx, dropY + dropletParts[i].dy);
		lv_obj_set_style_bg_color(px,
		                          dropletParts[i].shine ? AQUA_CHROME : AQUA_GLOW, 0);
		lv_obj_set_style_bg_opa(px, dropletParts[i].opa, 0);
		lv_obj_set_style_radius(px, 0, 0);
		lv_obj_set_style_border_width(px, 0, 0);
	}
}

void PhoneSynthwaveBg::buildRazrHotPinkWallpaper(){
	// ----- RAZR panel: night-magenta -> dark magenta vertical gradient -----
	//
	// Painted on the same `sky` member pointer the Synthwave variant
	// uses, mirroring the Nokia / DMG / Amber CRT / Sony Ericsson Aqua
	// builders so a future caller iterating wallpaper children can
	// rely on a single named root regardless of theme. The container
	// covers the entire 160x128 area - the RAZR menu screen, like the
	// LCD / CRT / Aqua panels, is a single flat surface with no
	// horizon. The vertical gradient (RAZR_BG_DARK at top -> RAZR_BG_DEEP
	// at bottom) reads as a hot-pink anodised back panel that gets
	// slightly warmer toward the hinge - the cue every V3i Pink owner
	// remembers from holding the closed flip in hand.
	sky = lv_obj_create(obj);
	lv_obj_remove_style_all(sky);
	lv_obj_clear_flag(sky, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(sky, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(sky, BgWidth, BgHeight);
	lv_obj_set_pos(sky, 0, 0);
	lv_obj_set_style_bg_color(sky, RAZR_BG_DARK, 0);
	lv_obj_set_style_bg_grad_color(sky, RAZR_BG_DEEP, 0);
	lv_obj_set_style_bg_grad_dir(sky, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_bg_opa(sky, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(sky, 0, 0);
	lv_obj_set_style_pad_all(sky, 0, 0);
	lv_obj_set_style_border_width(sky, 0, 0);

	// ----- Anodised-aluminium striation lines -----
	//
	// Five faint full-width horizontal lines in RAZR_SHINE at low
	// opacity. Approximate the brushed-aluminium texture every RAZR
	// owner ran a fingertip across - the back panel had a barely-
	// visible horizontal grain that caught light at certain angles.
	// Spaced 18-22 px apart so the eye reads them as a regular
	// surface texture rather than a counted set. Drawn 1 px tall to
	// stay subliminal at the 160x128 resolution; LVGL collapses
	// 1 px rects to a single horizontal line on flush.
	struct Striation { lv_coord_t y; lv_opa_t opa; };
	const Striation striations[] = {
			{  16, LV_OPA_10 },
			{  38, LV_OPA_20 },
			{  60, LV_OPA_10 },
			{  82, LV_OPA_20 },
			{ 104, LV_OPA_10 },
	};
	const uint8_t striationCount = sizeof(striations) / sizeof(striations[0]);
	for(uint8_t i = 0; i < striationCount; i++){
		lv_obj_t* st = lv_obj_create(sky);
		lv_obj_remove_style_all(st);
		lv_obj_clear_flag(st, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_flag(st, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_set_size(st, BgWidth, 1);
		lv_obj_set_pos(st, 0, striations[i].y);
		lv_obj_set_style_bg_color(st, RAZR_SHINE, 0);
		lv_obj_set_style_bg_opa(st, striations[i].opa, 0);
		lv_obj_set_style_radius(st, 0, 0);
		lv_obj_set_style_border_width(st, 0, 0);
	}

	// ----- LED-backlight specks: tiny 1-2 px chrome dots -----
	//
	// Eight RAZR_CHROME / RAZR_SHINE dots scattered across the panel,
	// suggesting the keypad's blue-white EL backlight bleeding through
	// the chemically-etched aluminium - the iconic V3 / V3i visual cue.
	// y-positions are spaced so a speck never sits behind the status
	// bar (y < 12) or the soft-key bar (y > BgHeight - 12). Drawn as
	// flat rects (no LV_RADIUS_CIRCLE - at this size LVGL rounds to a
	// pixel rect anyway).
	struct Speck { lv_coord_t x; lv_coord_t y; uint8_t s; bool chrome; lv_opa_t opa; };
	const Speck specks[] = {
			{  18,  26, 1, true,  LV_OPA_70 },
			{  72,  20, 2, true,  LV_OPA_60 },
			{ 124,  32, 1, false, LV_OPA_70 },
			{  44,  54, 2, false, LV_OPA_60 },
			{  98,  66, 1, true,  LV_OPA_70 },
			{  28,  92, 1, false, LV_OPA_60 },
			{  82, 102, 2, true,  LV_OPA_60 },
			{ 134,  88, 1, false, LV_OPA_70 },
	};
	const uint8_t speckCount = sizeof(specks) / sizeof(specks[0]);
	for(uint8_t i = 0; i < speckCount; i++){
		lv_obj_t* sp = lv_obj_create(sky);
		lv_obj_remove_style_all(sp);
		lv_obj_clear_flag(sp, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_flag(sp, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_set_size(sp, specks[i].s, specks[i].s);
		lv_obj_set_pos(sp, specks[i].x, specks[i].y);
		lv_obj_set_style_bg_color(sp,
		                          specks[i].chrome ? RAZR_CHROME : RAZR_SHINE, 0);
		lv_obj_set_style_bg_opa(sp, specks[i].opa, 0);
		lv_obj_set_style_radius(sp, 0, 0);
		lv_obj_set_style_border_width(sp, 0, 0);
	}

	// ----- Z-shaped lightning-bolt motif (bottom-right corner) -----
	//
	// Anchored ~14 px clear of the right edge and ~22 px clear of the
	// bottom edge, in the patch the soft-key bar will cover during
	// normal use. The bolt is a 6 wide x 9 tall pixel-art Z silhouette
	// in RAZR_CHROME (warm silver) - the universal "razor sharpness"
	// / "lightning speed" brand cue you saw on every mid-2000s RAZR
	// teaser ad and Motorola Originals interstitial. Crucially the
	// glyph is NOT the trademarked RAZR wordmark; it's the
	// hold-it-up-to-the-light "Z-bolt" silhouette that any 2005
	// flip-phone owner reads as 'this thing is fast'.
	//
	// A single RAZR_GLOW pixel sits one row below the bolt as a
	// 'spark' / hot-pink afterglow, plus a faint LV_OPA_30 RAZR_SHINE
	// halo along the upper edge of the top horizontal so the bolt
	// reads as 'lit by the back panel' rather than 'painted on'.
	const lv_coord_t boltX = BgWidth  - 14;   // 146
	const lv_coord_t boltY = BgHeight - 22;   // 106

	struct PixelRect {
		int8_t   dx;
		int8_t   dy;
		uint8_t  w;
		uint8_t  h;
		uint8_t  layer;     // 0 = chrome, 1 = glow, 2 = shine
		lv_opa_t opa;
	};

	// Pixel layout (6 wide x 9 tall) - a Z-shaped lightning bolt:
	//    ######    row 0 - top horizontal bar
	//    ######    row 1
	//    ....##    row 2 - down-left diagonal start (far right)
	//    ...##.    row 3
	//    ..##..    row 4
	//    .##...    row 5
	//    ##....    row 6 - down-left diagonal end (far left)
	//    ######    row 7 - bottom horizontal bar
	//    ######    row 8
	const PixelRect boltParts[] = {
			// Top bar (rows 0-1)
			{ 0, 0, 6, 1, 0, LV_OPA_COVER },
			{ 0, 1, 6, 1, 0, LV_OPA_COVER },
			// Diagonal slash (rows 2-6)
			{ 4, 2, 2, 1, 0, LV_OPA_COVER },
			{ 3, 3, 2, 1, 0, LV_OPA_COVER },
			{ 2, 4, 2, 1, 0, LV_OPA_COVER },
			{ 1, 5, 2, 1, 0, LV_OPA_COVER },
			{ 0, 6, 2, 1, 0, LV_OPA_COVER },
			// Bottom bar (rows 7-8)
			{ 0, 7, 6, 1, 0, LV_OPA_COVER },
			{ 0, 8, 6, 1, 0, LV_OPA_COVER },

			// Hot-pink shine highlight along the upper edge of the
			// top bar (suggesting the back panel's anodised colour
			// bleeding into the chrome glyph from above).
			{ 0, -1, 6, 1, 2, LV_OPA_30 },

			// Hot-pink 'spark' afterglow row beneath the bottom bar
			// (the bolt's electrical aftertrail - sells the 'this
			// thing just fired' read).
			{ 1, 9, 4, 1, 1, LV_OPA_50 },
	};
	const uint8_t boltCount = sizeof(boltParts) / sizeof(boltParts[0]);
	for(uint8_t i = 0; i < boltCount; i++){
		lv_obj_t* px = lv_obj_create(sky);
		lv_obj_remove_style_all(px);
		lv_obj_clear_flag(px, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_flag(px, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_set_size(px, boltParts[i].w, boltParts[i].h);
		lv_obj_set_pos(px, boltX + boltParts[i].dx, boltY + boltParts[i].dy);
		lv_color_t fill;
		switch(boltParts[i].layer){
			case 1:  fill = RAZR_GLOW;   break;
			case 2:  fill = RAZR_SHINE;  break;
			case 0:
			default: fill = RAZR_CHROME; break;
		}
		lv_obj_set_style_bg_color(px, fill, 0);
		lv_obj_set_style_bg_opa(px, boltParts[i].opa, 0);
		lv_obj_set_style_radius(px, 0, 0);
		lv_obj_set_style_border_width(px, 0, 0);
	}
}
