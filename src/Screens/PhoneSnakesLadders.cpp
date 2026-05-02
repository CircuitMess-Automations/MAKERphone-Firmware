#include "PhoneSnakesLadders.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <Audio/Piezo.h>
#include <Settings.h>
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

// Snake / ladder accent colours (kept local so the global palette macros
// stay clean -- only PhoneSnakesLadders ever uses these tints).
#define MP_SNAKE_HEAD  lv_color_make(240, 80, 90)    // bright red
#define MP_SNAKE_TAIL  lv_color_make(180, 50, 70)    // dim red
#define MP_LADDER_FOOT lv_color_make(120, 220, 110)  // soft green
#define MP_LADDER_TOP  lv_color_make(80, 200, 140)   // teal-green

// Out-of-line jump tables. Picked so the game routinely resolves in
// 30-50 turns -- enough drama, not enough to drag.
const PhoneSnakesLadders::Jump
PhoneSnakesLadders::kSnakes[PhoneSnakesLadders::kSnakeCount] = {
	{ 99, 78 },
	{ 95, 75 },
	{ 87, 24 },
	{ 62, 19 },
	{ 54, 34 },
	{ 17,  7 },
};

const PhoneSnakesLadders::Jump
PhoneSnakesLadders::kLadders[PhoneSnakesLadders::kLadderCount] = {
	{  4, 14 },
	{  9, 31 },
	{ 21, 42 },
	{ 28, 84 },
	{ 36, 44 },
	{ 51, 67 },
	{ 71, 91 },
};

// Pip slot indices used by setDiceFace().
namespace {
constexpr uint8_t PIP_TL = 0;
constexpr uint8_t PIP_TR = 1;
constexpr uint8_t PIP_ML = 2;
constexpr uint8_t PIP_C  = 3;
constexpr uint8_t PIP_MR = 4;
constexpr uint8_t PIP_BL = 5;
constexpr uint8_t PIP_BR = 6;
} // namespace

// ===========================================================================
// ctor / dtor
// ===========================================================================

PhoneSnakesLadders::PhoneSnakesLadders()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr) {

	for(uint8_t i = 0; i < kSnakeCount; ++i) {
		snakeFrom[i] = nullptr;
		snakeTo[i]   = nullptr;
		snakeLine[i] = nullptr;
	}
	for(uint8_t i = 0; i < kLadderCount; ++i) {
		ladderFrom[i] = nullptr;
		ladderTo[i]   = nullptr;
		ladderLine[i] = nullptr;
	}
	for(uint8_t i = 0; i < 7; ++i) {
		dicePips[i] = nullptr;
	}

	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildHud();
	buildBoard();
	buildSidePanel();
	buildPawns();
	buildOverlay();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("ROLL");
	softKeys->setRight("BACK");

	newRound();
}

PhoneSnakesLadders::~PhoneSnakesLadders() {
	stopTickTimer();
	silencePiezo();
}

void PhoneSnakesLadders::onStart() {
	Input::getInstance()->addListener(this);
	startTickTimer();
}

void PhoneSnakesLadders::onStop() {
	Input::getInstance()->removeListener(this);
	stopTickTimer();
	silencePiezo();
}

// ===========================================================================
// build helpers
// ===========================================================================

void PhoneSnakesLadders::buildHud() {
	hudYouLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudYouLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudYouLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(hudYouLabel, "YOU 00");
	lv_obj_set_pos(hudYouLabel, 4, HudY + 2);

	hudCpuLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudCpuLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudCpuLabel, MP_ACCENT, 0);
	lv_label_set_text(hudCpuLabel, "CPU 00");
	lv_obj_set_pos(hudCpuLabel, 56, HudY + 2);

	hudTurnLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudTurnLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudTurnLabel, MP_TEXT, 0);
	lv_label_set_text(hudTurnLabel, "TURN: YOU");
	lv_obj_set_pos(hudTurnLabel, 104, HudY + 2);
}

void PhoneSnakesLadders::buildBoard() {
	// Single dim background panel for the board area. We do NOT
	// instantiate per-cell rectangles -- 100 lv_obj children would
	// burn too much LVGL state for a screen that only animates a
	// pawn or two on top.
	boardPanel = lv_obj_create(obj);
	lv_obj_remove_style_all(boardPanel);
	lv_obj_set_size(boardPanel, BoardW, BoardH);
	lv_obj_set_pos(boardPanel, BoardX, BoardY);
	lv_obj_set_style_bg_color(boardPanel, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(boardPanel, LV_OPA_80, 0);
	lv_obj_set_style_border_color(boardPanel, MP_DIM, 0);
	lv_obj_set_style_border_width(boardPanel, 1, 0);
	lv_obj_set_style_radius(boardPanel, 1, 0);
	lv_obj_set_style_pad_all(boardPanel, 0, 0);
	lv_obj_clear_flag(boardPanel, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(boardPanel, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(boardPanel, LV_OBJ_FLAG_IGNORE_LAYOUT);

	// Snakes: the head + tail markers + the connector line.
	for(uint8_t i = 0; i < kSnakeCount; ++i) {
		const Jump& j = kSnakes[i];
		const lv_coord_t hx = cellCenterX(j.from);
		const lv_coord_t hy = cellCenterY(j.from);
		const lv_coord_t tx = cellCenterX(j.to);
		const lv_coord_t ty = cellCenterY(j.to);

		auto* line = lv_line_create(boardPanel);
		snakePts[i][0].x = static_cast<int16_t>(hx);
		snakePts[i][0].y = static_cast<int16_t>(hy);
		snakePts[i][1].x = static_cast<int16_t>(tx);
		snakePts[i][1].y = static_cast<int16_t>(ty);
		lv_line_set_points(line, snakePts[i], 2);
		lv_obj_set_style_line_color(line, MP_SNAKE_HEAD, 0);
		lv_obj_set_style_line_width(line, 1, 0);
		lv_obj_set_style_line_opa(line, LV_OPA_60, 0);
		snakeLine[i] = line;

		auto* h = lv_obj_create(boardPanel);
		lv_obj_remove_style_all(h);
		lv_obj_set_size(h, 5, 5);
		lv_obj_set_pos(h, hx - 2, hy - 2);
		lv_obj_set_style_bg_color(h, MP_SNAKE_HEAD, 0);
		lv_obj_set_style_bg_opa(h, LV_OPA_80, 0);
		lv_obj_set_style_radius(h, 2, 0);
		lv_obj_set_style_pad_all(h, 0, 0);
		lv_obj_clear_flag(h, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(h, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(h, LV_OBJ_FLAG_IGNORE_LAYOUT);
		snakeFrom[i] = h;

		auto* t = lv_obj_create(boardPanel);
		lv_obj_remove_style_all(t);
		lv_obj_set_size(t, 4, 4);
		lv_obj_set_pos(t, tx - 2, ty - 2);
		lv_obj_set_style_bg_color(t, MP_SNAKE_TAIL, 0);
		lv_obj_set_style_bg_opa(t, LV_OPA_70, 0);
		lv_obj_set_style_radius(t, 2, 0);
		lv_obj_set_style_pad_all(t, 0, 0);
		lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(t, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(t, LV_OBJ_FLAG_IGNORE_LAYOUT);
		snakeTo[i] = t;
	}

	// Ladders: foot + top markers + connector line.
	for(uint8_t i = 0; i < kLadderCount; ++i) {
		const Jump& j = kLadders[i];
		const lv_coord_t fx = cellCenterX(j.from);
		const lv_coord_t fy = cellCenterY(j.from);
		const lv_coord_t tx = cellCenterX(j.to);
		const lv_coord_t ty = cellCenterY(j.to);

		auto* line = lv_line_create(boardPanel);
		ladderPts[i][0].x = static_cast<int16_t>(fx);
		ladderPts[i][0].y = static_cast<int16_t>(fy);
		ladderPts[i][1].x = static_cast<int16_t>(tx);
		ladderPts[i][1].y = static_cast<int16_t>(ty);
		lv_line_set_points(line, ladderPts[i], 2);
		lv_obj_set_style_line_color(line, MP_LADDER_FOOT, 0);
		lv_obj_set_style_line_width(line, 1, 0);
		lv_obj_set_style_line_opa(line, LV_OPA_60, 0);
		ladderLine[i] = line;

		auto* f = lv_obj_create(boardPanel);
		lv_obj_remove_style_all(f);
		lv_obj_set_size(f, 4, 4);
		lv_obj_set_pos(f, fx - 2, fy - 2);
		lv_obj_set_style_bg_color(f, MP_LADDER_FOOT, 0);
		lv_obj_set_style_bg_opa(f, LV_OPA_70, 0);
		lv_obj_set_style_radius(f, 1, 0);
		lv_obj_set_style_pad_all(f, 0, 0);
		lv_obj_clear_flag(f, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(f, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(f, LV_OBJ_FLAG_IGNORE_LAYOUT);
		ladderFrom[i] = f;

		auto* t = lv_obj_create(boardPanel);
		lv_obj_remove_style_all(t);
		lv_obj_set_size(t, 5, 5);
		lv_obj_set_pos(t, tx - 2, ty - 2);
		lv_obj_set_style_bg_color(t, MP_LADDER_TOP, 0);
		lv_obj_set_style_bg_opa(t, LV_OPA_80, 0);
		lv_obj_set_style_radius(t, 1, 0);
		lv_obj_set_style_pad_all(t, 0, 0);
		lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(t, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(t, LV_OBJ_FLAG_IGNORE_LAYOUT);
		ladderTo[i] = t;
	}
}

void PhoneSnakesLadders::buildPawns() {
	// Two 5x5 pawns. Player = cyan, CPU = orange. They start
	// "off-board" (cell 0); the renderPawn call from newRound()
	// pins them just outside the bottom-left corner so the user
	// sees them waiting to enter the board.
	pawnYou = lv_obj_create(boardPanel);
	lv_obj_remove_style_all(pawnYou);
	lv_obj_set_size(pawnYou, 5, 5);
	lv_obj_set_style_bg_color(pawnYou, MP_HIGHLIGHT, 0);
	lv_obj_set_style_bg_opa(pawnYou, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(pawnYou, MP_BG_DARK, 0);
	lv_obj_set_style_border_width(pawnYou, 1, 0);
	lv_obj_set_style_radius(pawnYou, 2, 0);
	lv_obj_set_style_pad_all(pawnYou, 0, 0);
	lv_obj_clear_flag(pawnYou, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(pawnYou, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(pawnYou, LV_OBJ_FLAG_IGNORE_LAYOUT);

	pawnCpu = lv_obj_create(boardPanel);
	lv_obj_remove_style_all(pawnCpu);
	lv_obj_set_size(pawnCpu, 5, 5);
	lv_obj_set_style_bg_color(pawnCpu, MP_ACCENT, 0);
	lv_obj_set_style_bg_opa(pawnCpu, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(pawnCpu, MP_BG_DARK, 0);
	lv_obj_set_style_border_width(pawnCpu, 1, 0);
	lv_obj_set_style_radius(pawnCpu, 2, 0);
	lv_obj_set_style_pad_all(pawnCpu, 0, 0);
	lv_obj_clear_flag(pawnCpu, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(pawnCpu, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(pawnCpu, LV_OBJ_FLAG_IGNORE_LAYOUT);
}

void PhoneSnakesLadders::buildSidePanel() {
	// Dice cube: a small rounded panel framed in cream, with seven
	// pip-slot squares laid out on a 3x3 grid we toggle by face.
	diceBox = lv_obj_create(obj);
	lv_obj_remove_style_all(diceBox);
	lv_obj_set_size(diceBox, DiceW, DiceH);
	lv_obj_set_pos(diceBox, DiceX, DiceY);
	lv_obj_set_style_bg_color(diceBox, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(diceBox, LV_OPA_90, 0);
	lv_obj_set_style_border_color(diceBox, MP_TEXT, 0);
	lv_obj_set_style_border_width(diceBox, 1, 0);
	lv_obj_set_style_radius(diceBox, 3, 0);
	lv_obj_set_style_pad_all(diceBox, 0, 0);
	lv_obj_clear_flag(diceBox, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(diceBox, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(diceBox, LV_OBJ_FLAG_IGNORE_LAYOUT);

	// Pip slots inside the dice box. Coordinates are in dice-box
	// local space (0..32). 3x3 px pips at the seven dot positions.
	struct PipPos { int8_t x; int8_t y; };
	const PipPos pos[7] = {
		{ 6,  6 },     // TL
		{ 23, 6 },     // TR
		{ 6,  14 },    // ML
		{ 14, 14 },    // C
		{ 23, 14 },    // MR
		{ 6,  23 },    // BL
		{ 23, 23 },    // BR
	};
	for(uint8_t i = 0; i < 7; ++i) {
		auto* p = lv_obj_create(diceBox);
		lv_obj_remove_style_all(p);
		lv_obj_set_size(p, 3, 3);
		lv_obj_set_pos(p, pos[i].x, pos[i].y);
		lv_obj_set_style_bg_color(p, MP_TEXT, 0);
		lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
		lv_obj_set_style_radius(p, 1, 0);
		lv_obj_set_style_pad_all(p, 0, 0);
		lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(p, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(p, LV_OBJ_FLAG_IGNORE_LAYOUT);
		dicePips[i] = p;
	}

	// "ROLLED N" caption directly below the cube.
	rolledLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(rolledLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(rolledLabel, MP_TEXT, 0);
	lv_obj_set_style_text_align(rolledLabel, LV_TEXT_ALIGN_LEFT, 0);
	lv_label_set_text(rolledLabel, "ROLLED -");
	lv_obj_set_pos(rolledLabel, SideX + 2, DiceY + DiceH + 4);

	// "YOU 23" / "CPU 17" stack below the rolled caption.
	youCellLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(youCellLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(youCellLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(youCellLabel, "YOU   0");
	lv_obj_set_pos(youCellLabel, SideX + 2, DiceY + DiceH + 16);

	cpuCellLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(cpuCellLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(cpuCellLabel, MP_ACCENT, 0);
	lv_label_set_text(cpuCellLabel, "CPU   0");
	lv_obj_set_pos(cpuCellLabel, SideX + 2, DiceY + DiceH + 26);

	turnLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(turnLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(turnLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(turnLabel, "TURN: YOU");
	lv_obj_set_pos(turnLabel, SideX + 2, DiceY + DiceH + 36);

	setDiceFace(1);
}

void PhoneSnakesLadders::buildOverlay() {
	overlayLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(overlayLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(overlayLabel, MP_TEXT, 0);
	lv_obj_set_style_text_align(overlayLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_bg_color(overlayLabel, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(overlayLabel, LV_OPA_80, 0);
	lv_obj_set_style_border_color(overlayLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_style_border_width(overlayLabel, 1, 0);
	lv_obj_set_style_radius(overlayLabel, 2, 0);
	lv_obj_set_style_pad_all(overlayLabel, 4, 0);
	lv_label_set_text(overlayLabel, "");
	lv_obj_set_align(overlayLabel, LV_ALIGN_CENTER);
	lv_obj_add_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
}

// ===========================================================================
// state transitions
// ===========================================================================

void PhoneSnakesLadders::newRound() {
	stopTickTimer();
	silencePiezo();

	youCell = 0;
	cpuCell = 0;
	dieRollFinal = 1;
	dieFace      = 1;
	pendingLanding = 0;
	pendingDest    = 0;
	phaseTimerMs   = 0;
	flickerMs      = 0;

	playerTurn = playerStartsNext;
	playerStartsNext = !playerStartsNext;

	if(playerTurn) {
		phase = Phase::PlayerRoll;
	} else {
		phase = Phase::CpuPending;
		phaseTimerMs = kCpuThinkMs;
	}

	setDiceFace(1);
	renderAllPawns();
	refreshHud();
	refreshSidePanel();
	refreshSoftKeys();
	refreshOverlay();

	startTickTimer();
}

void PhoneSnakesLadders::beginPlayerTurn() {
	playerTurn = true;
	phase = Phase::PlayerRoll;
	refreshSidePanel();
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneSnakesLadders::beginCpuTurn() {
	playerTurn = false;
	phase = Phase::CpuPending;
	phaseTimerMs = kCpuThinkMs;
	refreshSidePanel();
	refreshSoftKeys();
}

void PhoneSnakesLadders::startRoll() {
	dieRollFinal = rollDie();
	dieFace      = static_cast<uint8_t>((rand() % 6) + 1);
	setDiceFace(dieFace);

	phase = Phase::AnimDice;
	phaseTimerMs = kAnimDiceMs;
	flickerMs    = kAnimFrameMs;

	playRollTone();

	refreshSidePanel();
	refreshSoftKeys();
}

void PhoneSnakesLadders::completeRoll() {
	silencePiezo();
	setDiceFace(dieRollFinal);

	const uint8_t fromCell = playerTurn ? youCell : cpuCell;
	uint8_t target = static_cast<uint8_t>(fromCell + dieRollFinal);

	// Must land EXACTLY on 100 -- overshoot stays put.
	if(target > 100) {
		target = fromCell;
	}

	pendingLanding = target;
	pendingDest    = destinationFor(target);

	if(playerTurn) youCell = pendingLanding;
	else           cpuCell = pendingLanding;

	renderAllPawns();
	refreshSidePanel();

	phase = Phase::PawnMove;
	phaseTimerMs = kPawnMoveMs;
}

void PhoneSnakesLadders::resolveTurn() {
	const uint8_t finalCell = playerTurn ? youCell : cpuCell;
	if(finalCell == 100) {
		awardWin(playerTurn);
		return;
	}
	phase = Phase::Handoff;
	phaseTimerMs = kHandoffMs;
	refreshSoftKeys();
}

void PhoneSnakesLadders::awardWin(bool playerWon) {
	silencePiezo();
	if(playerWon) {
		++winsYou;
		phase = Phase::PlayerWon;
	} else {
		++winsCpu;
		phase = Phase::CpuWon;
	}
	playWinTone();
	refreshHud();
	refreshSidePanel();
	refreshSoftKeys();
	refreshOverlay();
}

// ===========================================================================
// game loop
// ===========================================================================

void PhoneSnakesLadders::tick() {
	switch(phase) {
		case Phase::Idle:
		case Phase::PlayerRoll:
		case Phase::PlayerWon:
		case Phase::CpuWon: {
			// Nothing animated -- waiting for input or game-over.
			return;
		}

		case Phase::AnimDice: {
			phaseTimerMs -= static_cast<int16_t>(kTickMs);
			flickerMs    -= static_cast<int16_t>(kTickMs);
			if(flickerMs <= 0) {
				flickerMs = static_cast<int16_t>(kAnimFrameMs);
				dieFace = static_cast<uint8_t>((rand() % 6) + 1);
				setDiceFace(dieFace);
			}
			if(phaseTimerMs <= 0) {
				dieFace = dieRollFinal;
				setDiceFace(dieFace);
				phase = Phase::ShowDice;
				phaseTimerMs = static_cast<int16_t>(kShowDiceMs);
				silencePiezo();
				refreshSidePanel();
			}
			return;
		}

		case Phase::ShowDice: {
			phaseTimerMs -= static_cast<int16_t>(kTickMs);
			if(phaseTimerMs <= 0) {
				completeRoll();
			}
			return;
		}

		case Phase::PawnMove: {
			phaseTimerMs -= static_cast<int16_t>(kTickMs);
			if(phaseTimerMs <= 0) {
				if(pendingDest != pendingLanding) {
					// Trigger the snake/ladder visual.
					phase = Phase::PawnJump;
					phaseTimerMs = static_cast<int16_t>(kPawnJumpMs);

					// Detect ladder vs snake by direction.
					const bool ladder = pendingDest > pendingLanding;
					playJumpTone(ladder);
				} else {
					silencePiezo();
					resolveTurn();
				}
			}
			return;
		}

		case Phase::PawnJump: {
			phaseTimerMs -= static_cast<int16_t>(kTickMs);
			if(phaseTimerMs <= 0) {
				silencePiezo();
				if(playerTurn) youCell = pendingDest;
				else           cpuCell = pendingDest;
				renderAllPawns();
				refreshSidePanel();
				resolveTurn();
			}
			return;
		}

		case Phase::Handoff: {
			phaseTimerMs -= static_cast<int16_t>(kTickMs);
			if(phaseTimerMs <= 0) {
				if(playerTurn) beginCpuTurn();
				else           beginPlayerTurn();
			}
			return;
		}

		case Phase::CpuPending: {
			phaseTimerMs -= static_cast<int16_t>(kTickMs);
			if(phaseTimerMs <= 0) {
				startRoll();
			}
			return;
		}
	}
}

// ===========================================================================
// helpers
// ===========================================================================

lv_coord_t PhoneSnakesLadders::cellCenterX(uint8_t cell) const {
	if(cell < 1 || cell > 100) return 0;
	const uint8_t zero = static_cast<uint8_t>(cell - 1);
	const uint8_t row  = static_cast<uint8_t>(zero / BoardN);
	const uint8_t cir  = static_cast<uint8_t>(zero % BoardN);
	const uint8_t col  = (row & 1u) ? static_cast<uint8_t>(BoardN - 1 - cir)
	                                : cir;
	const lv_coord_t cx = static_cast<lv_coord_t>(col * CellPx + CellPx / 2);
	return cx;
}

lv_coord_t PhoneSnakesLadders::cellCenterY(uint8_t cell) const {
	if(cell < 1 || cell > 100) return 0;
	const uint8_t zero = static_cast<uint8_t>(cell - 1);
	const uint8_t row  = static_cast<uint8_t>(zero / BoardN);
	// row 0 is the BOTTOM of the board; flip into screen-down y.
	const uint8_t srow = static_cast<uint8_t>(BoardN - 1 - row);
	const lv_coord_t cy = static_cast<lv_coord_t>(srow * CellPx + CellPx / 2);
	return cy;
}

uint8_t PhoneSnakesLadders::destinationFor(uint8_t cell) const {
	for(uint8_t i = 0; i < kSnakeCount; ++i) {
		if(kSnakes[i].from == cell) return kSnakes[i].to;
	}
	for(uint8_t i = 0; i < kLadderCount; ++i) {
		if(kLadders[i].from == cell) return kLadders[i].to;
	}
	return cell;
}

uint8_t PhoneSnakesLadders::rollDie() {
	return static_cast<uint8_t>((rand() % 6) + 1);
}

void PhoneSnakesLadders::setDiceFace(uint8_t value) {
	if(dicePips[0] == nullptr) return;

	// Hide every pip first; then enable the ones for this face.
	for(uint8_t i = 0; i < 7; ++i) {
		if(dicePips[i] != nullptr) {
			lv_obj_add_flag(dicePips[i], LV_OBJ_FLAG_HIDDEN);
		}
	}
	auto show = [this](uint8_t idx) {
		if(idx < 7 && dicePips[idx] != nullptr) {
			lv_obj_clear_flag(dicePips[idx], LV_OBJ_FLAG_HIDDEN);
		}
	};
	switch(value) {
		case 1: show(PIP_C);  break;
		case 2: show(PIP_TL); show(PIP_BR); break;
		case 3: show(PIP_TL); show(PIP_C);  show(PIP_BR); break;
		case 4: show(PIP_TL); show(PIP_TR); show(PIP_BL); show(PIP_BR); break;
		case 5: show(PIP_TL); show(PIP_TR); show(PIP_C);
		        show(PIP_BL); show(PIP_BR); break;
		case 6: show(PIP_TL); show(PIP_TR);
		        show(PIP_ML); show(PIP_MR);
		        show(PIP_BL); show(PIP_BR); break;
		default:
			break;
	}
}

// ===========================================================================
// audio
// ===========================================================================

void PhoneSnakesLadders::playRollTone() {
	if(!Settings.get().sound) return;
	// A short percussive blip; the dice flicker drives the perceived
	// "rattle" -- a single tone is enough for feedback without
	// fighting the rest of the system.
	Piezo.tone(540);
}

void PhoneSnakesLadders::playJumpTone(bool ladder) {
	if(!Settings.get().sound) return;
	// Ladders rise -> bright tone; snakes fall -> low buzz.
	Piezo.tone(ladder ? 880 : 220);
}

void PhoneSnakesLadders::playWinTone() {
	if(!Settings.get().sound) return;
	Piezo.tone(1320);
}

void PhoneSnakesLadders::silencePiezo() {
	Piezo.noTone();
}

// ===========================================================================
// rendering
// ===========================================================================

void PhoneSnakesLadders::renderPawn(lv_obj_t* pawn, uint8_t cell, bool playerColor) {
	if(pawn == nullptr) return;

	if(cell < 1 || cell > 100) {
		// "Off-board" -- pin to the bottom-left corner of the board
		// just outside cell 1, so the user sees the pawn waiting.
		// Player slightly higher, CPU slightly lower so they don't
		// overlap.
		const lv_coord_t x = static_cast<lv_coord_t>(playerColor ? 0 : 4);
		const lv_coord_t y = static_cast<lv_coord_t>(BoardH - 6);
		lv_obj_set_pos(pawn, x, y);
		return;
	}

	const lv_coord_t cx = cellCenterX(cell);
	const lv_coord_t cy = cellCenterY(cell);
	// Offset the two pawns slightly so they don't overlap when both
	// sit on the same cell. Player to the upper-left, CPU to the
	// lower-right of the cell.
	const lv_coord_t dx = static_cast<lv_coord_t>(playerColor ? -3 : 0);
	const lv_coord_t dy = static_cast<lv_coord_t>(playerColor ? -3 : 0);
	const lv_coord_t px = static_cast<lv_coord_t>(cx + dx - 2);
	const lv_coord_t py = static_cast<lv_coord_t>(cy + dy - 2);
	lv_obj_set_pos(pawn, px, py);
	lv_obj_move_foreground(pawn);
}

void PhoneSnakesLadders::renderAllPawns() {
	renderPawn(pawnYou, youCell, true);
	renderPawn(pawnCpu, cpuCell, false);
}

void PhoneSnakesLadders::refreshHud() {
	if(hudYouLabel != nullptr) {
		char buf[12];
		const unsigned w = winsYou > 99 ? 99 : winsYou;
		snprintf(buf, sizeof(buf), "YOU %02u", w);
		lv_label_set_text(hudYouLabel, buf);
	}
	if(hudCpuLabel != nullptr) {
		char buf[12];
		const unsigned w = winsCpu > 99 ? 99 : winsCpu;
		snprintf(buf, sizeof(buf), "CPU %02u", w);
		lv_label_set_text(hudCpuLabel, buf);
	}
	if(hudTurnLabel != nullptr) {
		const char* who = playerTurn ? "YOU" : "CPU";
		char buf[16];
		snprintf(buf, sizeof(buf), "TURN: %s", who);
		lv_label_set_text(hudTurnLabel, buf);
	}
}

void PhoneSnakesLadders::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	switch(phase) {
		case Phase::Idle:
		case Phase::PlayerRoll:
			softKeys->setLeft("ROLL");
			softKeys->setRight("BACK");
			break;
		case Phase::AnimDice:
		case Phase::ShowDice:
		case Phase::PawnMove:
		case Phase::PawnJump:
		case Phase::Handoff:
		case Phase::CpuPending:
			softKeys->setLeft("WAIT");
			softKeys->setRight("BACK");
			break;
		case Phase::PlayerWon:
		case Phase::CpuWon:
			softKeys->setLeft("AGAIN");
			softKeys->setRight("BACK");
			break;
	}
}

void PhoneSnakesLadders::refreshSidePanel() {
	if(rolledLabel != nullptr) {
		char buf[16];
		if(phase == Phase::Idle || phase == Phase::PlayerRoll
		   || phase == Phase::CpuPending) {
			snprintf(buf, sizeof(buf), "ROLLED -");
		} else {
			snprintf(buf, sizeof(buf), "ROLLED %u",
			         static_cast<unsigned>(dieRollFinal));
		}
		lv_label_set_text(rolledLabel, buf);
	}
	if(youCellLabel != nullptr) {
		char buf[16];
		snprintf(buf, sizeof(buf), "YOU %3u",
		         static_cast<unsigned>(youCell));
		lv_label_set_text(youCellLabel, buf);
	}
	if(cpuCellLabel != nullptr) {
		char buf[16];
		snprintf(buf, sizeof(buf), "CPU %3u",
		         static_cast<unsigned>(cpuCell));
		lv_label_set_text(cpuCellLabel, buf);
	}
	if(turnLabel != nullptr) {
		const char* who;
		switch(phase) {
			case Phase::PlayerWon: who = "YOU WIN!"; break;
			case Phase::CpuWon:    who = "CPU WIN";  break;
			default:               who = playerTurn ? "TURN: YOU" : "TURN: CPU";
		}
		lv_label_set_text(turnLabel, who);
	}
	refreshHud();
}

void PhoneSnakesLadders::refreshOverlay() {
	if(overlayLabel == nullptr) return;
	switch(phase) {
		case Phase::Idle:
		case Phase::PlayerRoll:
		case Phase::AnimDice:
		case Phase::ShowDice:
		case Phase::PawnMove:
		case Phase::PawnJump:
		case Phase::Handoff:
		case Phase::CpuPending:
			lv_obj_add_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			return;
		case Phase::PlayerWon:
			lv_label_set_text(overlayLabel, "YOU WIN!\nA TO PLAY AGAIN");
			lv_obj_set_style_text_color(overlayLabel, MP_HIGHLIGHT, 0);
			lv_obj_set_style_border_color(overlayLabel, MP_HIGHLIGHT, 0);
			break;
		case Phase::CpuWon:
			lv_label_set_text(overlayLabel, "CPU WINS\nA TO TRY AGAIN");
			lv_obj_set_style_text_color(overlayLabel, MP_ACCENT, 0);
			lv_obj_set_style_border_color(overlayLabel, MP_ACCENT, 0);
			break;
	}
	lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
	lv_obj_move_foreground(overlayLabel);
}

// ===========================================================================
// timer helpers
// ===========================================================================

void PhoneSnakesLadders::startTickTimer() {
	if(tickTimer != nullptr) return;
	tickTimer = lv_timer_create(&PhoneSnakesLadders::onTickStatic,
	                            kTickMs, this);
}

void PhoneSnakesLadders::stopTickTimer() {
	if(tickTimer != nullptr) {
		lv_timer_del(tickTimer);
		tickTimer = nullptr;
	}
}

void PhoneSnakesLadders::onTickStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneSnakesLadders*>(timer->user_data);
	if(self == nullptr) return;
	self->tick();
}

// ===========================================================================
// input
// ===========================================================================

void PhoneSnakesLadders::buttonPressed(uint i) {
	// BACK always pops out, regardless of phase.
	if(i == BTN_BACK) {
		if(softKeys) softKeys->flashRight();
		pop();
		return;
	}

	// Reshuffle works in every phase -- "give up" / "play again" share
	// the same key for muscle-memory parity with PhoneSlidingPuzzle.
	if(i == BTN_R) {
		newRound();
		return;
	}

	switch(phase) {
		case Phase::Idle:
		case Phase::PlayerRoll: {
			if(i == BTN_5 || i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				startRoll();
			}
			return;
		}

		case Phase::AnimDice:
		case Phase::ShowDice:
		case Phase::PawnMove:
		case Phase::PawnJump:
		case Phase::Handoff:
		case Phase::CpuPending: {
			// Animation in flight -- ignore everything except the
			// global BACK / R handled above.
			return;
		}

		case Phase::PlayerWon:
		case Phase::CpuWon: {
			if(i == BTN_5 || i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				newRound();
			}
			return;
		}
	}
}
