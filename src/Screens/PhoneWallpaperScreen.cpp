#include "PhoneWallpaperScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <Settings.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - inlined per the established pattern in the
// rest of the Phone* screens (see PhoneBrightnessScreen.cpp,
// PhoneSettingsScreen.cpp). Cyan for caption + pager (informational),
// warm cream for the style name, sunset orange + dim purple for the
// swatch ornaments, deep purple / magenta for the swatch sky/ground.
#define MP_BG_DARK      lv_color_make( 20,  12,  36)
#define MP_PURPLE_MID   lv_color_make( 70,  40, 110)
#define MP_MAGENTA      lv_color_make(180,  40, 140)
#define MP_ACCENT       lv_color_make(255, 140,  30)
#define MP_SUN          lv_color_make(255, 170,  50)
#define MP_GROUND_TOP   lv_color_make( 80,  20,  90)
#define MP_GROUND_BOT   lv_color_make( 10,   5,  25)
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)
#define MP_TEXT         lv_color_make(255, 220, 180)
#define MP_DIM          lv_color_make( 70,  56, 100)
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)
#define MP_STAR         lv_color_make(255, 240, 220)

// Swatch row: y of the swatch frame and the y of its inner sky band.
// The swatch is 80x44 split 22/22 between sky (top) and ground (bottom)
// so each variant has a clear horizon line at y = 22.
static constexpr lv_coord_t kSwatchSkyH    = 22;
static constexpr lv_coord_t kSwatchGroundH = PhoneWallpaperScreen::SwatchH - kSwatchSkyH;

// Pager row: name + index labels, both centred horizontally.
static constexpr lv_coord_t kPagerNameY    = 76;
static constexpr lv_coord_t kPagerIdxY     = 90;
static constexpr lv_coord_t kPagerChevronY = 80;
static constexpr lv_coord_t kPagerChevronX = 24;   // distance from horizontal centre to chevrons

PhoneWallpaperScreen::PhoneWallpaperScreen()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  swatchFrame(nullptr),
		  swatchInner(nullptr),
		  nameLabel(nullptr),
		  pagerLabel(nullptr),
		  leftChevron(nullptr),
		  rightChevron(nullptr) {

	// Snapshot the persisted style on entry. BACK pops without any
	// destructive change, so we don't strictly need this, but it lets
	// hosts / tests reason about "did the user actually change it?".
	initialStyle = Settings.get().wallpaperStyle;

	// Start with the persisted style focused, falling back to Synthwave
	// for any out-of-range NVS byte. Same defensive pattern as
	// PhoneSoundScreen / PhoneBrightnessScreen.
	uint8_t snapped = initialStyle;
	if(snapped >= StyleCount) snapped = 0;
	cursor = snapped;

	// Full-screen container, no scrollbars, no padding - same blank-canvas
	// pattern PhoneSettingsScreen / PhoneBrightnessScreen use.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// The picker's own backdrop stays full Synthwave - the live preview
	// lives in the swatch, not in the screen's wallpaper. We pass an
	// explicit Style here so a user mid-pick (Settings.wallpaperStyle
	// not yet saved) still sees the canonical Synthwave backdrop.
	wallpaper = new PhoneSynthwaveBg(obj, PhoneSynthwaveBg::Style::Synthwave);

	// Status bar at the top so the user always sees signal / clock /
	// battery while drilling through the picker.
	statusBar = new PhoneStatusBar(obj);

	buildCaption();
	buildSwatch();
	buildPager();

	// Bottom: SAVE on the left, BACK on the right - matches the
	// Sony-Ericsson convention and the rest of the Phase-J screens.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("SAVE");
	softKeys->setRight("BACK");

	// Initial paint: render the swatch for the persisted style and
	// the pager labels.
	rebuildSwatch();
	refreshPager();
}

PhoneWallpaperScreen::~PhoneWallpaperScreen() {
	// All children are parented to obj - LVGL frees them recursively
	// when the screen's obj is destroyed by the LVScreen base destructor.
}

void PhoneWallpaperScreen::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneWallpaperScreen::onStop() {
	Input::getInstance()->removeListener(this);
}

PhoneWallpaperScreen::Style PhoneWallpaperScreen::getCurrentStyle() const {
	return PhoneSynthwaveBg::styleFromByte(cursor);
}

const char* PhoneWallpaperScreen::nameForIndex(uint8_t idx) {
	switch(idx){
		case 0:  return "SYNTHWAVE";
		case 1:  return "PLAIN";
		case 2:  return "GRID ONLY";
		case 3:  return "STARS";
		default: return "WALLPAPER";
	}
}

// ----- builders --------------------------------------------------------

void PhoneWallpaperScreen::buildCaption() {
	// "WALLPAPER" caption in pixelbasic7 cyan, just under the status bar.
	// Same anchor pattern PhoneBrightnessScreen / PhoneSoundScreen use.
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "WALLPAPER");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, 12);
}

void PhoneWallpaperScreen::buildSwatch() {
	// Outer frame - thin dim-purple rounded rect that hosts the inner
	// swatch composition. Centred horizontally on the 160 px display,
	// anchored at SwatchY so it sits in a comfortable middle band
	// beneath the caption.
	swatchFrame = lv_obj_create(obj);
	lv_obj_remove_style_all(swatchFrame);
	lv_obj_set_size(swatchFrame, SwatchW + 2, SwatchH + 2);
	lv_obj_set_align(swatchFrame, LV_ALIGN_TOP_MID);
	lv_obj_set_y(swatchFrame, SwatchY - 1);
	lv_obj_set_scrollbar_mode(swatchFrame, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(swatchFrame, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_pad_all(swatchFrame, 0, 0);
	lv_obj_set_style_radius(swatchFrame, 2, 0);
	lv_obj_set_style_bg_opa(swatchFrame, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_color(swatchFrame, MP_LABEL_DIM, 0);
	lv_obj_set_style_border_opa(swatchFrame, LV_OPA_70, 0);
	lv_obj_set_style_border_width(swatchFrame, 1, 0);

	// Inner container - this is the one we tear down + rebuild on every
	// cursor step. Sized to SwatchW x SwatchH so the rebuilt children
	// don't have to know about the frame's 1 px border.
	swatchInner = lv_obj_create(swatchFrame);
	lv_obj_remove_style_all(swatchInner);
	lv_obj_set_size(swatchInner, SwatchW, SwatchH);
	lv_obj_set_pos(swatchInner, 1, 1);
	lv_obj_set_scrollbar_mode(swatchInner, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(swatchInner, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_pad_all(swatchInner, 0, 0);
	lv_obj_set_style_radius(swatchInner, 1, 0);
	lv_obj_set_style_bg_opa(swatchInner, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(swatchInner, 0, 0);
}

void PhoneWallpaperScreen::buildPager() {
	// Big pixelbasic7 style name in warm cream, centred under the swatch.
	nameLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(nameLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(nameLabel, MP_TEXT, 0);
	lv_label_set_text(nameLabel, nameForIndex(cursor));
	lv_obj_set_align(nameLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(nameLabel, kPagerNameY);

	// Index "1/4" readout in cyan, sat under the name.
	pagerLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(pagerLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(pagerLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(pagerLabel, "");
	lv_obj_set_align(pagerLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(pagerLabel, kPagerIdxY);

	// "<" / ">" chevrons flanking the name. Sunset orange so they
	// read as the press affordance for LEFT / RIGHT, distinct from
	// the dim chevrons used as static decoration in PhoneSettingsScreen.
	leftChevron = lv_label_create(obj);
	lv_obj_set_style_text_font(leftChevron, &pixelbasic7, 0);
	lv_obj_set_style_text_color(leftChevron, MP_ACCENT, 0);
	lv_label_set_text(leftChevron, "<");
	lv_obj_set_align(leftChevron, LV_ALIGN_TOP_MID);
	lv_obj_set_pos(leftChevron, -kPagerChevronX, kPagerChevronY);

	rightChevron = lv_label_create(obj);
	lv_obj_set_style_text_font(rightChevron, &pixelbasic7, 0);
	lv_obj_set_style_text_color(rightChevron, MP_ACCENT, 0);
	lv_label_set_text(rightChevron, ">");
	lv_obj_set_align(rightChevron, LV_ALIGN_TOP_MID);
	lv_obj_set_pos(rightChevron, kPagerChevronX, kPagerChevronY);
}

// ----- swatch rebuild --------------------------------------------------

void PhoneWallpaperScreen::rebuildSwatch() {
	if(swatchInner == nullptr) return;

	// Tear down any existing children. lv_obj_clean removes every child
	// recursively and frees their LVGL state - matches the pattern other
	// Phase-G/H screens use to redraw lists. Cheap for a swatch with
	// fewer than 10 children.
	lv_obj_clean(swatchInner);

	// Every variant paints its own sky/ground gradient inside the
	// inner container. We don't paint a default backdrop here so the
	// per-style builder fully owns the composition.
	switch(cursor){
		case 0:  drawSynthwaveSwatch(); break;
		case 1:  drawPlainSwatch();     break;
		case 2:  drawGridOnlySwatch();  break;
		case 3:  drawStarsSwatch();     break;
		default: drawPlainSwatch();     break;  // defensive
	}
}

void PhoneWallpaperScreen::refreshPager() {
	if(nameLabel) {
		lv_label_set_text(nameLabel, nameForIndex(cursor));
	}
	if(pagerLabel) {
		char buf[8];
		snprintf(buf, sizeof(buf), "%u/%u", static_cast<unsigned>(cursor) + 1u,
				 static_cast<unsigned>(StyleCount));
		lv_label_set_text(pagerLabel, buf);
	}
}

// ----- per-style swatch builders ---------------------------------------
//
// Each builder draws a flat (non-animated) preview of its style inside
// the SwatchW x SwatchH box. The horizon sits at y = kSwatchSkyH so all
// four previews share a consistent "this is wallpaper" framing. No
// SPIFFS - every element is a tiny LVGL primitive, fewer than ten per
// preview. Total LVGL object cost across the lifetime of this screen
// stays bounded.

void PhoneWallpaperScreen::drawSynthwaveSwatch() {
	// Sky gradient - same colour ramp PhoneSynthwaveBg::buildSky uses
	// (deep purple top -> magenta horizon).
	lv_obj_t* sky = lv_obj_create(swatchInner);
	lv_obj_remove_style_all(sky);
	lv_obj_set_size(sky, SwatchW, kSwatchSkyH);
	lv_obj_set_pos(sky, 0, 0);
	lv_obj_clear_flag(sky, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(sky, MP_BG_DARK, 0);
	lv_obj_set_style_bg_grad_color(sky, MP_MAGENTA, 0);
	lv_obj_set_style_bg_grad_dir(sky, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_bg_opa(sky, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(sky, 0, 0);
	lv_obj_set_style_border_width(sky, 0, 0);

	// Ground gradient - mid purple at horizon -> near-black at bottom.
	lv_obj_t* ground = lv_obj_create(swatchInner);
	lv_obj_remove_style_all(ground);
	lv_obj_set_size(ground, SwatchW, kSwatchGroundH);
	lv_obj_set_pos(ground, 0, kSwatchSkyH);
	lv_obj_clear_flag(ground, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(ground, MP_GROUND_TOP, 0);
	lv_obj_set_style_bg_grad_color(ground, MP_GROUND_BOT, 0);
	lv_obj_set_style_bg_grad_dir(ground, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_bg_opa(ground, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(ground, 0, 0);
	lv_obj_set_style_border_width(ground, 0, 0);

	// Tiny semi-circle sun resting on the horizon - 14 px diameter
	// rect with circular radius, vertically clipped by the sky's
	// bottom edge so only the upper half is visible (same trick the
	// real PhoneSynthwaveBg uses).
	const lv_coord_t sunDia = 14;
	lv_obj_t* sun = lv_obj_create(sky);
	lv_obj_remove_style_all(sun);
	lv_obj_set_size(sun, sunDia, sunDia);
	lv_obj_set_pos(sun, (SwatchW - sunDia) / 2, kSwatchSkyH - (sunDia / 2));
	lv_obj_set_style_radius(sun, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_color(sun, MP_SUN, 0);
	lv_obj_set_style_bg_grad_color(sun, MP_ACCENT, 0);
	lv_obj_set_style_bg_grad_dir(sun, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_bg_opa(sun, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(sun, 0, 0);

	// Two scan lines across the sun - the same dark purple bands the
	// real wallpaper draws. Sells the retro vibe in the swatch.
	for(uint8_t i = 0; i < 2; ++i) {
		lv_obj_t* scan = lv_obj_create(sky);
		lv_obj_remove_style_all(scan);
		lv_obj_set_size(scan, sunDia + 2, 1);
		lv_obj_set_pos(scan, (SwatchW - sunDia) / 2 - 1, kSwatchSkyH - 5 + i * 3);
		lv_obj_set_style_bg_color(scan, MP_BG_DARK, 0);
		lv_obj_set_style_bg_opa(scan, LV_OPA_70, 0);
		lv_obj_set_style_radius(scan, 0, 0);
		lv_obj_set_style_border_width(scan, 0, 0);
	}

	// Three horizontal grid lines on the ground - faint at the
	// horizon, brighter toward the bottom (cheap perspective fake).
	for(uint8_t i = 0; i < 3; ++i) {
		lv_obj_t* h = lv_obj_create(ground);
		lv_obj_remove_style_all(h);
		lv_obj_set_size(h, SwatchW, 1);
		lv_obj_set_pos(h, 0, 4 + i * 6);
		lv_obj_set_style_bg_color(h, MP_HIGHLIGHT, 0);
		lv_obj_set_style_bg_opa(h, 80 + i * 60, 0);
		lv_obj_set_style_radius(h, 0, 0);
		lv_obj_set_style_border_width(h, 0, 0);
	}

	// One central vertical ray converging on the vanishing point at
	// the horizon - cyan, 1 px, full opacity. Reads as "perspective".
	lv_obj_t* ray = lv_obj_create(ground);
	lv_obj_remove_style_all(ray);
	lv_obj_set_size(ray, 1, kSwatchGroundH);
	lv_obj_set_pos(ray, SwatchW / 2, 0);
	lv_obj_set_style_bg_color(ray, MP_HIGHLIGHT, 0);
	lv_obj_set_style_bg_opa(ray, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(ray, 0, 0);
	lv_obj_set_style_border_width(ray, 0, 0);

	// Two stars in the upper sky.
	for(uint8_t i = 0; i < 2; ++i) {
		lv_obj_t* star = lv_obj_create(sky);
		lv_obj_remove_style_all(star);
		lv_obj_set_size(star, 1, 1);
		lv_obj_set_pos(star, 8 + i * 56, 4 + i * 2);
		lv_obj_set_style_bg_color(star, MP_STAR, 0);
		lv_obj_set_style_bg_opa(star, LV_OPA_COVER, 0);
		lv_obj_set_style_radius(star, 0, 0);
		lv_obj_set_style_border_width(star, 0, 0);
	}
}

void PhoneWallpaperScreen::drawPlainSwatch() {
	// Sky gradient only - calmest variant.
	lv_obj_t* sky = lv_obj_create(swatchInner);
	lv_obj_remove_style_all(sky);
	lv_obj_set_size(sky, SwatchW, kSwatchSkyH);
	lv_obj_set_pos(sky, 0, 0);
	lv_obj_clear_flag(sky, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(sky, MP_BG_DARK, 0);
	lv_obj_set_style_bg_grad_color(sky, MP_MAGENTA, 0);
	lv_obj_set_style_bg_grad_dir(sky, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_bg_opa(sky, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(sky, 0, 0);
	lv_obj_set_style_border_width(sky, 0, 0);

	lv_obj_t* ground = lv_obj_create(swatchInner);
	lv_obj_remove_style_all(ground);
	lv_obj_set_size(ground, SwatchW, kSwatchGroundH);
	lv_obj_set_pos(ground, 0, kSwatchSkyH);
	lv_obj_clear_flag(ground, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(ground, MP_GROUND_TOP, 0);
	lv_obj_set_style_bg_grad_color(ground, MP_GROUND_BOT, 0);
	lv_obj_set_style_bg_grad_dir(ground, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_bg_opa(ground, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(ground, 0, 0);
	lv_obj_set_style_border_width(ground, 0, 0);
}

void PhoneWallpaperScreen::drawGridOnlySwatch() {
	// Same gradient sky/ground as Synthwave / Plain - the shared
	// canvas every variant builds on.
	lv_obj_t* sky = lv_obj_create(swatchInner);
	lv_obj_remove_style_all(sky);
	lv_obj_set_size(sky, SwatchW, kSwatchSkyH);
	lv_obj_set_pos(sky, 0, 0);
	lv_obj_clear_flag(sky, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(sky, MP_BG_DARK, 0);
	lv_obj_set_style_bg_grad_color(sky, MP_MAGENTA, 0);
	lv_obj_set_style_bg_grad_dir(sky, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_bg_opa(sky, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(sky, 0, 0);
	lv_obj_set_style_border_width(sky, 0, 0);

	lv_obj_t* ground = lv_obj_create(swatchInner);
	lv_obj_remove_style_all(ground);
	lv_obj_set_size(ground, SwatchW, kSwatchGroundH);
	lv_obj_set_pos(ground, 0, kSwatchSkyH);
	lv_obj_clear_flag(ground, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(ground, MP_GROUND_TOP, 0);
	lv_obj_set_style_bg_grad_color(ground, MP_GROUND_BOT, 0);
	lv_obj_set_style_bg_grad_dir(ground, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_bg_opa(ground, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(ground, 0, 0);
	lv_obj_set_style_border_width(ground, 0, 0);

	// Three horizontal grid lines (perspective fake, no sun) plus
	// three perspective rays fanning from the centre of the horizon.
	// Same colour vocabulary the real wallpaper uses (cyan + alpha
	// ramp) so the swatch reads as "this is the grid look".
	for(uint8_t i = 0; i < 3; ++i) {
		lv_obj_t* h = lv_obj_create(ground);
		lv_obj_remove_style_all(h);
		lv_obj_set_size(h, SwatchW, 1);
		lv_obj_set_pos(h, 0, 3 + i * 6);
		lv_obj_set_style_bg_color(h, MP_HIGHLIGHT, 0);
		lv_obj_set_style_bg_opa(h, 70 + i * 60, 0);
		lv_obj_set_style_radius(h, 0, 0);
		lv_obj_set_style_border_width(h, 0, 0);
	}

	// Three rays - left/centre/right - drawn as 1 px tall bars whose
	// width grows with distance from the vanishing point. Approximates
	// real perspective lines without using lv_line.
	const int16_t cx = SwatchW / 2;
	for(int8_t r = -1; r <= 1; ++r) {
		lv_obj_t* ray = lv_obj_create(ground);
		lv_obj_remove_style_all(ray);
		lv_obj_set_size(ray, 1, kSwatchGroundH);
		const int16_t bottomX = cx + r * (SwatchW / 3);
		// Anchor the ray at its bottom-x; LVGL doesn't support
		// rotated lv_obj, so we approximate with a 1 px column at
		// the bottom and rely on the eye to fuse it with the rays
		// from the horizontal grid lines.
		lv_obj_set_pos(ray, bottomX, 0);
		lv_obj_set_style_bg_color(ray, MP_HIGHLIGHT, 0);
		lv_obj_set_style_bg_opa(ray, LV_OPA_70, 0);
		lv_obj_set_style_radius(ray, 0, 0);
		lv_obj_set_style_border_width(ray, 0, 0);
	}
}

void PhoneWallpaperScreen::drawStarsSwatch() {
	// Gradient sky + ground.
	lv_obj_t* sky = lv_obj_create(swatchInner);
	lv_obj_remove_style_all(sky);
	lv_obj_set_size(sky, SwatchW, kSwatchSkyH);
	lv_obj_set_pos(sky, 0, 0);
	lv_obj_clear_flag(sky, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(sky, MP_BG_DARK, 0);
	lv_obj_set_style_bg_grad_color(sky, MP_MAGENTA, 0);
	lv_obj_set_style_bg_grad_dir(sky, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_bg_opa(sky, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(sky, 0, 0);
	lv_obj_set_style_border_width(sky, 0, 0);

	lv_obj_t* ground = lv_obj_create(swatchInner);
	lv_obj_remove_style_all(ground);
	lv_obj_set_size(ground, SwatchW, kSwatchGroundH);
	lv_obj_set_pos(ground, 0, kSwatchSkyH);
	lv_obj_clear_flag(ground, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(ground, MP_GROUND_TOP, 0);
	lv_obj_set_style_bg_grad_color(ground, MP_GROUND_BOT, 0);
	lv_obj_set_style_bg_grad_dir(ground, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_bg_opa(ground, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(ground, 0, 0);
	lv_obj_set_style_border_width(ground, 0, 0);

	// Six hand-placed stars across the sky. Mix of 1 and 2 px squares
	// at varying opacities to fake a twinkle field even though this
	// preview is intentionally still.
	struct Star {
		int8_t  x;
		int8_t  y;
		uint8_t size;
		lv_opa_t opa;
	};
	const Star stars[] = {
			{  6,  4, 1, LV_OPA_COVER },
			{ 18,  9, 1, LV_OPA_70    },
			{ 30,  3, 2, LV_OPA_COVER },
			{ 50,  6, 1, LV_OPA_50    },
			{ 64, 12, 2, LV_OPA_70    },
			{ 74,  4, 1, LV_OPA_COVER },
	};
	for(uint8_t i = 0; i < (sizeof(stars) / sizeof(stars[0])); ++i) {
		lv_obj_t* s = lv_obj_create(sky);
		lv_obj_remove_style_all(s);
		lv_obj_set_size(s, stars[i].size, stars[i].size);
		lv_obj_set_pos(s, stars[i].x, stars[i].y);
		lv_obj_set_style_bg_color(s, MP_STAR, 0);
		lv_obj_set_style_bg_opa(s, stars[i].opa, 0);
		lv_obj_set_style_radius(s, 0, 0);
		lv_obj_set_style_border_width(s, 0, 0);
	}
}

// ----- input + commit --------------------------------------------------

void PhoneWallpaperScreen::stepBy(int8_t delta) {
	// Wrap around - 4 styles is small enough that cycling reads better
	// than clamping. Same UX choice PhoneMainMenu's icon cursor uses.
	int16_t next = static_cast<int16_t>(cursor) + delta;
	while(next < 0)                                  next += StyleCount;
	while(next >= static_cast<int16_t>(StyleCount))  next -= StyleCount;
	if(static_cast<uint8_t>(next) == cursor) return;
	cursor = static_cast<uint8_t>(next);
	rebuildSwatch();
	refreshPager();
}

void PhoneWallpaperScreen::saveAndExit() {
	// Persist the chosen style. The very next screen that drops a
	// `new PhoneSynthwaveBg(obj)` will pick up the new look (the
	// default ctor reads Settings on every build).
	Settings.get().wallpaperStyle = cursor;
	Settings.store();
	if(softKeys) softKeys->flashLeft();
	pop();
}

void PhoneWallpaperScreen::cancelAndExit() {
	// Nothing destructive to revert - the wallpaper isn't applied
	// until SAVE. Just flash the BACK softkey for tactile feedback
	// and pop().
	if(softKeys) softKeys->flashRight();
	pop();
}

void PhoneWallpaperScreen::buttonPressed(uint i) {
	switch(i) {
		case BTN_LEFT:
		case BTN_4:
		case BTN_2:
			stepBy(-1);
			break;

		case BTN_RIGHT:
		case BTN_6:
		case BTN_8:
			stepBy(+1);
			break;

		case BTN_ENTER:
			saveAndExit();
			break;

		case BTN_BACK:
			cancelAndExit();
			break;

		default:
			break;
	}
}
