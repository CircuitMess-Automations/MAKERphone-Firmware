#include "PhoneTetris.h"

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
// arcade screen slots in beside PhoneGamesScreen (S65), PhoneStopwatch (S61)
// and the dialer family without a visual seam. Inlined per the established
// pattern (see PhoneCalculator.cpp / PhoneGamesScreen.cpp).
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)  // dim purple captions

// ---------- tetromino definitions ----------------------------------------
//
// Each piece is described by 4 rotation states, each as a 4x4 cell grid.
// `1` marks a filled cell. The classic SRS spawn orientation is rotation 0
// for every piece. Rotations CW around the cell grid (no SRS wall kicks in
// S71 - those land later with the level-progression split in S72).
//
// Index 0..6 = I, O, T, S, Z, J, L. The colour index stored in `board[]` /
// pieceType is `index + 1` so `0` keeps reading as "empty" without a
// dedicated "occupied" flag.
namespace {

// 4D array of piece shapes: [piece][rotation][row][col].
// Sized 7 * 4 * 4 * 4 = 448 bytes -- tiny enough to live in flash via
// constexpr. Skipping the `using Shape = ...` typedef indirection keeps
// this compatible with toolchains where array-typedefs interact oddly
// with constexpr aggregate initialisation.
constexpr uint8_t kShapes[PhoneTetris::PieceCount]
                         [PhoneTetris::Rotations]
                         [PhoneTetris::PieceCells]
                         [PhoneTetris::PieceCells] = {
	// I -- 4-tall column / 4-wide row.
	{
		{ {0,0,0,0}, {1,1,1,1}, {0,0,0,0}, {0,0,0,0} },
		{ {0,0,1,0}, {0,0,1,0}, {0,0,1,0}, {0,0,1,0} },
		{ {0,0,0,0}, {0,0,0,0}, {1,1,1,1}, {0,0,0,0} },
		{ {0,1,0,0}, {0,1,0,0}, {0,1,0,0}, {0,1,0,0} },
	},
	// O -- 2x2 square (rotation is a no-op).
	{
		{ {0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0} },
		{ {0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0} },
		{ {0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0} },
		{ {0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0} },
	},
	// T.
	{
		{ {0,0,0,0}, {1,1,1,0}, {0,1,0,0}, {0,0,0,0} },
		{ {0,1,0,0}, {1,1,0,0}, {0,1,0,0}, {0,0,0,0} },
		{ {0,1,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0} },
		{ {0,1,0,0}, {0,1,1,0}, {0,1,0,0}, {0,0,0,0} },
	},
	// S.
	{
		{ {0,0,0,0}, {0,1,1,0}, {1,1,0,0}, {0,0,0,0} },
		{ {1,0,0,0}, {1,1,0,0}, {0,1,0,0}, {0,0,0,0} },
		{ {0,0,0,0}, {0,1,1,0}, {1,1,0,0}, {0,0,0,0} },
		{ {1,0,0,0}, {1,1,0,0}, {0,1,0,0}, {0,0,0,0} },
	},
	// Z.
	{
		{ {0,0,0,0}, {1,1,0,0}, {0,1,1,0}, {0,0,0,0} },
		{ {0,1,0,0}, {1,1,0,0}, {1,0,0,0}, {0,0,0,0} },
		{ {0,0,0,0}, {1,1,0,0}, {0,1,1,0}, {0,0,0,0} },
		{ {0,1,0,0}, {1,1,0,0}, {1,0,0,0}, {0,0,0,0} },
	},
	// J.
	{
		{ {1,0,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0} },
		{ {1,1,0,0}, {1,0,0,0}, {1,0,0,0}, {0,0,0,0} },
		{ {0,0,0,0}, {1,1,1,0}, {0,0,1,0}, {0,0,0,0} },
		{ {0,1,0,0}, {0,1,0,0}, {1,1,0,0}, {0,0,0,0} },
	},
	// L.
	{
		{ {0,0,1,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0} },
		{ {1,0,0,0}, {1,0,0,0}, {1,1,0,0}, {0,0,0,0} },
		{ {0,0,0,0}, {1,1,1,0}, {1,0,0,0}, {0,0,0,0} },
		{ {1,1,0,0}, {0,1,0,0}, {0,1,0,0}, {0,0,0,0} },
	},
};

// One colour per piece type (1-based: kColors[0] is reserved for "empty").
// We deliberately pick NES-Tetris-leaning hues that still read OK against
// the deep-purple board background.
const lv_color_t kColors[PhoneTetris::PieceCount + 1] = {
	lv_color_make( 0,   0,   0),    // 0 -> empty (sentinel; never drawn)
	lv_color_make(122, 232, 255),   // 1 I -- cyan
	lv_color_make(255, 220,  60),   // 2 O -- yellow
	lv_color_make(220, 130, 240),   // 3 T -- magenta
	lv_color_make(120, 220, 110),   // 4 S -- green
	lv_color_make(240,  90,  90),   // 5 Z -- red
	lv_color_make(110, 150, 240),   // 6 J -- blue
	lv_color_make(255, 140,  30),   // 7 L -- sunset orange
};

// Utility: read the (r,c) cell of piece `t` at rotation `rot`. Bounds are
// validated by callers so this is a tight inline helper.
inline bool pieceCell(uint8_t t, uint8_t rot, uint8_t r, uint8_t c) {
	return kShapes[t][rot][r][c] != 0;
}

// Pull a uniform random integer in [0, n). Uses Arduino's `random()` helper
// so we inherit the platform's seeding and avoid <random>'s code-size cost.
inline uint8_t rand_u8(uint8_t n) {
	return static_cast<uint8_t>(random(n));
}

// Tiny helper - drop a borderless rectangle into `parent` at (x, y) with
// the given size and colour. Used by buildBoard() / buildSidebar() so the
// individual cells stay terse to read.
lv_obj_t* makeCell(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
                   lv_coord_t w, lv_coord_t h) {
	auto* o = lv_obj_create(parent);
	lv_obj_remove_style_all(o);
	lv_obj_set_size(o, w, h);
	lv_obj_set_pos(o, x, y);
	lv_obj_set_style_bg_color(o, MP_DIM, 0);
	lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(o, 0, 0);
	lv_obj_set_style_radius(o, 0, 0);
	lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
	return o;
}

} // namespace

// ---------- ctor / dtor --------------------------------------------------

PhoneTetris::PhoneTetris()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr) {

	// Zero out cell tables / state arrays defensively. C++ already
	// zero-initialises them via brace-list aggregate rules, but writing it
	// out explicitly keeps the lifecycle obvious.
	for(uint8_t r = 0; r < Rows; ++r) {
		for(uint8_t c = 0; c < Cols; ++c) {
			cells[r][c] = nullptr;
			board[r][c] = 0;
		}
		clearedRows[r] = false;
	}
	for(uint8_t r = 0; r < PieceCells; ++r) {
		for(uint8_t c = 0; c < PieceCells; ++c) {
			previewCells[r][c] = nullptr;
		}
	}
	for(uint8_t i = 0; i < PieceCount; ++i) bag[i] = i;

	// Full-screen container, no scrollbars, no padding -- same blank-canvas
	// pattern PhoneCalculator / PhoneStopwatch / PhoneGamesScreen use.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of the LVGL z-order.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: signal | clock | battery (10 px tall).
	statusBar = new PhoneStatusBar(obj);

	buildTitle();
	buildBoard();
	buildSidebar();
	buildOverlay();

	// Bottom: phone-style softkeys. Captions are state-driven, see
	// refreshSoftKeys(). Initial pair == Idle pair ("START" / "BACK").
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("START");
	softKeys->setRight("BACK");

	// Seed the bag, queue a "next" piece so the preview is non-empty even
	// in the Idle state. The piece is not actually placed on the board
	// until the player presses START.
	refillBag();
	nextPiece = pullFromBag();

	// Initial paint -- show the empty well + "PRESS START" overlay.
	enterIdle();
}

PhoneTetris::~PhoneTetris() {
	stopDropTimer();
	stopLineClearTimer();
	stopLevelUpTimer();
	// All children are parented to obj; LVGL frees them recursively when
	// the screen's obj is destroyed by the LVScreen base destructor.
}

void PhoneTetris::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneTetris::onStop() {
	Input::getInstance()->removeListener(this);
	// Defensive: stop any active timers if the screen is popped mid-game.
	stopDropTimer();
	stopLineClearTimer();
	stopLevelUpTimer();
}

// ---------- build helpers ------------------------------------------------

void PhoneTetris::buildTitle() {
	titleLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(titleLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(titleLabel, MP_ACCENT, 0);
	lv_label_set_text(titleLabel, "TETRIS");
	lv_obj_set_align(titleLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(titleLabel, TitleY);
}

void PhoneTetris::buildBoard() {
	// 1-px MP_DIM border around the playfield (52x82 -- one px gutter on
	// each side). Drawn as an empty container with border_width=1; cells
	// sit inside at their own absolute coordinates so we keep everything
	// pinned under our explicit layout.
	boardFrame = lv_obj_create(obj);
	lv_obj_remove_style_all(boardFrame);
	lv_obj_set_size(boardFrame, BoardW + 2, BoardH + 2);
	lv_obj_set_pos(boardFrame, BoardX - 1, BoardY - 1);
	lv_obj_set_style_bg_color(boardFrame, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(boardFrame, LV_OPA_80, 0);
	lv_obj_set_style_border_color(boardFrame, MP_DIM, 0);
	lv_obj_set_style_border_width(boardFrame, 1, 0);
	lv_obj_set_style_radius(boardFrame, 0, 0);
	lv_obj_set_style_pad_all(boardFrame, 0, 0);
	lv_obj_clear_flag(boardFrame, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(boardFrame, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(boardFrame, LV_OBJ_FLAG_IGNORE_LAYOUT);

	// 10x16 grid of 5x5 cells, all hidden by default. Each cell is an
	// independent lv_obj rectangle so the renderer can update them in
	// any order without re-layout cost.
	for(uint8_t r = 0; r < Rows; ++r) {
		for(uint8_t c = 0; c < Cols; ++c) {
			const lv_coord_t x = BoardX + c * CellPx;
			const lv_coord_t y = BoardY + r * CellPx;
			cells[r][c] = makeCell(obj, x, y, CellPx, CellPx);
			lv_obj_add_flag(cells[r][c], LV_OBJ_FLAG_IGNORE_LAYOUT);
		}
	}
}

void PhoneTetris::buildSidebar() {
	// "NEXT" caption (centred over the preview frame).
	nextCaption = lv_label_create(obj);
	lv_obj_set_style_text_font(nextCaption, &pixelbasic7, 0);
	lv_obj_set_style_text_color(nextCaption, MP_LABEL_DIM, 0);
	lv_label_set_text(nextCaption, "NEXT");
	lv_obj_set_pos(nextCaption, SideX + 12, BoardY);

	// Preview frame: 18x18 with 1-px MP_DIM border. The 4x4 preview cells
	// inside are 4 px each, anchored to the frame's local origin so we can
	// move the frame without re-laying-out the cells.
	const lv_coord_t pvW = PieceCells * PreviewCellPx + 2;   // 4*4+2 = 18
	const lv_coord_t pvH = PieceCells * PreviewCellPx + 2;
	const lv_coord_t pvX = SideX + 10;                       // 68
	const lv_coord_t pvY = BoardY + 9;                       // 33

	previewFrame = lv_obj_create(obj);
	lv_obj_remove_style_all(previewFrame);
	lv_obj_set_size(previewFrame, pvW, pvH);
	lv_obj_set_pos(previewFrame, pvX, pvY);
	lv_obj_set_style_bg_color(previewFrame, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(previewFrame, LV_OPA_70, 0);
	lv_obj_set_style_border_color(previewFrame, MP_DIM, 0);
	lv_obj_set_style_border_width(previewFrame, 1, 0);
	lv_obj_set_style_radius(previewFrame, 0, 0);
	lv_obj_set_style_pad_all(previewFrame, 0, 0);
	lv_obj_clear_flag(previewFrame, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(previewFrame, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(previewFrame, LV_OBJ_FLAG_IGNORE_LAYOUT);

	for(uint8_t r = 0; r < PieceCells; ++r) {
		for(uint8_t c = 0; c < PieceCells; ++c) {
			const lv_coord_t x = pvX + 1 + c * PreviewCellPx;
			const lv_coord_t y = pvY + 1 + r * PreviewCellPx;
			previewCells[r][c] = makeCell(obj, x, y,
			                              PreviewCellPx, PreviewCellPx);
			lv_obj_add_flag(previewCells[r][c], LV_OBJ_FLAG_IGNORE_LAYOUT);
		}
	}

	// Stat captions + value labels, vertically stacked under the preview.
	auto buildStat = [this](lv_obj_t*& cap, lv_obj_t*& val,
	                        const char* capText, lv_coord_t y) {
		cap = lv_label_create(obj);
		lv_obj_set_style_text_font(cap, &pixelbasic7, 0);
		lv_obj_set_style_text_color(cap, MP_LABEL_DIM, 0);
		lv_label_set_text(cap, capText);
		lv_obj_set_pos(cap, SideX + 4, y);

		val = lv_label_create(obj);
		lv_obj_set_style_text_font(val, &pixelbasic7, 0);
		lv_obj_set_style_text_color(val, MP_TEXT, 0);
		lv_label_set_text(val, "0");
		lv_obj_set_pos(val, SideX + 4, y + 9);
	};

	buildStat(scoreCaption, scoreLabel, "SCORE", 56);
	buildStat(linesCaption, linesLabel, "LINES", 76);
	buildStat(levelCaption, levelLabel, "LEVEL", 96);
}

void PhoneTetris::buildOverlay() {
	// Overlay sits in front of the playfield (LVGL z-order = creation
	// order), centred in the board. Used for "PRESS START" / "PAUSED" /
	// "GAME OVER" text. Shown/hidden by refreshOverlay().
	overlayLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(overlayLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(overlayLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_style_text_align(overlayLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(overlayLabel, "PRESS\nSTART");
	// Centre over the board: BoardX..BoardX+BoardW horizontally, and the
	// vertical midline of the board for the y-axis. We use absolute
	// coords (no LV_ALIGN) so the label cooperates with our IGNORE_LAYOUT
	// pinned cells around it.
	lv_obj_set_pos(overlayLabel, BoardX + 6, BoardY + (BoardH / 2) - 8);
	lv_obj_add_flag(overlayLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
}

// ---------- state transitions --------------------------------------------

void PhoneTetris::enterIdle() {
	state = GameState::Idle;
	stopDropTimer();
	stopLineClearTimer();
	resetBoard();
	score = 0;
	lines = 0;
	level = 0;
	pieceType = 0;
	pieceRot  = 0;
	pieceX = 0;
	pieceY = -4;     // off-screen so render() does not stamp anything
	render();
	renderPreview();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneTetris::startGame() {
	state = GameState::Playing;
	score = 0;
	lines = 0;
	level = 0;
	resetBoard();
	// Make sure the next-piece queue is fresh for the new game.
	bagIndex = PieceCount;
	refillBag();
	nextPiece = pullFromBag();
	spawnPiece();
	render();
	renderPreview();
	refreshHud();
	refreshOverlay();
	refreshSoftKeys();
	// spawnPiece can flip state to GameOver if the spawn cell is blocked
	// (e.g. an immediate topout in pathological layouts). Skip the timer
	// in that case so we do not create a no-op timer.
	if(state == GameState::Playing) {
		startDropTimer();
	}
}

void PhoneTetris::pauseGame() {
	if(state != GameState::Playing) return;
	state = GameState::Paused;
	stopDropTimer();
	refreshOverlay();
	refreshSoftKeys();
}

void PhoneTetris::resumeGame() {
	if(state != GameState::Paused) return;
	state = GameState::Playing;
	startDropTimer();
	refreshOverlay();
	refreshSoftKeys();
}

void PhoneTetris::endGame() {
	state = GameState::GameOver;
	// NOTE: do NOT call stopDropTimer() here. endGame() is reachable from
	// inside the drop-timer callback (softDrop -> lockPiece -> spawnPiece
	// -> endGame), and lv_timer_del from inside its own callback is risky.
	// The next drop tick will bail early because state != Playing, and the
	// timer is then properly torn down on the next lifecycle boundary
	// (pauseGame / enterIdle / onStop / destructor).
	stopLineClearTimer();
	refreshOverlay();
	refreshSoftKeys();
}

// ---------- core game ops ------------------------------------------------

void PhoneTetris::resetBoard() {
	for(uint8_t r = 0; r < Rows; ++r) {
		for(uint8_t c = 0; c < Cols; ++c) {
			board[r][c] = 0;
		}
		clearedRows[r] = false;
	}
	lastActionRotation = false; // (S72)
	pendingTSpin       = false; // (S72)
}

void PhoneTetris::refillBag() {
	for(uint8_t i = 0; i < PieceCount; ++i) bag[i] = i;
	// Fisher-Yates shuffle. Arduino's random() is fine here (we are not
	// trying to be cryptographically uniform, just to avoid same-piece
	// streaks longer than a single bag).
	for(uint8_t i = PieceCount - 1; i > 0; --i) {
		const uint8_t j = rand_u8(static_cast<uint8_t>(i + 1));
		const uint8_t tmp = bag[i];
		bag[i] = bag[j];
		bag[j] = tmp;
	}
	bagIndex = 0;
}

uint8_t PhoneTetris::pullFromBag() {
	if(bagIndex >= PieceCount) refillBag();
	return bag[bagIndex++];
}

void PhoneTetris::spawnPiece() {
	// Promote the queued next-piece to the active piece, queue a fresh
	// next-piece. This is the path called both at game start and every
	// time a piece locks.
	pieceType = nextPiece;
	pieceRot  = 0;
	// Spawn near the top middle. Using -1 in y lets the I-piece's empty
	// top row clip cleanly above the well, matching the modern guideline
	// spawn that lets pieces "peek" before fully entering the field.
	pieceX = static_cast<int8_t>((Cols / 2) - 2);
	pieceY = -1;
	lastActionRotation = false; // (S72) fresh piece, no rotation yet
	nextPiece = pullFromBag();
	renderPreview();
	// Top-out check: if the spawn location is already blocked, the well
	// is full and the player loses.
	if(collides(pieceX, pieceY, pieceRot)) {
		endGame();
	}
}

bool PhoneTetris::collides(int8_t nx, int8_t ny, uint8_t nrot) const {
	for(uint8_t r = 0; r < PieceCells; ++r) {
		for(uint8_t c = 0; c < PieceCells; ++c) {
			if(!pieceCell(pieceType, nrot, r, c)) continue;
			const int8_t bc = static_cast<int8_t>(nx + c);
			const int8_t br = static_cast<int8_t>(ny + r);
			// Walls / floor.
			if(bc < 0 || bc >= static_cast<int8_t>(Cols)) return true;
			if(br >= static_cast<int8_t>(Rows))           return true;
			// Allow negative row (piece partially above the well).
			if(br < 0) continue;
			if(board[br][bc] != 0) return true;
		}
	}
	return false;
}

void PhoneTetris::lockPiece() {
	for(uint8_t r = 0; r < PieceCells; ++r) {
		for(uint8_t c = 0; c < PieceCells; ++c) {
			if(!pieceCell(pieceType, pieceRot, r, c)) continue;
			const int8_t bc = static_cast<int8_t>(pieceX + c);
			const int8_t br = static_cast<int8_t>(pieceY + r);
			if(br < 0 || br >= static_cast<int8_t>(Rows)) continue;
			if(bc < 0 || bc >= static_cast<int8_t>(Cols)) continue;
			board[br][bc] = static_cast<uint8_t>(pieceType + 1);
		}
	}

	// (S72) Latch the T-spin verdict for this piece. Done after stamping
	// because none of the T's rotations cover the corner cells around
	// its centre, so stamping is a no-op for the corner sample.
	pendingTSpin = detectTSpin();

	// Detect full rows. If any, kick off the line-clear flash animation;
	// the real collapse + score award happens in the timer callback so
	// the player sees the flash before the rows disappear.
	const uint8_t cleared = findFullRows();
	if(cleared > 0) {
		state = GameState::LineClear;
		// drop timer keeps ticking but onDropTimerStatic bails because
		// state != Playing. Avoiding the in-callback stopDropTimer() here
		// keeps lv_timer_del out of its own callback (see endGame()).
		render();   // paint the cleared rows in flash colour
		startLineClearTimer();
		return;
	}
	// (S72) No lines cleared but a T-spin still earns its base bonus.
	if(pendingTSpin) {
		awardLineScore(0, true);
		refreshHud();
		pendingTSpin = false;
	}
	// No lines cleared: spawn the next piece directly.
	spawnPiece();
	render();
}

uint8_t PhoneTetris::findFullRows() {
	uint8_t cleared = 0;
	for(uint8_t r = 0; r < Rows; ++r) {
		bool full = true;
		for(uint8_t c = 0; c < Cols; ++c) {
			if(board[r][c] == 0) { full = false; break; }
		}
		clearedRows[r] = full;
		if(full) ++cleared;
	}
	return cleared;
}

void PhoneTetris::awardLineScore(uint8_t cleared, bool tSpin) {
	static const uint16_t kBase[5]      = {   0, 100,  300,  500,  800 };
	// (S72) T-spin payout table, indexed by lines cleared. Index 0 is
	// a "T-spin no lines" setup move; 1-3 stack the bonus on top of
	// the line clear; 4 mirrors triple as a safety landing.
	static const uint16_t kTSpinBase[5] = { 400, 800, 1200, 1600, 1600 };
	const uint8_t  idx  = (cleared > 4 ? 4 : cleared);
	const uint16_t base = tSpin ? kTSpinBase[idx] : kBase[idx];
	const uint32_t add  = static_cast<uint32_t>(base) *
	                      static_cast<uint32_t>(level + 1);
	score += add;
	lines += cleared;

	// Level-up curve: every LinesPerLevel cleared lines bumps the level
	// by 1 (so the drop interval gets ~15 % shorter). Cap at 99 so the
	// HUD label width stays predictable.
	const uint8_t prevLevel = level;
	const uint8_t newLevel  = static_cast<uint8_t>(lines / LinesPerLevel);
	if(newLevel != level) {
		level = newLevel > 99 ? 99 : newLevel;
	}
	// (S72) Brief HUD flash whenever the level actually changed.
	if(level != prevLevel) {
		startLevelUpFlash();
	}
}

void PhoneTetris::collapseClearedRows() {
	// Collapse from the bottom: find each cleared row, shift everything
	// above it down by one, then re-check the same row index.
	for(int r = Rows - 1; r >= 0; --r) {
		if(!clearedRows[r]) continue;
		for(int rr = r; rr > 0; --rr) {
			for(uint8_t c = 0; c < Cols; ++c) {
				board[rr][c] = board[rr - 1][c];
			}
			clearedRows[rr] = clearedRows[rr - 1];
		}
		// Top row becomes empty.
		for(uint8_t c = 0; c < Cols; ++c) board[0][c] = 0;
		clearedRows[0] = false;
		// Re-check the same row index because it now holds what used to
		// be one row up (which itself might have been cleared).
		++r;
	}
	// Just to be safe, wipe the cleared-row markers entirely.
	for(uint8_t r = 0; r < Rows; ++r) clearedRows[r] = false;
}

void PhoneTetris::moveLeft() {
	if(state != GameState::Playing) return;
	if(!collides(pieceX - 1, pieceY, pieceRot)) {
		--pieceX;
		lastActionRotation = false; // (S72) lateral move kills T-spin
		render();
	}
}

void PhoneTetris::moveRight() {
	if(state != GameState::Playing) return;
	if(!collides(pieceX + 1, pieceY, pieceRot)) {
		++pieceX;
		lastActionRotation = false; // (S72) lateral move kills T-spin
		render();
	}
}

void PhoneTetris::rotateCW() {
	if(state != GameState::Playing) return;
	const uint8_t nrot = static_cast<uint8_t>((pieceRot + 1) % Rotations);
	if(!collides(pieceX, pieceY, nrot)) {
		pieceRot = nrot;
		lastActionRotation = true; // (S72) latch for T-spin detection
		render();
	}
}

bool PhoneTetris::softDrop() {
	if(state != GameState::Playing) return true;
	if(!collides(pieceX, pieceY + 1, pieceRot)) {
		++pieceY;
		lastActionRotation = false; // (S72) drop step kills T-spin
		render();
		return true;
	}
	// Hit the bottom -> lock the piece in place.
	lockPiece();
	return false;
}

void PhoneTetris::hardDrop() {
	if(state != GameState::Playing) return;
	// (S72) Track whether the piece actually moved during the slam --
	// a hardDrop from an already-resting position can still latch a
	// T-spin if the previous action was a rotation.
	bool moved = false;
	while(!collides(pieceX, pieceY + 1, pieceRot)) {
		++pieceY;
		moved = true;
	}
	if(moved) lastActionRotation = false;
	lockPiece();
}

// ---------- timers -------------------------------------------------------

uint32_t PhoneTetris::currentDropMs() const {
	float ms = static_cast<float>(StartTickMs);
	for(uint8_t i = 0; i < level; ++i) ms *= LevelScale;
	if(ms < static_cast<float>(MinTickMs)) ms = static_cast<float>(MinTickMs);
	return static_cast<uint32_t>(ms);
}

void PhoneTetris::startDropTimer() {
	stopDropTimer();
	dropTimer = lv_timer_create(onDropTimerStatic, currentDropMs(), this);
}

void PhoneTetris::stopDropTimer() {
	if(dropTimer == nullptr) return;
	lv_timer_del(dropTimer);
	dropTimer = nullptr;
}

void PhoneTetris::startLineClearTimer() {
	stopLineClearTimer();
	lineClearTimer = lv_timer_create(onLineClearTimerStatic, LineClearMs, this);
	lv_timer_set_repeat_count(lineClearTimer, 1);
}

void PhoneTetris::stopLineClearTimer() {
	if(lineClearTimer == nullptr) return;
	lv_timer_del(lineClearTimer);
	lineClearTimer = nullptr;
}

void PhoneTetris::onDropTimerStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneTetris*>(timer->user_data);
	if(self == nullptr) return;
	if(self->state != GameState::Playing) return;

	self->softDrop();
	// We do NOT call lv_timer_set_period here -- that API is only
	// guaranteed on LVGL 8.3+, and the PhoneBatteryIcon comment in this
	// repo explicitly avoids it. The drop period is re-set naturally
	// when the line-clear timer callback restarts the drop timer with
	// startDropTimer() (which picks up the new currentDropMs()).
}

void PhoneTetris::onLineClearTimerStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneTetris*>(timer->user_data);
	if(self == nullptr) return;

	// Tally lines + collapse rows + drop next piece + resume.
	uint8_t cleared = 0;
	for(uint8_t r = 0; r < Rows; ++r) if(self->clearedRows[r]) ++cleared;
	self->awardLineScore(cleared, self->pendingTSpin);
	self->pendingTSpin = false;
	self->collapseClearedRows();
	self->refreshHud();

	// Self-deletes (set_repeat_count(1) above).
	self->lineClearTimer = nullptr;

	self->state = GameState::Playing;
	self->spawnPiece();
	self->render();
	// We are inside the line-clear timer's callback (a different timer
	// from the drop timer), so a stop+restart of the drop timer here is
	// safe. Skip the restart entirely if spawnPiece flipped us into
	// GameOver via the topout check.
	self->stopDropTimer();
	if(self->state == GameState::Playing) {
		self->startDropTimer();
	}
}

// ---------- rendering ----------------------------------------------------

void PhoneTetris::render() {
	// 1) Paint the locked board.
	for(uint8_t r = 0; r < Rows; ++r) {
		// In LineClear state, cleared rows draw fully white as the flash.
		const bool flashing = (state == GameState::LineClear) && clearedRows[r];
		for(uint8_t c = 0; c < Cols; ++c) {
			const uint8_t v = board[r][c];
			lv_obj_t* cell = cells[r][c];
			if(cell == nullptr) continue;
			if(flashing) {
				lv_obj_clear_flag(cell, LV_OBJ_FLAG_HIDDEN);
				lv_obj_set_style_bg_color(cell, lv_color_white(), 0);
			} else if(v != 0) {
				lv_obj_clear_flag(cell, LV_OBJ_FLAG_HIDDEN);
				lv_obj_set_style_bg_color(cell, kColors[v], 0);
			} else {
				lv_obj_add_flag(cell, LV_OBJ_FLAG_HIDDEN);
			}
		}
	}

	// We only draw the ghost + active piece while actually playing. In
	// Idle / Paused / GameOver / LineClear we either have no piece or
	// do not want to draw it.
	if(state != GameState::Playing) return;

	// 2) (S72) Paint the ghost piece -- a dimmed projection of where
	// the active piece would land if hard-dropped from its current
	// (X, rotation). Sits above the locked board but below the active
	// piece, so an active cell always overrides the ghost on overlap.
	// We deliberately reuse MP_DIM (the same purple as the empty cell
	// look) so the ghost reads as "about to be filled" rather than as
	// a competing piece.
	int8_t ghostY = pieceY;
	while(!collides(pieceX, static_cast<int8_t>(ghostY + 1), pieceRot)) ++ghostY;
	if(ghostY != pieceY) {
		for(uint8_t r = 0; r < PieceCells; ++r) {
			for(uint8_t c = 0; c < PieceCells; ++c) {
				if(!pieceCell(pieceType, pieceRot, r, c)) continue;
				const int8_t bc = static_cast<int8_t>(pieceX + c);
				const int8_t br = static_cast<int8_t>(ghostY + r);
				if(br < 0 || br >= static_cast<int8_t>(Rows)) continue;
				if(bc < 0 || bc >= static_cast<int8_t>(Cols)) continue;
				if(board[br][bc] != 0) continue; // skip locked cells
				lv_obj_t* cell = cells[br][bc];
				if(cell == nullptr) continue;
				lv_obj_clear_flag(cell, LV_OBJ_FLAG_HIDDEN);
				lv_obj_set_style_bg_color(cell, MP_DIM, 0);
			}
		}
	}

	// 3) Stamp the active falling piece on top of the ghost + board.
	for(uint8_t r = 0; r < PieceCells; ++r) {
		for(uint8_t c = 0; c < PieceCells; ++c) {
			if(!pieceCell(pieceType, pieceRot, r, c)) continue;
			const int8_t bc = static_cast<int8_t>(pieceX + c);
			const int8_t br = static_cast<int8_t>(pieceY + r);
			if(br < 0 || br >= static_cast<int8_t>(Rows)) continue;
			if(bc < 0 || bc >= static_cast<int8_t>(Cols)) continue;
			lv_obj_t* cell = cells[br][bc];
			if(cell == nullptr) continue;
			lv_obj_clear_flag(cell, LV_OBJ_FLAG_HIDDEN);
			lv_obj_set_style_bg_color(cell, kColors[pieceType + 1], 0);
		}
	}
}

void PhoneTetris::renderPreview() {
	for(uint8_t r = 0; r < PieceCells; ++r) {
		for(uint8_t c = 0; c < PieceCells; ++c) {
			lv_obj_t* cell = previewCells[r][c];
			if(cell == nullptr) continue;
			if(pieceCell(nextPiece, 0, r, c)) {
				lv_obj_clear_flag(cell, LV_OBJ_FLAG_HIDDEN);
				lv_obj_set_style_bg_color(cell, kColors[nextPiece + 1], 0);
			} else {
				lv_obj_add_flag(cell, LV_OBJ_FLAG_HIDDEN);
			}
		}
	}
}

void PhoneTetris::refreshHud() {
	if(scoreLabel != nullptr) {
		char buf[12];
		snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(score));
		lv_label_set_text(scoreLabel, buf);
	}
	if(linesLabel != nullptr) {
		char buf[8];
		snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(lines));
		lv_label_set_text(linesLabel, buf);
	}
	if(levelLabel != nullptr) {
		char buf[6];
		snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(level));
		lv_label_set_text(levelLabel, buf);
	}
}

void PhoneTetris::refreshSoftKeys() {
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
		case GameState::LineClear:
			softKeys->setLeft("");
			softKeys->setRight("BACK");
			break;
		case GameState::GameOver:
			softKeys->setLeft("RETRY");
			softKeys->setRight("BACK");
			break;
	}
}

void PhoneTetris::refreshOverlay() {
	if(overlayLabel == nullptr) return;
	switch(state) {
		case GameState::Idle:
			lv_label_set_text(overlayLabel, "PRESS\nSTART");
			lv_obj_set_style_text_color(overlayLabel, MP_HIGHLIGHT, 0);
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
		case GameState::Playing:
			lv_obj_add_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
		case GameState::Paused:
			lv_label_set_text(overlayLabel, "PAUSED");
			lv_obj_set_style_text_color(overlayLabel, MP_ACCENT, 0);
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
		case GameState::LineClear:
			lv_obj_add_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
		case GameState::GameOver:
			lv_label_set_text(overlayLabel, "GAME\nOVER");
			lv_obj_set_style_text_color(overlayLabel, MP_ACCENT, 0);
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
	}
}

// ---------- T-spin detection (S72) -------------------------------------

bool PhoneTetris::detectTSpin() const {
	// Only T pieces can T-spin (T's piece-table index is 2).
	if(pieceType != 2)        return false;
	if(!lastActionRotation)   return false;

	// The T-piece's centre cell is (1, 1) in the 4x4 piece-local box for
	// every rotation in our shape table -- verified by inspection of
	// kShapes[2][0..3]: (1, 1) is filled in all four rotations.
	const int8_t cy = static_cast<int8_t>(pieceY + 1);
	const int8_t cx = static_cast<int8_t>(pieceX + 1);

	// Three-corner rule: count the four diagonal neighbours of the
	// centre that are blocked (wall, floor, or any non-zero board
	// cell). Cells above the playfield (br < 0) count as open so we
	// match the modern guideline behaviour.
	static constexpr int8_t kCornerOff[4][2] = {
		{-1, -1}, {-1, 1}, {1, -1}, {1, 1}
	};
	uint8_t blocked = 0;
	for(uint8_t i = 0; i < 4; ++i) {
		const int8_t cr = static_cast<int8_t>(cy + kCornerOff[i][0]);
		const int8_t cc = static_cast<int8_t>(cx + kCornerOff[i][1]);
		bool isBlocked = false;
		if(cc < 0 || cc >= static_cast<int8_t>(Cols))      isBlocked = true;
		else if(cr >= static_cast<int8_t>(Rows))           isBlocked = true;
		else if(cr < 0)                                    isBlocked = false;
		else if(board[cr][cc] != 0)                        isBlocked = true;
		if(isBlocked) ++blocked;
	}
	return blocked >= 3;
}

// ---------- level-up flash (S72) ---------------------------------------

void PhoneTetris::startLevelUpFlash() {
	// Recolour the LEVEL caption + value to MP_ACCENT, then schedule a
	// one-shot timer that flips them back. The drop-speed change itself
	// kicks in via the existing stop+start of the drop timer in the
	// line-clear callback (which calls awardLineScore -> us). This is
	// purely a visual cue.
	if(levelLabel   != nullptr) lv_obj_set_style_text_color(levelLabel,   MP_ACCENT, 0);
	if(levelCaption != nullptr) lv_obj_set_style_text_color(levelCaption, MP_ACCENT, 0);
	stopLevelUpTimer();
	levelUpTimer = lv_timer_create(onLevelUpTimerStatic, 600, this);
	if(levelUpTimer != nullptr) lv_timer_set_repeat_count(levelUpTimer, 1);
}

void PhoneTetris::stopLevelUpTimer() {
	if(levelUpTimer == nullptr) return;
	lv_timer_del(levelUpTimer);
	levelUpTimer = nullptr;
}

void PhoneTetris::onLevelUpTimerStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneTetris*>(timer->user_data);
	if(self == nullptr) return;
	if(self->levelLabel   != nullptr) lv_obj_set_style_text_color(self->levelLabel,   MP_TEXT,      0);
	if(self->levelCaption != nullptr) lv_obj_set_style_text_color(self->levelCaption, MP_LABEL_DIM, 0);
	// Self-deletes via set_repeat_count(1).
	self->levelUpTimer = nullptr;
}

// ---------- input --------------------------------------------------------

void PhoneTetris::buttonPressed(uint i) {
	switch(i) {
		case BTN_4:
		case BTN_LEFT:
			moveLeft();
			break;

		case BTN_6:
		case BTN_RIGHT:
			moveRight();
			break;

		case BTN_8:
			softDrop();
			break;

		case BTN_2:
		case BTN_L:
			rotateCW();
			break;

		case BTN_5:
		case BTN_R:
			hardDrop();
			break;

		case BTN_ENTER:
			if(softKeys != nullptr) softKeys->flashLeft();
			switch(state) {
				case GameState::Idle:      startGame();   break;
				case GameState::Playing:   pauseGame();   break;
				case GameState::Paused:    resumeGame();  break;
				case GameState::LineClear:                 break; // ignore
				case GameState::GameOver:  enterIdle();   break;
			}
			break;

		case BTN_BACK:
			if(softKeys != nullptr) softKeys->flashRight();
			pop();
			break;

		default:
			break;
	}
}
