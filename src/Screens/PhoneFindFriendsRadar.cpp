#include "PhoneFindFriendsRadar.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../Storage/Storage.h"
#include "../Storage/PhoneContacts.h"

// MAKERphone retro palette -- kept identical to every other Phone*
// widget so the radar slots in beside PhoneFortuneCookie (S133),
// PhoneAsciiArt (S170), PhoneStressReliever (S171) and the rest of
// Phase S without a visual seam.
#define MP_BG_DARK         lv_color_make( 20,  12,  36)  // deep purple
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim purple
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange
#define MP_DIM             lv_color_make( 70,  56, 100)  // muted purple

// ---- layout -----------------------------------------------------------
//
// 160x128 budget:
//   y=0..10    PhoneStatusBar
//   y=12..18   "FIND FRIENDS" caption (cyan)
//   y=22..28   "RADAR LOCK" subtitle (dim)
//   y=34..106  radar plate    (cx=50, cy=70, R=36)
//   x=92..156  right banner   (3 lines: name / bearing / range)
//   y=110..116 hint
//   y=118..128 PhoneSoftKeyBar
//
// All coordinates centralised here so a future skin only needs to
// tweak these constants.

static constexpr lv_coord_t kCaptionY      = 12;
static constexpr lv_coord_t kSubtitleY     = 22;
static constexpr lv_coord_t kHintY         = 110;

static constexpr lv_coord_t kBannerX       = 92;
static constexpr lv_coord_t kBannerY       = 36;
static constexpr lv_coord_t kBannerW       = 64;
static constexpr lv_coord_t kBannerH       = 70;

// Sweep tail: each trailing line is offset by this many degrees
// behind the head, drawn with progressively lower opacity.
static constexpr int16_t  kSweepTailDeg1 = 20;
static constexpr int16_t  kSweepTailDeg2 = 40;
static constexpr lv_opa_t kSweepHeadOpa  = LV_OPA_COVER;
static constexpr lv_opa_t kSweepTailOpa1 = LV_OPA_60;
static constexpr lv_opa_t kSweepTailOpa2 = LV_OPA_30;

// Blip dot footprint.
static constexpr lv_coord_t kDotSize = 4;

// ---- helpers ----------------------------------------------------------
//
// We avoid floating point on the radar maths by using lv_trigo_sin,
// which returns sin(deg) * 32767. Conversion back to int pixels is a
// plain divide.

static inline int16_t mpSinScaled(int16_t deg) {
	// lv_trigo_sin handles wrap-around for any signed angle.
	return static_cast<int16_t>(lv_trigo_sin(deg));
}

static inline int16_t mpCosScaled(int16_t deg) {
	// cos(x) = sin(x + 90)
	return static_cast<int16_t>(lv_trigo_sin(deg + 90));
}

// Project a (bearingDeg, radiusPx) polar coord to the screen-space
// (x, y) where bearingDeg=0 means up, clockwise. Convention matches
// every retro radar HUD on the planet.
static inline void mpPolarToXY(uint16_t bearingDeg, int16_t radiusPx,
                                lv_coord_t cx, lv_coord_t cy,
                                lv_coord_t& outX, lv_coord_t& outY) {
	const int16_t deg = static_cast<int16_t>(bearingDeg % 360);
	const int32_t s   = mpSinScaled(deg);
	const int32_t c   = mpCosScaled(deg);
	outX = static_cast<lv_coord_t>(cx + (s * radiusPx) / 32767);
	outY = static_cast<lv_coord_t>(cy - (c * radiusPx) / 32767);
}

// Linear interpolation between two lv_color_t values. The MAKERphone
// theme already uses lv_color_mix in ChatterTheme.cpp, so we lean on
// the LVGL helper instead of poking lv_color_t internals directly --
// keeps us colour-depth agnostic and matches the theme's mixing path.
//
// Note: lv_color_mix's `mix` argument weights its FIRST colour, so
// mpLerpColor(a, b, 0) returns b and mpLerpColor(a, b, 255) returns a.
// Callers below pass `255 - w` when they need "mix toward b".
static inline lv_color_t mpLerpColor(lv_color_t a, lv_color_t b, uint8_t w) {
	return lv_color_mix(a, b, static_cast<uint8_t>(255 - w));
}

// Map a brightness (0..100) to a colour along the radar palette.
//   100..80  : pure cyan
//    80..40  : cyan -> warm cream
//    40..15  : cream -> dim purple
//    15..0   : dim purple, no further lerp (saturated tail)
static lv_color_t mpRadarColorFor(uint8_t brightness) {
	if(brightness >= 80) return MP_HIGHLIGHT;
	if(brightness >= 40) {
		const uint8_t w = static_cast<uint8_t>(((80 - brightness) * 255) / 40);
		return mpLerpColor(MP_HIGHLIGHT, MP_TEXT, w);
	}
	if(brightness >= 15) {
		const uint8_t w = static_cast<uint8_t>(((40 - brightness) * 255) / 25);
		return mpLerpColor(MP_TEXT, MP_DIM, w);
	}
	return MP_DIM;
}

// ---- public statics --------------------------------------------------

uint16_t PhoneFindFriendsRadar::bearingForSeed(uint32_t seed) {
	// Mix the seed with two well-known constants so adjacent UIDs
	// don't end up at neighbouring bearings. The output is a stable
	// 0..359 -- a real radar look-up table would have hand-picked
	// bearings, but we want zero per-friend tuning.
	uint32_t x = seed * 2654435761UL;
	x ^= (x >> 16);
	x *= 0x9E3779B1UL;
	x ^= (x >> 13);
	return static_cast<uint16_t>(x % 360);
}

uint8_t PhoneFindFriendsRadar::rangeForSeed(uint32_t seed, uint8_t maxR) {
	// Different mix from bearingForSeed so a UID's (bearing, range)
	// don't end up correlated -- otherwise high-bearing blips would
	// always sit at the same range and the radar would read as a
	// ring rather than a scatter.
	uint32_t x = seed ^ 0xDEADBEEFUL;
	x = x * 1664525UL + 1013904223UL;
	x ^= (x >> 17);
	// Bias to land between 35% and 95% of the radius so the blip
	// never hides under the centre dot, never clips outside the
	// outer ring.
	const uint8_t lo = static_cast<uint8_t>((maxR * 35) / 100);
	const uint8_t hi = static_cast<uint8_t>((maxR * 95) / 100);
	const uint8_t span = (hi > lo) ? static_cast<uint8_t>(hi - lo) : 1;
	return static_cast<uint8_t>(lo + (x % span));
}

uint8_t PhoneFindFriendsRadar::brightnessFor(uint16_t sweepDeg, uint16_t blipDeg) {
	// Degrees since the sweep last passed the blip's bearing,
	// modulo 360.
	const int32_t s = static_cast<int32_t>(sweepDeg % 360);
	const int32_t b = static_cast<int32_t>(blipDeg  % 360);
	int32_t since = (s - b) % 360;
	if(since < 0) since += 360;
	if(since > 100) return 0;
	return static_cast<uint8_t>(100 - since);
}

// ---- ctor / dtor ------------------------------------------------------

PhoneFindFriendsRadar::PhoneFindFriendsRadar()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  subtitleLabel(nullptr),
		  hintLabel(nullptr),
		  radarFrame(nullptr),
		  radarMidRing(nullptr),
		  radarInnerRing(nullptr),
		  crossH(nullptr),
		  crossV(nullptr),
		  radarCentreDot(nullptr),
		  sweepLine(nullptr),
		  sweepFade1(nullptr),
		  sweepFade2(nullptr),
		  bannerBox(nullptr),
		  bannerNameLabel(nullptr),
		  bannerBearingLabel(nullptr),
		  bannerRangeLabel(nullptr) {

	// Zero-init blip array so renderBlips() has well-defined state
	// even before seedFromStorageOrSamples() runs.
	for(uint8_t i = 0; i < MaxBlips; ++i) {
		blips[i].name[0]    = '\0';
		blips[i].bearingDeg = 0;
		blips[i].rangePx    = 0;
		blips[i].brightness = 0;
		blips[i].dot        = nullptr;
	}

	// Full-screen container, no scrollbars, no padding -- same blank
	// canvas pattern as every other Phone* utility screen.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px).
	statusBar = new PhoneStatusBar(obj);

	buildHud();
	buildRadarPlate();
	buildSweep();
	buildBanner();
	buildBlipDots();

	seedFromStorageOrSamples();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("PING");
	softKeys->setRight("BACK");

	// Final paint with sweep at 0 deg and blips initialised.
	renderSweep();
	renderBlips();
	renderBanner();
}

PhoneFindFriendsRadar::~PhoneFindFriendsRadar() {
	stopTickTimer();
}

// ---- build helpers ---------------------------------------------------

void PhoneFindFriendsRadar::buildHud() {
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_width(captionLabel, 160);
	lv_obj_set_pos(captionLabel, 0, kCaptionY);
	lv_obj_set_style_text_align(captionLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(captionLabel, "FIND FRIENDS");

	subtitleLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(subtitleLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(subtitleLabel, MP_LABEL_DIM, 0);
	lv_obj_set_width(subtitleLabel, 160);
	lv_obj_set_pos(subtitleLabel, 0, kSubtitleY);
	lv_obj_set_style_text_align(subtitleLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(subtitleLabel, "RADAR LOCK");

	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_obj_set_width(hintLabel, 160);
	lv_obj_set_pos(hintLabel, 0, kHintY);
	lv_obj_set_style_text_align(hintLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(hintLabel, "0 PINGS LOCK ON FRIEND");
}

void PhoneFindFriendsRadar::buildRadarPlate() {
	// Outer ring: a circular obj with transparent bg + cyan border.
	const lv_coord_t outerD = static_cast<lv_coord_t>(RadarR * 2);
	radarFrame = lv_obj_create(obj);
	lv_obj_remove_style_all(radarFrame);
	lv_obj_set_size(radarFrame, outerD, outerD);
	lv_obj_set_pos(radarFrame,
	               static_cast<lv_coord_t>(RadarCx - RadarR),
	               static_cast<lv_coord_t>(RadarCy - RadarR));
	lv_obj_set_style_radius(radarFrame, RadarR, 0);
	lv_obj_set_style_bg_opa(radarFrame, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(radarFrame, 1, 0);
	lv_obj_set_style_border_color(radarFrame, MP_HIGHLIGHT, 0);
	lv_obj_set_style_border_opa(radarFrame, LV_OPA_60, 0);
	lv_obj_set_style_pad_all(radarFrame, 0, 0);
	lv_obj_clear_flag(radarFrame, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(radarFrame, LV_OBJ_FLAG_CLICKABLE);

	// Mid ring: same trick at 2/3 of the radius.
	const lv_coord_t midR = static_cast<lv_coord_t>((RadarR * 2) / 3);
	const lv_coord_t midD = static_cast<lv_coord_t>(midR * 2);
	radarMidRing = lv_obj_create(obj);
	lv_obj_remove_style_all(radarMidRing);
	lv_obj_set_size(radarMidRing, midD, midD);
	lv_obj_set_pos(radarMidRing,
	               static_cast<lv_coord_t>(RadarCx - midR),
	               static_cast<lv_coord_t>(RadarCy - midR));
	lv_obj_set_style_radius(radarMidRing, midR, 0);
	lv_obj_set_style_bg_opa(radarMidRing, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(radarMidRing, 1, 0);
	lv_obj_set_style_border_color(radarMidRing, MP_DIM, 0);
	lv_obj_set_style_border_opa(radarMidRing, LV_OPA_50, 0);
	lv_obj_set_style_pad_all(radarMidRing, 0, 0);
	lv_obj_clear_flag(radarMidRing, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(radarMidRing, LV_OBJ_FLAG_CLICKABLE);

	// Inner ring at 1/3 -- gives the eye three radial reference rings.
	const lv_coord_t innR = static_cast<lv_coord_t>(RadarR / 3);
	const lv_coord_t innD = static_cast<lv_coord_t>(innR * 2);
	radarInnerRing = lv_obj_create(obj);
	lv_obj_remove_style_all(radarInnerRing);
	lv_obj_set_size(radarInnerRing, innD, innD);
	lv_obj_set_pos(radarInnerRing,
	               static_cast<lv_coord_t>(RadarCx - innR),
	               static_cast<lv_coord_t>(RadarCy - innR));
	lv_obj_set_style_radius(radarInnerRing, innR, 0);
	lv_obj_set_style_bg_opa(radarInnerRing, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(radarInnerRing, 1, 0);
	lv_obj_set_style_border_color(radarInnerRing, MP_DIM, 0);
	lv_obj_set_style_border_opa(radarInnerRing, LV_OPA_40, 0);
	lv_obj_set_style_pad_all(radarInnerRing, 0, 0);
	lv_obj_clear_flag(radarInnerRing, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(radarInnerRing, LV_OBJ_FLAG_CLICKABLE);

	// Cross-hairs (1 px wide rectangles, dim purple). Crisper than
	// trying to draw two diagonals + a ring would be, and matches
	// the look of a feature-phone radar HUD.
	crossH = lv_obj_create(obj);
	lv_obj_remove_style_all(crossH);
	lv_obj_set_size(crossH, static_cast<lv_coord_t>(RadarR * 2), 1);
	lv_obj_set_pos(crossH,
	               static_cast<lv_coord_t>(RadarCx - RadarR),
	               static_cast<lv_coord_t>(RadarCy));
	lv_obj_set_style_bg_opa(crossH, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(crossH, MP_DIM, 0);
	lv_obj_set_style_border_width(crossH, 0, 0);
	lv_obj_set_style_pad_all(crossH, 0, 0);
	lv_obj_clear_flag(crossH, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(crossH, LV_OBJ_FLAG_CLICKABLE);

	crossV = lv_obj_create(obj);
	lv_obj_remove_style_all(crossV);
	lv_obj_set_size(crossV, 1, static_cast<lv_coord_t>(RadarR * 2));
	lv_obj_set_pos(crossV,
	               static_cast<lv_coord_t>(RadarCx),
	               static_cast<lv_coord_t>(RadarCy - RadarR));
	lv_obj_set_style_bg_opa(crossV, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(crossV, MP_DIM, 0);
	lv_obj_set_style_border_width(crossV, 0, 0);
	lv_obj_set_style_pad_all(crossV, 0, 0);
	lv_obj_clear_flag(crossV, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(crossV, LV_OBJ_FLAG_CLICKABLE);

	// Centre dot: a 3x3 cyan square exactly at the sweep origin so
	// the eye reads "this is the radar centre".
	radarCentreDot = lv_obj_create(obj);
	lv_obj_remove_style_all(radarCentreDot);
	lv_obj_set_size(radarCentreDot, 3, 3);
	lv_obj_set_pos(radarCentreDot,
	               static_cast<lv_coord_t>(RadarCx - 1),
	               static_cast<lv_coord_t>(RadarCy - 1));
	lv_obj_set_style_radius(radarCentreDot, 1, 0);
	lv_obj_set_style_bg_opa(radarCentreDot, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(radarCentreDot, MP_HIGHLIGHT, 0);
	lv_obj_set_style_border_width(radarCentreDot, 0, 0);
	lv_obj_set_style_pad_all(radarCentreDot, 0, 0);
	lv_obj_clear_flag(radarCentreDot, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(radarCentreDot, LV_OBJ_FLAG_CLICKABLE);
}

void PhoneFindFriendsRadar::buildSweep() {
	// Default both endpoints to centre so the line is a degenerate
	// dot until renderSweep() runs. lv_line keeps a *pointer* to the
	// point arrays so the on-tick redraw is just a memcpy + invalidate.
	sweepPoints[0].x = RadarCx;
	sweepPoints[0].y = RadarCy;
	sweepPoints[1].x = RadarCx;
	sweepPoints[1].y = RadarCy;

	fade1Points[0]   = sweepPoints[0];
	fade1Points[1]   = sweepPoints[1];
	fade2Points[0]   = sweepPoints[0];
	fade2Points[1]   = sweepPoints[1];

	// Two trailing fades drawn first so the head paints on top.
	sweepFade2 = lv_line_create(obj);
	lv_line_set_points(sweepFade2, fade2Points, 2);
	lv_obj_set_style_line_color(sweepFade2, MP_HIGHLIGHT, 0);
	lv_obj_set_style_line_width(sweepFade2, 1, 0);
	lv_obj_set_style_line_opa(sweepFade2, kSweepTailOpa2, 0);
	lv_obj_clear_flag(sweepFade2, LV_OBJ_FLAG_CLICKABLE);

	sweepFade1 = lv_line_create(obj);
	lv_line_set_points(sweepFade1, fade1Points, 2);
	lv_obj_set_style_line_color(sweepFade1, MP_HIGHLIGHT, 0);
	lv_obj_set_style_line_width(sweepFade1, 1, 0);
	lv_obj_set_style_line_opa(sweepFade1, kSweepTailOpa1, 0);
	lv_obj_clear_flag(sweepFade1, LV_OBJ_FLAG_CLICKABLE);

	sweepLine = lv_line_create(obj);
	lv_line_set_points(sweepLine, sweepPoints, 2);
	lv_obj_set_style_line_color(sweepLine, MP_HIGHLIGHT, 0);
	lv_obj_set_style_line_width(sweepLine, 1, 0);
	lv_obj_set_style_line_opa(sweepLine, kSweepHeadOpa, 0);
	lv_obj_clear_flag(sweepLine, LV_OBJ_FLAG_CLICKABLE);
}

void PhoneFindFriendsRadar::buildBanner() {
	bannerBox = lv_obj_create(obj);
	lv_obj_remove_style_all(bannerBox);
	lv_obj_set_size(bannerBox, kBannerW, kBannerH);
	lv_obj_set_pos(bannerBox, kBannerX, kBannerY);
	lv_obj_set_style_radius(bannerBox, 2, 0);
	lv_obj_set_style_bg_opa(bannerBox, LV_OPA_60, 0);
	lv_obj_set_style_bg_color(bannerBox, MP_BG_DARK, 0);
	lv_obj_set_style_border_width(bannerBox, 1, 0);
	lv_obj_set_style_border_color(bannerBox, MP_HIGHLIGHT, 0);
	lv_obj_set_style_pad_all(bannerBox, 3, 0);
	lv_obj_clear_flag(bannerBox, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(bannerBox, LV_OBJ_FLAG_CLICKABLE);

	bannerNameLabel = lv_label_create(bannerBox);
	lv_obj_set_style_text_font(bannerNameLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(bannerNameLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_width(bannerNameLabel, kBannerW - 8);
	lv_obj_set_pos(bannerNameLabel, 0, 0);
	lv_obj_set_style_text_align(bannerNameLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_long_mode(bannerNameLabel, LV_LABEL_LONG_CLIP);
	lv_label_set_text(bannerNameLabel, "SCANNING");

	bannerBearingLabel = lv_label_create(bannerBox);
	lv_obj_set_style_text_font(bannerBearingLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(bannerBearingLabel, MP_TEXT, 0);
	lv_obj_set_width(bannerBearingLabel, kBannerW - 8);
	lv_obj_set_pos(bannerBearingLabel, 0, 22);
	lv_obj_set_style_text_align(bannerBearingLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(bannerBearingLabel, "BRG ---");

	bannerRangeLabel = lv_label_create(bannerBox);
	lv_obj_set_style_text_font(bannerRangeLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(bannerRangeLabel, MP_TEXT, 0);
	lv_obj_set_width(bannerRangeLabel, kBannerW - 8);
	lv_obj_set_pos(bannerRangeLabel, 0, 38);
	lv_obj_set_style_text_align(bannerRangeLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(bannerRangeLabel, "RNG ---");
}

void PhoneFindFriendsRadar::buildBlipDots() {
	for(uint8_t i = 0; i < MaxBlips; ++i) {
		lv_obj_t* dot = lv_obj_create(obj);
		lv_obj_remove_style_all(dot);
		lv_obj_set_size(dot, kDotSize, kDotSize);
		lv_obj_set_pos(dot, RadarCx, RadarCy);
		lv_obj_set_style_radius(dot, kDotSize / 2, 0);
		lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
		lv_obj_set_style_bg_color(dot, MP_DIM, 0);
		lv_obj_set_style_border_width(dot, 0, 0);
		lv_obj_set_style_pad_all(dot, 0, 0);
		lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
		// Hidden until populated.
		lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
		blips[i].dot = dot;
	}
}

// ---- population ------------------------------------------------------

void PhoneFindFriendsRadar::seedFromStorageOrSamples() {
	// Walk every paired peer in the Friends repo, skipping the
	// device's own efuse-mac id (the same convention FriendsScreen
	// + PhoneContactsScreen use). PhoneContacts::displayNameOf()
	// transparently picks the user-edited override if present.
	const UID_t selfUid = static_cast<UID_t>(ESP.getEfuseMac());
	for(UID_t uid : Storage.Friends.all()) {
		if(uid == selfUid) continue;
		if(blipsCount >= MaxBlips) break;
		const char* nm = PhoneContacts::displayNameOf(uid);
		if(nm == nullptr || nm[0] == '\0') nm = "FRIEND";
		addBlip(nm, static_cast<uint32_t>(uid));
	}

	// Fallback: a representative roster so the radar reads as
	// populated on first boot. Same names PhoneContactsScreen uses
	// in its sample-fallback so the two screens feel consistent.
	if(blipsCount == 0) {
		struct Sample { const char* name; uint32_t seed; };
		static constexpr Sample kRoster[] = {
			{ "ALEX KIM",     0xA1E4UL },
			{ "ALICE",        0xA11CUL },
			{ "BRIAN COOPER", 0xB10CUL },
			{ "CARL",         0xCA21UL },
			{ "DAD",          0xDAD0UL },
			{ "ELLA",         0xE11AUL },
			{ "GEORGE",       0x6606UL },
			{ "HANNAH",       0x4A44UL },
			{ "JOHN DOE",     0x10E0UL },
			{ "MOM",          0x90909UL },
			{ "PIZZA SHOP",   0x71224UL },
			{ "SAM",          0x5A45UL },
			{ "WORK",         0x77CCUL },
		};
		for(const Sample& s : kRoster) {
			if(blipsCount >= MaxBlips) break;
			addBlip(s.name, s.seed);
		}
	}
}

uint8_t PhoneFindFriendsRadar::addBlip(const char* name, uint32_t seed) {
	if(blipsCount >= MaxBlips) return blipsCount;
	if(name == nullptr) name = "FRIEND";

	Blip& b = blips[blipsCount];
	// Copy + uppercase + truncate name. uppercase makes the label
	// match the rest of the MAKERphone shell visually.
	uint8_t i = 0;
	for(; name[i] != '\0' && i < MaxNameLen; ++i) {
		const char c = name[i];
		b.name[i] = static_cast<char>((c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c);
	}
	b.name[i] = '\0';

	b.bearingDeg = bearingForSeed(seed);
	b.rangePx    = rangeForSeed(seed, RadarR);
	b.brightness = 0;

	if(b.dot != nullptr) {
		lv_coord_t x, y;
		mpPolarToXY(b.bearingDeg, b.rangePx, RadarCx, RadarCy, x, y);
		lv_obj_set_pos(b.dot,
		               static_cast<lv_coord_t>(x - kDotSize / 2),
		               static_cast<lv_coord_t>(y - kDotSize / 2));
		lv_obj_clear_flag(b.dot, LV_OBJ_FLAG_HIDDEN);
	}

	blipsCount++;
	return blipsCount;
}

// ---- lifecycle -------------------------------------------------------

void PhoneFindFriendsRadar::onStart() {
	Input::getInstance()->addListener(this);
	sweepDeg    = 0;
	lockedIdx   = -1;
	sweepActive = true;
	startTickTimer();
	renderSweep();
	renderBlips();
	renderBanner();
}

void PhoneFindFriendsRadar::onStop() {
	Input::getInstance()->removeListener(this);
	stopTickTimer();
}

// ---- render ----------------------------------------------------------

void PhoneFindFriendsRadar::renderSweep() {
	// Head: from centre to perimeter at sweepDeg.
	lv_coord_t hx, hy;
	mpPolarToXY(sweepDeg, RadarR, RadarCx, RadarCy, hx, hy);
	sweepPoints[0].x = RadarCx;
	sweepPoints[0].y = RadarCy;
	sweepPoints[1].x = hx;
	sweepPoints[1].y = hy;
	if(sweepLine) lv_line_set_points(sweepLine, sweepPoints, 2);

	// Trail 1: 20 deg behind head.
	const int16_t tail1Deg = static_cast<int16_t>(
			((static_cast<int32_t>(sweepDeg) - kSweepTailDeg1) % 360 + 360) % 360);
	lv_coord_t t1x, t1y;
	mpPolarToXY(static_cast<uint16_t>(tail1Deg), RadarR, RadarCx, RadarCy, t1x, t1y);
	fade1Points[0].x = RadarCx;
	fade1Points[0].y = RadarCy;
	fade1Points[1].x = t1x;
	fade1Points[1].y = t1y;
	if(sweepFade1) lv_line_set_points(sweepFade1, fade1Points, 2);

	// Trail 2: 40 deg behind head.
	const int16_t tail2Deg = static_cast<int16_t>(
			((static_cast<int32_t>(sweepDeg) - kSweepTailDeg2) % 360 + 360) % 360);
	lv_coord_t t2x, t2y;
	mpPolarToXY(static_cast<uint16_t>(tail2Deg), RadarR, RadarCx, RadarCy, t2x, t2y);
	fade2Points[0].x = RadarCx;
	fade2Points[0].y = RadarCy;
	fade2Points[1].x = t2x;
	fade2Points[1].y = t2y;
	if(sweepFade2) lv_line_set_points(sweepFade2, fade2Points, 2);
}

void PhoneFindFriendsRadar::renderBlips() {
	uint8_t bestBright = 0;
	int8_t  bestIdx    = -1;
	for(uint8_t i = 0; i < blipsCount; ++i) {
		Blip& b = blips[i];
		const uint8_t br = brightnessFor(sweepDeg, b.bearingDeg);
		b.brightness = br;
		if(b.dot != nullptr) {
			lv_obj_set_style_bg_color(b.dot, mpRadarColorFor(br), 0);
			// Bright blips get a one-pixel halo border so they
			// pop visually -- toggled on/off as we cross 50.
			if(br >= 50) {
				lv_obj_set_style_border_width(b.dot, 1, 0);
				lv_obj_set_style_border_color(b.dot, MP_HIGHLIGHT, 0);
			} else {
				lv_obj_set_style_border_width(b.dot, 0, 0);
			}
		}
		if(br > bestBright) {
			bestBright = br;
			bestIdx    = static_cast<int8_t>(i);
		}
	}
	lockedIdx = (bestBright >= LockBrightnessFloor) ? bestIdx : (int8_t) -1;
}

void PhoneFindFriendsRadar::renderBanner() {
	if(lockedIdx < 0 || lockedIdx >= static_cast<int8_t>(blipsCount)) {
		if(bannerNameLabel)    lv_label_set_text(bannerNameLabel,    "SCANNING");
		if(bannerBearingLabel) lv_label_set_text(bannerBearingLabel, "BRG ---");
		if(bannerRangeLabel)   lv_label_set_text(bannerRangeLabel,   "RNG ---");
		if(bannerBox)          lv_obj_set_style_border_color(bannerBox, MP_DIM, 0);
		return;
	}

	const Blip& b = blips[lockedIdx];
	if(bannerNameLabel) lv_label_set_text(bannerNameLabel, b.name);

	char buf[16];
	snprintf(buf, sizeof(buf), "BRG %03u", static_cast<unsigned>(b.bearingDeg));
	if(bannerBearingLabel) lv_label_set_text(bannerBearingLabel, buf);

	// Map the on-screen pixel range onto a 0..99 m fake "metres"
	// readout so the banner reads as a real radar console.
	const unsigned metres = static_cast<unsigned>((b.rangePx * 99u) / RadarR);
	snprintf(buf, sizeof(buf), "RNG %02uM", metres);
	if(bannerRangeLabel) lv_label_set_text(bannerRangeLabel, buf);

	if(bannerBox) {
		lv_obj_set_style_border_color(bannerBox,
		                              (b.brightness >= 70) ? MP_HIGHLIGHT : MP_ACCENT,
		                              0);
	}
}

// ---- ping ------------------------------------------------------------

void PhoneFindFriendsRadar::pingLockedBlip() {
	if(lockedIdx < 0 || lockedIdx >= static_cast<int8_t>(blipsCount)) {
		// No lock -- a ping just flashes the softkey so the press
		// is felt; the radar keeps sweeping.
		if(softKeys) softKeys->flashLeft();
		return;
	}
	if(softKeys) softKeys->flashLeft();
	// Snap the sweep to the locked blip's bearing so the user sees
	// an instantaneous "lock-on" pulse.
	sweepDeg = blips[lockedIdx].bearingDeg;
	renderSweep();
	renderBlips();
	renderBanner();
}

// ---- timer -----------------------------------------------------------

void PhoneFindFriendsRadar::startTickTimer() {
	if(tickTimer != nullptr) return;
	tickTimer = lv_timer_create(&PhoneFindFriendsRadar::onTickStatic,
	                            TickPeriodMs, this);
}

void PhoneFindFriendsRadar::stopTickTimer() {
	if(tickTimer == nullptr) return;
	lv_timer_del(tickTimer);
	tickTimer = nullptr;
}

void PhoneFindFriendsRadar::onTickStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneFindFriendsRadar*>(timer->user_data);
	if(self == nullptr) return;
	self->onTick();
}

void PhoneFindFriendsRadar::onTick() {
	if(!sweepActive) return;
	sweepDeg = static_cast<uint16_t>((sweepDeg + SweepStepDeg) % 360);
	renderSweep();
	renderBlips();
	renderBanner();
}

// ---- input -----------------------------------------------------------

void PhoneFindFriendsRadar::buttonPressed(uint i) {
	switch(i) {
		case BTN_5:
		case BTN_ENTER:
		case BTN_L:
			pingLockedBlip();
			break;

		case BTN_0:
			// Pause / resume the sweep without leaving the screen.
			sweepActive = !sweepActive;
			if(hintLabel) {
				lv_label_set_text(hintLabel,
				                  sweepActive ? "0 PINGS LOCK ON FRIEND"
				                              : "PAUSED -- 0 RESUMES");
			}
			break;

		case BTN_LEFT:
		case BTN_4:
			// Nudge the sweep counter-clockwise by one step --
			// useful for a stop-frame scrub through the friends list.
			sweepDeg = static_cast<uint16_t>(
					(sweepDeg + 360 - SweepStepDeg) % 360);
			renderSweep();
			renderBlips();
			renderBanner();
			break;

		case BTN_RIGHT:
		case BTN_6:
			// Nudge clockwise.
			sweepDeg = static_cast<uint16_t>((sweepDeg + SweepStepDeg) % 360);
			renderSweep();
			renderBlips();
			renderBanner();
			break;

		case BTN_R:
			if(softKeys) softKeys->flashRight();
			pop();
			break;

		case BTN_BACK:
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneFindFriendsRadar::buttonReleased(uint i) {
	switch(i) {
		case BTN_BACK:
			if(!backLongFired) {
				if(softKeys) softKeys->flashRight();
				pop();
			}
			backLongFired = false;
			break;
		default:
			break;
	}
}

void PhoneFindFriendsRadar::buttonHeld(uint i) {
	switch(i) {
		case BTN_BACK:
			backLongFired = true;
			pop();
			break;
		default:
			break;
	}
}
