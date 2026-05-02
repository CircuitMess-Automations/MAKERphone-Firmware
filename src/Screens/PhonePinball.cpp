#include "PhonePinball.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <stdlib.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - identical to every other Phone* widget so
// the arcade screen slots in beside PhoneTetris (S71/S72), PhoneBounce
// (S73/S74), PhoneBrickBreaker (S75) and the rest of the device without
// a visual seam. Inlined per the established pattern.
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)  // dim purple captions

namespace {

// Tiny xorshift PRNG so the launch nudge is repeatable per-session
// regardless of what the platform's `random()` is doing.
inline uint16_t xorshift16(uint16_t& s) {
	uint16_t x = s ? s : 0xACE1u;
	x ^= x << 7;
	x ^= x >> 9;
	x ^= x << 8;
	s = x;
	return x;
}

// Newton's-method 32-bit integer square root. Used for the rare
// normalisation in collision response. Cheap (<= 5 iterations for any
// input under 2^16, and the inputs are tiny here).
uint32_t isqrt32(uint32_t n) {
	if(n == 0) return 0;
	uint32_t x = n;
	uint32_t y = (x + 1) >> 1;
	while(y < x) {
		x = y;
		y = (x + n / x) >> 1;
	}
	return x;
}

// Bumper layout (per table). Centres in playfield coords (which are
// screen coords for this game -- the playfield rectangle pins to
// (0, 20)). Radii kept small (5-6 px) to leave room for the ball to
// weave between them on the 160x128 panel.
struct BumperDef {
	int16_t cx;
	int16_t cy;
	int16_t r;
};

// Per-table layout table (S86). The first row is S85's classic Williams
// arrangement -- top trio + side pair. The second row is the S86
// "Cluster" preset: a denser 7-disc field with a centred lower bumper
// that nudges drained shots back toward the flippers.
//
// Sized to the largest layout (BumperCount). Tables with fewer active
// bumpers leave the trailing entries with `r == 0`; the runtime hides
// those sprites and skips them in collision and render loops via
// activeBumperCount().
constexpr BumperDef kTableBumpers[PhonePinball::TableCount]
                                 [PhonePinball::BumperCount] = {
	// Classic (S85) -- 5 bumpers, trio + side pair.
	{
		{ 40,  46, 6 },   // upper-left
		{ 80,  36, 6 },   // top centre
		{ 120, 46, 6 },   // upper-right
		{ 56,  72, 5 },   // lower-left
		{ 104, 72, 5 },   // lower-right
		{ 0,   0,  0 },
		{ 0,   0,  0 },
	},
	// Cluster (S86) -- 7 bumpers, denser field with central kicker.
	// Top row of 4 packs the upper third; side pair drops a row;
	// the lone centre-bottom disc rebounds drained shots back up.
	{
		{ 28,  34, 5 },   // top-left
		{ 64,  28, 5 },   // top mid-left
		{ 96,  28, 5 },   // top mid-right
		{ 132, 34, 5 },   // top-right
		{ 44,  62, 5 },   // mid-left
		{ 116, 62, 5 },   // mid-right
		{ 80,  82, 6 },   // bottom centre kicker
	},
};

constexpr uint8_t kTableBumperCount[PhonePinball::TableCount] = { 5, 7 };

constexpr uint8_t HitFlashTicks = 6;   // ~200 ms at 30 Hz

} // namespace

// S86 -- per-table top-LeaderboardSize scores. RAM-only, so the
// leaderboard survives across re-entries to the screen during the same
// power cycle and resets on reboot. Zero-initialised at first use.
uint32_t PhonePinball::leaderboard[PhonePinball::TableCount]
                                  [PhonePinball::LeaderboardSize] = {
	{ 0, 0, 0 },
	{ 0, 0, 0 },
};

uint8_t PhonePinball::activeBumperCount() const {
	const uint8_t idx = static_cast<uint8_t>(currentTable);
	return kTableBumperCount[idx];
}

int16_t PhonePinball::bumperCx(uint8_t i) const {
	return kTableBumpers[static_cast<uint8_t>(currentTable)][i].cx;
}

int16_t PhonePinball::bumperCy(uint8_t i) const {
	return kTableBumpers[static_cast<uint8_t>(currentTable)][i].cy;
}

int16_t PhonePinball::bumperRadius(uint8_t i) const {
	return kTableBumpers[static_cast<uint8_t>(currentTable)][i].r;
}

const char* PhonePinball::tableName(TableId t) {
	switch(t) {
		case TableId::Classic: return "CLASSIC";
		case TableId::Cluster: return "CLUSTER";
	}
	return "?";
}

// ===========================================================================
// ctor / dtor
// ===========================================================================

PhonePinball::PhonePinball()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr) {

	// Defensive zero-init for sprite pools and per-tick state.
	for(uint8_t i = 0; i < BumperCount; ++i) {
		bumperSprites[i]    = nullptr;
		bumperHalos[i]      = nullptr;
		bumperFlashTicks[i] = 0;
	}
	for(uint8_t i = 0; i < FlipperDots; ++i) {
		leftFlipperDots[i]  = nullptr;
		rightFlipperDots[i] = nullptr;
	}

	// Full-screen container, no scrollbars, no padding -- same blank-canvas
	// pattern PhoneTetris / PhoneBounce / PhoneBrickBreaker use.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order. Status
	// bar / HUD / playfield / soft-keys overlay it without per-child opacity
	// gymnastics.
	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildHud();
	buildPlayfield();
	buildBumpers();
	buildFlippers();
	buildBall();
	buildOverlay();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("START");
	softKeys->setRight("BACK");

	enterIdle();
}

PhonePinball::~PhonePinball() {
	stopTickTimer();
	// All children are parented to obj; LVGL frees them recursively when
	// the screen's obj is destroyed by the LVScreen base destructor.
}

void PhonePinball::onStart() {
	Input::getInstance()->addListener(this);
}

void PhonePinball::onStop() {
	Input::getInstance()->removeListener(this);
	stopTickTimer();
}

// ===========================================================================
// build helpers
// ===========================================================================

void PhonePinball::buildHud() {
	// Left HUD slot -- "BALL n" so the player can see lives at a glance
	// even with the cream stars on the right.
	hudLeftLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudLeftLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudLeftLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(hudLeftLabel, "BALL 1");
	lv_obj_set_pos(hudLeftLabel, 4, 12);

	// Centred title.
	titleLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(titleLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(titleLabel, MP_ACCENT, 0);
	lv_label_set_text(titleLabel, "PINBALL");
	lv_obj_set_align(titleLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(titleLabel, 12);

	// Lives strip -- compact pixelbasic7 stars right above the score.
	livesLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(livesLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(livesLabel, MP_TEXT, 0);
	lv_label_set_text(livesLabel, "***");
	lv_obj_set_align(livesLabel, LV_ALIGN_TOP_RIGHT);
	lv_obj_set_pos(livesLabel, -34, 12);

	// Numeric score, right-anchored.
	scoreLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(scoreLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(scoreLabel, MP_LABEL_DIM, 0);
	lv_obj_set_style_text_align(scoreLabel, LV_TEXT_ALIGN_RIGHT, 0);
	lv_label_set_text(scoreLabel, "0");
	lv_obj_set_align(scoreLabel, LV_ALIGN_TOP_RIGHT);
	lv_obj_set_pos(scoreLabel, -3, 12);
}

void PhonePinball::buildPlayfield() {
	// Playfield container -- thin MP_DIM border so the ball reads as
	// "in a chamber" against the synthwave wallpaper. Children pin
	// themselves with absolute coords inside the screen (not the
	// playfield) so we keep the same coordinate space for collision
	// math and rendering.
	playfield = lv_obj_create(obj);
	lv_obj_remove_style_all(playfield);
	lv_obj_set_size(playfield, PlayfieldW, PlayfieldH);
	lv_obj_set_pos(playfield, PlayfieldX, PlayfieldY);
	lv_obj_set_style_bg_color(playfield, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(playfield, LV_OPA_50, 0);
	lv_obj_set_style_border_color(playfield, MP_DIM, 0);
	lv_obj_set_style_border_width(playfield, 1, 0);
	lv_obj_set_style_pad_all(playfield, 0, 0);
	lv_obj_clear_flag(playfield, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(playfield, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(playfield, LV_OBJ_FLAG_IGNORE_LAYOUT);

	// Decorative floors flanking the drain. They make the gap read as
	// "the only way out" without us having to draw extra collidable
	// geometry -- the side walls are still flat verticals, the floor
	// strips just hint at the table apron.
	const lv_coord_t floorY = PlayfieldY + PlayfieldH - 4;
	leftFloor = lv_obj_create(obj);
	lv_obj_remove_style_all(leftFloor);
	lv_obj_set_size(leftFloor, 28, 3);
	lv_obj_set_pos(leftFloor, PlayfieldX + 1, floorY);
	lv_obj_set_style_bg_color(leftFloor, MP_DIM, 0);
	lv_obj_set_style_bg_opa(leftFloor, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(leftFloor, 0, 0);
	lv_obj_set_style_radius(leftFloor, 0, 0);
	lv_obj_clear_flag(leftFloor, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(leftFloor, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(leftFloor, LV_OBJ_FLAG_IGNORE_LAYOUT);

	rightFloor = lv_obj_create(obj);
	lv_obj_remove_style_all(rightFloor);
	lv_obj_set_size(rightFloor, 28, 3);
	lv_obj_set_pos(rightFloor, PlayfieldX + PlayfieldW - 29, floorY);
	lv_obj_set_style_bg_color(rightFloor, MP_DIM, 0);
	lv_obj_set_style_bg_opa(rightFloor, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(rightFloor, 0, 0);
	lv_obj_set_style_radius(rightFloor, 0, 0);
	lv_obj_clear_flag(rightFloor, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(rightFloor, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(rightFloor, LV_OBJ_FLAG_IGNORE_LAYOUT);
}

void PhonePinball::buildBumpers() {
	// Allocate the maximum-sized sprite pool once; applyTable() shows,
	// hides, and repositions sprites when the table changes (S86).
	for(uint8_t i = 0; i < BumperCount; ++i) {
		auto* halo = lv_obj_create(obj);
		lv_obj_remove_style_all(halo);
		lv_obj_set_size(halo, 6, 6);
		lv_obj_set_pos(halo, -20, -20);
		lv_obj_set_style_bg_color(halo, MP_HIGHLIGHT, 0);
		lv_obj_set_style_bg_opa(halo, LV_OPA_TRANSP, 0);
		lv_obj_set_style_border_color(halo, MP_TEXT, 0);
		lv_obj_set_style_border_width(halo, 1, 0);
		lv_obj_set_style_border_opa(halo, LV_OPA_TRANSP, 0);
		lv_obj_set_style_radius(halo, LV_RADIUS_CIRCLE, 0);
		lv_obj_clear_flag(halo, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(halo, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(halo, LV_OBJ_FLAG_IGNORE_LAYOUT);
		bumperHalos[i] = halo;

		auto* disc = lv_obj_create(obj);
		lv_obj_remove_style_all(disc);
		lv_obj_set_size(disc, 6, 6);
		lv_obj_set_pos(disc, -20, -20);
		lv_obj_set_style_bg_color(disc, MP_ACCENT, 0);
		lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
		lv_obj_set_style_border_color(disc, MP_TEXT, 0);
		lv_obj_set_style_border_width(disc, 1, 0);
		lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
		lv_obj_clear_flag(disc, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(disc, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(disc, LV_OBJ_FLAG_IGNORE_LAYOUT);
		bumperSprites[i] = disc;
	}
}

void PhonePinball::buildFlippers() {
	// Each flipper is rendered as FlipperDots small 3x3 dot sprites
	// positioned along the line from pivot to tip. The actual position
	// of each dot is computed in renderFlipper() because the tip moves
	// when the flipper toggles between rest and active.
	for(uint8_t side = 0; side < 2; ++side) {
		for(uint8_t i = 0; i < FlipperDots; ++i) {
			auto* d = lv_obj_create(obj);
			lv_obj_remove_style_all(d);
			lv_obj_set_size(d, 3, 3);
			lv_obj_set_pos(d, -10, -10);
			lv_obj_set_style_bg_color(d, MP_TEXT, 0);
			lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
			lv_obj_set_style_border_width(d, 0, 0);
			lv_obj_set_style_radius(d, 1, 0);
			lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_flag(d, LV_OBJ_FLAG_IGNORE_LAYOUT);
			if(side == 0) leftFlipperDots[i]  = d;
			else          rightFlipperDots[i] = d;
		}
	}
}

void PhonePinball::buildBall() {
	ball = lv_obj_create(obj);
	lv_obj_remove_style_all(ball);
	lv_obj_set_size(ball, BallSize, BallSize);
	lv_obj_set_pos(ball, -10, -10);
	lv_obj_set_style_bg_color(ball, MP_HIGHLIGHT, 0);
	lv_obj_set_style_bg_opa(ball, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(ball, MP_TEXT, 0);
	lv_obj_set_style_border_width(ball, 0, 0);
	lv_obj_set_style_radius(ball, LV_RADIUS_CIRCLE, 0);
	lv_obj_clear_flag(ball, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(ball, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(ball, LV_OBJ_FLAG_IGNORE_LAYOUT);
}

void PhonePinball::buildOverlay() {
	overlayLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(overlayLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(overlayLabel, MP_TEXT, 0);
	lv_obj_set_style_text_align(overlayLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(overlayLabel, "");
	lv_obj_set_align(overlayLabel, LV_ALIGN_CENTER);
	lv_obj_set_y(overlayLabel, -4);

	// S86 -- table-name caption shown only on the Idle screen. Pinned
	// to the strip just below the HUD and above the top bumpers (the
	// uppermost bumper centre lives at screen y ~= 48 in the Cluster
	// preset, so we have room for two compact lines of pixelbasic7).
	tableLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(tableLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(tableLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_style_text_align(tableLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(tableLabel, "");
	lv_obj_set_align(tableLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(tableLabel, 21);

	// S86 -- top-LeaderboardSize preview ("TOP 1234 / 800 / 500"),
	// shown both on the Idle screen and below the GameOver overlay so
	// the player can see how this run ranked against earlier ones.
	leaderboardLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(leaderboardLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(leaderboardLabel, MP_LABEL_DIM, 0);
	lv_obj_set_style_text_align(leaderboardLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(leaderboardLabel, "");
	lv_obj_set_align(leaderboardLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(leaderboardLabel, 30);
}

// ===========================================================================
// state transitions
// ===========================================================================

void PhonePinball::enterIdle() {
	state = GameState::Idle;
	stopTickTimer();
	leftActive  = false;
	rightActive = false;
	score = 0;
	lives = StartLives;
	for(uint8_t i = 0; i < BumperCount; ++i) bumperFlashTicks[i] = 0;
	applyTable();
	resetBall();
	render();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
	refreshTableLabel();
	refreshLeaderboardLabel();
}

void PhonePinball::startGame() {
	state = GameState::Playing;
	startTickTimer();
	refreshSoftKeys();
	refreshOverlay();
}

void PhonePinball::pauseGame() {
	state = GameState::Paused;
	stopTickTimer();
	refreshSoftKeys();
	refreshOverlay();
}

void PhonePinball::resumeGame() {
	state = GameState::Playing;
	startTickTimer();
	refreshSoftKeys();
	refreshOverlay();
}

void PhonePinball::endGame() {
	state = GameState::GameOver;
	stopTickTimer();
	leftActive  = false;
	rightActive = false;
	// S86 -- commit the run's score to the active table's leaderboard
	// before refreshing the overlay so the GameOver screen reads the
	// final ranking.
	recordScore(score);
	refreshSoftKeys();
	refreshOverlay();
	refreshLeaderboardLabel();
}

// ===========================================================================
// core game ops
// ===========================================================================

void PhonePinball::resetBall() {
	// Park ball just below the top wall, slightly off-centre so the
	// initial drop is more interesting than "dead-centre into the
	// middle bumper". Stuck until the player hits ENTER.
	ballStuck = true;
	const int16_t spawnX = 80;
	const int16_t spawnY = PlayfieldY + 6;
	ballXQ8  = static_cast<int32_t>(spawnX) * Q8;
	ballYQ8  = static_cast<int32_t>(spawnY) * Q8;
	ballVxQ8 = 0;
	ballVyQ8 = 0;
}

void PhonePinball::launchBall() {
	if(!ballStuck) return;
	ballStuck = false;

	// Tiny X nudge so successive launches don't follow the exact same
	// path. Range: -64 .. +64 in Q8 (about +/- 0.25 px/tick).
	const uint16_t roll = xorshift16(rngState);
	const int16_t  nudge = static_cast<int16_t>((roll % 129) - 64);
	ballVxQ8 = nudge;
	ballVyQ8 = static_cast<int16_t>(Q8);   // 1 px/tick downward
}

void PhonePinball::physicsStep() {
	if(state != GameState::Playing) return;

	if(!ballStuck) {
		integrate();
		collideWalls();
		collideBumpers();
		collideFlippers();
		clampSpeed();

		// Drain check: ball entirely below the bottom of the playfield.
		const int16_t by = static_cast<int16_t>(ballYQ8 / Q8);
		if(by >= PlayfieldY + PlayfieldH) {
			loseLife();
		}
	}

	// Tick down per-bumper hit-flash timers regardless of stuck state so
	// the visual decays even if the player is fiddling with the launch.
	for(uint8_t i = 0; i < BumperCount; ++i) {
		if(bumperFlashTicks[i] > 0) --bumperFlashTicks[i];
	}

	render();
}

void PhonePinball::integrate() {
	// Apply gravity, then integrate position. Vy clamping happens later
	// in clampSpeed().
	ballVyQ8 += GravityQ8;
	ballXQ8  += ballVxQ8;
	ballYQ8  += ballVyQ8;
}

void PhonePinball::collideWalls() {
	int16_t bx = static_cast<int16_t>(ballXQ8 / Q8);
	int16_t by = static_cast<int16_t>(ballYQ8 / Q8);

	const int16_t leftWall  = PlayfieldX + 1;
	const int16_t rightWall = PlayfieldX + PlayfieldW - 1 - BallSize;
	const int16_t topWall   = PlayfieldY + 1;

	if(bx < leftWall) {
		bx = leftWall;
		ballXQ8 = static_cast<int32_t>(bx) * Q8;
		if(ballVxQ8 < 0) ballVxQ8 = -ballVxQ8;
	}
	if(bx > rightWall) {
		bx = rightWall;
		ballXQ8 = static_cast<int32_t>(bx) * Q8;
		if(ballVxQ8 > 0) ballVxQ8 = -ballVxQ8;
	}
	if(by < topWall) {
		by = topWall;
		ballYQ8 = static_cast<int32_t>(by) * Q8;
		if(ballVyQ8 < 0) ballVyQ8 = -ballVyQ8;
		score += TopWallScore;
	}
}

void PhonePinball::collideBumpers() {
	const int16_t bcx = static_cast<int16_t>(ballXQ8 / Q8) + BallRadius;
	const int16_t bcy = static_cast<int16_t>(ballYQ8 / Q8) + BallRadius;

	const uint8_t count = activeBumperCount();
	for(uint8_t i = 0; i < count; ++i) {
		const int16_t cx = bumperCx(i);
		const int16_t cy = PlayfieldY + bumperCy(i);
		const int16_t r  = bumperRadius(i);

		const int32_t dx    = static_cast<int32_t>(bcx - cx);
		const int32_t dy    = static_cast<int32_t>(bcy - cy);
		const int32_t distSq = dx * dx + dy * dy;
		const int32_t sumR  = static_cast<int32_t>(r + BallRadius);
		if(distSq >= sumR * sumR) continue;

		// Compute distance + unit normal pointing away from the bumper.
		const uint32_t distU = isqrt32(static_cast<uint32_t>(distSq));
		const int32_t  dist  = (distU == 0) ? 1 : static_cast<int32_t>(distU);

		// Unit normal in Q8 (avoid divide-by-zero when ball spawned
		// exactly on top of a bumper).
		int32_t nxQ8 = (dx * Q8) / dist;
		int32_t nyQ8 = (dy * Q8) / dist;
		if(nxQ8 == 0 && nyQ8 == 0) {
			nyQ8 = -Q8;   // shove straight up if perfectly overlapped
		}

		// Reflect velocity along the normal: V' = V - 2*(V . N)*N
		// All math in Q8; we store the dot product in Q8 too.
		int32_t vdotn = (static_cast<int32_t>(ballVxQ8) * nxQ8 +
		                 static_cast<int32_t>(ballVyQ8) * nyQ8) / Q8;
		ballVxQ8 = static_cast<int16_t>(ballVxQ8 - (2 * vdotn * nxQ8) / Q8);
		ballVyQ8 = static_cast<int16_t>(ballVyQ8 - (2 * vdotn * nyQ8) / Q8);

		// Tiny "kick" along the normal so soft taps still feel lively
		// (classic bumper behaviour -- a real pinball bumper adds energy).
		ballVxQ8 = static_cast<int16_t>(ballVxQ8 + (nxQ8 / 2));
		ballVyQ8 = static_cast<int16_t>(ballVyQ8 + (nyQ8 / 2));

		// Push the ball out of the bumper along the normal so we never
		// register a second collision next tick from the same overlap.
		const int32_t push = sumR + 1;
		const int16_t newCx = static_cast<int16_t>(cx + (nxQ8 * push) / Q8);
		const int16_t newCy = static_cast<int16_t>(cy + (nyQ8 * push) / Q8);
		ballXQ8 = static_cast<int32_t>(newCx - BallRadius) * Q8;
		ballYQ8 = static_cast<int32_t>(newCy - BallRadius) * Q8;

		bumperFlashTicks[i] = HitFlashTicks;
		score += BumperScore;
		break;   // one bumper per tick keeps the trajectory predictable
	}
}

void PhonePinball::collideFlippers() {
	// Try left flipper first; if it didn't connect, fall through to the
	// right one. Allowing both to fire on the same tick is unusual
	// enough geometrically that limiting to one keeps things stable.
	if(collideOneFlipper(/*isLeft=*/true)) return;
	collideOneFlipper(/*isLeft=*/false);
}

bool PhonePinball::collideOneFlipper(bool isLeft) {
	int16_t tipX, tipY;
	getFlipperTip(isLeft, tipX, tipY);

	const int16_t ax = isLeft ? LeftPivotX : RightPivotX;
	const int16_t ay = FlipperPivotY;
	const int16_t bx = tipX;
	const int16_t by = tipY;

	const int16_t bcx = static_cast<int16_t>(ballXQ8 / Q8) + BallRadius;
	const int16_t bcy = static_cast<int16_t>(ballYQ8 / Q8) + BallRadius;

	const int32_t abx = bx - ax;
	const int32_t aby = by - ay;
	const int32_t apx = bcx - ax;
	const int32_t apy = bcy - ay;
	const int32_t lenSq = abx * abx + aby * aby;
	if(lenSq == 0) return false;

	// Project AP onto AB, clamp to segment.
	int32_t tQ8 = (apx * abx + apy * aby) * Q8 / lenSq;
	if(tQ8 < 0)   tQ8 = 0;
	if(tQ8 > Q8)  tQ8 = Q8;

	const int16_t qx = static_cast<int16_t>(ax + (tQ8 * abx) / Q8);
	const int16_t qy = static_cast<int16_t>(ay + (tQ8 * aby) / Q8);

	const int32_t dx    = bcx - qx;
	const int32_t dy    = bcy - qy;
	const int32_t distSq = dx * dx + dy * dy;
	// Flipper has a small visual half-thickness; collide as ballR + 2.
	const int32_t hitR  = BallRadius + 2;
	if(distSq >= hitR * hitR) return false;

	const uint32_t distU = isqrt32(static_cast<uint32_t>(distSq));
	const int32_t  dist  = (distU == 0) ? 1 : static_cast<int32_t>(distU);

	int32_t nxQ8 = (dx * Q8) / dist;
	int32_t nyQ8 = (dy * Q8) / dist;
	if(nxQ8 == 0 && nyQ8 == 0) {
		nyQ8 = -Q8;
	}

	// Reflect velocity along the normal.
	int32_t vdotn = (static_cast<int32_t>(ballVxQ8) * nxQ8 +
	                 static_cast<int32_t>(ballVyQ8) * nyQ8) / Q8;
	ballVxQ8 = static_cast<int16_t>(ballVxQ8 - (2 * vdotn * nxQ8) / Q8);
	ballVyQ8 = static_cast<int16_t>(ballVyQ8 - (2 * vdotn * nyQ8) / Q8);

	// If the flipper is currently in its "active" (raised) state, give
	// the ball a serious upward kick. This is how the player turns
	// downward-falling balls back into upward attacks. The kick is
	// applied as an additional velocity along the normal so it
	// composes with the reflection rather than fighting it.
	const bool active = isLeft ? leftActive : rightActive;
	if(active) {
		const int16_t kick = static_cast<int16_t>(Q8);   // 1 px/tick boost
		ballVxQ8 = static_cast<int16_t>(ballVxQ8 + (nxQ8 * kick) / Q8);
		ballVyQ8 = static_cast<int16_t>(ballVyQ8 + (nyQ8 * kick) / Q8);
		// And clamp the upward velocity so a flicked ball always rises.
		if(ballVyQ8 > -Q8) ballVyQ8 = -Q8;
	}

	// Push ball out of the segment along the normal.
	const int32_t push = hitR + 1;
	const int16_t newCx = static_cast<int16_t>(qx + (nxQ8 * push) / Q8);
	const int16_t newCy = static_cast<int16_t>(qy + (nyQ8 * push) / Q8);
	ballXQ8 = static_cast<int32_t>(newCx - BallRadius) * Q8;
	ballYQ8 = static_cast<int32_t>(newCy - BallRadius) * Q8;

	score += FlipperScore;
	return true;
}

void PhonePinball::clampSpeed() {
	// Clamp each velocity component independently. Pinball doesn't
	// need true magnitude clamping -- per-axis is fine on a 160x128
	// integer grid and avoids the sqrt.
	if(ballVxQ8 >  MaxComponentQ8) ballVxQ8 =  MaxComponentQ8;
	if(ballVxQ8 < -MaxComponentQ8) ballVxQ8 = -MaxComponentQ8;
	if(ballVyQ8 >  MaxComponentQ8) ballVyQ8 =  MaxComponentQ8;
	if(ballVyQ8 < -MaxComponentQ8) ballVyQ8 = -MaxComponentQ8;
}

void PhonePinball::loseLife() {
	if(lives == 0) {
		endGame();
		return;
	}
	--lives;
	if(lives == 0) {
		endGame();
		return;
	}
	resetBall();
	refreshHud();
	refreshOverlay();
}

void PhonePinball::getFlipperTip(bool isLeft, int16_t& tipX, int16_t& tipY) const {
	const int16_t pivotX = isLeft ? LeftPivotX : RightPivotX;
	const int16_t pivotY = FlipperPivotY;
	const int16_t direction = isLeft ? +1 : -1;
	const bool active = isLeft ? leftActive : rightActive;
	tipX = pivotX + direction * FlipperLen;
	tipY = pivotY + (active ? -FlipperRise : +FlipperRise);
}

// ===========================================================================
// rendering
// ===========================================================================

void PhonePinball::render() {
	if(ball != nullptr) {
		const int16_t bx = static_cast<int16_t>(ballXQ8 / Q8);
		const int16_t by = static_cast<int16_t>(ballYQ8 / Q8);
		lv_obj_set_pos(ball, bx, by);
	}
	renderBumpers();
	renderFlipper(/*isLeft=*/true);
	renderFlipper(/*isLeft=*/false);
}

void PhonePinball::renderBumpers() {
	const uint8_t count = activeBumperCount();
	for(uint8_t i = 0; i < count; ++i) {
		auto* halo = bumperHalos[i];
		auto* disc = bumperSprites[i];
		if(halo == nullptr || disc == nullptr) continue;

		// Halo opacity fades with the flash counter so the burst feels
		// like a soft pulse rather than a hard on/off.
		const uint8_t t = bumperFlashTicks[i];
		const lv_opa_t opa = static_cast<lv_opa_t>((t * 200u) / HitFlashTicks);
		lv_obj_set_style_bg_opa(halo, opa, 0);

		// Tint the disc itself slightly toward cream during a flash so
		// the player sees the kill at the very centre of the action.
		const lv_color_t base = MP_ACCENT;
		lv_obj_set_style_bg_color(disc, t > 0 ? MP_TEXT : base, 0);
	}
}

void PhonePinball::renderFlipper(bool isLeft) {
	int16_t tipX, tipY;
	getFlipperTip(isLeft, tipX, tipY);
	const int16_t pivotX = isLeft ? LeftPivotX : RightPivotX;
	const int16_t pivotY = FlipperPivotY;

	auto** dots = isLeft ? leftFlipperDots : rightFlipperDots;
	const bool active = isLeft ? leftActive : rightActive;
	const lv_color_t restColor   = MP_TEXT;
	const lv_color_t activeColor = MP_HIGHLIGHT;
	const lv_color_t color = active ? activeColor : restColor;

	for(uint8_t i = 0; i < FlipperDots; ++i) {
		auto* d = dots[i];
		if(d == nullptr) continue;
		// Distribute dots from pivot (i = 0) to tip (i = N-1).
		const int32_t tQ8 = (static_cast<int32_t>(i) * Q8) /
		                    (FlipperDots - 1);
		const int16_t dx = static_cast<int16_t>(pivotX +
		                   (tQ8 * (tipX - pivotX)) / Q8);
		const int16_t dy = static_cast<int16_t>(pivotY +
		                   (tQ8 * (tipY - pivotY)) / Q8);
		// Centre the 3x3 dot on the computed point.
		lv_obj_set_pos(d, dx - 1, dy - 1);
		lv_obj_set_style_bg_color(d, color, 0);
	}
}

void PhonePinball::refreshHud() {
	if(hudLeftLabel != nullptr) {
		char buf[12];
		const uint8_t ballNum = static_cast<uint8_t>(StartLives - lives + 1);
		snprintf(buf, sizeof(buf), "BALL %u",
		         static_cast<unsigned>(ballNum > StartLives ? StartLives : ballNum));
		lv_label_set_text(hudLeftLabel, buf);
	}
	if(livesLabel != nullptr) {
		char buf[8];
		uint8_t n = (lives <= MaxLives) ? lives : MaxLives;
		uint8_t k = 0;
		for(; k < n && k < 7; ++k) buf[k] = '*';
		buf[k] = '\0';
		lv_label_set_text(livesLabel, buf);
	}
	if(scoreLabel != nullptr) {
		char buf[12];
		snprintf(buf, sizeof(buf), "%lu",
		         static_cast<unsigned long>(score));
		lv_label_set_text(scoreLabel, buf);
	}
}

void PhonePinball::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	switch(state) {
		case GameState::Idle:
			softKeys->setLeft("START");
			softKeys->setRight("BACK");
			break;
		case GameState::Playing:
			softKeys->setLeft(ballStuck ? "LAUNCH" : "PAUSE");
			softKeys->setRight("EXIT");
			break;
		case GameState::Paused:
			softKeys->setLeft("RESUME");
			softKeys->setRight("EXIT");
			break;
		case GameState::GameOver:
			softKeys->setLeft("RETRY");
			softKeys->setRight("BACK");
			break;
	}
}

void PhonePinball::refreshOverlay() {
	if(overlayLabel == nullptr) return;

	// S86 -- the table + leaderboard captions are useful to the player
	// only on the Idle (table-select) screen, so hide them everywhere
	// else. GameOver replaces them with its own score line below.
	const bool showTable =
		(state == GameState::Idle) && (tableLabel != nullptr);
	const bool showLeaderboard =
		(state == GameState::Idle || state == GameState::GameOver) &&
		(leaderboardLabel != nullptr);
	if(tableLabel != nullptr) {
		if(showTable) lv_obj_clear_flag(tableLabel, LV_OBJ_FLAG_HIDDEN);
		else          lv_obj_add_flag(tableLabel,   LV_OBJ_FLAG_HIDDEN);
	}
	if(leaderboardLabel != nullptr) {
		if(showLeaderboard) lv_obj_clear_flag(leaderboardLabel,
		                                      LV_OBJ_FLAG_HIDDEN);
		else                lv_obj_add_flag(leaderboardLabel,
		                                    LV_OBJ_FLAG_HIDDEN);
	}

	switch(state) {
		case GameState::Idle:
			lv_label_set_text(overlayLabel, "PRESS\nSTART");
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
		case GameState::Playing:
			if(ballStuck) {
				lv_label_set_text(overlayLabel, "LAUNCH!");
				lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			} else {
				lv_obj_add_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			}
			break;
		case GameState::Paused:
			lv_label_set_text(overlayLabel, "PAUSED");
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
		case GameState::GameOver: {
			// S86 -- "GAME OVER\n<score>" so the player sees the run's
			// score front-and-centre; the leaderboard label below the
			// overlay shows the new top-3 ranking.
			char buf[28];
			snprintf(buf, sizeof(buf), "GAME OVER\n%lu",
			         static_cast<unsigned long>(score));
			lv_label_set_text(overlayLabel, buf);
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
		}
	}
}

// ===========================================================================
// S86 -- table select + leaderboard
// ===========================================================================

void PhonePinball::cycleTable(int8_t delta) {
	int8_t v = static_cast<int8_t>(currentTable) + delta;
	while(v < 0)               v += TableCount;
	while(v >= TableCount)     v -= TableCount;
	currentTable = static_cast<TableId>(v);
	applyTable();
	resetBall();
	render();
	refreshTableLabel();
	refreshLeaderboardLabel();
}

void PhonePinball::applyTable() {
	const uint8_t count = activeBumperCount();
	for(uint8_t i = 0; i < BumperCount; ++i) {
		auto* halo = bumperHalos[i];
		auto* disc = bumperSprites[i];
		if(halo == nullptr || disc == nullptr) continue;

		if(i < count) {
			const int16_t r  = bumperRadius(i);
			const int16_t cx = bumperCx(i);
			const int16_t cy = PlayfieldY + bumperCy(i);

			lv_obj_set_size(halo, (r + 2) * 2, (r + 2) * 2);
			lv_obj_set_pos(halo, cx - r - 2, cy - r - 2);
			lv_obj_set_style_bg_opa(halo, LV_OPA_TRANSP, 0);
			lv_obj_clear_flag(halo, LV_OBJ_FLAG_HIDDEN);

			lv_obj_set_size(disc, r * 2, r * 2);
			lv_obj_set_pos(disc, cx - r, cy - r);
			lv_obj_set_style_bg_color(disc, MP_ACCENT, 0);
			lv_obj_clear_flag(disc, LV_OBJ_FLAG_HIDDEN);
		} else {
			lv_obj_add_flag(halo, LV_OBJ_FLAG_HIDDEN);
			lv_obj_add_flag(disc, LV_OBJ_FLAG_HIDDEN);
		}
		bumperFlashTicks[i] = 0;
	}
}

void PhonePinball::refreshTableLabel() {
	if(tableLabel == nullptr) return;
	char buf[24];
	snprintf(buf, sizeof(buf), "<  TABLE: %s  >", tableName(currentTable));
	lv_label_set_text(tableLabel, buf);
}

void PhonePinball::refreshLeaderboardLabel() {
	if(leaderboardLabel == nullptr) return;
	const uint8_t idx = static_cast<uint8_t>(currentTable);
	uint32_t a = leaderboard[idx][0];
	uint32_t b = (LeaderboardSize > 1) ? leaderboard[idx][1] : 0;
	uint32_t c = (LeaderboardSize > 2) ? leaderboard[idx][2] : 0;
	char buf[40];
	if(a == 0 && b == 0 && c == 0) {
		snprintf(buf, sizeof(buf), "TOP: --- / --- / ---");
	} else {
		snprintf(buf, sizeof(buf), "TOP: %lu / %lu / %lu",
		         static_cast<unsigned long>(a),
		         static_cast<unsigned long>(b),
		         static_cast<unsigned long>(c));
	}
	lv_label_set_text(leaderboardLabel, buf);
}

void PhonePinball::recordScore(uint32_t finalScore) {
	if(finalScore == 0) return;   // don't pollute the board with zeros
	const uint8_t idx = static_cast<uint8_t>(currentTable);
	for(uint8_t i = 0; i < LeaderboardSize; ++i) {
		if(finalScore > leaderboard[idx][i]) {
			// Shift everything from i..end-1 down by one slot.
			for(uint8_t j = LeaderboardSize - 1; j > i; --j) {
				leaderboard[idx][j] = leaderboard[idx][j - 1];
			}
			leaderboard[idx][i] = finalScore;
			return;
		}
	}
}

uint32_t PhonePinball::bestScoreForCurrentTable() const {
	const uint8_t idx = static_cast<uint8_t>(currentTable);
	return leaderboard[idx][0];
}

// ===========================================================================
// timers
// ===========================================================================

void PhonePinball::startTickTimer() {
	if(tickTimer != nullptr) return;
	tickTimer = lv_timer_create(&PhonePinball::onTickTimerStatic,
	                            TickMs, this);
}

void PhonePinball::stopTickTimer() {
	if(tickTimer != nullptr) {
		lv_timer_del(tickTimer);
		tickTimer = nullptr;
	}
}

void PhonePinball::onTickTimerStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhonePinball*>(timer->user_data);
	if(self == nullptr) return;
	if(self->state != GameState::Playing) return;
	self->physicsStep();
}

// ===========================================================================
// input
// ===========================================================================

void PhonePinball::buttonPressed(uint i) {
	switch(state) {
		case GameState::Idle:
			if(i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				startGame();
				refreshOverlay();
			} else if(i == BTN_BACK) {
				if(softKeys) softKeys->flashRight();
				pop();
			} else if(i == BTN_LEFT || i == BTN_4) {
				// S86 -- L/R cycles the active table while idle.
				cycleTable(-1);
			} else if(i == BTN_RIGHT || i == BTN_6) {
				cycleTable(+1);
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
				enterIdle();
			} else if(i == BTN_BACK) {
				if(softKeys) softKeys->flashRight();
				pop();
			}
			return;

		case GameState::Playing:
			break;   // handled below
	}

	switch(i) {
		case BTN_LEFT:
		case BTN_4:
			leftActive = true;
			renderFlipper(true);
			break;
		case BTN_RIGHT:
		case BTN_6:
			rightActive = true;
			renderFlipper(false);
			break;
		case BTN_ENTER:
			if(softKeys) softKeys->flashLeft();
			if(ballStuck) {
				launchBall();
				refreshSoftKeys();
				refreshOverlay();
			} else {
				pauseGame();
			}
			break;
		case BTN_BACK:
			if(softKeys) softKeys->flashRight();
			pop();
			break;
		default:
			break;
	}
}

void PhonePinball::buttonReleased(uint i) {
	switch(i) {
		case BTN_LEFT:
		case BTN_4:
			leftActive = false;
			renderFlipper(true);
			break;
		case BTN_RIGHT:
		case BTN_6:
			rightActive = false;
			renderFlipper(false);
			break;
		default:
			break;
	}
}
