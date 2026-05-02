#include "PhoneThemeScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <Settings.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../MakerphoneTheme.h"

// MAKERphone retro palette - inlined per the established pattern in
// the rest of the Phone* screens (see PhoneWallpaperScreen.cpp,
// PhoneSettingsScreen.cpp). Cyan for caption + pager, warm cream for
// the theme name, sunset orange for chevrons, magenta + dim purple
// for the Synthwave swatch ornaments. Nokia palette pulled in via
// MakerphoneTheme.h.
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

// Pager row geometry - matches PhoneWallpaperScreen so the two
// pickers feel like part of the same family.
static constexpr lv_coord_t kPagerNameY    = 76;
static constexpr lv_coord_t kPagerIdxY     = 90;
static constexpr lv_coord_t kPagerChevronY = 80;
static constexpr lv_coord_t kPagerChevronX = 24;

// Swatch geometry - same 80×44 split as PhoneWallpaperScreen.
static constexpr lv_coord_t kSwatchSkyH    = 22;
static constexpr lv_coord_t kSwatchGroundH = PhoneThemeScreen::SwatchH - kSwatchSkyH;

PhoneThemeScreen::PhoneThemeScreen()
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

	// Snapshot the persisted theme on entry. BACK pops without any
	// destructive change, but we keep this around for the dirty-aware
	// softkey labels (SAVE/CANCEL while dirty, ""/BACK while pristine).
	initialTheme = Settings.get().themeId;

	// Start with the persisted theme focused, falling back to Default
	// for any out-of-range NVS byte. Same defensive pattern
	// PhoneWallpaperScreen / PhoneSoundScreen use.
	uint8_t snapped = initialTheme;
	if(snapped >= ThemeCount) snapped = 0;
	cursor = snapped;

	// Full-screen container, no scrollbars, no padding.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// The picker's own backdrop honours the *currently saved* theme,
	// not the focused one - so the user gets visual feedback that the
	// theme they had selected before opening the picker is still
	// active until they SAVE. The default `new PhoneSynthwaveBg(obj)`
	// constructor consults Settings.themeId via resolveStyleFromSettings.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Status bar at the top so the user always sees signal / clock /
	// battery while drilling through the picker.
	statusBar = new PhoneStatusBar(obj);

	buildCaption();
	buildSwatch();
	buildPager();

	// Bottom: SAVE on the left, BACK on the right - matches the
	// Sony-Ericsson convention and the rest of the settings family.
	softKeys = new PhoneSoftKeyBar(obj);
	refreshSoftKeys();

	// Initial paint: render the swatch for the persisted theme and
	// the pager labels.
	rebuildSwatch();
	refreshPager();
}

PhoneThemeScreen::~PhoneThemeScreen() {
	// All children are parented to obj - LVGL frees them recursively
	// when the screen's obj is destroyed by the LVScreen base destructor.
}

void PhoneThemeScreen::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneThemeScreen::onStop() {
	Input::getInstance()->removeListener(this);
}

PhoneThemeScreen::Theme PhoneThemeScreen::getCurrentTheme() const {
	return MakerphoneTheme::themeFromByte(cursor);
}

// ----- builders --------------------------------------------------------

void PhoneThemeScreen::buildCaption() {
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "THEME");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, 12);
}

void PhoneThemeScreen::buildSwatch() {
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

void PhoneThemeScreen::buildPager() {
	nameLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(nameLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(nameLabel, MP_TEXT, 0);
	lv_label_set_text(nameLabel,
	                  MakerphoneTheme::getName(MakerphoneTheme::themeFromByte(cursor)));
	lv_obj_set_align(nameLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(nameLabel, kPagerNameY);

	pagerLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(pagerLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(pagerLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(pagerLabel, "");
	lv_obj_set_align(pagerLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(pagerLabel, kPagerIdxY);

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

void PhoneThemeScreen::rebuildSwatch() {
	if(swatchInner == nullptr) return;
	lv_obj_clean(swatchInner);

	const Theme t = MakerphoneTheme::themeFromByte(cursor);
	switch(t){
		case Theme::Default:   drawDefaultSwatch();   break;
		case Theme::Nokia3310: drawNokia3310Swatch(); break;
		default:               drawDefaultSwatch();   break;  // defensive
	}
}

void PhoneThemeScreen::refreshPager() {
	if(nameLabel) {
		lv_label_set_text(nameLabel,
		                  MakerphoneTheme::getName(MakerphoneTheme::themeFromByte(cursor)));
	}
	if(pagerLabel) {
		char buf[8];
		snprintf(buf, sizeof(buf), "%u/%u", static_cast<unsigned>(cursor) + 1u,
				 static_cast<unsigned>(ThemeCount));
		lv_label_set_text(pagerLabel, buf);
	}
}

// ----- per-theme swatch builders --------------------------------------
//
// Each builder draws a flat (non-animated) palette swatch of its theme
// inside the SwatchW x SwatchH box. Synthwave shows the iconic sun-on-
// horizon vignette; Nokia 3310 shows the LCD panel with the antenna
// motif. No SPIFFS - every element is a tiny LVGL primitive.

void PhoneThemeScreen::drawDefaultSwatch() {
	// Sky gradient - same colour ramp the real wallpaper uses
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

	// Ground gradient.
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

	// Tiny semi-circle sun resting on the horizon.
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

	// Three horizontal grid lines on the ground.
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
}

void PhoneThemeScreen::drawNokia3310Swatch() {
	// LCD-green vertical gradient covering the full swatch (no horizon
	// in the Nokia variant - the panel is one flat surface).
	lv_obj_t* panel = lv_obj_create(swatchInner);
	lv_obj_remove_style_all(panel);
	lv_obj_set_size(panel, SwatchW, SwatchH);
	lv_obj_set_pos(panel, 0, 0);
	lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(panel, N3310_BG_LIGHT, 0);
	lv_obj_set_style_bg_grad_color(panel, N3310_BG_DEEP, 0);
	lv_obj_set_style_bg_grad_dir(panel, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(panel, 0, 0);
	lv_obj_set_style_border_width(panel, 0, 0);

	// Three faint scanlines (the swatch is tiny, so 3 reads as the
	// "LCD row structure" without overcrowding).
	for(uint8_t i = 0; i < 3; ++i) {
		lv_obj_t* sl = lv_obj_create(panel);
		lv_obj_remove_style_all(sl);
		lv_obj_set_size(sl, SwatchW, 1);
		lv_obj_set_pos(sl, 0, 6 + i * 12);
		lv_obj_set_style_bg_color(sl, N3310_PIXEL_DIM, 0);
		lv_obj_set_style_bg_opa(sl, LV_OPA_30, 0);
		lv_obj_set_style_radius(sl, 0, 0);
		lv_obj_set_style_border_width(sl, 0, 0);
	}

	// Small antenna motif centred in the swatch (mirrors the wallpaper's
	// bottom-right motif but pulled to the centre so it reads as the
	// theme's signature glyph rather than incidental decor).
	const lv_coord_t mx = SwatchW / 2;
	const lv_coord_t my = SwatchH / 2 - 6;

	// Mast
	{
		lv_obj_t* mast = lv_obj_create(panel);
		lv_obj_remove_style_all(mast);
		lv_obj_set_size(mast, 1, 8);
		lv_obj_set_pos(mast, mx, my);
		lv_obj_set_style_bg_color(mast, N3310_PIXEL, 0);
		lv_obj_set_style_bg_opa(mast, LV_OPA_COVER, 0);
		lv_obj_set_style_radius(mast, 0, 0);
		lv_obj_set_style_border_width(mast, 0, 0);
	}
	// Base
	{
		lv_obj_t* base = lv_obj_create(panel);
		lv_obj_remove_style_all(base);
		lv_obj_set_size(base, 5, 2);
		lv_obj_set_pos(base, mx - 2, my + 8);
		lv_obj_set_style_bg_color(base, N3310_PIXEL, 0);
		lv_obj_set_style_bg_opa(base, LV_OPA_COVER, 0);
		lv_obj_set_style_radius(base, 0, 0);
		lv_obj_set_style_border_width(base, 0, 0);
	}
	// Two pairs of "wave" ticks flanking the mast.
	struct Tick { int8_t dx; int8_t dy; uint8_t w; uint8_t h; lv_opa_t opa; };
	const Tick ticks[] = {
			{ -3,  4, 1, 1, LV_OPA_70 },
			{ -5,  2, 1, 3, LV_OPA_50 },
			{  3,  4, 1, 1, LV_OPA_70 },
			{  5,  2, 1, 3, LV_OPA_50 },
	};
	const uint8_t tickCount = sizeof(ticks) / sizeof(ticks[0]);
	for(uint8_t i = 0; i < tickCount; ++i){
		lv_obj_t* t = lv_obj_create(panel);
		lv_obj_remove_style_all(t);
		lv_obj_set_size(t, ticks[i].w, ticks[i].h);
		lv_obj_set_pos(t, mx + ticks[i].dx, my + ticks[i].dy);
		lv_obj_set_style_bg_color(t, N3310_PIXEL, 0);
		lv_obj_set_style_bg_opa(t, ticks[i].opa, 0);
		lv_obj_set_style_radius(t, 0, 0);
		lv_obj_set_style_border_width(t, 0, 0);
	}
}

// ----- input + commit --------------------------------------------------

void PhoneThemeScreen::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	const bool dirty = (cursor != initialTheme);
	softKeys->set(dirty ? "SAVE"   : "",
	              dirty ? "CANCEL" : "BACK");
}

void PhoneThemeScreen::stepBy(int8_t delta) {
	int16_t next = static_cast<int16_t>(cursor) + delta;
	while(next < 0)                                  next += ThemeCount;
	while(next >= static_cast<int16_t>(ThemeCount))  next -= ThemeCount;
	if(static_cast<uint8_t>(next) == cursor) return;
	cursor = static_cast<uint8_t>(next);
	rebuildSwatch();
	refreshPager();
	refreshSoftKeys();
}

void PhoneThemeScreen::saveAndExit() {
	// Persist the chosen theme. The very next screen that drops a
	// `new PhoneSynthwaveBg(obj)` will pick up the new look (the
	// default ctor reads Settings on every build via
	// resolveStyleFromSettings).
	Settings.get().themeId = cursor;
	Settings.store();
	if(softKeys) softKeys->flashLeft();
	pop();
}

void PhoneThemeScreen::cancelAndExit() {
	// Nothing destructive to revert - the theme isn't applied until
	// SAVE. Just flash the BACK softkey for tactile feedback and pop().
	if(softKeys) softKeys->flashRight();
	pop();
}

void PhoneThemeScreen::buttonPressed(uint i) {
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
