#include "PhoneAirHockey.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <stdlib.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - identical to every other Phone* widget so
// the screen sits beside the rest of the Phase-N arcade without a seam.
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)  // dim purple captions

namespace {

inline int16_t clampI16(int16_t v, int16_t lo, int16_t hi) {
	if(v < lo) return lo;
	if(v > hi) return hi;
	return v;
}

inline int32_t clampI32(int32_t v, int32_t lo, int32_t hi) {
	if(v < lo) return lo;
	if(v > hi) return hi;
	return v;
}

inline int16_t sgn16(int16_t v) {
	return (v > 0) ? 1 : ((v < 0) ? -1 : 0);
}

// Coarse integer hypotenuse approximation. We deliberately avoid pulling
// in <math.h>/sqrtf for one collision test on an ESP32 -- the Octagon
// approximation max((|a|+|b|)*7/8, max(|a|,|b|)) is ~3% accurate, which
// is fine for "is the puck overlapping a mallet" inside an 11-px disc.
inline int32_t hypotApprox(int32_t a, int32_t b) {
	if(a < 0) a = -a;
	if(b < 0) b = -b;
	const int32_t maxv = (a > b) ? a : b;
	const int32_t alt  = ((a + b) * 7) / 8;
	return (maxv > alt) ? maxv : alt;
}

} // namespace

// ===========================================================================
// ctor / dtor
// ===========================================================================

PhoneAirHockey::PhoneAirHockey()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr) {

	// Full-screen container, no scrollbars, no padding -- same blank-canvas
	// pattern PhoneBrickBreaker / PhoneTetris / PhoneHelicopter use.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order. The
	// status bar / HUD / rink / soft-keys overlay it without per-child
	// opacity gymnastics.
	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildHud();
	buildRink();
	buildPaddles();
	buildPuck();
	buildOverlay();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("PLAY");
	softKeys->setRight("BACK");

	enterIdle();
}

PhoneAirHockey::~PhoneAirHockey() {
	stopTickTimer();
	// All children are parented to obj; LVGL frees them recursively when
	// the screen's obj is destroyed by the LVScreen base destructor.
}

void PhoneAirHockey::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneAirHockey::onStop() {
	Input::getInstance()->removeListener(this);
	stopTickTimer();
}

// ===========================================================================
// build helpers
// ===========================================================================

void PhoneAirHockey::buildHud() {
	// Player score (cyan, left).
	hudYouLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudYouLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudYouLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(hudYouLabel, "YOU 0");
	lv_obj_set_pos(hudYouLabel, 4, 12);

	// Centred title.
	hudTitleLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudTitleLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudTitleLabel, MP_ACCENT, 0);
	lv_label_set_text(hudTitleLabel, "AIR HOCKEY");
	lv_obj_set_align(hudTitleLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hudTitleLabel, 12);

	// CPU score (orange, right).
	hudCpuLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudCpuLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudCpuLabel, MP_ACCENT, 0);
	lv_obj_set_style_text_align(hudCpuLabel, LV_TEXT_ALIGN_RIGHT, 0);
	lv_label_set_text(hudCpuLabel, "CPU 0");
	lv_obj_set_align(hudCpuLabel, LV_ALIGN_TOP_RIGHT);
	lv_obj_set_pos(hudCpuLabel, -3, 12);
}

void PhoneAirHockey::buildRink() {
	// Rink container -- thin MP_DIM border with a faintly translucent
	// purple fill so the rink reads as a chamber over the synthwave
	// wallpaper. Children pin themselves with absolute coords inside
	// the container.
	rink = lv_obj_create(obj);
	lv_obj_remove_style_all(rink);
	lv_obj_set_size(rink, RinkW, RinkH);
	lv_obj_set_pos(rink, RinkX, RinkY);
	lv_obj_set_style_bg_color(rink, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(rink, LV_OPA_50, 0);
	lv_obj_set_style_border_color(rink, MP_DIM, 0);
	lv_obj_set_style_border_width(rink, 1, 0);
	lv_obj_set_style_radius(rink, 0, 0);
	lv_obj_set_style_pad_all(rink, 0, 0);
	lv_obj_clear_flag(rink, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(rink, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(rink, LV_OBJ_FLAG_IGNORE_LAYOUT);

	// Centre line: two short rectangles flanking the centre dot. We
	// deliberately split them so the centre dot sits on top with a
	// clean colour rather than sharing the dim line tint.
	const lv_coord_t midY = RinkH / 2;
	const lv_coord_t lineGap = 7;       // gap so centre dot has air around it
	const lv_coord_t halfLen = (RinkW / 2) - (lineGap / 2) - 2;

	centreLineL = lv_obj_create(rink);
	lv_obj_remove_style_all(centreLineL);
	lv_obj_set_size(centreLineL, halfLen, 1);
	lv_obj_set_pos(centreLineL, 1, midY);
	lv_obj_set_style_bg_color(centreLineL, MP_DIM, 0);
	lv_obj_set_style_bg_opa(centreLineL, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(centreLineL, 0, 0);
	lv_obj_set_style_radius(centreLineL, 0, 0);
	lv_obj_clear_flag(centreLineL, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(centreLineL, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(centreLineL, LV_OBJ_FLAG_IGNORE_LAYOUT);

	centreLineR = lv_obj_create(rink);
	lv_obj_remove_style_all(centreLineR);
	lv_obj_set_size(centreLineR, halfLen, 1);
	lv_obj_set_pos(centreLineR, RinkW - 1 - halfLen, midY);
	lv_obj_set_style_bg_color(centreLineR, MP_DIM, 0);
	lv_obj_set_style_bg_opa(centreLineR, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(centreLineR, 0, 0);
	lv_obj_set_style_radius(centreLineR, 0, 0);
	lv_obj_clear_flag(centreLineR, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(centreLineR, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(centreLineR, LV_OBJ_FLAG_IGNORE_LAYOUT);

	// Centre dot.
	const lv_coord_t dotSize = 5;
	centreDot = lv_obj_create(rink);
	lv_obj_remove_style_all(centreDot);
	lv_obj_set_size(centreDot, dotSize, dotSize);
	lv_obj_set_pos(centreDot, (RinkW - dotSize) / 2, midY - dotSize / 2);
	lv_obj_set_style_bg_color(centreDot, MP_LABEL_DIM, 0);
	lv_obj_set_style_bg_opa(centreDot, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(centreDot, 0, 0);
	lv_obj_set_style_radius(centreDot, LV_RADIUS_CIRCLE, 0);
	lv_obj_clear_flag(centreDot, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(centreDot, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(centreDot, LV_OBJ_FLAG_IGNORE_LAYOUT);

	// Goal mouths -- thin coloured strips overlaid INSIDE the rink, on
	// the top and bottom borders. They visually erase the wall in the
	// goal area so the puck reads as "able to slip through" rather than
	// the player wondering why the ball bounces here but scores there.
	goalTop = lv_obj_create(rink);
	lv_obj_remove_style_all(goalTop);
	lv_obj_set_size(goalTop, GoalW, 2);
	lv_obj_set_pos(goalTop, GoalX, 0);
	lv_obj_set_style_bg_color(goalTop, MP_ACCENT, 0);
	lv_obj_set_style_bg_opa(goalTop, LV_OPA_70, 0);
	lv_obj_set_style_border_width(goalTop, 0, 0);
	lv_obj_set_style_radius(goalTop, 0, 0);
	lv_obj_clear_flag(goalTop, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(goalTop, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(goalTop, LV_OBJ_FLAG_IGNORE_LAYOUT);

	goalBottom = lv_obj_create(rink);
	lv_obj_remove_style_all(goalBottom);
	lv_obj_set_size(goalBottom, GoalW, 2);
	lv_obj_set_pos(goalBottom, GoalX, RinkH - 2);
	lv_obj_set_style_bg_color(goalBottom, MP_HIGHLIGHT, 0);
	lv_obj_set_style_bg_opa(goalBottom, LV_OPA_70, 0);
	lv_obj_set_style_border_width(goalBottom, 0, 0);
	lv_obj_set_style_radius(goalBottom, 0, 0);
	lv_obj_clear_flag(goalBottom, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(goalBottom, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(goalBottom, LV_OBJ_FLAG_IGNORE_LAYOUT);
}

void PhoneAirHockey::buildPaddles() {
	// Player mallet (cyan disc, with an orange ring rim so it pops on
	// the dim purple rink). Parented to the rink so its coordinates
	// are rink-local.
	playerMallet = lv_obj_create(rink);
	lv_obj_remove_style_all(playerMallet);
	lv_obj_set_size(playerMallet, MalletSize, MalletSize);
	lv_obj_set_pos(playerMallet, 0, 0);
	lv_obj_set_style_bg_color(playerMallet, MP_HIGHLIGHT, 0);
	lv_obj_set_style_bg_opa(playerMallet, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(playerMallet, MP_TEXT, 0);
	lv_obj_set_style_border_width(playerMallet, 1, 0);
	lv_obj_set_style_radius(playerMallet, LV_RADIUS_CIRCLE, 0);
	lv_obj_clear_flag(playerMallet, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(playerMallet, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(playerMallet, LV_OBJ_FLAG_IGNORE_LAYOUT);

	// CPU mallet (orange disc, cream rim).
	cpuMallet = lv_obj_create(rink);
	lv_obj_remove_style_all(cpuMallet);
	lv_obj_set_size(cpuMallet, MalletSize, MalletSize);
	lv_obj_set_pos(cpuMallet, 0, 0);
	lv_obj_set_style_bg_color(cpuMallet, MP_ACCENT, 0);
	lv_obj_set_style_bg_opa(cpuMallet, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(cpuMallet, MP_TEXT, 0);
	lv_obj_set_style_border_width(cpuMallet, 1, 0);
	lv_obj_set_style_radius(cpuMallet, LV_RADIUS_CIRCLE, 0);
	lv_obj_clear_flag(cpuMallet, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(cpuMallet, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(cpuMallet, LV_OBJ_FLAG_IGNORE_LAYOUT);
}

void PhoneAirHockey::buildPuck() {
	puck = lv_obj_create(rink);
	lv_obj_remove_style_all(puck);
	lv_obj_set_size(puck, PuckSize, PuckSize);
	lv_obj_set_pos(puck, 0, 0);
	lv_obj_set_style_bg_color(puck, MP_TEXT, 0);
	lv_obj_set_style_bg_opa(puck, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(puck, 0, 0);
	lv_obj_set_style_radius(puck, LV_RADIUS_CIRCLE, 0);
	lv_obj_clear_flag(puck, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(puck, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(puck, LV_OBJ_FLAG_IGNORE_LAYOUT);
}

void PhoneAirHockey::buildOverlay() {
	// Centre overlay used for "PRESS PLAY" / "PAUSED" / "YOU WIN" /
	// "CPU WINS". Cream text with a faint dark-purple drop so it stays
	// legible over the wallpaper without a background plate. We park
	// it on the screen root rather than the rink so it can sit slightly
	// above the centre of the rink.
	overlayLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(overlayLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(overlayLabel, MP_TEXT, 0);
	lv_obj_set_style_text_align(overlayLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(overlayLabel, "PRESS PLAY");
	lv_obj_set_align(overlayLabel, LV_ALIGN_TOP_MID);
	// Drop the overlay just above the centre of the rink (rink starts
	// at y=20, midpoint at y=69, label is ~7 px tall).
	lv_obj_set_y(overlayLabel, 64);
}

// ===========================================================================
// state transitions
// ===========================================================================

void PhoneAirHockey::enterIdle() {
	stopTickTimer();
	state = GameState::Idle;
	scoreYou = 0;
	scoreCpu = 0;
	serveToPlayer = true;
	resetMallets();
	puckXQ8 = static_cast<int32_t>(RinkW / 2 - PuckR) * Q8;
	puckYQ8 = static_cast<int32_t>(RinkH / 2 - PuckR) * Q8;
	puckVxQ8 = 0;
	puckVyQ8 = 0;
	holdUp = holdDown = holdLeft = holdRight = false;
	render();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneAirHockey::startMatch() {
	state = GameState::Serving;
	serveNext();
	refreshSoftKeys();
	refreshOverlay();
	startTickTimer();
}

void PhoneAirHockey::serveNext() {
	state = GameState::Serving;
	resetMallets();
	puckXQ8 = static_cast<int32_t>(RinkW / 2 - PuckR) * Q8;
	puckYQ8 = static_cast<int32_t>(RinkH / 2 - PuckR) * Q8;
	puckVxQ8 = 0;
	puckVyQ8 = 0;
	render();
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneAirHockey::launchPuck() {
	// Kick the puck toward whoever is supposed to receive serve. Slight
	// horizontal jitter so the first hit is interesting rather than a
	// perfect axis-aligned launch every round.
	puckVyQ8 = serveToPlayer ?  ServeSpeedQ8 : -ServeSpeedQ8;
	// Use the puck's Q8 fractional bits (which are 0 here) and the
	// score bytes as a pinch-of-salt jitter. Keeps things deterministic
	// per-score so a player can learn the launch direction.
	const int16_t jitter = static_cast<int16_t>(((scoreYou + scoreCpu) & 1) ? 80 : -80);
	puckVxQ8 = jitter;
	state = GameState::Playing;
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneAirHockey::pauseGame() {
	if(state != GameState::Playing) return;
	state = GameState::Paused;
	stopTickTimer();
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneAirHockey::resumeGame() {
	if(state != GameState::Paused) return;
	state = GameState::Playing;
	startTickTimer();
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneAirHockey::endMatchPlayerWon() {
	state = GameState::PlayerWon;
	stopTickTimer();
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneAirHockey::endMatchCpuWon() {
	state = GameState::CpuWon;
	stopTickTimer();
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneAirHockey::onGoal(bool playerScored) {
	if(playerScored) {
		++scoreYou;
		serveToPlayer = false;   // CPU receives next serve
	} else {
		++scoreCpu;
		serveToPlayer = true;    // player receives next serve
	}
	refreshHud();

	if(scoreYou >= WinGoals) {
		endMatchPlayerWon();
		return;
	}
	if(scoreCpu >= WinGoals) {
		endMatchCpuWon();
		return;
	}
	serveNext();
}

// ===========================================================================
// core game ops
// ===========================================================================

void PhoneAirHockey::resetMallets() {
	// Player mallet parks centred horizontally, two-thirds down the rink.
	playerX = RinkW / 2;
	playerY = RinkH - MalletR - 6;
	playerPrevX = playerX;
	playerPrevY = playerY;

	// CPU mallet parks centred horizontally, one-third down the rink.
	cpuX = RinkW / 2;
	cpuY = MalletR + 6;
	cpuPrevX = cpuX;
	cpuPrevY = cpuY;
}

void PhoneAirHockey::physicsStep() {
	if(state != GameState::Playing) return;

	stepPlayer();
	stepCpu();
	stepPuck();
	render();
}

void PhoneAirHockey::stepPlayer() {
	playerPrevX = playerX;
	playerPrevY = playerY;

	int16_t dx = 0;
	int16_t dy = 0;
	if(holdLeft)  dx -= PlayerStep;
	if(holdRight) dx += PlayerStep;
	if(holdUp)    dy -= PlayerStep;
	if(holdDown)  dy += PlayerStep;
	playerX += dx;
	playerY += dy;

	// Player stays in the bottom half of the rink (the player half
	// includes the centre line so a quick jab at the line is allowed).
	const int16_t minX = MalletR + 1;
	const int16_t maxX = RinkW - MalletR - 1;
	const int16_t minY = (RinkH / 2) + 1;            // top of player half
	const int16_t maxY = RinkH - MalletR - 1;
	playerX = clampI16(playerX, minX, maxX);
	playerY = clampI16(playerY, minY, maxY);
}

void PhoneAirHockey::stepCpu() {
	cpuPrevX = cpuX;
	cpuPrevY = cpuY;

	const int16_t puckPx = static_cast<int16_t>(puckXQ8 / Q8) + PuckR;
	const int16_t puckPy = static_cast<int16_t>(puckYQ8 / Q8) + PuckR;

	// Target X: track the puck horizontally with a small lazy zone so
	// the CPU does not jitter on every micro-frame.
	int16_t targetX = puckPx;
	int16_t targetY;

	// CPU defensive zone is the top half of the rink. If the puck is
	// in the CPU half, intercept; otherwise return to a defensive
	// position near the goal mouth.
	if(puckPy < (RinkH / 2)) {
		// Aggressive: try to slap the puck toward the player goal.
		// Aim slightly behind the puck so the contact pushes it down
		// rather than up over the CPU's own goal.
		targetY = puckPy - (MalletR + PuckR + 1);
	} else {
		// Defensive: park on the goal-mouth line, drift toward the
		// puck's X so a long shot still has the CPU lined up.
		targetY = MalletR + 6;
	}

	// Clamp the AI target inside the CPU half so it cannot wander
	// across the centre line and grief the player.
	const int16_t minX = MalletR + 1;
	const int16_t maxX = RinkW - MalletR - 1;
	const int16_t minY = MalletR + 1;
	const int16_t maxY = (RinkH / 2) - 1;
	targetX = clampI16(targetX, minX, maxX);
	targetY = clampI16(targetY, minY, maxY);

	// Move toward the target, capped at CpuMaxStep per axis.
	const int16_t dx = targetX - cpuX;
	const int16_t dy = targetY - cpuY;
	const int16_t stepX = (dx >  CpuMaxStep) ?  CpuMaxStep
	                    : (dx < -CpuMaxStep) ? -CpuMaxStep
	                    : dx;
	const int16_t stepY = (dy >  CpuMaxStep) ?  CpuMaxStep
	                    : (dy < -CpuMaxStep) ? -CpuMaxStep
	                    : dy;
	cpuX = clampI16(cpuX + stepX, minX, maxX);
	cpuY = clampI16(cpuY + stepY, minY, maxY);
}

void PhoneAirHockey::stepPuck() {
	// Apply velocity.
	puckXQ8 += puckVxQ8;
	puckYQ8 += puckVyQ8;

	// Friction (multiplicative, Q8).
	puckVxQ8 = static_cast<int16_t>((static_cast<int32_t>(puckVxQ8) * FrictionQ8) >> 8);
	puckVyQ8 = static_cast<int16_t>((static_cast<int32_t>(puckVyQ8) * FrictionQ8) >> 8);

	// Clamp to max speed component-wise so a chain of hits cannot
	// tunnel the puck through a wall.
	if(puckVxQ8 >  PuckMaxQ8) puckVxQ8 =  PuckMaxQ8;
	if(puckVxQ8 < -PuckMaxQ8) puckVxQ8 = -PuckMaxQ8;
	if(puckVyQ8 >  PuckMaxQ8) puckVyQ8 =  PuckMaxQ8;
	if(puckVyQ8 < -PuckMaxQ8) puckVyQ8 = -PuckMaxQ8;

	// ---- Wall + goal handling --------------------------------------
	// The puck's lv_obj is sized PuckSize x PuckSize and positioned by
	// its top-left, so its centre x = puckXQ8/Q8 + PuckR.
	const int32_t maxXQ8 = static_cast<int32_t>(RinkW - 1 - PuckSize) * Q8;
	const int32_t maxYQ8 = static_cast<int32_t>(RinkH - 1 - PuckSize) * Q8;

	// Left / right walls always bounce.
	if(puckXQ8 < (1 * Q8)) {
		puckXQ8 = 1 * Q8;
		if(puckVxQ8 < 0) puckVxQ8 = -puckVxQ8;
	} else if(puckXQ8 > maxXQ8) {
		puckXQ8 = maxXQ8;
		if(puckVxQ8 > 0) puckVxQ8 = -puckVxQ8;
	}

	// Top wall: goal if puck centre is inside the goal mouth, otherwise
	// bounce off the wall.
	const int16_t puckCx = static_cast<int16_t>(puckXQ8 / Q8) + PuckR;
	if(puckYQ8 < (1 * Q8)) {
		const bool inGoal = (puckCx >= GoalX) && (puckCx <= GoalX + GoalW);
		if(inGoal) {
			onGoal(/*playerScored*/ true);
			return;
		}
		puckYQ8 = 1 * Q8;
		if(puckVyQ8 < 0) puckVyQ8 = -puckVyQ8;
	} else if(puckYQ8 > maxYQ8) {
		const bool inGoal = (puckCx >= GoalX) && (puckCx <= GoalX + GoalW);
		if(inGoal) {
			onGoal(/*playerScored*/ false);
			return;
		}
		puckYQ8 = maxYQ8;
		if(puckVyQ8 > 0) puckVyQ8 = -puckVyQ8;
	}

	// ---- Mallet collisions -----------------------------------------
	resolveMalletHit(playerX, playerY, playerPrevX, playerPrevY);
	resolveMalletHit(cpuX,    cpuY,    cpuPrevX,    cpuPrevY);
}

void PhoneAirHockey::resolveMalletHit(int16_t mx, int16_t my,
                                      int16_t prevX, int16_t prevY) {
	// Puck centre.
	const int32_t pcx = puckXQ8 + (PuckR * Q8);
	const int32_t pcy = puckYQ8 + (PuckR * Q8);
	// Mallet centre in Q8.
	const int32_t mcx = static_cast<int32_t>(mx) * Q8;
	const int32_t mcy = static_cast<int32_t>(my) * Q8;

	const int32_t dx = pcx - mcx;
	const int32_t dy = pcy - mcy;
	const int32_t distQ8  = hypotApprox(dx, dy);
	const int32_t minDist = static_cast<int32_t>(MalletR + PuckR) * Q8;

	if(distQ8 >= minDist) return;          // no overlap, no work

	// Compute a unit-ish direction by scaling so the longer axis is Q8.
	// Avoids a sqrt while keeping the bounce direction sensible. If the
	// puck is exactly on top of the mallet (rare), fall back to "push
	// straight up" so the mallet always clears its own footprint.
	int32_t nxQ8;
	int32_t nyQ8;
	if(distQ8 == 0) {
		nxQ8 = 0;
		nyQ8 = -static_cast<int32_t>(Q8);
	} else {
		nxQ8 = (dx * Q8) / distQ8;
		nyQ8 = (dy * Q8) / distQ8;
	}

	// Push the puck out so it no longer overlaps. Add a Q8 of slack
	// so the next tick doesn't immediately re-trigger the test on the
	// same overlap.
	const int32_t penetrationQ8 = (minDist - distQ8) + (Q8 / 4);
	puckXQ8 += (nxQ8 * penetrationQ8) >> 8;
	puckYQ8 += (nyQ8 * penetrationQ8) >> 8;

	// Reflect the puck velocity along the contact normal. The dot
	// product (in Q8 after scaling back) is `vDotN = vx*nx + vy*ny`.
	// We only flip the velocity component when the puck is moving INTO
	// the mallet (vDotN < 0), otherwise a glancing pass-by would weirdly
	// accelerate the puck away from contact.
	const int32_t vDotN = (static_cast<int32_t>(puckVxQ8) * nxQ8
	                     + static_cast<int32_t>(puckVyQ8) * nyQ8) >> 8;
	if(vDotN < 0) {
		// Reflection: v' = v - 2 * (v . n) * n
		puckVxQ8 = static_cast<int16_t>(static_cast<int32_t>(puckVxQ8)
		                              - ((2 * vDotN * nxQ8) >> 8));
		puckVyQ8 = static_cast<int16_t>(static_cast<int32_t>(puckVyQ8)
		                              - ((2 * vDotN * nyQ8) >> 8));
	}

	// Add the mallet's recent motion as bonus velocity so a moving
	// mallet feels like it actually "hits" the puck. Scale the delta
	// to Q8 and clamp so a single fast jab does not slingshot beyond
	// the velocity cap.
	const int16_t mdx = mx - prevX;
	const int16_t mdy = my - prevY;
	int32_t bonusVx = static_cast<int32_t>(mdx) * Q8;
	int32_t bonusVy = static_cast<int32_t>(mdy) * Q8;

	// Always add a small kick along the contact normal so a stationary
	// mallet still nudges a stationary puck. Sign matches the normal:
	// the puck travels AWAY from the mallet centre on impact.
	bonusVx += (nxQ8 * MalletHitQ8) >> 8;
	bonusVy += (nyQ8 * MalletHitQ8) >> 8;

	int32_t newVx = static_cast<int32_t>(puckVxQ8) + bonusVx;
	int32_t newVy = static_cast<int32_t>(puckVyQ8) + bonusVy;
	newVx = clampI32(newVx, -PuckMaxQ8, PuckMaxQ8);
	newVy = clampI32(newVy, -PuckMaxQ8, PuckMaxQ8);
	puckVxQ8 = static_cast<int16_t>(newVx);
	puckVyQ8 = static_cast<int16_t>(newVy);

	// Final position clamp -- the post-push X/Y might be just outside
	// the rink if the mallet is hugging a wall when the contact occurs.
	puckXQ8 = clampI32(puckXQ8, 1 * Q8,
	                   static_cast<int32_t>(RinkW - 1 - PuckSize) * Q8);
	puckYQ8 = clampI32(puckYQ8, 1 * Q8,
	                   static_cast<int32_t>(RinkH - 1 - PuckSize) * Q8);
}

// ===========================================================================
// rendering
// ===========================================================================

void PhoneAirHockey::render() {
	if(playerMallet != nullptr) {
		lv_obj_set_pos(playerMallet, playerX - MalletR, playerY - MalletR);
	}
	if(cpuMallet != nullptr) {
		lv_obj_set_pos(cpuMallet, cpuX - MalletR, cpuY - MalletR);
	}
	if(puck != nullptr) {
		const int16_t px = static_cast<int16_t>(puckXQ8 / Q8);
		const int16_t py = static_cast<int16_t>(puckYQ8 / Q8);
		lv_obj_set_pos(puck, px, py);
	}
}

void PhoneAirHockey::refreshHud() {
	if(hudYouLabel != nullptr) {
		char buf[12];
		snprintf(buf, sizeof(buf), "YOU %u", static_cast<unsigned>(scoreYou));
		lv_label_set_text(hudYouLabel, buf);
	}
	if(hudCpuLabel != nullptr) {
		char buf[12];
		snprintf(buf, sizeof(buf), "CPU %u", static_cast<unsigned>(scoreCpu));
		lv_label_set_text(hudCpuLabel, buf);
	}
}

void PhoneAirHockey::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	switch(state) {
		case GameState::Idle:
			softKeys->setLeft("PLAY");
			softKeys->setRight("BACK");
			break;
		case GameState::Serving:
			softKeys->setLeft("SERVE");
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
		case GameState::PlayerWon:
		case GameState::CpuWon:
			softKeys->setLeft("AGAIN");
			softKeys->setRight("BACK");
			break;
	}
}

void PhoneAirHockey::refreshOverlay() {
	if(overlayLabel == nullptr) return;
	switch(state) {
		case GameState::Idle:
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			lv_obj_set_style_text_color(overlayLabel, MP_TEXT, 0);
			lv_label_set_text(overlayLabel, "PRESS PLAY");
			break;
		case GameState::Serving:
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			lv_obj_set_style_text_color(overlayLabel, MP_HIGHLIGHT, 0);
			lv_label_set_text(overlayLabel,
			                  serveToPlayer ? "YOUR SERVE" : "CPU SERVE");
			break;
		case GameState::Playing:
			lv_obj_add_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
		case GameState::Paused:
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			lv_obj_set_style_text_color(overlayLabel, MP_LABEL_DIM, 0);
			lv_label_set_text(overlayLabel, "PAUSED");
			break;
		case GameState::PlayerWon:
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			lv_obj_set_style_text_color(overlayLabel, MP_HIGHLIGHT, 0);
			lv_label_set_text(overlayLabel, "YOU WIN");
			break;
		case GameState::CpuWon:
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			lv_obj_set_style_text_color(overlayLabel, MP_ACCENT, 0);
			lv_label_set_text(overlayLabel, "CPU WINS");
			break;
	}
}

// ===========================================================================
// timers
// ===========================================================================

void PhoneAirHockey::startTickTimer() {
	if(tickTimer != nullptr) return;
	tickTimer = lv_timer_create(&PhoneAirHockey::onTickTimerStatic,
	                            TickMs, this);
}

void PhoneAirHockey::stopTickTimer() {
	if(tickTimer == nullptr) return;
	lv_timer_del(tickTimer);
	tickTimer = nullptr;
}

void PhoneAirHockey::onTickTimerStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneAirHockey*>(timer->user_data);
	if(self == nullptr) return;
	self->physicsStep();
}

// ===========================================================================
// input
// ===========================================================================

void PhoneAirHockey::buttonPressed(uint i) {
	switch(state) {
		case GameState::Idle:
			if(i == BTN_ENTER || i == BTN_5) {
				if(softKeys) softKeys->flashLeft();
				startMatch();
			} else if(i == BTN_BACK) {
				if(softKeys) softKeys->flashRight();
				pop();
			}
			return;

		case GameState::Serving:
			if(i == BTN_ENTER || i == BTN_5) {
				if(softKeys) softKeys->flashLeft();
				launchPuck();
				return;
			} else if(i == BTN_BACK) {
				if(softKeys) softKeys->flashRight();
				pop();
				return;
			}
			// Otherwise let the player start sliding the mallet around
			// before the serve, so the launch isn't a cold start. Fall
			// through to the movement-key handler below.
			break;

		case GameState::Paused:
			if(i == BTN_ENTER || i == BTN_5) {
				if(softKeys) softKeys->flashLeft();
				resumeGame();
			} else if(i == BTN_BACK) {
				if(softKeys) softKeys->flashRight();
				pop();
			}
			return;

		case GameState::PlayerWon:
		case GameState::CpuWon:
			if(i == BTN_ENTER || i == BTN_5) {
				if(softKeys) softKeys->flashLeft();
				enterIdle();
			} else if(i == BTN_BACK) {
				if(softKeys) softKeys->flashRight();
				pop();
			}
			return;

		case GameState::Playing:
			break;  // fall through to the movement keys
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
		case BTN_2:
			holdUp = true;
			break;
		case BTN_8:
			holdDown = true;
			break;
		case BTN_ENTER:
		case BTN_5:
			// During Playing, ENTER pauses. (Serving is handled above.)
			if(state == GameState::Playing) {
				if(softKeys) softKeys->flashLeft();
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

void PhoneAirHockey::buttonReleased(uint i) {
	switch(i) {
		case BTN_LEFT:
		case BTN_4:
			holdLeft = false;
			break;
		case BTN_RIGHT:
		case BTN_6:
			holdRight = false;
			break;
		case BTN_2:
			holdUp = false;
			break;
		case BTN_8:
			holdDown = false;
			break;
		default:
			break;
	}
}
