#include "PhoneSynthwaveBg.h"
#include <stdio.h>
#include <Settings.h>

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

PhoneSynthwaveBg::PhoneSynthwaveBg(lv_obj_t* parent)
		: PhoneSynthwaveBg(parent, styleFromByte(Settings.get().wallpaperStyle)) {
	// Delegating ctor - reads the persisted style on every drop so every
	// new screen picks up the user's current preference without each
	// caller having to know the wiring.
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

	// Sky + ground are the foundation of every variant - they always run
	// so the screen never falls back to the default LVGL theme color.
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
