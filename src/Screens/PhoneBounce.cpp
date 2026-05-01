#include "PhoneBounce.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - identical to every other Phone* widget so the
// arcade screen slots in beside PhoneTetris (S71/S72) and the rest of the
// device without a visual seam. Inlined per the established pattern.
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)  // dim purple captions

// =========================================================================
// Level data (S74)
// =========================================================================
//
// Each level is a uint8_t array of column heights (0 = gap, >=1 = ground
// stack from the bottom of the playfield upwards). A trailing "goal pad"
// stack is part of the level itself, so the goal flag plants on the last
// column.
//
// Rings are stored as a parallel table of (col, rowFromBottom) pairs. A
// rowFromBottom of N means "the ring's centre sits in the row that is N
// tiles up from the bottom of the playfield" (so N=2 floats the ring one
// tile above a flat 1-tile floor). All four levels keep their ring count
// at or below MaxRingsPerLevel so the sprite pool covers them.

namespace {

struct LevelRing {
	uint8_t col;
	uint8_t rowFromBottom;
};

struct LevelDef {
	const uint8_t*   tiles;
	uint8_t          length;
	uint8_t          ringCount;
	const LevelRing* rings;
	const char*      name;
};

// ---------- Level 1: VALLEY (the original S73 layout) -------------------
// 56 columns: warm-up floor, mini hop, real jump, small hill, big jump,
// low plateau, run-up, raised goal pad.
constexpr uint8_t kLevel1Tiles[] = {
	1, 1, 1, 1, 1,        // 0..4   warm-up
	0,                    // 5      mini-hop gap
	1, 1, 1, 1, 1, 1,     // 6..11  floor
	0, 0, 0,              // 12..14 real jump
	1, 2, 3, 2,           // 15..18 small hill
	1, 1,                 // 19..20 floor
	0, 0,                 // 21..22 gap
	1, 1, 1, 1, 1, 1, 1, 1, // 23..30 floor
	0, 0, 0, 0,           // 31..34 big jump
	1, 2, 2, 2, 2, 1,     // 35..40 low plateau
	0, 0, 0,              // 41..43 gap
	1, 1, 1, 1, 1, 1, 1, 1, // 44..51 run-up
	2, 2, 2, 2,           // 52..55 raised goal pad
};

constexpr LevelRing kLevel1Rings[] = {
	{ 8,  3},   // floating above the long floor
	{17,  4},   // crowning the small hill
	{26,  3},   // mid run, before the big jump
	{37,  4},   // above the low plateau
	{50,  3},   // last freebie before the goal
};

// ---------- Level 2: RIDGE ---------------------------------------------
// 64 columns of stepped hills and gaps that demand both the small jump
// and the long-jump-while-thrusting move learned in level 1.
constexpr uint8_t kLevel2Tiles[] = {
	1, 1, 1, 1,                 // 0..3   warm-up
	0, 0,                       // 4..5   small gap
	2, 2, 2,                    // 6..8   raised platform
	0, 0,                       // 9..10  gap
	3, 3, 2, 2,                 // 11..14 ridge descending
	1, 1,                       // 15..16 floor
	0, 0, 0,                    // 17..19 wider gap
	1, 2, 3, 2, 1,              // 20..24 mountain
	0, 0, 0,                    // 25..27 wider gap
	2, 2, 2, 2,                 // 28..31 mid plateau
	0, 0, 0,                    // 32..34 gap
	1, 2, 3, 4, 3, 2, 1,        // 35..41 big mountain
	0, 0, 0,                    // 42..44 gap
	2, 2, 2, 2, 2, 2, 2, 2,     // 45..52 long high run
	0, 0,                       // 53..54 small gap
	1, 1, 1,                    // 55..57 dip
	0, 0,                       // 58..59 gap
	2, 2, 2, 2,                 // 60..63 goal pad
};

constexpr LevelRing kLevel2Rings[] = {
	{ 7, 4},    // above the first raised platform
	{13, 5},    // crest of the descending ridge
	{22, 5},    // mountain peak
	{38, 6},    // tip of the big mountain
	{49, 4},    // mid long run
	{62, 4},    // hovering at the goal pad
};

// ---------- Level 3: CANYON --------------------------------------------
// 64 columns of long gaps and tall plateaus -- punishes weak thrust.
constexpr uint8_t kLevel3Tiles[] = {
	1, 1, 1,                    // 0..2   warm-up
	0, 0, 0, 0,                 // 3..6   first big jump
	1, 2, 2, 1,                 // 7..10  small island
	0, 0, 0, 0,                 // 11..14 second big jump
	2, 2, 2, 2, 2,              // 15..19 high plateau
	0, 0,                       // 20..21 gap
	1, 2, 3, 2, 1,              // 22..26 mountain
	0, 0, 0, 0,                 // 27..30 massive gap
	3, 3, 3, 3, 3,              // 31..35 very high plateau
	0, 0,                       // 36..37 gap
	1, 1, 1, 1, 1, 1,           // 38..43 low run
	0, 0, 0, 0,                 // 44..47 gap
	2, 2, 2, 2, 2,              // 48..52 high pad
	0, 0, 0,                    // 53..55 gap
	2, 2, 2, 2, 3, 3, 3, 3,     // 56..63 stepped goal pad
};

constexpr LevelRing kLevel3Rings[] = {
	{ 8, 4},    // above the small island
	{17, 5},    // above the high plateau
	{24, 5},    // mountain peak
	{33, 5},    // above the very high plateau
	{50, 5},    // crowning the last pad before the goal
	{60, 5},    // by the goal stack
};

// ---------- Level 4: SUMMIT --------------------------------------------
// 72 columns. A staircase climb to a tall summit. Every section is built
// so the obvious path passes near a ring.
constexpr uint8_t kLevel4Tiles[] = {
	1, 1, 1, 1,                 // 0..3   warm-up
	0, 0,                       // 4..5   gap
	2, 2, 2, 2,                 // 6..9   first platform
	0, 0, 0,                    // 10..12 gap
	1, 2, 3, 4, 5, 5,           // 13..18 climbing
	0, 0,                       // 19..20 narrow gap at altitude
	5, 4, 3, 2, 1,              // 21..25 descent
	0, 0, 0,                    // 26..28 gap
	1, 1, 1, 1,                 // 29..32 floor
	0, 0, 0, 0,                 // 33..36 wide gap
	2, 3, 4, 5, 4, 3, 2,        // 37..43 second peak
	0, 0, 0,                    // 44..46 gap
	1, 2, 3, 2, 1,              // 47..51 small mountain
	0, 0, 0,                    // 52..54 gap
	1, 1, 1,                    // 55..57 low run
	0, 0, 0,                    // 58..60 gap
	2, 3, 4,                    // 61..63 climbing
	4, 5, 6,                    // 64..66 climbing summit
	6, 6, 6, 6, 6,              // 67..71 summit goal pad
};

constexpr LevelRing kLevel4Rings[] = {
	{ 8, 4},    // first platform
	{17, 7},    // climb peak
	{40, 6},    // second peak (col 40 = height 5)
	{49, 5},    // small mountain peak
	{56, 4},    // low run, mid-air
	{63, 5},    // start of the summit climb
	{69, 8},    // crown of the summit
};

constexpr LevelDef kLevels[PhoneBounce::LevelCount] = {
	{ kLevel1Tiles, sizeof(kLevel1Tiles), sizeof(kLevel1Rings) / sizeof(LevelRing), kLevel1Rings, "VALLEY" },
	{ kLevel2Tiles, sizeof(kLevel2Tiles), sizeof(kLevel2Rings) / sizeof(LevelRing), kLevel2Rings, "RIDGE"  },
	{ kLevel3Tiles, sizeof(kLevel3Tiles), sizeof(kLevel3Rings) / sizeof(LevelRing), kLevel3Rings, "CANYON" },
	{ kLevel4Tiles, sizeof(kLevel4Tiles), sizeof(kLevel4Rings) / sizeof(LevelRing), kLevel4Rings, "SUMMIT" },
};

// Pin level tables to the public LevelTiles cap so a future overflow at
// edit-time is caught at build-time rather than wandering into UB.
static_assert(sizeof(kLevel1Tiles) <= PhoneBounce::LevelTiles, "L1 too long");
static_assert(sizeof(kLevel2Tiles) <= PhoneBounce::LevelTiles, "L2 too long");
static_assert(sizeof(kLevel3Tiles) <= PhoneBounce::LevelTiles, "L3 too long");
static_assert(sizeof(kLevel4Tiles) <= PhoneBounce::LevelTiles, "L4 too long");

static_assert(sizeof(kLevel1Rings) / sizeof(LevelRing) <= PhoneBounce::MaxRingsPerLevel, "L1 rings");
static_assert(sizeof(kLevel2Rings) / sizeof(LevelRing) <= PhoneBounce::MaxRingsPerLevel, "L2 rings");
static_assert(sizeof(kLevel3Rings) / sizeof(LevelRing) <= PhoneBounce::MaxRingsPerLevel, "L3 rings");
static_assert(sizeof(kLevel4Rings) / sizeof(LevelRing) <= PhoneBounce::MaxRingsPerLevel, "L4 rings");

// Bottom of the playfield, in screen-space px. The playfield begins
// at PhoneBounce::PlayfieldY and is PhoneBounce::PlayfieldH tall.
constexpr lv_coord_t kPlayfieldBottom =
	PhoneBounce::PlayfieldY + PhoneBounce::PlayfieldH;

// Helper: ring centre (absolute screen-space) for the given LevelRing.
inline lv_coord_t ringCentreX(const LevelRing& r) {
	return r.col * PhoneBounce::TileSize + PhoneBounce::TileSize / 2;
}

inline lv_coord_t ringCentreY(const LevelRing& r) {
	// Row N from bottom occupies pixels [bottom - N*size, bottom - (N-1)*size].
	// Centre is the midpoint.
	return kPlayfieldBottom - r.rowFromBottom * PhoneBounce::TileSize
		+ PhoneBounce::TileSize / 2;
}

} // namespace

// ---------- ctor / dtor --------------------------------------------------

PhoneBounce::PhoneBounce()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr) {

	// Defensive initialisation of pool pointers so render() can rely on
	// every slot existing (or being explicitly null) regardless of build
	// helper order.
	for(uint8_t i = 0; i < TileSpritePoolSize; ++i) {
		tileSprites[i] = nullptr;
	}
	for(uint8_t i = 0; i < MaxRingsPerLevel; ++i) {
		ringSprites[i] = nullptr;
	}

	// Full-screen blank canvas. Same pattern PhoneTetris / PhoneCalculator
	// use - status bar + HUD + playfield + soft-key bar are all positioned
	// manually rather than relying on LVGL's flex layout.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of the LVGL z-order. The
	// playfield, ball, status bar and soft-keys all overlay it without
	// per-child opacity gymnastics.
	wallpaper = new PhoneSynthwaveBg(obj);

	statusBar = new PhoneStatusBar(obj);

	buildHud();
	buildPlayfield();
	buildBall();
	buildOverlay();

	softKeys = new PhoneSoftKeyBar(obj);

	// Initial state: idle on level 0 with the overlay showing.
	currentLevelIdx = 0;
	enterIdle();
	refreshSoftKeys();
	refreshOverlay();
}

PhoneBounce::~PhoneBounce() {
	stopTickTimer();
	// All children are parented to obj; LVGL frees them recursively when
	// the screen's obj is destroyed by the LVScreen base destructor.
}

void PhoneBounce::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneBounce::onStop() {
	Input::getInstance()->removeListener(this);
	stopTickTimer();
	holdLeft = false;
	holdRight = false;
}

// ---------- build helpers -----------------------------------------------

void PhoneBounce::buildHud() {
	// Level indicator (left). pixelbasic7 -- 7 px tall -- sits flush in
	// the 10 px HUD strip with a single px of breathing room.
	hudLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(hudLabel, "L1/4");
	lv_obj_set_pos(hudLabel, 4, 12);

	// Centered title -- doubles as level name once a run begins.
	titleLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(titleLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(titleLabel, MP_ACCENT, 0);
	lv_label_set_text(titleLabel, "BOUNCE");
	lv_obj_set_align(titleLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(titleLabel, 12);

	// Right-anchored numeric score so a 4-digit score doesn't crowd the
	// title. LV_ALIGN_TOP_RIGHT pins to the right edge with a 2 px inset
	// and a -2 px offset on the right keeps it from kissing the bezel.
	scoreLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(scoreLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(scoreLabel, MP_LABEL_DIM, 0);
	lv_obj_set_style_text_align(scoreLabel, LV_TEXT_ALIGN_RIGHT, 0);
	lv_label_set_text(scoreLabel, "0");
	lv_obj_set_align(scoreLabel, LV_ALIGN_TOP_RIGHT);
	lv_obj_set_pos(scoreLabel, -3, 12);
}

void PhoneBounce::buildPlayfield() {
	// Playfield container -- clips its children, no scroll, no border.
	playfield = lv_obj_create(obj);
	lv_obj_remove_style_all(playfield);
	lv_obj_set_size(playfield, 160, PlayfieldH);
	lv_obj_set_pos(playfield, 0, PlayfieldY);
	lv_obj_set_style_bg_opa(playfield, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(playfield, 0, 0);
	lv_obj_set_style_pad_all(playfield, 0, 0);
	lv_obj_clear_flag(playfield, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(playfield, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(playfield, LV_OBJ_FLAG_IGNORE_LAYOUT);

	// Pool of tile sprites. Each one is a single rectangle -- we recycle
	// it per render pass by repositioning + resizing it. Hidden by
	// default so unused slots don't draw.
	for(uint8_t i = 0; i < TileSpritePoolSize; ++i) {
		auto* t = lv_obj_create(playfield);
		lv_obj_remove_style_all(t);
		lv_obj_set_size(t, TileSize, TileSize);
		lv_obj_set_pos(t, -100, -100);
		lv_obj_set_style_bg_color(t, MP_DIM, 0);
		lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
		lv_obj_set_style_border_color(t, MP_HIGHLIGHT, 0);
		lv_obj_set_style_border_width(t, 1, 0);
		lv_obj_set_style_radius(t, 0, 0);
		lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(t, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(t, LV_OBJ_FLAG_HIDDEN);
		tileSprites[i] = t;
	}

	// Pool of ring sprites. Each ring is a 7x7 transparent-fill ring with
	// a sunset-orange border, drawn round via LV_RADIUS_CIRCLE. They start
	// hidden; render() shows the ones the active level uses.
	for(uint8_t i = 0; i < MaxRingsPerLevel; ++i) {
		auto* r = lv_obj_create(playfield);
		lv_obj_remove_style_all(r);
		lv_obj_set_size(r, RingSize, RingSize);
		lv_obj_set_pos(r, -100, -100);
		lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
		lv_obj_set_style_border_color(r, MP_ACCENT, 0);
		lv_obj_set_style_border_width(r, 1, 0);
		lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0);
		lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(r, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(r, LV_OBJ_FLAG_HIDDEN);
		ringSprites[i] = r;
	}

	// Goal flag -- a small vertical pole + pennant at the right edge of
	// the level. We position it in screen-space during render().
	goalFlag = lv_obj_create(playfield);
	lv_obj_remove_style_all(goalFlag);
	lv_obj_set_size(goalFlag, 8, 18);
	lv_obj_set_pos(goalFlag, -100, -100);
	lv_obj_set_style_bg_color(goalFlag, MP_ACCENT, 0);
	lv_obj_set_style_bg_opa(goalFlag, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(goalFlag, 0, 0);
	lv_obj_set_style_radius(goalFlag, 0, 0);
	lv_obj_clear_flag(goalFlag, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(goalFlag, LV_OBJ_FLAG_CLICKABLE);
}

void PhoneBounce::buildBall() {
	ball = lv_obj_create(playfield);
	lv_obj_remove_style_all(ball);
	lv_obj_set_size(ball, BallRadius * 2, BallRadius * 2);
	lv_obj_set_pos(ball, -100, -100);
	lv_obj_set_style_bg_color(ball, MP_HIGHLIGHT, 0);
	lv_obj_set_style_bg_opa(ball, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(ball, MP_ACCENT, 0);
	lv_obj_set_style_border_width(ball, 1, 0);
	lv_obj_set_style_radius(ball, LV_RADIUS_CIRCLE, 0);
	lv_obj_clear_flag(ball, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(ball, LV_OBJ_FLAG_CLICKABLE);
}

void PhoneBounce::buildOverlay() {
	overlayLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(overlayLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(overlayLabel, MP_TEXT, 0);
	lv_obj_set_style_text_align(overlayLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(overlayLabel, "");
	lv_obj_set_align(overlayLabel, LV_ALIGN_CENTER);
	lv_obj_set_y(overlayLabel, 4);
}

// ---------- state transitions -------------------------------------------

void PhoneBounce::enterIdle() {
	state = GameState::Idle;
	stopTickTimer();
	holdLeft = false;
	holdRight = false;
	ringsCollectedMask = 0;
	ringsThisLevel = 0;
	resetBall();
	cameraX = 0;
	furthestColumn = 0;
	render();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneBounce::startGame() {
	// Fresh campaign: reset score and start at level 0.
	score = 0;
	currentLevelIdx = 0;
	startCurrentLevel();
}

void PhoneBounce::startCurrentLevel() {
	state = GameState::Playing;
	resetBall();
	cameraX = 0;
	furthestColumn = 0;
	ringsCollectedMask = 0;
	ringsThisLevel = 0;
	startTickMs = millis();
	startTickTimer();
	render();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneBounce::pauseGame() {
	state = GameState::Paused;
	stopTickTimer();
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneBounce::resumeGame() {
	state = GameState::Playing;
	startTickTimer();
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneBounce::endGame() {
	state = GameState::GameOver;
	stopTickTimer();
	holdLeft = false;
	holdRight = false;
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneBounce::winLevel() {
	// Time bonus -- generous floor, but deliberately easy to beat after
	// one good run so the player has something to chase.
	const uint32_t elapsedMs = (startTickMs == 0) ? 0 : (millis() - startTickMs);
	const uint32_t elapsedSec = elapsedMs / 1000;
	const uint32_t penalty = elapsedSec * 10;
	const uint32_t bonus = (penalty >= 600) ? 0 : (600 - penalty);
	score += bonus;

	stopTickTimer();
	holdLeft = false;
	holdRight = false;

	if(currentLevelIdx + 1 >= LevelCount) {
		clearedAll();
		return;
	}

	state = GameState::Won;
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneBounce::clearedAll() {
	state = GameState::Cleared;
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
}

// ---------- physics -----------------------------------------------------

void PhoneBounce::resetBall() {
	// Spawn the ball one tile in from the left edge, sitting on top of
	// the first column's floor. Two pixels above so the first physics
	// tick settles naturally rather than starting in a "grounded" pose.
	const lv_coord_t groundY = columnTopY(1);
	ballXQ8 = static_cast<int32_t>(TileSize + TileSize / 2) * Q8;
	ballYQ8 = static_cast<int32_t>(groundY - BallRadius - 2) * Q8;
	ballVxQ8 = 0;
	ballVyQ8 = 0;
	grounded = false;
}

uint16_t PhoneBounce::currentLevelLength() const {
	return kLevels[currentLevelIdx].length;
}

uint8_t PhoneBounce::currentTileAt(uint16_t column) const {
	const LevelDef& lvl = kLevels[currentLevelIdx];
	if(column >= lvl.length) return 0;
	return lvl.tiles[column];
}

lv_coord_t PhoneBounce::columnTopY(uint16_t col) const {
	const LevelDef& lvl = kLevels[currentLevelIdx];
	if(col >= lvl.length) {
		return kPlayfieldBottom + 1;
	}
	const uint8_t h = lvl.tiles[col];
	if(h == 0) {
		return kPlayfieldBottom + 1;
	}
	return kPlayfieldBottom - h * TileSize;
}

uint8_t PhoneBounce::currentRingCount() const {
	return kLevels[currentLevelIdx].ringCount;
}

void PhoneBounce::checkRingPickup() {
	const LevelDef& lvl = kLevels[currentLevelIdx];
	if(lvl.ringCount == 0) return;

	const int16_t bcx = static_cast<int16_t>(ballXQ8 / Q8);
	const int16_t bcy = static_cast<int16_t>(ballYQ8 / Q8);

	bool picked = false;
	for(uint8_t i = 0; i < lvl.ringCount && i < MaxRingsPerLevel; ++i) {
		const uint16_t bit = static_cast<uint16_t>(1) << i;
		if(ringsCollectedMask & bit) continue;

		const LevelRing& r = lvl.rings[i];
		const int16_t rcx = ringCentreX(r);
		const int16_t rcy = ringCentreY(r);
		int16_t dx = bcx - rcx;
		int16_t dy = bcy - rcy;
		if(dx < 0) dx = -dx;
		if(dy < 0) dy = -dy;
		if(dx <= RingPickupR && dy <= RingPickupR) {
			ringsCollectedMask |= bit;
			ringsThisLevel++;
			score += RingScore;
			picked = true;
		}
	}

	if(picked) {
		refreshHud();
	}
}

void PhoneBounce::physicsStep() {
	// Apply input thrust + drag.
	if(holdRight) ballVxQ8 += ThrustQ8;
	if(holdLeft)  ballVxQ8 -= ThrustQ8;

	// Air / ground drag -- multiply by DragNumQ8/Q8 each tick. Keeps the
	// ball from accelerating without bound and decays sideways momentum
	// once thrust is released.
	ballVxQ8 = static_cast<int32_t>(ballVxQ8) * DragNumQ8 / Q8;

	// Gravity.
	ballVyQ8 += GravityQ8;

	// Velocity clamps.
	if(ballVxQ8 >  MaxVxQ8) ballVxQ8 = MaxVxQ8;
	if(ballVxQ8 < -MaxVxQ8) ballVxQ8 = -MaxVxQ8;
	if(ballVyQ8 >  MaxVyQ8) ballVyQ8 = MaxVyQ8;
	if(ballVyQ8 < -MaxVyQ8) ballVyQ8 = -MaxVyQ8;

	// Integrate.
	int32_t newXQ8 = ballXQ8 + ballVxQ8;
	int32_t newYQ8 = ballYQ8 + ballVyQ8;

	// Clamp ball to level bounds in X. Past the right edge means
	// "we won this level".
	const int32_t levelWidthPx =
		static_cast<int32_t>(currentLevelLength()) * TileSize;
	const int32_t leftLimitQ8  = static_cast<int32_t>(BallRadius) * Q8;
	const int32_t rightLimitQ8 = (levelWidthPx - BallRadius) * Q8;

	if(newXQ8 < leftLimitQ8) {
		newXQ8 = leftLimitQ8;
		if(ballVxQ8 < 0) ballVxQ8 = 0;
	}
	bool reachedRight = false;
	if(newXQ8 >= rightLimitQ8) {
		newXQ8 = rightLimitQ8;
		reachedRight = true;
	}

	// --- Y collision: only check the column directly beneath the ball.
	// One-column resolution is good enough for a Nokia-Bounce-style game
	// at TileSize=8 and BallRadius=3 -- the ball's footprint is narrower
	// than a tile, so it cannot straddle two tiles.
	const int32_t ballScreenX = newXQ8 / Q8;
	const uint16_t column = static_cast<uint16_t>(ballScreenX / TileSize);
	const lv_coord_t topY = columnTopY(column);
	const int32_t restYQ8 = static_cast<int32_t>(topY - BallRadius) * Q8;

	bool wasGrounded = grounded;
	grounded = false;

	if(ballVyQ8 >= 0 && newYQ8 >= restYQ8 && topY <= kPlayfieldBottom) {
		// Hit the top of a solid column. Bounce decays vertical velocity
		// each impact; once it falls under BounceCutoff we treat the ball
		// as resting on the surface.
		newYQ8 = restYQ8;
		const int16_t reflected = -static_cast<int16_t>(
			(static_cast<int32_t>(ballVyQ8) * BounceLossN) / Q8);
		if(reflected > -BounceCutoff) {
			ballVyQ8 = 0;
			grounded = true;
		} else {
			ballVyQ8 = reflected;
		}
	}

	// Death: ball fell past the bottom of the playfield (i.e. into a gap
	// and below the world).
	const lv_coord_t deathY = kPlayfieldBottom + BallRadius * 2;
	if(newYQ8 / Q8 >= deathY) {
		ballXQ8 = newXQ8;
		ballYQ8 = newYQ8;
		render();
		endGame();
		return;
	}

	ballXQ8 = newXQ8;
	ballYQ8 = newYQ8;
	(void)wasGrounded;

	// Score: count newly-cleared columns.
	const uint16_t currentCol = static_cast<uint16_t>(ballScreenX / TileSize);
	if(currentCol > furthestColumn && currentCol < currentLevelLength()) {
		const uint16_t delta = currentCol - furthestColumn;
		furthestColumn = currentCol;
		score += delta;
		refreshHud();
	}

	// Ring pickups -- tested every tick; cheap (O(rings_per_level) <= 8).
	checkRingPickup();

	// Camera follow -- keep the ball roughly in the middle third of the
	// 160 px viewport. The camera is clamped to the level bounds.
	const int16_t targetCamera = static_cast<int16_t>(ballScreenX) - 80;
	int16_t maxCamera =
		static_cast<int16_t>(static_cast<int32_t>(currentLevelLength()) * TileSize - 160);
	if(maxCamera < 0) maxCamera = 0;
	if(targetCamera < 0) cameraX = 0;
	else if(targetCamera > maxCamera) cameraX = maxCamera;
	else cameraX = targetCamera;

	render();

	if(reachedRight) {
		winLevel();
	}
}

// ---------- timers -------------------------------------------------------

void PhoneBounce::startTickTimer() {
	if(tickTimer != nullptr) return;
	tickTimer = lv_timer_create(&PhoneBounce::onTickTimerStatic, TickMs, this);
}

void PhoneBounce::stopTickTimer() {
	if(tickTimer != nullptr) {
		lv_timer_del(tickTimer);
		tickTimer = nullptr;
	}
}

void PhoneBounce::onTickTimerStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneBounce*>(timer->user_data);
	if(self == nullptr) return;
	if(self->state != GameState::Playing) return;
	self->physicsStep();
}

// ---------- rendering ---------------------------------------------------

void PhoneBounce::render() {
	if(playfield == nullptr) return;

	const LevelDef& lvl = kLevels[currentLevelIdx];

	// Tile sprites: paint the columns visible under the current camera.
	// Visible range is [cameraX, cameraX + 160). Columns outside the
	// level bounds are rendered as hidden slots.
	const int16_t startCol = cameraX / TileSize;
	for(uint8_t i = 0; i < TileSpritePoolSize; ++i) {
		auto* spr = tileSprites[i];
		if(spr == nullptr) continue;

		const int16_t col = startCol + i;
		if(col < 0 || col >= static_cast<int16_t>(lvl.length)) {
			lv_obj_add_flag(spr, LV_OBJ_FLAG_HIDDEN);
			continue;
		}
		const uint8_t h = lvl.tiles[col];
		if(h == 0) {
			lv_obj_add_flag(spr, LV_OBJ_FLAG_HIDDEN);
			continue;
		}
		// Resize the sprite to the column's height.
		const lv_coord_t spriteH = h * TileSize;
		const lv_coord_t spriteW = TileSize;
		lv_obj_set_size(spr, spriteW, spriteH);

		// Position in playfield-local px (playfield is at (0, PlayfieldY)
		// and is PlayfieldH tall, so local Y 0 is the top of the play
		// area).
		const lv_coord_t worldX  = col * TileSize;
		const lv_coord_t localX  = worldX - cameraX;
		const lv_coord_t localY  = PlayfieldH - spriteH;
		lv_obj_set_pos(spr, localX, localY);
		lv_obj_clear_flag(spr, LV_OBJ_FLAG_HIDDEN);
	}

	// Ring sprites. Slots beyond the level's ringCount, or rings already
	// collected, are simply hidden.
	for(uint8_t i = 0; i < MaxRingsPerLevel; ++i) {
		auto* rspr = ringSprites[i];
		if(rspr == nullptr) continue;

		const bool inUse =
			(i < lvl.ringCount) &&
			((ringsCollectedMask & (static_cast<uint16_t>(1) << i)) == 0);
		if(!inUse) {
			lv_obj_add_flag(rspr, LV_OBJ_FLAG_HIDDEN);
			continue;
		}

		const LevelRing& r = lvl.rings[i];
		const lv_coord_t worldCx = ringCentreX(r);
		const lv_coord_t worldCy = ringCentreY(r);
		const lv_coord_t localX  = (worldCx - cameraX) - RingSize / 2;
		const lv_coord_t localY  = (worldCy - PlayfieldY) - RingSize / 2;

		// Cull rings entirely off the visible playfield to save the
		// driver some drawing.
		if(localX < -RingSize || localX > 160) {
			lv_obj_add_flag(rspr, LV_OBJ_FLAG_HIDDEN);
			continue;
		}
		lv_obj_clear_flag(rspr, LV_OBJ_FLAG_HIDDEN);
		lv_obj_set_pos(rspr, localX, localY);
	}

	// Goal flag at the very last column of the active level.
	if(goalFlag != nullptr) {
		const int16_t goalCol = static_cast<int16_t>(lvl.length) - 1;
		const uint8_t goalH = lvl.tiles[goalCol];
		const lv_coord_t flagWorldX = goalCol * TileSize;
		const lv_coord_t flagLocalX = flagWorldX - cameraX;
		const lv_coord_t flagH = 18;
		const lv_coord_t flagY = PlayfieldH - goalH * TileSize - flagH;
		// Hide if off-screen.
		if(flagLocalX < -8 || flagLocalX > 160) {
			lv_obj_add_flag(goalFlag, LV_OBJ_FLAG_HIDDEN);
		} else {
			lv_obj_clear_flag(goalFlag, LV_OBJ_FLAG_HIDDEN);
			lv_obj_set_pos(goalFlag, flagLocalX, flagY);
		}
	}

	// Ball -- world -> playfield-local coords.
	if(ball != nullptr) {
		const lv_coord_t ballWorldX = ballXQ8 / Q8;
		const lv_coord_t ballWorldY = ballYQ8 / Q8;
		const lv_coord_t ballLocalX =
			static_cast<lv_coord_t>(ballWorldX - cameraX) - BallRadius;
		// ballYQ8 is the centre of the ball in absolute screen-Y. Convert
		// to playfield-local Y for the LVGL position.
		const lv_coord_t ballLocalY =
			static_cast<lv_coord_t>(ballWorldY - PlayfieldY) - BallRadius;
		lv_obj_set_pos(ball, ballLocalX, ballLocalY);
	}
}

void PhoneBounce::refreshHud() {
	if(hudLabel != nullptr) {
		char buf[16];
		snprintf(buf, sizeof(buf), "L%u/%u",
			static_cast<unsigned>(currentLevelIdx + 1),
			static_cast<unsigned>(LevelCount));
		lv_label_set_text(hudLabel, buf);
	}

	if(titleLabel != nullptr) {
		// The title doubles as the level name once a run begins. While
		// idle it stays as the franchise name "BOUNCE" so the boot view
		// looks like an arcade splash.
		if(state == GameState::Idle) {
			lv_label_set_text(titleLabel, "BOUNCE");
		} else {
			lv_label_set_text(titleLabel, kLevels[currentLevelIdx].name);
		}
	}

	if(scoreLabel != nullptr) {
		char buf[24];
		const uint8_t total = currentRingCount();
		// Show "score/rings" together so the player can see "what does it
		// take to 100% this level?" at a glance.
		snprintf(buf, sizeof(buf), "%lu R%u/%u",
			static_cast<unsigned long>(score),
			static_cast<unsigned>(ringsThisLevel),
			static_cast<unsigned>(total));
		lv_label_set_text(scoreLabel, buf);
	}
}

void PhoneBounce::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	switch(state) {
		case GameState::Idle:
			softKeys->setLeft("START");
			softKeys->setRight("BACK");
			break;
		case GameState::Playing:
			softKeys->setLeft("PAUSE");
			softKeys->setRight("BACK");
			break;
		case GameState::Paused:
			softKeys->setLeft("RESUME");
			softKeys->setRight("BACK");
			break;
		case GameState::GameOver:
			softKeys->setLeft("RETRY");
			softKeys->setRight("BACK");
			break;
		case GameState::Won:
			softKeys->setLeft("NEXT");
			softKeys->setRight("BACK");
			break;
		case GameState::Cleared:
			softKeys->setLeft("AGAIN");
			softKeys->setRight("BACK");
			break;
	}
}

void PhoneBounce::refreshOverlay() {
	if(overlayLabel == nullptr) return;
	char buf[48];
	switch(state) {
		case GameState::Idle:
			lv_label_set_text(overlayLabel, "PRESS\nSTART");
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
		case GameState::Playing:
			lv_obj_add_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
		case GameState::Paused:
			lv_label_set_text(overlayLabel, "PAUSED");
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
		case GameState::GameOver:
			snprintf(buf, sizeof(buf), "GAME OVER\nSCORE %lu",
			         static_cast<unsigned long>(score));
			lv_label_set_text(overlayLabel, buf);
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
		case GameState::Won:
			snprintf(buf, sizeof(buf), "GOAL!\n%s\nNEXT: %s",
			         kLevels[currentLevelIdx].name,
			         (currentLevelIdx + 1 < LevelCount)
			             ? kLevels[currentLevelIdx + 1].name
			             : "-");
			lv_label_set_text(overlayLabel, buf);
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
		case GameState::Cleared:
			snprintf(buf, sizeof(buf), "ALL CLEAR!\nSCORE %lu",
			         static_cast<unsigned long>(score));
			lv_label_set_text(overlayLabel, buf);
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
	}
}

// ---------- input -------------------------------------------------------

void PhoneBounce::buttonPressed(uint i) {
	switch(state) {
		case GameState::Idle:
			if(i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				startGame();
			} else if(i == BTN_BACK) {
				if(softKeys) softKeys->flashRight();
				pop();
			}
			return;

		case GameState::Paused:
			if(i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				resumeGame();
			} else if(i == BTN_BACK) {
				if(softKeys) softKeys->flashRight();
				pop();
			}
			return;

		case GameState::GameOver:
			if(i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				// Retry the same level. Campaign score so far stays.
				startCurrentLevel();
			} else if(i == BTN_BACK) {
				if(softKeys) softKeys->flashRight();
				pop();
			}
			return;

		case GameState::Won:
			if(i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				// Advance to the next level. winLevel() guaranteed
				// currentLevelIdx + 1 < LevelCount when we arrived here.
				currentLevelIdx++;
				startCurrentLevel();
			} else if(i == BTN_BACK) {
				if(softKeys) softKeys->flashRight();
				pop();
			}
			return;

		case GameState::Cleared:
			if(i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				currentLevelIdx = 0;
				enterIdle();
			} else if(i == BTN_BACK) {
				if(softKeys) softKeys->flashRight();
				pop();
			}
			return;

		case GameState::Playing:
			break;  // fall through to the in-game handler below
	}

	// In-game key chord.
	switch(i) {
		case BTN_LEFT:
		case BTN_4:
			holdLeft = true;
			break;
		case BTN_RIGHT:
		case BTN_6:
			holdRight = true;
			break;
		case BTN_2:
		case BTN_5:
			// Jump only if grounded (stops the ball from cheesing the gaps).
			if(grounded) {
				ballVyQ8 = JumpVyQ8;
				grounded = false;
			}
			break;
		case BTN_ENTER:
			if(softKeys) softKeys->flashLeft();
			pauseGame();
			break;
		case BTN_BACK:
			if(softKeys) softKeys->flashRight();
			pop();
			break;
		default:
			break;
	}
}

void PhoneBounce::buttonReleased(uint i) {
	switch(i) {
		case BTN_LEFT:
		case BTN_4:
			holdLeft = false;
			break;
		case BTN_RIGHT:
		case BTN_6:
			holdRight = false;
			break;
		default:
			break;
	}
}
