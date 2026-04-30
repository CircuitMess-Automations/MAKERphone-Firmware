#include "PhonePixelAvatar.h"

// MAKERphone retro palette - kept identical to the other Phone* widgets
// (PhoneStatusBar, PhoneSoftKeyBar, PhoneClockFace, PhoneSynthwaveBg,
//  PhoneIconTile, PhoneDialerKey, PhoneDialerPad).
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange (selected border)
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple (idle border)

// ----- variant palettes -----
//
// Stored as raw uint8_t RGB triplets so the tables themselves are pure
// constexpr - lv_color_make() is then called at use-time. This sidesteps
// any chance of LVGL's color-pack inline doing anything surprising in a
// static initializer and keeps the .data section tiny (3 bytes per
// swatch instead of the full lv_color_t).
//
// Each palette is intentionally short: variety across 256 seeds comes
// from *combinations* of dimensions, not from a long list of near-
// duplicates inside any single dimension.

// Tile background swatches (deep purples / night blues that play well
// with the synthwave wallpaper).
static const uint8_t kBgColors[][3] = {
		{ 20, 12, 36},   // deep purple
		{ 36, 20, 60},   // brighter purple
		{ 50, 30, 80},   // lavender
		{ 20, 30, 60},   // night blue
};
static constexpr uint8_t kBgCount = sizeof(kBgColors) / sizeof(kBgColors[0]);

static const uint8_t kSkinColors[][3] = {
		{255, 219, 172}, // pale
		{229, 184, 143}, // light
		{190, 138,  99}, // medium
		{140,  95,  65}, // tan
		{ 95,  60,  45}, // deep
};
static constexpr uint8_t kSkinCount = sizeof(kSkinColors) / sizeof(kSkinColors[0]);

static const uint8_t kHairColors[][3] = {
		{ 35,  25,  25}, // black
		{110,  65,  35}, // brown
		{220, 180,  90}, // blonde
		{195,  60,  50}, // red
		{220, 220, 230}, // grey/white
		{ 80, 200, 230}, // cyan punk
		{220, 110, 220}, // pink punk
};
static constexpr uint8_t kHairCount = sizeof(kHairColors) / sizeof(kHairColors[0]);

static const uint8_t kShirtColors[][3] = {
		{255, 140,  30}, // sunset orange
		{122, 232, 255}, // cyan highlight
		{120, 220, 100}, // lime
		{220,  90, 220}, // magenta
		{240, 220,  80}, // yellow
		{255,  90,  90}, // coral
};
static constexpr uint8_t kShirtCount = sizeof(kShirtColors) / sizeof(kShirtColors[0]);

// Eyes/mouth use the same dark / dusty-rose accents on every face so
// they always "read" against any skin tone.
static inline lv_color_t eyeColor()  { return lv_color_make( 20,  12,  36); }
static inline lv_color_t mouthColor(){ return lv_color_make(180,  60,  80); }

static inline lv_color_t mkColor(const uint8_t (&rgb)[3]){
	return lv_color_make(rgb[0], rgb[1], rgb[2]);
}

PhonePixelAvatar::PhonePixelAvatar(lv_obj_t* parent, uint8_t seed)
		: LVObject(parent), seed(seed), face(nullptr){

	lv_obj_set_size(obj, AvatarSize, AvatarSize);
	buildBackground();
	buildFaceLayer();
	rebuild();
	refreshSelection();
}

// ----- structural builders -----

void PhonePixelAvatar::buildBackground(){
	// Frame: rounded 2 px slab with a 1 px MP_DIM border, switched to a
	// thicker MP_ACCENT border by setSelected(). The fill color itself
	// (the seed-driven background swatch) is set later in rebuild() so
	// it can change with the seed.
	lv_obj_set_style_radius(obj, 2, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_border_width(obj, 1, 0);
	lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(obj, MP_DIM, 0);
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void PhonePixelAvatar::buildFaceLayer(){
	// Transparent overlay container that holds every per-cell rectangle.
	// We use a sub-container (not direct children of obj) so rebuild()
	// can wipe just the pixel art with lv_obj_clean(face) without
	// touching the frame itself.
	face = lv_obj_create(obj);
	lv_obj_remove_style_all(face);
	lv_obj_clear_flag(face, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(face, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(face, AvatarSize, AvatarSize);
	lv_obj_set_pos(face, 0, 0);
	lv_obj_set_style_bg_opa(face, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(face, 0, 0);
	lv_obj_set_style_border_width(face, 0, 0);
}

// ----- public API -----

void PhonePixelAvatar::setSeed(uint8_t newSeed){
	if(newSeed == seed) return;
	seed = newSeed;
	rebuild();
}

void PhonePixelAvatar::setSelected(bool sel){
	if(selected == sel) return;
	selected = sel;
	refreshSelection();
}

// ----- internals -----

void PhonePixelAvatar::refreshSelection(){
	if(selected){
		lv_obj_set_style_border_color(obj, MP_ACCENT, 0);
		lv_obj_set_style_border_width(obj, 2, 0);
	}else{
		lv_obj_set_style_border_color(obj, MP_DIM, 0);
		lv_obj_set_style_border_width(obj, 1, 0);
	}
}

void PhonePixelAvatar::clearFace(){
	// Remove every previously-drawn pixel rectangle. The frame border on
	// `obj` and the `face` container itself are preserved.
	lv_obj_clean(face);
}

void PhonePixelAvatar::rebuild(){
	clearFace();

	// Independent salt per dimension so neighboring seeds still look
	// meaningfully different - if both hair style and shirt color used
	// the same dispersion, seed N and N+1 would always agree on one.
	const uint8_t bgIdx     = hashByte(seed,  1) % kBgCount;
	const uint8_t skinIdx   = hashByte(seed,  2) % kSkinCount;
	const uint8_t hairIdx   = hashByte(seed,  3) % kHairCount;
	const uint8_t shirtIdx  = hashByte(seed,  4) % kShirtCount;
	const uint8_t hairStyle = hashByte(seed,  5) % 4;
	const uint8_t eyeStyle  = hashByte(seed,  6) % 3;
	const uint8_t mouthStyle= hashByte(seed,  7) % 3;

	// Frame fill (background swatch behind the face).
	lv_obj_set_style_bg_color(obj, mkColor(kBgColors[bgIdx]), 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);

	// Order matters: skin first so hair / eyes / mouth paint over it.
	drawSkin(mkColor(kSkinColors[skinIdx]));
	drawHair(hairStyle, mkColor(kHairColors[hairIdx]));
	drawEyes(eyeStyle, eyeColor());
	drawMouth(mouthStyle, mouthColor());
	drawShirt(mkColor(kShirtColors[shirtIdx]));
}

uint8_t PhonePixelAvatar::hashByte(uint8_t seed, uint8_t salt){
	// Tiny xorshift-like 8-bit mixer. Perfect dispersion is not required
	// - we only need that close seeds (e.g. 5 and 6) can fall on
	// different variant indices for each salt. Empirically this passes
	// that bar across all 256 seeds for every salt 1..7.
	uint16_t x = (uint16_t) seed * (uint16_t)((salt | 1u) * 31u + 17u);
	x ^= (uint16_t)(x >> 5);
	x = (uint16_t)(x * 0xb5u);
	x ^= (uint16_t)(x >> 7);
	return (uint8_t)(x & 0xffu);
}

// ----- composition primitive -----

lv_obj_t* PhonePixelAvatar::px(uint8_t gx, uint8_t gy, uint8_t gw, uint8_t gh, lv_color_t color){
	if(gx >= GridSize || gy >= GridSize) return nullptr;
	if(gx + gw > GridSize) gw = GridSize - gx;
	if(gy + gh > GridSize) gh = GridSize - gy;
	if(gw == 0 || gh == 0) return nullptr;

	lv_obj_t* p = lv_obj_create(face);
	lv_obj_remove_style_all(p);
	lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(p, gw * CellPx, gh * CellPx);
	lv_obj_set_pos(p, GridOriginX + gx * CellPx, GridOriginY + gy * CellPx);
	lv_obj_set_style_bg_color(p, color, 0);
	lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(p, 0, 0);
	return p;
}

// ----- per-feature builders -----
//
// All coordinates below are in the 14x14 logical grid: top-left is (0,0),
// bottom-right is (13,13). The face oval is fixed - hair/eyes/mouth/shirt
// vary by seed.

void PhonePixelAvatar::drawSkin(lv_color_t skin){
	// Face oval, run-length encoded by row to keep the rect count low.
	// Rows 2..11 of the grid form a 10-row classic-pixel-portrait oval.
	px(3,  2, 8,  1, skin); // brow line  - narrow top
	px(2,  3, 10, 1, skin);
	px(1,  4, 12, 1, skin);
	px(1,  5, 12, 1, skin);
	px(1,  6, 12, 1, skin); // eye row    - widest stretch
	px(1,  7, 12, 1, skin);
	px(1,  8, 12, 1, skin);
	px(1,  9, 12, 1, skin); // mouth row
	px(2, 10, 10, 1, skin);
	px(3, 11, 8,  1, skin); // chin       - narrow bottom
}

void PhonePixelAvatar::drawHair(uint8_t style, lv_color_t color){
	switch(style){
		case 0: // helmet / round
			px(2, 0, 10, 1, color);
			px(1, 1, 12, 1, color);
			px(0, 2, 3,  1, color);   // left temple
			px(11, 2, 3, 1, color);   // right temple
			px(0, 3, 2,  1, color);
			px(12, 3, 2, 1, color);
			px(0, 4, 1,  1, color);
			px(13, 4, 1, 1, color);
			break;
		case 1: // spiky
			px(2, 0, 1, 1, color);
			px(4, 0, 1, 1, color);
			px(6, 0, 1, 1, color);
			px(8, 0, 1, 1, color);
			px(10, 0, 1, 1, color);
			px(1, 1, 12, 1, color);
			px(0, 2, 3,  1, color);
			px(11, 2, 3, 1, color);
			px(0, 3, 1,  1, color);
			px(13, 3, 1, 1, color);
			break;
		case 2: // side-parted (asymmetric, hair flops to the left)
			px(2, 0, 7, 1, color);
			px(1, 1, 9, 1, color);
			px(0, 2, 4, 1, color);
			px(11, 2, 1, 1, color);   // small tuft on right
			px(0, 3, 1, 1, color);
			px(0, 4, 1, 1, color);
			break;
		case 3: // bald with side strands only
			px(0, 4, 1, 2, color);
			px(13, 4, 1, 2, color);
			break;
	}
}

void PhonePixelAvatar::drawEyes(uint8_t style, lv_color_t color){
	switch(style){
		case 0: // dot eyes
			px(4, 6, 1, 1, color);
			px(9, 6, 1, 1, color);
			break;
		case 1: // wide eyes
			px(4, 6, 2, 1, color);
			px(8, 6, 2, 1, color);
			break;
		case 2: // brows + eyes (intense look)
			px(4, 5, 2, 1, color);    // left brow
			px(8, 5, 2, 1, color);    // right brow
			px(4, 6, 2, 1, color);
			px(8, 6, 2, 1, color);
			break;
	}
}

void PhonePixelAvatar::drawMouth(uint8_t style, lv_color_t color){
	switch(style){
		case 0: // smile (corners + flat middle)
			px(5, 9, 4, 1, color);
			px(4, 9, 1, 1, color);    // left corner
			px(9, 9, 1, 1, color);    // right corner
			break;
		case 1: // small dot
			px(6, 9, 2, 1, color);
			break;
		case 2: // open
			px(5, 9, 4, 1, color);
			px(5, 10, 4, 1, color);
			break;
	}
}

void PhonePixelAvatar::drawShirt(lv_color_t color){
	// Two-row collar at the bottom; the lower row is full-width so the
	// avatar visually "anchors" at the frame edge.
	px(2, 12, 10, 1, color);
	px(0, 13, 14, 1, color);
}
