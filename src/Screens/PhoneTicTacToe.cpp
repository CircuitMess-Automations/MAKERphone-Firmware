#include "PhoneTicTacToe.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <stdlib.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - identical to every other Phone* widget so
// the screen sits beside PhoneTetris (S71/72), PhoneBantumi (S76),
// PhoneBubbleSmile (S77/78), PhoneMinesweeper (S79), PhoneSlidingPuzzle
// (S80) without a visual seam.
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)  // dim purple captions

// Win-line lookup: every triple of cell indices that constitutes a line
// on the 3x3 board. Three rows, three columns, two diagonals -- 8 in
// total. Used by both findWinningLine() (post-move resolution) and the
// CPU's "win-now / block-now" search.
namespace {
constexpr uint8_t kLines[8][3] = {
	{ 0, 1, 2 }, { 3, 4, 5 }, { 6, 7, 8 },   // rows
	{ 0, 3, 6 }, { 1, 4, 7 }, { 2, 5, 8 },   // columns
	{ 0, 4, 8 }, { 2, 4, 6 },                // diagonals
};
} // namespace

// ===========================================================================
// ctor / dtor
// ===========================================================================

PhoneTicTacToe::PhoneTicTacToe()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr) {

	for(uint8_t i = 0; i < CellCount; ++i) {
		cellSprites[i] = nullptr;
		cellLabels[i]  = nullptr;
		board[i]       = Mark::Empty;
	}

	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildHud();
	buildCells();
	buildOverlay();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("PLACE");
	softKeys->setRight("BACK");

	newRound();
}

PhoneTicTacToe::~PhoneTicTacToe() {
	cancelCpuTimer();
	// All children parented to obj; LVScreen frees them.
}

void PhoneTicTacToe::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneTicTacToe::onStop() {
	Input::getInstance()->removeListener(this);
	cancelCpuTimer();
}

// ===========================================================================
// build helpers
// ===========================================================================

void PhoneTicTacToe::buildHud() {
	// Three small score badges arranged left-mid-right inside the HUD
	// strip. pixelbasic7 in cyan / orange / dim cream so YOU vs CPU vs
	// TIE read distinctly without a separator glyph.
	hudYouLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudYouLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudYouLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(hudYouLabel, "YOU 00");
	lv_obj_set_pos(hudYouLabel, 4, HudY + 2);

	hudCpuLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudCpuLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudCpuLabel, MP_ACCENT, 0);
	lv_label_set_text(hudCpuLabel, "CPU 00");
	lv_obj_set_align(hudCpuLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hudCpuLabel, HudY + 2);

	hudTieLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudTieLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudTieLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(hudTieLabel, "TIE 00");
	lv_obj_set_pos(hudTieLabel, 120, HudY + 2);
}

void PhoneTicTacToe::buildOverlay() {
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

void PhoneTicTacToe::buildCells() {
	// One sprite + one mark label per cell. Sprites are 29x29 (1 px gap)
	// pinned to the (col, row) grid origin; labels are pixelbasic16,
	// re-tinted at render time depending on which mark sits in the cell.
	for(uint8_t row = 0; row < BoardSize; ++row) {
		for(uint8_t col = 0; col < BoardSize; ++col) {
			const uint8_t cell = indexOf(col, row);

			auto* s = lv_obj_create(obj);
			lv_obj_remove_style_all(s);
			lv_obj_set_size(s, CellPx - 1, CellPx - 1);
			lv_obj_set_pos(s,
			               BoardOriginX + col * CellPx,
			               BoardOriginY + row * CellPx);
			lv_obj_set_style_bg_color(s, MP_BG_DARK, 0);
			lv_obj_set_style_bg_opa(s, LV_OPA_70, 0);
			lv_obj_set_style_border_color(s, MP_DIM, 0);
			lv_obj_set_style_border_width(s, 1, 0);
			lv_obj_set_style_radius(s, 2, 0);
			lv_obj_set_style_pad_all(s, 0, 0);
			lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_clear_flag(s, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_flag(s, LV_OBJ_FLAG_IGNORE_LAYOUT);
			cellSprites[cell] = s;

			auto* lbl = lv_label_create(s);
			lv_obj_set_style_text_font(lbl, &pixelbasic16, 0);
			lv_obj_set_style_text_color(lbl, MP_TEXT, 0);
			lv_obj_set_style_pad_all(lbl, 0, 0);
			lv_label_set_text(lbl, "");
			lv_obj_set_align(lbl, LV_ALIGN_CENTER);
			cellLabels[cell] = lbl;
		}
	}
}

// ===========================================================================
// state transitions
// ===========================================================================

void PhoneTicTacToe::newRound() {
	cancelCpuTimer();

	for(uint8_t i = 0; i < CellCount; ++i) {
		board[i] = Mark::Empty;
	}
	winLine[0] = winLine[1] = winLine[2] = 0xFF;

	// Park the cursor on the centre cell -- the most common opening
	// move and the friendliest default for first-time players.
	cursorCol = 1;
	cursorRow = 1;

	if(playerStartsNext) {
		state = GameState::PlayerTurn;
	} else {
		state = GameState::CpuPending;
	}
	playerStartsNext = !playerStartsNext;

	renderAllCells();
	renderCursor();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();

	if(state == GameState::CpuPending) {
		scheduleCpuMove();
	}
}

void PhoneTicTacToe::placeMark(uint8_t cell, Mark m) {
	if(cell >= CellCount) return;
	if(board[cell] != Mark::Empty) return;
	if(m == Mark::Empty) return;

	board[cell] = m;
	renderCell(cell);
}

void PhoneTicTacToe::afterMove() {
	// Did the player just win?
	uint8_t lineX[3];
	if(findWinningLine(Mark::X, lineX)) {
		winLine[0] = lineX[0]; winLine[1] = lineX[1]; winLine[2] = lineX[2];
		state = GameState::PlayerWon;
		++winsYou;
		renderAllCells();
		refreshHud();
		refreshSoftKeys();
		refreshOverlay();
		return;
	}
	// Did the CPU just win?
	uint8_t lineO[3];
	if(findWinningLine(Mark::O, lineO)) {
		winLine[0] = lineO[0]; winLine[1] = lineO[1]; winLine[2] = lineO[2];
		state = GameState::CpuWon;
		++winsCpu;
		renderAllCells();
		refreshHud();
		refreshSoftKeys();
		refreshOverlay();
		return;
	}
	// Out of squares?
	if(isBoardFull()) {
		state = GameState::Tie;
		++ties;
		refreshHud();
		refreshSoftKeys();
		refreshOverlay();
		return;
	}

	// Game continues -- swap turns. If we just resolved a player move
	// (state was PlayerTurn before the place), it's the CPU's turn now.
	if(state == GameState::PlayerTurn) {
		state = GameState::CpuPending;
		refreshSoftKeys();
		scheduleCpuMove();
	} else {
		// CPU just moved -> player's turn.
		state = GameState::PlayerTurn;
		refreshSoftKeys();
	}
}

void PhoneTicTacToe::scheduleCpuMove() {
	cancelCpuTimer();
	cpuTimer = lv_timer_create(&PhoneTicTacToe::onCpuTimerStatic,
	                           kCpuThinkMs, this);
	if(cpuTimer != nullptr) {
		// One-shot semantics: the callback deletes the timer itself, but
		// we also set repeat_count=1 so a stray run after deletion (e.g.
		// during teardown) is harmless.
		lv_timer_set_repeat_count(cpuTimer, 1);
	}
}

void PhoneTicTacToe::doCpuMove() {
	// Defensive: only act if we're still in the pending state. If the
	// player popped the screen or hit reshuffle while the timer was in
	// flight, cancelCpuTimer() may not have caught the racing fire.
	if(state != GameState::CpuPending) return;

	const uint8_t pick = pickCpuMove();
	if(pick < CellCount && board[pick] == Mark::Empty) {
		placeMark(pick, Mark::O);
	}
	afterMove();
}

uint8_t PhoneTicTacToe::pickCpuMove() const {
	// 1. If the CPU can finish a line, take it.
	int8_t winCell = findWinningCellFor(Mark::O);
	if(winCell >= 0) return static_cast<uint8_t>(winCell);

	// 2. If the player threatens a line, block it.
	int8_t blockCell = findWinningCellFor(Mark::X);
	if(blockCell >= 0) return static_cast<uint8_t>(blockCell);

	// 3. Take the centre.
	if(board[4] == Mark::Empty) return 4;

	// 4. Take a free corner. Order is randomised across the four
	//    corners so the CPU's openings don't feel scripted.
	const uint8_t corners[4] = { 0, 2, 6, 8 };
	uint8_t corderOrder[4] = { 0, 1, 2, 3 };
	for(uint8_t i = 3; i > 0; --i) {
		const uint8_t j = static_cast<uint8_t>(rand() % (i + 1));
		const uint8_t tmp = corderOrder[i];
		corderOrder[i] = corderOrder[j];
		corderOrder[j] = tmp;
	}
	for(uint8_t k = 0; k < 4; ++k) {
		const uint8_t c = corners[corderOrder[k]];
		if(board[c] == Mark::Empty) return c;
	}

	// 5. Take a free edge.
	const uint8_t edges[4] = { 1, 3, 5, 7 };
	uint8_t edgeOrder[4] = { 0, 1, 2, 3 };
	for(uint8_t i = 3; i > 0; --i) {
		const uint8_t j = static_cast<uint8_t>(rand() % (i + 1));
		const uint8_t tmp = edgeOrder[i];
		edgeOrder[i] = edgeOrder[j];
		edgeOrder[j] = tmp;
	}
	for(uint8_t k = 0; k < 4; ++k) {
		const uint8_t e = edges[edgeOrder[k]];
		if(board[e] == Mark::Empty) return e;
	}

	// 6. Should never reach here (board is full -> afterMove() would
	//    have caught a tie), but fall back to the first empty cell.
	for(uint8_t i = 0; i < CellCount; ++i) {
		if(board[i] == Mark::Empty) return i;
	}
	return 0;
}

bool PhoneTicTacToe::isBoardFull() const {
	for(uint8_t i = 0; i < CellCount; ++i) {
		if(board[i] == Mark::Empty) return false;
	}
	return true;
}

bool PhoneTicTacToe::findWinningLine(Mark m, uint8_t out[3]) const {
	for(uint8_t l = 0; l < 8; ++l) {
		const uint8_t a = kLines[l][0];
		const uint8_t b = kLines[l][1];
		const uint8_t c = kLines[l][2];
		if(board[a] == m && board[b] == m && board[c] == m) {
			out[0] = a; out[1] = b; out[2] = c;
			return true;
		}
	}
	return false;
}

int8_t PhoneTicTacToe::findWinningCellFor(Mark m) const {
	// "Two of mine + one empty" search across all 8 lines. Returns the
	// index of the empty cell that completes a line for `m`, or -1.
	for(uint8_t l = 0; l < 8; ++l) {
		const uint8_t a = kLines[l][0];
		const uint8_t b = kLines[l][1];
		const uint8_t c = kLines[l][2];
		uint8_t mineCount = 0;
		uint8_t emptyCount = 0;
		int8_t  emptyIdx = -1;
		const uint8_t cells[3] = { a, b, c };
		for(uint8_t k = 0; k < 3; ++k) {
			const uint8_t cell = cells[k];
			if(board[cell] == m) ++mineCount;
			else if(board[cell] == Mark::Empty) {
				++emptyCount;
				emptyIdx = static_cast<int8_t>(cell);
			}
		}
		if(mineCount == 2 && emptyCount == 1) return emptyIdx;
	}
	return -1;
}

// ===========================================================================
// rendering
// ===========================================================================

void PhoneTicTacToe::renderAllCells() {
	for(uint8_t i = 0; i < CellCount; ++i) {
		renderCell(i);
	}
	renderCursor();
}

void PhoneTicTacToe::renderCell(uint8_t cell) {
	if(cell >= CellCount) return;
	auto* s = cellSprites[cell];
	auto* l = cellLabels[cell];
	if(s == nullptr || l == nullptr) return;

	const Mark mk = board[cell];

	// Default styling. Selection cursor + win-line highlight override
	// these in the post-passes below.
	lv_obj_set_style_bg_color(s, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(s, LV_OPA_70, 0);
	lv_obj_set_style_border_color(s, MP_DIM, 0);
	lv_obj_set_style_border_width(s, 1, 0);

	switch(mk) {
		case Mark::Empty:
			lv_label_set_text(l, "");
			break;
		case Mark::X:
			lv_obj_set_style_text_color(l, MP_HIGHLIGHT, 0);
			lv_label_set_text(l, "X");
			break;
		case Mark::O:
			lv_obj_set_style_text_color(l, MP_ACCENT, 0);
			lv_label_set_text(l, "O");
			break;
	}

	// Win-line highlight: cells that participated in the winning line
	// get a brighter background + accent border.
	const bool winning = (state == GameState::PlayerWon
	                      || state == GameState::CpuWon)
	                     && (cell == winLine[0] || cell == winLine[1]
	                         || cell == winLine[2]);
	if(winning) {
		lv_obj_set_style_bg_color(s, MP_DIM, 0);
		lv_obj_set_style_bg_opa(s, LV_OPA_90, 0);
		lv_obj_set_style_border_color(s,
		                              state == GameState::PlayerWon
		                                  ? MP_HIGHLIGHT : MP_ACCENT,
		                              0);
		lv_obj_set_style_border_width(s, 1, 0);
	}
}

void PhoneTicTacToe::renderCursor() {
	// Reset every cell's border to the default first (so a stale
	// cursor border on the previous cell goes away), then paint the
	// cursor accent. We re-apply the win-line highlight after the
	// reset because that takes precedence over the cursor visually.
	for(uint8_t i = 0; i < CellCount; ++i) {
		renderCell(i);
	}

	// Don't show a cursor frame on finished states -- the win-line is
	// the primary visual then, and a duelling accent border just adds
	// clutter.
	if(state != GameState::PlayerTurn) return;

	const uint8_t cell = indexOf(cursorCol, cursorRow);
	auto* s = cellSprites[cell];
	if(s == nullptr) return;

	// Cursor border colour: cyan if the cell is empty (legal move),
	// dim cream if the cell is already filled (informs "you can't
	// place here" without a separate beep / shake animation).
	lv_obj_set_style_border_color(s,
	                              board[cell] == Mark::Empty
	                                  ? MP_HIGHLIGHT : MP_LABEL_DIM,
	                              0);
	lv_obj_set_style_border_width(s, 2, 0);
	lv_obj_move_foreground(s);
}

void PhoneTicTacToe::refreshHud() {
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
	if(hudTieLabel != nullptr) {
		char buf[12];
		const unsigned t = ties > 99 ? 99 : ties;
		snprintf(buf, sizeof(buf), "TIE %02u", t);
		lv_label_set_text(hudTieLabel, buf);
	}
}

void PhoneTicTacToe::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	switch(state) {
		case GameState::PlayerTurn:
			softKeys->setLeft("PLACE");
			softKeys->setRight("BACK");
			break;
		case GameState::CpuPending:
			softKeys->setLeft("WAIT");
			softKeys->setRight("BACK");
			break;
		case GameState::PlayerWon:
		case GameState::CpuWon:
		case GameState::Tie:
			softKeys->setLeft("AGAIN");
			softKeys->setRight("BACK");
			break;
	}
}

void PhoneTicTacToe::refreshOverlay() {
	if(overlayLabel == nullptr) return;
	switch(state) {
		case GameState::PlayerTurn:
		case GameState::CpuPending:
			lv_obj_add_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			return;
		case GameState::PlayerWon:
			lv_label_set_text(overlayLabel, "YOU WIN!\nA TO PLAY AGAIN");
			lv_obj_set_style_text_color(overlayLabel, MP_HIGHLIGHT, 0);
			lv_obj_set_style_border_color(overlayLabel, MP_HIGHLIGHT, 0);
			break;
		case GameState::CpuWon:
			lv_label_set_text(overlayLabel, "CPU WINS\nA TO TRY AGAIN");
			lv_obj_set_style_text_color(overlayLabel, MP_ACCENT, 0);
			lv_obj_set_style_border_color(overlayLabel, MP_ACCENT, 0);
			break;
		case GameState::Tie:
			lv_label_set_text(overlayLabel, "TIE GAME\nA TO PLAY AGAIN");
			lv_obj_set_style_text_color(overlayLabel, MP_TEXT, 0);
			lv_obj_set_style_border_color(overlayLabel, MP_LABEL_DIM, 0);
			break;
	}
	lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
	lv_obj_move_foreground(overlayLabel);
}

// ===========================================================================
// timer helpers
// ===========================================================================

void PhoneTicTacToe::cancelCpuTimer() {
	if(cpuTimer != nullptr) {
		lv_timer_del(cpuTimer);
		cpuTimer = nullptr;
	}
}

void PhoneTicTacToe::onCpuTimerStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneTicTacToe*>(timer->user_data);
	if(self == nullptr) return;
	// One-shot: clear our cached pointer first so doCpuMove() can
	// reschedule (theoretically) without colliding with the dying timer.
	self->cpuTimer = nullptr;
	self->doCpuMove();
}

// ===========================================================================
// input
// ===========================================================================

void PhoneTicTacToe::buttonPressed(uint i) {
	// BACK always pops out, regardless of state.
	if(i == BTN_BACK) {
		if(softKeys) softKeys->flashRight();
		pop();
		return;
	}

	// Reshuffle works in every state -- "give up" / "play again" share
	// the same key for muscle-memory parity with PhoneSlidingPuzzle.
	if(i == BTN_R) {
		newRound();
		return;
	}

	switch(state) {
		case GameState::PlayerTurn: {
			// Movement (d-pad + numpad both supported).
			if(i == BTN_LEFT || i == BTN_4) {
				if(cursorCol > 0) --cursorCol;
				else cursorCol = static_cast<uint8_t>(BoardSize - 1);
				renderCursor();
				return;
			}
			if(i == BTN_RIGHT || i == BTN_6) {
				cursorCol = static_cast<uint8_t>((cursorCol + 1) % BoardSize);
				renderCursor();
				return;
			}
			if(i == BTN_2) {
				if(cursorRow > 0) --cursorRow;
				else cursorRow = static_cast<uint8_t>(BoardSize - 1);
				renderCursor();
				return;
			}
			if(i == BTN_8) {
				cursorRow = static_cast<uint8_t>((cursorRow + 1) % BoardSize);
				renderCursor();
				return;
			}

			// Place (numpad 5 + ENTER). Illegal placements (cell
			// already filled) just flash the soft key without changing
			// state, matching the gentle-feedback approach of the rest
			// of the Phone* games.
			if(i == BTN_5 || i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				const uint8_t cell = indexOf(cursorCol, cursorRow);
				if(board[cell] != Mark::Empty) return;
				placeMark(cell, Mark::X);
				afterMove();
				return;
			}
			return;
		}

		case GameState::CpuPending: {
			// CPU is "thinking" -- input intentionally locked except
			// BACK / R (handled above). We don't want the player to
			// double-place by mashing buttons through the delay.
			return;
		}

		case GameState::PlayerWon:
		case GameState::CpuWon:
		case GameState::Tie: {
			if(i == BTN_ENTER || i == BTN_5) {
				if(softKeys) softKeys->flashLeft();
				newRound();
				return;
			}
			return;
		}
	}
}
