#include "PhoneBrickBreaker.h"

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
// arcade screen slots in beside PhoneTetris (S71/S72), PhoneBounce (S73/S74)
// and the rest of the device without a visual seam. Inlined per the
// established pattern (see PhoneBounce.cpp / PhoneTetris.cpp).
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)  // dim purple captions

namespace {

// Tiny xorshift PRNG so the level plan is deterministic per-seed
// regardless of what the platform's `random()` is doing. Keeps power-up
// drops repeatable on retry (memorisable -- a Breakout staple).
inline uint16_t xorshift16(uint16_t& s) {
	uint16_t x = s ? s : 0xACE1u;
	x ^= x << 7;
	x ^= x >> 9;
	x ^= x << 8;
	s = x;
	return x;
}

inline int16_t sgn(int32_t v) {
	return (v > 0) ? 1 : ((v < 0) ? -1 : 0);
}

inline int16_t absI16(int16_t v) { return v < 0 ? -v : v; }

} // namespace

// ===========================================================================
// ctor / dtor
// ===========================================================================

PhoneBrickBreaker::PhoneBrickBreaker()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr) {

	// Defensive zero-init for the sprite pools so render() can rely on
	// `nullptr` meaning "uninitialised".
	for(uint8_t i = 0; i < BrickCount; ++i) {
		brickSprites[i]   = nullptr;
		brickAlive[i]     = false;
		brickPowerUp[i]   = PowerUpKind::None;
	}
	for(uint8_t i = 0; i < MaxActivePowerUps; ++i) {
		tokenSprites[i] = nullptr;
		tokenLabels[i]  = nullptr;
		tokens[i] = Token{};
	}

	// Full-screen container, no scrollbars, no padding -- same blank-canvas
	// pattern PhoneTetris / PhoneBounce / PhoneCalculator use.
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
	buildBricks();
	buildPaddle();
	buildBall();
	buildTokens();
	buildOverlay();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("START");
	softKeys->setRight("BACK");

	enterIdle();
}

PhoneBrickBreaker::~PhoneBrickBreaker() {
	stopTickTimer();
	// All children are parented to obj; LVGL frees them recursively when
	// the screen's obj is destroyed by the LVScreen base destructor.
}

void PhoneBrickBreaker::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneBrickBreaker::onStop() {
	Input::getInstance()->removeListener(this);
	stopTickTimer();
}

// ===========================================================================
// build helpers
// ===========================================================================

void PhoneBrickBreaker::buildHud() {
	// Level indicator (left). pixelbasic7 sits flush in the 10-px HUD strip.
	hudLeftLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudLeftLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudLeftLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(hudLeftLabel, "L1");
	lv_obj_set_pos(hudLeftLabel, 4, 12);

	// Centred title.
	titleLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(titleLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(titleLabel, MP_ACCENT, 0);
	lv_label_set_text(titleLabel, "BRICK");
	lv_obj_set_align(titleLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(titleLabel, 12);

	// Lives indicator -- tiny dim-cream squares above the score. We just
	// use a label with up to MaxLives glyphs ("***" etc.) because a
	// pixelbasic7 asterisk is exactly the right vibe for this size.
	livesLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(livesLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(livesLabel, MP_TEXT, 0);
	lv_label_set_text(livesLabel, "***");
	lv_obj_set_align(livesLabel, LV_ALIGN_TOP_RIGHT);
	lv_obj_set_pos(livesLabel, -34, 12);

	// Right-anchored numeric score so a 4-digit score doesn't crowd the
	// title. -3 keeps the rightmost glyph from kissing the bezel.
	scoreLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(scoreLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(scoreLabel, MP_LABEL_DIM, 0);
	lv_obj_set_style_text_align(scoreLabel, LV_TEXT_ALIGN_RIGHT, 0);
	lv_label_set_text(scoreLabel, "0");
	lv_obj_set_align(scoreLabel, LV_ALIGN_TOP_RIGHT);
	lv_obj_set_pos(scoreLabel, -3, 12);
}

void PhoneBrickBreaker::buildPlayfield() {
	// Playfield container -- thin MP_DIM border so the ball reads as
	// "in a chamber" against the synthwave wallpaper. Children pin
	// themselves with absolute coords inside the container.
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
}

void PhoneBrickBreaker::buildBricks() {
	for(uint8_t i = 0; i < BrickCount; ++i) {
		const uint8_t row = i / BrickCols;
		const uint8_t col = i % BrickCols;
		const lv_coord_t x = BrickAreaX + col * (BrickW + BrickGapX);
		const lv_coord_t y = BrickAreaY + row * (BrickH + BrickGapY);

		auto* b = lv_obj_create(obj);
		lv_obj_remove_style_all(b);
		lv_obj_set_size(b, BrickW, BrickH);
		lv_obj_set_pos(b, x, y);
		lv_obj_set_style_bg_color(b, brickColor(row), 0);
		lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
		lv_obj_set_style_border_width(b, 0, 0);
		lv_obj_set_style_radius(b, 0, 0);
		lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(b, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
		brickSprites[i] = b;
	}
}

void PhoneBrickBreaker::buildPaddle() {
	paddle = lv_obj_create(obj);
	lv_obj_remove_style_all(paddle);
	lv_obj_set_size(paddle, PaddleNormalW, PaddleH);
	lv_obj_set_pos(paddle, paddleX - PaddleNormalW / 2, PaddleY);
	lv_obj_set_style_bg_color(paddle, MP_TEXT, 0);
	lv_obj_set_style_bg_opa(paddle, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(paddle, MP_ACCENT, 0);
	lv_obj_set_style_border_width(paddle, 0, 0);
	lv_obj_set_style_radius(paddle, 1, 0);
	lv_obj_clear_flag(paddle, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(paddle, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(paddle, LV_OBJ_FLAG_IGNORE_LAYOUT);
}

void PhoneBrickBreaker::buildBall() {
	ball = lv_obj_create(obj);
	lv_obj_remove_style_all(ball);
	lv_obj_set_size(ball, BallSize, BallSize);
	lv_obj_set_pos(ball, -10, -10);
	lv_obj_set_style_bg_color(ball, MP_HIGHLIGHT, 0);
	lv_obj_set_style_bg_opa(ball, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(ball, 0, 0);
	lv_obj_set_style_radius(ball, 1, 0);
	lv_obj_clear_flag(ball, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(ball, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(ball, LV_OBJ_FLAG_IGNORE_LAYOUT);
}

void PhoneBrickBreaker::buildTokens() {
	for(uint8_t i = 0; i < MaxActivePowerUps; ++i) {
		auto* t = lv_obj_create(obj);
		lv_obj_remove_style_all(t);
		lv_obj_set_size(t, TokenSize, TokenH);
		lv_obj_set_pos(t, -20, -20);
		lv_obj_set_style_bg_color(t, MP_ACCENT, 0);
		lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
		lv_obj_set_style_border_color(t, MP_TEXT, 0);
		lv_obj_set_style_border_width(t, 1, 0);
		lv_obj_set_style_radius(t, 1, 0);
		lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(t, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(t, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_add_flag(t, LV_OBJ_FLAG_HIDDEN);
		tokenSprites[i] = t;

		// Single-letter caption (W / S / L) centred on the token. Stays
		// hidden together with its parent until the token activates.
		auto* lbl = lv_label_create(t);
		lv_obj_set_style_text_font(lbl, &pixelbasic7, 0);
		lv_obj_set_style_text_color(lbl, MP_BG_DARK, 0);
		lv_label_set_text(lbl, "W");
		lv_obj_set_align(lbl, LV_ALIGN_CENTER);
		lv_obj_set_y(lbl, -1);
		tokenLabels[i] = lbl;
	}
}

void PhoneBrickBreaker::buildOverlay() {
	overlayLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(overlayLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(overlayLabel, MP_TEXT, 0);
	lv_obj_set_style_text_align(overlayLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(overlayLabel, "");
	lv_obj_set_align(overlayLabel, LV_ALIGN_CENTER);
	lv_obj_set_y(overlayLabel, 4);
}

// ===========================================================================
// state transitions
// ===========================================================================

void PhoneBrickBreaker::enterIdle() {
	state = GameState::Idle;
	stopTickTimer();
	holdLeft  = false;
	holdRight = false;
	score = 0;
	lives = StartLives;
	wideTicksLeft = 0;
	slowTicksLeft = 0;
	currentPaddleW = PaddleNormalW;
	for(uint8_t i = 0; i < MaxActivePowerUps; ++i) {
		tokens[i].active = false;
	}
	planLevel();
	resetPaddleAndBall();
	render();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneBrickBreaker::startGame() {
	state = GameState::Playing;
	startTickTimer();
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneBrickBreaker::pauseGame() {
	state = GameState::Paused;
	stopTickTimer();
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneBrickBreaker::resumeGame() {
	state = GameState::Playing;
	startTickTimer();
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneBrickBreaker::endGame() {
	state = GameState::GameOver;
	stopTickTimer();
	holdLeft  = false;
	holdRight = false;
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneBrickBreaker::clearedAll() {
	state = GameState::Cleared;
	stopTickTimer();
	holdLeft  = false;
	holdRight = false;
	refreshSoftKeys();
	refreshOverlay();
}

// ===========================================================================
// core game ops
// ===========================================================================

void PhoneBrickBreaker::planLevel() {
	uint16_t s = levelSeed;
	bricksRemaining = 0;
	for(uint8_t i = 0; i < BrickCount; ++i) {
		brickAlive[i] = true;
		++bricksRemaining;

		// Roll for a power-up drop. We deliberately spread the kinds so
		// the player gets a mix even on a quick run.
		const uint16_t roll = xorshift16(s) % 100u;
		if(roll < PowerUpDropPct) {
			const uint16_t kindRoll = xorshift16(s) % 3u;
			switch(kindRoll) {
				case 0:  brickPowerUp[i] = PowerUpKind::Wide; break;
				case 1:  brickPowerUp[i] = PowerUpKind::Slow; break;
				default: brickPowerUp[i] = PowerUpKind::Life; break;
			}
		} else {
			brickPowerUp[i] = PowerUpKind::None;
		}
	}
}

void PhoneBrickBreaker::resetPaddleAndBall() {
	paddleX = 80;
	currentPaddleW = (wideTicksLeft > 0) ? PaddleWideW : PaddleNormalW;

	// Park ball just above the paddle, centred. ballStuck = true means
	// the ball follows the paddle until ENTER.
	ballStuck = true;
	ballXQ8  = static_cast<int32_t>(paddleX - BallSize / 2) * Q8;
	ballYQ8  = static_cast<int32_t>(PaddleY - BallSize - 1) * Q8;
	ballVxQ8 = 0;
	ballVyQ8 = 0;
}

void PhoneBrickBreaker::launchBall() {
	if(!ballStuck) return;
	ballStuck = false;
	// Launch up-and-slightly-right by default. The exact mix gives the
	// classic "shallow rising arc" feel; later collisions will steer it.
	ballVxQ8 = BallSpeedQ8 / 2;          // ~+0.75 px/tick
	ballVyQ8 = -BallSpeedQ8;             // ~-1.5 px/tick (negative = up)
}

void PhoneBrickBreaker::physicsStep() {
	updatePaddle();

	if(ballStuck) {
		// Ball follows the paddle while parked.
		ballXQ8 = static_cast<int32_t>(paddleX - BallSize / 2) * Q8;
		ballYQ8 = static_cast<int32_t>(PaddleY - BallSize - 1) * Q8;
	} else {
		updateBall();
	}

	updateTokens();
	tickEffects();
	render();
	refreshHud();
}

void PhoneBrickBreaker::updatePaddle() {
	if(holdLeft && !holdRight) {
		paddleX -= PaddleSpeed;
	} else if(holdRight && !holdLeft) {
		paddleX += PaddleSpeed;
	}

	// Width follows wide-mode timer.
	currentPaddleW = (wideTicksLeft > 0) ? PaddleWideW : PaddleNormalW;

	// Clamp paddle inside the playfield (1 px gutter so the border reads).
	const int16_t halfW = currentPaddleW / 2;
	const int16_t minX  = PlayfieldX + halfW + 1;
	const int16_t maxX  = PlayfieldX + PlayfieldW - halfW - 1;
	if(paddleX < minX) paddleX = minX;
	if(paddleX > maxX) paddleX = maxX;
}

void PhoneBrickBreaker::updateBall() {
	// Apply slow-ball multiplier (0.6) by stepping every other tick when
	// active. Cheaper and keeps the integer math clean.
	int16_t vx = ballVxQ8;
	int16_t vy = ballVyQ8;
	if(slowTicksLeft > 0) {
		vx = static_cast<int16_t>((static_cast<int32_t>(vx) * 6) / 10);
		vy = static_cast<int16_t>((static_cast<int32_t>(vy) * 6) / 10);
	}

	ballXQ8 += vx;
	ballYQ8 += vy;

	int16_t bx = static_cast<int16_t>(ballXQ8 / Q8);
	int16_t by = static_cast<int16_t>(ballYQ8 / Q8);

	// ---- Wall collisions ---------------------------------------------
	const int16_t leftWall   = PlayfieldX + 1;
	const int16_t rightWall  = PlayfieldX + PlayfieldW - 1 - BallSize;
	const int16_t topWall    = PlayfieldY + 1;
	const int16_t bottomEdge = PlayfieldY + PlayfieldH;   // bottom = death
	if(bx < leftWall) {
		bx = leftWall;
		ballXQ8 = static_cast<int32_t>(bx) * Q8;
		ballVxQ8 = -ballVxQ8;
	}
	if(bx > rightWall) {
		bx = rightWall;
		ballXQ8 = static_cast<int32_t>(bx) * Q8;
		ballVxQ8 = -ballVxQ8;
	}
	if(by < topWall) {
		by = topWall;
		ballYQ8 = static_cast<int32_t>(by) * Q8;
		ballVyQ8 = -ballVyQ8;
	}

	// ---- Floor (death) -----------------------------------------------
	if(by + BallSize >= bottomEdge) {
		loseLife();
		return;
	}

	// ---- Paddle collision --------------------------------------------
	// Only meaningful when the ball is moving downward.
	if(ballVyQ8 > 0) {
		const int16_t halfW = currentPaddleW / 2;
		const int16_t pLeft  = paddleX - halfW;
		const int16_t pRight = paddleX + halfW;
		if(by + BallSize >= PaddleY && by + BallSize <= PaddleY + PaddleH + 2 &&
		   bx + BallSize > pLeft && bx < pRight) {
			// Reflect Y. Re-aim X based on impact position so the player
			// can steer the ball: hit near the left edge -> ball goes
			// hard left; hit near the centre -> mostly vertical.
			by = PaddleY - BallSize;
			ballYQ8 = static_cast<int32_t>(by) * Q8;
			ballVyQ8 = -absI16(ballVyQ8);

			const int16_t ballCx   = bx + BallSize / 2;
			const int16_t deltaPx  = ballCx - paddleX;
			// Normalise impact into [-Q8 .. +Q8] across half the paddle,
			// then scale by 1.25 to keep edge hits feeling lively.
			int32_t scaled = (static_cast<int32_t>(deltaPx) * Q8) / halfW;
			scaled = (scaled * 5) / 4;
			if(scaled >  Q8) scaled =  Q8;
			if(scaled < -Q8) scaled = -Q8;
			ballVxQ8 = static_cast<int16_t>((scaled * BallSpeedQ8) / Q8);

			// Force vertical component to keep the magnitude up (ball
			// must always have non-trivial Vy or it gets stuck riding
			// the paddle).
			const int16_t minVy = (BallSpeedQ8 * 3) / 4;
			if(absI16(ballVyQ8) < minVy) {
				ballVyQ8 = -minVy;
			}
		}
	}

	// ---- Brick collisions --------------------------------------------
	// Iterate alive bricks; check AABB overlap with the ball's current
	// position. On overlap, decide reflection axis by comparing the
	// X-overlap depth against the Y-overlap depth and bounce the
	// shallower axis. Process at most one brick per tick to keep the
	// trajectory predictable.
	for(uint8_t i = 0; i < BrickCount; ++i) {
		if(!brickAlive[i]) continue;
		const uint8_t row = i / BrickCols;
		const uint8_t col = i % BrickCols;
		const lv_coord_t bxL = BrickAreaX + col * (BrickW + BrickGapX);
		const lv_coord_t byT = BrickAreaY + row * (BrickH + BrickGapY);
		const lv_coord_t bxR = bxL + BrickW;
		const lv_coord_t byB = byT + BrickH;

		if(bx + BallSize <= bxL) continue;
		if(bx >= bxR) continue;
		if(by + BallSize <= byT) continue;
		if(by >= byB) continue;

		// Overlap depths along each axis.
		const int16_t overlapLeft   = (bx + BallSize) - bxL;
		const int16_t overlapRight  = bxR - bx;
		const int16_t overlapTop    = (by + BallSize) - byT;
		const int16_t overlapBottom = byB - by;

		const int16_t minXOverlap = (overlapLeft  < overlapRight)  ? overlapLeft  : overlapRight;
		const int16_t minYOverlap = (overlapTop   < overlapBottom) ? overlapTop   : overlapBottom;

		if(minYOverlap <= minXOverlap) {
			// Vertical reflection: push ball out of the brick on Y.
			if(overlapTop < overlapBottom) {
				ballYQ8 -= static_cast<int32_t>(minYOverlap) * Q8;
			} else {
				ballYQ8 += static_cast<int32_t>(minYOverlap) * Q8;
			}
			ballVyQ8 = -ballVyQ8;
		} else {
			// Horizontal reflection: push ball out of the brick on X.
			if(overlapLeft < overlapRight) {
				ballXQ8 -= static_cast<int32_t>(minXOverlap) * Q8;
			} else {
				ballXQ8 += static_cast<int32_t>(minXOverlap) * Q8;
			}
			ballVxQ8 = -ballVxQ8;
		}

		destroyBrick(i);
		break;
	}
}

void PhoneBrickBreaker::updateTokens() {
	for(uint8_t i = 0; i < MaxActivePowerUps; ++i) {
		if(!tokens[i].active) continue;
		tokens[i].yQ8 += TokenFallQ8;

		const int16_t tx = static_cast<int16_t>(tokens[i].xQ8 / Q8);
		const int16_t ty = static_cast<int16_t>(tokens[i].yQ8 / Q8);

		// Off the bottom -> deactivate.
		if(ty >= PlayfieldY + PlayfieldH) {
			tokens[i].active = false;
			continue;
		}

		// Catch -- token bottom overlaps the paddle's strip.
		if(ty + TokenH >= PaddleY && ty <= PaddleY + PaddleH) {
			const int16_t halfW  = currentPaddleW / 2;
			const int16_t pLeft  = paddleX - halfW;
			const int16_t pRight = paddleX + halfW;
			if(tx + TokenSize > pLeft && tx < pRight) {
				applyPowerUp(tokens[i].kind);
				tokens[i].active = false;
				continue;
			}
		}
	}
}

void PhoneBrickBreaker::tickEffects() {
	if(wideTicksLeft > 0) --wideTicksLeft;
	if(slowTicksLeft > 0) --slowTicksLeft;
}

void PhoneBrickBreaker::destroyBrick(uint8_t idx) {
	if(idx >= BrickCount) return;
	if(!brickAlive[idx]) return;

	brickAlive[idx] = false;
	if(bricksRemaining > 0) --bricksRemaining;

	const uint8_t row = idx / BrickCols;
	const uint8_t col = idx % BrickCols;
	score += static_cast<uint32_t>(10u) * (1u + (BrickRows - 1u - row));
	(void)col;

	if(brickPowerUp[idx] != PowerUpKind::None) {
		const lv_coord_t spawnX = BrickAreaX + col * (BrickW + BrickGapX) +
		                          (BrickW - TokenSize) / 2;
		const lv_coord_t spawnY = BrickAreaY + row * (BrickH + BrickGapY);
		spawnToken(brickPowerUp[idx], spawnX, spawnY);
	}

	if(bricksRemaining == 0) {
		clearedAll();
	}
}

void PhoneBrickBreaker::spawnToken(PowerUpKind kind, lv_coord_t x, lv_coord_t y) {
	for(uint8_t i = 0; i < MaxActivePowerUps; ++i) {
		if(tokens[i].active) continue;
		tokens[i].active = true;
		tokens[i].kind   = kind;
		tokens[i].xQ8    = static_cast<int32_t>(x) * Q8;
		tokens[i].yQ8    = static_cast<int32_t>(y) * Q8;
		return;
	}
	// Pool full -- silently drop. Power-ups are nice-to-have, not core.
}

void PhoneBrickBreaker::applyPowerUp(PowerUpKind kind) {
	switch(kind) {
		case PowerUpKind::Wide:
			wideTicksLeft = WideTicks;
			currentPaddleW = PaddleWideW;
			break;
		case PowerUpKind::Slow:
			slowTicksLeft = SlowTicks;
			break;
		case PowerUpKind::Life:
			if(lives < MaxLives) {
				++lives;
			} else {
				score += 50;   // consolation when at the cap
			}
			break;
		case PowerUpKind::None:
		default:
			break;
	}
}

void PhoneBrickBreaker::loseLife() {
	if(lives == 0) {
		endGame();
		return;
	}
	--lives;
	if(lives == 0) {
		endGame();
		return;
	}
	// Otherwise respawn the ball stuck above the paddle.
	resetPaddleAndBall();
	refreshHud();
}

// ===========================================================================
// rendering
// ===========================================================================

void PhoneBrickBreaker::render() {
	// Bricks: visibility tracks brickAlive[].
	for(uint8_t i = 0; i < BrickCount; ++i) {
		renderBrick(i);
	}

	// Paddle: width + position.
	if(paddle != nullptr) {
		lv_obj_set_size(paddle, currentPaddleW, PaddleH);
		lv_obj_set_pos(paddle, paddleX - currentPaddleW / 2, PaddleY);
	}

	// Ball: position straight from Q8.
	if(ball != nullptr) {
		const int16_t bx = static_cast<int16_t>(ballXQ8 / Q8);
		const int16_t by = static_cast<int16_t>(ballYQ8 / Q8);
		lv_obj_set_pos(ball, bx, by);
	}

	// Tokens: visibility + position + colour.
	for(uint8_t i = 0; i < MaxActivePowerUps; ++i) {
		renderToken(i);
	}
}

void PhoneBrickBreaker::renderBrick(uint8_t idx) {
	if(idx >= BrickCount) return;
	auto* spr = brickSprites[idx];
	if(spr == nullptr) return;
	if(brickAlive[idx]) {
		lv_obj_clear_flag(spr, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_add_flag(spr, LV_OBJ_FLAG_HIDDEN);
	}
}

void PhoneBrickBreaker::renderToken(uint8_t idx) {
	if(idx >= MaxActivePowerUps) return;
	auto* spr = tokenSprites[idx];
	auto* lbl = tokenLabels[idx];
	if(spr == nullptr) return;
	const Token& t = tokens[idx];
	if(!t.active) {
		lv_obj_add_flag(spr, LV_OBJ_FLAG_HIDDEN);
		return;
	}
	lv_obj_clear_flag(spr, LV_OBJ_FLAG_HIDDEN);
	lv_obj_set_style_bg_color(spr, tokenColor(t.kind), 0);
	const int16_t tx = static_cast<int16_t>(t.xQ8 / Q8);
	const int16_t ty = static_cast<int16_t>(t.yQ8 / Q8);
	lv_obj_set_pos(spr, tx, ty);
	if(lbl != nullptr) {
		switch(t.kind) {
			case PowerUpKind::Wide: lv_label_set_text(lbl, "W"); break;
			case PowerUpKind::Slow: lv_label_set_text(lbl, "S"); break;
			case PowerUpKind::Life: lv_label_set_text(lbl, "L"); break;
			case PowerUpKind::None:
			default:                lv_label_set_text(lbl, "?"); break;
		}
	}
}

lv_color_t PhoneBrickBreaker::brickColor(uint8_t row) const {
	// Five-row palette top-to-bottom -- matches the canonical Breakout
	// gradient cast through the MAKERphone retro palette so the visuals
	// stay coherent with the rest of the device.
	switch(row) {
		case 0:  return lv_color_make(240,  90,  90);  // red
		case 1:  return lv_color_make(255, 140,  30);  // sunset orange
		case 2:  return lv_color_make(255, 220,  60);  // yellow
		case 3:  return lv_color_make(120, 220, 110);  // green
		case 4:
		default: return lv_color_make(122, 232, 255);  // cyan
	}
}

lv_color_t PhoneBrickBreaker::tokenColor(PowerUpKind kind) const {
	switch(kind) {
		case PowerUpKind::Wide: return lv_color_make(255, 140,  30);  // orange
		case PowerUpKind::Slow: return lv_color_make(122, 232, 255);  // cyan
		case PowerUpKind::Life: return lv_color_make(120, 220, 110);  // green
		case PowerUpKind::None:
		default:                return MP_DIM;
	}
}

void PhoneBrickBreaker::refreshHud() {
	if(hudLeftLabel != nullptr) {
		lv_label_set_text(hudLeftLabel, "L1");
	}
	if(livesLabel != nullptr) {
		// Up to MaxLives '*' glyphs.
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

void PhoneBrickBreaker::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	switch(state) {
		case GameState::Idle:
			softKeys->setLeft("START");
			softKeys->setRight("BACK");
			break;
		case GameState::Playing:
			if(ballStuck) softKeys->setLeft("LAUNCH");
			else          softKeys->setLeft("PAUSE");
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
		case GameState::Cleared:
			softKeys->setLeft("RETRY");
			softKeys->setRight("BACK");
			break;
	}
}

void PhoneBrickBreaker::refreshOverlay() {
	if(overlayLabel == nullptr) return;
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
		case GameState::GameOver:
			lv_label_set_text(overlayLabel, "GAME\nOVER");
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
		case GameState::Cleared:
			lv_label_set_text(overlayLabel, "WELL\nDONE!");
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
	}
}

// ===========================================================================
// timers
// ===========================================================================

void PhoneBrickBreaker::startTickTimer() {
	if(tickTimer != nullptr) return;
	tickTimer = lv_timer_create(&PhoneBrickBreaker::onTickTimerStatic,
	                            TickMs, this);
}

void PhoneBrickBreaker::stopTickTimer() {
	if(tickTimer != nullptr) {
		lv_timer_del(tickTimer);
		tickTimer = nullptr;
	}
}

void PhoneBrickBreaker::onTickTimerStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneBrickBreaker*>(timer->user_data);
	if(self == nullptr) return;
	if(self->state != GameState::Playing) return;
	self->physicsStep();
}

// ===========================================================================
// input
// ===========================================================================

void PhoneBrickBreaker::buttonPressed(uint i) {
	switch(state) {
		case GameState::Idle:
			if(i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				startGame();
				refreshOverlay();
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
		case GameState::Cleared:
			if(i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				enterIdle();
			} else if(i == BTN_BACK) {
				if(softKeys) softKeys->flashRight();
				pop();
			}
			return;

		case GameState::Playing:
			break;  // fall through
	}

	switch(i) {
		case BTN_LEFT:
		case BTN_4:
			holdLeft = true;
			break;
		case BTN_RIGHT:
		case BTN_6:
			holdRight = true;
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

void PhoneBrickBreaker::buttonReleased(uint i) {
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
