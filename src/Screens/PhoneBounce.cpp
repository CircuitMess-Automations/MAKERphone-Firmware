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

// ---------- level layout -------------------------------------------------
//
// One uint8_t per column. The value is the ground height in tiles (0..N).
// 0 means "no ground -- gap in the floor"; values >= 1 stack solid tiles
// from the bottom of the playfield upwards.
//
// 56 columns is a deliberately short single level for S73. The roadmap
// spends S74 on additional levels + collectibles, so the right move here
// is to ship one level that exercises every shape (flat ground, low
// step-up, step-down, gap, low platform) and call it done.
//
// Profile (read left to right):
//   - 5 cols of 1-tile floor                 (warm-up)
//   - 1-col gap                              (mini hop)
//   - 6 cols of 1-tile floor
//   - 3-col gap                              (real jump)
//   - 4 cols stepping 1->2->3->2 (small hill)
//   - 2 cols of 1-tile floor
//   - 2-col gap
//   - 8 cols of 1-tile floor
//   - 4-col gap (the big jump -- needs forward thrust)
//   - 6 cols stepping 1->2->2->1 (low plateau)
//   - 3-col gap
//   - 8 cols of 1-tile floor (run-up to the goal)
//   - 6 cols of 2-tile floor under the goal flag
//
// Total = 56. The right edge is the "win" column.

namespace {

constexpr uint8_t kLevel[PhoneBounce::LevelTiles] = {
	// 0..4 warm-up
	1, 1, 1, 1, 1,
	// 5 gap
	0,
	// 6..11 floor
	1, 1, 1, 1, 1, 1,
	// 12..14 gap
	0, 0, 0,
	// 15..18 small hill
	1, 2, 3, 2,
	// 19..20 floor
	1, 1,
	// 21..22 gap
	0, 0,
	// 23..30 floor
	1, 1, 1, 1, 1, 1, 1, 1,
	// 31..34 big gap
	0, 0, 0, 0,
	// 35..40 low plateau
	1, 2, 2, 2, 2, 1,
	// 41..43 gap
	0, 0, 0,
	// 44..51 run-up floor
	1, 1, 1, 1, 1, 1, 1, 1,
	// 52..55 goal pad
	2, 2, 2, 2,
};

// Bottom of the playfield, in screen-space px. The playfield begins
// at PhoneBounce::PlayfieldY and is PhoneBounce::PlayfieldH tall.
constexpr lv_coord_t kPlayfieldBottom =
	PhoneBounce::PlayfieldY + PhoneBounce::PlayfieldH;

// Convert a tile column / row to screen-space pixel coordinates.
// `row 0` = top of the visible playfield.
inline lv_coord_t tileScreenY(uint8_t row) {
	return PhoneBounce::PlayfieldY + row * PhoneBounce::TileSize;
}

// Solid surface Y (top of the ground stack) for a given column, in
// screen-space px. If the column is a gap, returns the bottom edge of
// the playfield + 1 (i.e. "below the world").
inline lv_coord_t columnTopY(uint16_t col) {
	if(col >= PhoneBounce::LevelTiles) {
		return kPlayfieldBottom + 1;
	}
	const uint8_t h = kLevel[col];
	if(h == 0) {
		return kPlayfieldBottom + 1;
	}
	return kPlayfieldBottom - h * PhoneBounce::TileSize;
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

	// Full-screen blank canvas. Same pattern PhoneTetris / PhoneCalculator
	// use - status bar + title + body + soft-key bar are all positioned
	// manually rather than relying on LVGL's flex layout.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of the LVGL z-order. The
	// playfield, ball, status bar and soft-keys all overlay it without
	// per-child opacity gymnastics.
	wallpaper = new PhoneSynthwaveBg(obj);

	statusBar = new PhoneStatusBar(obj);

	buildTitle();
	buildPlayfield();
	buildBall();
	buildOverlay();

	softKeys = new PhoneSoftKeyBar(obj);

	// Initial state: idle on level start with the overlay showing.
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

void PhoneBounce::buildTitle() {
	titleLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(titleLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(titleLabel, MP_ACCENT, 0);
	lv_label_set_text(titleLabel, "BOUNCE");
	lv_obj_set_align(titleLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(titleLabel, 12);

	scoreLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(scoreLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(scoreLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(scoreLabel, "0");
	lv_obj_set_pos(scoreLabel, 138, 12);
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
	resetBall();
	cameraX = 0;
	score = 0;
	furthestColumn = 0;
	render();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneBounce::startGame() {
	state = GameState::Playing;
	resetBall();
	cameraX = 0;
	score = 0;
	furthestColumn = 0;
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

void PhoneBounce::winGame() {
	// Time bonus -- generous floor, but deliberately easy to beat after
	// one good run so the player has something to chase.
	const uint32_t elapsedMs = (startTickMs == 0) ? 0 : (millis() - startTickMs);
	const uint32_t elapsedSec = elapsedMs / 1000;
	const uint32_t penalty = elapsedSec * 10;
	const uint32_t bonus = (penalty >= 600) ? 0 : (600 - penalty);
	score += bonus;

	state = GameState::Won;
	stopTickTimer();
	holdLeft = false;
	holdRight = false;
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

uint8_t PhoneBounce::levelHeightAt(uint16_t column) const {
	if(column >= LevelTiles) return 0;
	return kLevel[column];
}

bool PhoneBounce::columnSolid(uint16_t column, uint8_t row) const {
	if(column >= LevelTiles) return false;
	const uint8_t h = kLevel[column];
	if(h == 0) return false;
	// Tile rows from the bottom of the playfield upwards are "solid"
	// when row index counted from the bottom is < h. Convert: row 0 is
	// at the top, so the bottom-most row is ViewRows - 1.
	if(row >= ViewRows) return false;
	const uint8_t fromBottom = (ViewRows - 1) - row;
	return fromBottom < h;
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

	// Clamp ball to level bounds in X. The level is LevelTiles*TileSize
	// pixels wide. Past the right edge means "we won this run".
	const int32_t levelWidthPx = static_cast<int32_t>(LevelTiles) * TileSize;
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
	if(currentCol > furthestColumn && currentCol < LevelTiles) {
		const uint16_t delta = currentCol - furthestColumn;
		furthestColumn = currentCol;
		score += delta;
		refreshHud();
	}

	// Camera follow -- keep the ball roughly in the middle third of the
	// 160 px viewport. The camera is clamped to the level bounds.
	const int16_t targetCamera = static_cast<int16_t>(ballScreenX) - 80;
	int16_t maxCamera =
		static_cast<int16_t>(static_cast<int32_t>(LevelTiles) * TileSize - 160);
	if(maxCamera < 0) maxCamera = 0;
	if(targetCamera < 0) cameraX = 0;
	else if(targetCamera > maxCamera) cameraX = maxCamera;
	else cameraX = targetCamera;

	render();

	if(reachedRight) {
		winGame();
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

	// Tile sprites: paint the columns visible under the current camera.
	// Visible range is [cameraX, cameraX + 160). Columns outside the
	// level bounds are rendered as hidden slots.
	const int16_t startCol = cameraX / TileSize;
	for(uint8_t i = 0; i < TileSpritePoolSize; ++i) {
		auto* spr = tileSprites[i];
		if(spr == nullptr) continue;

		const int16_t col = startCol + i;
		if(col < 0 || col >= static_cast<int16_t>(LevelTiles)) {
			lv_obj_add_flag(spr, LV_OBJ_FLAG_HIDDEN);
			continue;
		}
		const uint8_t h = kLevel[col];
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

	// Goal flag at the very last column.
	if(goalFlag != nullptr) {
		const int16_t goalCol = LevelTiles - 1;
		const uint8_t goalH = kLevel[goalCol];
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
		// ballYQ8 is the centre of the ball, expressed in screen-space
		// (playfield uses absolute Y because columnTopY returns
		// absolute Y). Convert back to playfield-local Y here so the
		// ball ends up positioned correctly inside the playfield.
		const lv_coord_t ballLocalY =
			static_cast<lv_coord_t>(ballWorldY - PlayfieldY) - BallRadius;
		lv_obj_set_pos(ball, ballLocalX, ballLocalY);
	}
}

void PhoneBounce::refreshHud() {
	if(scoreLabel == nullptr) return;
	char buf[12];
	snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(score));
	lv_label_set_text(scoreLabel, buf);
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
			softKeys->setLeft("AGAIN");
			softKeys->setRight("BACK");
			break;
	}
}

void PhoneBounce::refreshOverlay() {
	if(overlayLabel == nullptr) return;
	char buf[40];
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
			snprintf(buf, sizeof(buf), "GOAL!\nSCORE %lu",
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
		case GameState::Won:
			if(i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				enterIdle();
				startGame();
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
