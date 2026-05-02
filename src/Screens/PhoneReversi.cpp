#include "PhoneReversi.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <climits>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - identical to every other Phone* widget so
// the screen sits beside PhoneTetris (S71/72), PhoneTicTacToe (S81),
// PhoneSokoban (S83/84), PhoneConnectFour (S88) without a visual seam.
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)  // dim purple captions

namespace {

// The eight directions Reversi scans for a captured run from a candidate
// move. Stored as (dCol, dRow). Unlike Connect Four we need every
// polarity here -- a flip line in (-1, 0) is independent from one in
// (+1, 0). Eight directions -> the eight standard chess "queen moves".
struct Dir { int8_t dc; int8_t dr; };
constexpr Dir kDirs[8] = {
	{ -1, -1 }, {  0, -1 }, {  1, -1 },
	{ -1,  0 },             {  1,  0 },
	{ -1,  1 }, {  0,  1 }, {  1,  1 },
};

} // namespace

// ===========================================================================
// ctor / dtor
// ===========================================================================

PhoneReversi::PhoneReversi()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr) {

	for(uint8_t i = 0; i < CellCount; ++i) {
		cellFrames[i] = nullptr;
		cellDiscs[i]  = nullptr;
		board[i]      = Disc::Empty;
	}

	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildHud();
	buildBoard();
	buildOverlay();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("PLACE");
	softKeys->setRight("BACK");

	newRound();
}

PhoneReversi::~PhoneReversi() {
	cancelCpuTimer();
	// All children parented to obj; LVScreen frees them.
}

void PhoneReversi::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneReversi::onStop() {
	Input::getInstance()->removeListener(this);
	cancelCpuTimer();
}

// ===========================================================================
// build helpers
// ===========================================================================

void PhoneReversi::buildHud() {
	// Three small captions arranged left-mid-right inside the HUD strip.
	// pixelbasic7 in cyan / cream / orange so YOU vs status vs CPU read
	// distinctly without a separator glyph -- consistent with
	// PhoneTicTacToe (S81) and PhoneConnectFour (S88).
	hudYouLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudYouLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudYouLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(hudYouLabel, "YOU 02");
	lv_obj_set_pos(hudYouLabel, 4, HudY + 2);

	hudTurnLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudTurnLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudTurnLabel, MP_TEXT, 0);
	lv_label_set_text(hudTurnLabel, "");
	lv_obj_set_align(hudTurnLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hudTurnLabel, HudY + 2);

	hudCpuLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudCpuLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudCpuLabel, MP_ACCENT, 0);
	lv_label_set_text(hudCpuLabel, "CPU 02");
	lv_obj_set_pos(hudCpuLabel, 120, HudY + 2);
}

void PhoneReversi::buildOverlay() {
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

void PhoneReversi::buildBoard() {
	// Frame panel: a single rounded rectangle behind every cell. The
	// dim-purple fill gives the empty cells a "felt board" look once
	// the discs paint over it.
	boardPanel = lv_obj_create(obj);
	lv_obj_remove_style_all(boardPanel);
	lv_obj_set_size(boardPanel, BoardW, BoardH);
	lv_obj_set_pos(boardPanel, BoardOriginX, BoardOriginY);
	lv_obj_set_style_bg_color(boardPanel, MP_DIM, 0);
	lv_obj_set_style_bg_opa(boardPanel, LV_OPA_90, 0);
	lv_obj_set_style_border_color(boardPanel, MP_HIGHLIGHT, 0);
	lv_obj_set_style_border_width(boardPanel, 1, 0);
	lv_obj_set_style_radius(boardPanel, 2, 0);
	lv_obj_set_style_pad_all(boardPanel, 0, 0);
	lv_obj_clear_flag(boardPanel, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(boardPanel, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(boardPanel, LV_OBJ_FLAG_IGNORE_LAYOUT);

	// One cell frame per square + one disc obj per square. The frame
	// is an 11x11 rectangle with a 1 px dim border (the grid) and the
	// disc is a 7x7 child rounded square (radius 3 == circle on this
	// pixel scale) centred inside the frame, hidden when the cell is
	// empty.
	for(uint8_t row = 0; row < BoardRows; ++row) {
		for(uint8_t col = 0; col < BoardCols; ++col) {
			const uint8_t cell = indexOf(col, row);

			auto* frame = lv_obj_create(boardPanel);
			lv_obj_remove_style_all(frame);
			lv_obj_set_size(frame, CellPx, CellPx);
			lv_obj_set_pos(frame,
			               static_cast<lv_coord_t>(col * CellPx),
			               static_cast<lv_coord_t>(row * CellPx));
			lv_obj_set_style_bg_opa(frame, LV_OPA_TRANSP, 0);
			lv_obj_set_style_border_color(frame, MP_BG_DARK, 0);
			lv_obj_set_style_border_width(frame, 1, 0);
			lv_obj_set_style_radius(frame, 0, 0);
			lv_obj_set_style_pad_all(frame, 0, 0);
			lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_clear_flag(frame, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_flag(frame, LV_OBJ_FLAG_IGNORE_LAYOUT);
			cellFrames[cell] = frame;

			auto* disc = lv_obj_create(frame);
			lv_obj_remove_style_all(disc);
			lv_obj_set_size(disc, 7, 7);
			lv_obj_set_pos(disc, 2, 2);
			lv_obj_set_style_bg_color(disc, MP_HIGHLIGHT, 0);
			lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
			lv_obj_set_style_radius(disc, 4, 0); // 4 ~= 7/2 -> circle
			lv_obj_set_style_border_width(disc, 0, 0);
			lv_obj_set_style_pad_all(disc, 0, 0);
			lv_obj_clear_flag(disc, LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_clear_flag(disc, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_flag(disc, LV_OBJ_FLAG_IGNORE_LAYOUT);
			lv_obj_add_flag(disc, LV_OBJ_FLAG_HIDDEN);
			cellDiscs[cell] = disc;
		}
	}
}

// ===========================================================================
// state transitions
// ===========================================================================

void PhoneReversi::newRound() {
	cancelCpuTimer();

	for(uint8_t i = 0; i < CellCount; ++i) {
		board[i] = Disc::Empty;
	}

	// Standard Reversi opening: two discs of each colour at the centre,
	// arranged so each colour's pair sits on a diagonal.
	const uint8_t midA = BoardCols / 2 - 1; // 3
	const uint8_t midB = BoardCols / 2;     // 4
	board[indexOf(midA, midA)] = Disc::Cpu;
	board[indexOf(midB, midA)] = Disc::You;
	board[indexOf(midA, midB)] = Disc::You;
	board[indexOf(midB, midB)] = Disc::Cpu;

	// Cursor parks on a square next to the centre cluster -- a friendly
	// default since every legal opening move is exactly there.
	cursorCol = static_cast<uint8_t>(midA + 1);
	cursorRow = static_cast<uint8_t>(midA - 1);
	if(cursorRow >= BoardRows) cursorRow = 0;

	passNote = PassNote::None;

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

bool PhoneReversi::placeDisc(uint8_t col, uint8_t row, Disc who) {
	if(col >= BoardCols || row >= BoardRows) return false;
	if(who == Disc::Empty) return false;
	if(board[indexOf(col, row)] != Disc::Empty) return false;

	const Disc opp = (who == Disc::You) ? Disc::Cpu : Disc::You;

	bool capturedAny = false;

	// For every direction, walk opponents until we either fall off the
	// board, hit an empty cell, or hit one of our own discs. The third
	// case is the only "capturing" outcome.
	for(uint8_t d = 0; d < 8; ++d) {
		const int8_t dc = kDirs[d].dc;
		const int8_t dr = kDirs[d].dr;
		int8_t c = static_cast<int8_t>(col) + dc;
		int8_t r = static_cast<int8_t>(row) + dr;
		uint8_t run = 0;
		while(isInBounds(c, r)
		      && board[indexOf(static_cast<uint8_t>(c),
		                       static_cast<uint8_t>(r))] == opp) {
			c = static_cast<int8_t>(c + dc);
			r = static_cast<int8_t>(r + dr);
			++run;
		}
		if(run == 0) continue;
		if(!isInBounds(c, r)) continue;
		if(board[indexOf(static_cast<uint8_t>(c),
		                 static_cast<uint8_t>(r))] != who) continue;

		// Walk back from (c-dc, r-dr) toward (col+dc, row+dr) flipping
		// every opponent disc along the line.
		int8_t fc = static_cast<int8_t>(c - dc);
		int8_t fr = static_cast<int8_t>(r - dr);
		while(fc != static_cast<int8_t>(col)
		      || fr != static_cast<int8_t>(row)) {
			board[indexOf(static_cast<uint8_t>(fc),
			              static_cast<uint8_t>(fr))] = who;
			fc = static_cast<int8_t>(fc - dc);
			fr = static_cast<int8_t>(fr - dr);
		}
		capturedAny = true;
	}

	if(!capturedAny) return false;

	// The placed disc itself goes down last so that the per-direction
	// flip walks above can't mistake it for a "wall".
	board[indexOf(col, row)] = who;
	return true;
}

void PhoneReversi::afterMove(Disc who) {
	const Disc opp = (who == Disc::You) ? Disc::Cpu : Disc::You;

	// Whatever pass note was sticking from the previous move is now
	// stale -- the player just made a move, so the indicator clears.
	passNote = PassNote::None;

	renderAllCells();
	refreshHud();

	const bool oppHas = hasAnyLegalMove(opp);
	const bool selfHas = hasAnyLegalMove(who);

	if(oppHas) {
		// Normal turn handover.
		if(opp == Disc::You) {
			state = GameState::PlayerTurn;
			renderCursor();
			refreshSoftKeys();
		} else {
			state = GameState::CpuPending;
			renderCursor();
			refreshSoftKeys();
			scheduleCpuMove();
		}
		return;
	}

	if(selfHas) {
		// Opponent passes -- control bounces back to the active side
		// after a brief notification beat so the player understands
		// what just happened.
		passNote = (opp == Disc::You) ? PassNote::YouPassed
		                              : PassNote::CpuPassed;
		refreshHud();
		if(who == Disc::You) {
			// We just moved; CPU has nothing -> CPU passes -> our turn
			// continues. Use the pass-flash delay so the caption is
			// visible for a beat before the cursor reactivates.
			state = GameState::CpuPending; // borrows the timer slot
			renderCursor();
			refreshSoftKeys();
			schedulePassThenContinue();
		} else {
			// CPU just moved; player has nothing -> player passes ->
			// CPU goes again. Use the same path so the timing reads
			// the same to the player.
			state = GameState::CpuPending;
			renderCursor();
			refreshSoftKeys();
			schedulePassThenContinue();
		}
		return;
	}

	// Neither side can move -- terminal. Decide winner from disc count.
	uint16_t youCount = 0, cpuCount = 0;
	countDiscs(&youCount, &cpuCount);
	if(youCount > cpuCount) {
		state = GameState::PlayerWon;
		++winsYou;
	} else if(cpuCount > youCount) {
		state = GameState::CpuWon;
		++winsCpu;
	} else {
		state = GameState::Tie;
		++ties;
	}
	renderCursor();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneReversi::scheduleCpuMove() {
	cancelCpuTimer();
	cpuTimer = lv_timer_create(&PhoneReversi::onCpuTimerStatic,
	                           kCpuThinkMs, this);
	if(cpuTimer != nullptr) {
		lv_timer_set_repeat_count(cpuTimer, 1);
	}
}

void PhoneReversi::schedulePassThenContinue() {
	// Same timer slot as scheduleCpuMove() -- the timer callback decides
	// what to do based on the current game state and pass note. The
	// "what comes next" decision is intentionally re-derived in the
	// callback rather than baked into a separate field, so the screen
	// state is the single source of truth.
	cancelCpuTimer();
	cpuTimer = lv_timer_create(&PhoneReversi::onCpuTimerStatic,
	                           kPassFlashMs, this);
	if(cpuTimer != nullptr) {
		lv_timer_set_repeat_count(cpuTimer, 1);
	}
}

void PhoneReversi::doCpuMove() {
	// Defensive: only act if we're still in a pending state. If the
	// player popped the screen or hit reshuffle while the timer was in
	// flight, cancelCpuTimer() may not have caught the racing fire.
	if(state != GameState::CpuPending) return;

	// Was the timer actually a "pass continuation" (the active side is
	// You, but we borrowed the CpuPending slot to delay the bounce)?
	if(passNote == PassNote::CpuPassed) {
		// The CPU just passed; control returns to the player.
		state = GameState::PlayerTurn;
		// Note: leave passNote as CpuPassed so the HUD keeps showing
		// the caption until the player makes a real move (it's cleared
		// in afterMove). This matches how a real Othello UI behaves.
		renderCursor();
		refreshSoftKeys();
		return;
	}
	if(passNote == PassNote::YouPassed) {
		// The player just passed; CPU goes again.
		const int8_t pick = pickCpuMove();
		if(pick < 0) {
			// No legal move for CPU either -- afterMove() will end the
			// game when we re-enter it. Synthesise a no-op afterMove
			// for Cpu so the terminal-state branch fires.
			afterMove(Disc::Cpu);
			return;
		}
		const uint8_t col = static_cast<uint8_t>(pick % BoardCols);
		const uint8_t row = static_cast<uint8_t>(pick / BoardCols);
		if(!placeDisc(col, row, Disc::Cpu)) return;
		afterMove(Disc::Cpu);
		return;
	}

	// Vanilla CPU "thinking" timer fired; pick + place a real move.
	const int8_t pick = pickCpuMove();
	if(pick < 0) {
		// Unreachable in normal flow because afterMove() routes the
		// no-move case through schedulePassThenContinue(). Be defensive
		// anyway: pretend CPU made a move and let afterMove() resolve.
		afterMove(Disc::Cpu);
		return;
	}
	const uint8_t col = static_cast<uint8_t>(pick % BoardCols);
	const uint8_t row = static_cast<uint8_t>(pick / BoardCols);
	if(!placeDisc(col, row, Disc::Cpu)) {
		// pickCpuMove returned an illegal cell -- shouldn't happen, but
		// fall back to scanning the board for the first legal move.
		bool placed = false;
		for(uint8_t r = 0; r < BoardRows && !placed; ++r) {
			for(uint8_t c = 0; c < BoardCols && !placed; ++c) {
				if(placeDisc(c, r, Disc::Cpu)) placed = true;
			}
		}
		if(!placed) {
			afterMove(Disc::Cpu);
			return;
		}
	}
	afterMove(Disc::Cpu);
}

int8_t PhoneReversi::pickCpuMove() const {
	int  bestScore   = INT_MIN;
	int8_t bestCells[BoardCols * BoardRows];
	uint8_t bestCount = 0;

	for(uint8_t row = 0; row < BoardRows; ++row) {
		for(uint8_t col = 0; col < BoardCols; ++col) {
			if(board[indexOf(col, row)] != Disc::Empty) continue;
			const uint8_t flips = flipsForMove(col, row, Disc::Cpu);
			if(flips == 0) continue;

			// Positional score on top of the flip count. Corners are
			// gold, X / C squares are awful, edges are slightly
			// preferred over interior cells.
			int score = static_cast<int>(flips) * 2;
			if(isCorner(col, row)) {
				score += 100;
			} else if(isXSquare(col, row)) {
				score -= 20;
			} else if(isCSquare(col, row)) {
				score -= 10;
			} else if(isEdge(col, row)) {
				score += 3;
			}

			if(score > bestScore) {
				bestScore = score;
				bestCount = 0;
				bestCells[bestCount++] = static_cast<int8_t>(indexOf(col, row));
			} else if(score == bestScore && bestCount < BoardCols * BoardRows) {
				bestCells[bestCount++] = static_cast<int8_t>(indexOf(col, row));
			}
		}
	}

	if(bestCount == 0) return -1;
	// Random jitter: pick uniformly among ties so the CPU's openings
	// don't always start in the same corner.
	const uint8_t pickIdx = static_cast<uint8_t>(rand() % bestCount);
	return bestCells[pickIdx];
}

// ===========================================================================
// board queries
// ===========================================================================

bool PhoneReversi::isInBounds(int8_t col, int8_t row) const {
	return col >= 0 && col < static_cast<int8_t>(BoardCols)
	    && row >= 0 && row < static_cast<int8_t>(BoardRows);
}

bool PhoneReversi::hasAnyLegalMove(Disc who) const {
	if(who == Disc::Empty) return false;
	for(uint8_t row = 0; row < BoardRows; ++row) {
		for(uint8_t col = 0; col < BoardCols; ++col) {
			if(board[indexOf(col, row)] != Disc::Empty) continue;
			if(flipsForMove(col, row, who) > 0) return true;
		}
	}
	return false;
}

bool PhoneReversi::isLegalMove(uint8_t col, uint8_t row, Disc who,
                               uint8_t* outFlipsByDir) const {
	const uint8_t flips = flipsForMove(col, row, who);
	if(outFlipsByDir != nullptr) *outFlipsByDir = flips;
	return flips > 0;
}

uint8_t PhoneReversi::flipsForMove(uint8_t col, uint8_t row, Disc who) const {
	if(col >= BoardCols || row >= BoardRows) return 0;
	if(who == Disc::Empty) return 0;
	if(board[indexOf(col, row)] != Disc::Empty) return 0;

	const Disc opp = (who == Disc::You) ? Disc::Cpu : Disc::You;
	uint8_t total = 0;
	for(uint8_t d = 0; d < 8; ++d) {
		const int8_t dc = kDirs[d].dc;
		const int8_t dr = kDirs[d].dr;
		int8_t c = static_cast<int8_t>(col) + dc;
		int8_t r = static_cast<int8_t>(row) + dr;
		uint8_t run = 0;
		while(isInBounds(c, r)
		      && board[indexOf(static_cast<uint8_t>(c),
		                       static_cast<uint8_t>(r))] == opp) {
			c = static_cast<int8_t>(c + dc);
			r = static_cast<int8_t>(r + dr);
			++run;
		}
		if(run == 0) continue;
		if(!isInBounds(c, r)) continue;
		if(board[indexOf(static_cast<uint8_t>(c),
		                 static_cast<uint8_t>(r))] != who) continue;
		total = static_cast<uint8_t>(total + run);
	}
	return total;
}

void PhoneReversi::countDiscs(uint16_t* outYou, uint16_t* outCpu) const {
	uint16_t y = 0;
	uint16_t c = 0;
	for(uint8_t i = 0; i < CellCount; ++i) {
		if(board[i] == Disc::You) ++y;
		else if(board[i] == Disc::Cpu) ++c;
	}
	if(outYou != nullptr) *outYou = y;
	if(outCpu != nullptr) *outCpu = c;
}

bool PhoneReversi::isCorner(uint8_t col, uint8_t row) const {
	return (col == 0 || col == BoardCols - 1)
	    && (row == 0 || row == BoardRows - 1);
}

bool PhoneReversi::isXSquare(uint8_t col, uint8_t row) const {
	// Diagonally adjacent to a corner.
	const uint8_t lastC = BoardCols - 1;
	const uint8_t lastR = BoardRows - 1;
	if((col == 1 && row == 1) ||
	   (col == 1 && row == lastR - 1) ||
	   (col == lastC - 1 && row == 1) ||
	   (col == lastC - 1 && row == lastR - 1)) {
		return true;
	}
	return false;
}

bool PhoneReversi::isCSquare(uint8_t col, uint8_t row) const {
	// Orthogonally adjacent to a corner -- e.g. (0,1) or (1,0) for the
	// top-left corner.
	const uint8_t lastC = BoardCols - 1;
	const uint8_t lastR = BoardRows - 1;
	if((col == 0 && row == 1) ||
	   (col == 1 && row == 0) ||
	   (col == 0 && row == lastR - 1) ||
	   (col == 1 && row == lastR) ||
	   (col == lastC && row == 1) ||
	   (col == lastC - 1 && row == 0) ||
	   (col == lastC && row == lastR - 1) ||
	   (col == lastC - 1 && row == lastR)) {
		return true;
	}
	return false;
}

bool PhoneReversi::isEdge(uint8_t col, uint8_t row) const {
	return col == 0 || col == BoardCols - 1
	    || row == 0 || row == BoardRows - 1;
}

// ===========================================================================
// rendering
// ===========================================================================

void PhoneReversi::renderAllCells() {
	for(uint8_t i = 0; i < CellCount; ++i) {
		renderCell(i);
	}
}

void PhoneReversi::renderCell(uint8_t cell) {
	if(cell >= CellCount) return;
	auto* d = cellDiscs[cell];
	if(d == nullptr) return;

	const Disc disc = board[cell];
	switch(disc) {
		case Disc::Empty:
			lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
			break;
		case Disc::You:
			lv_obj_clear_flag(d, LV_OBJ_FLAG_HIDDEN);
			lv_obj_set_style_bg_color(d, MP_HIGHLIGHT, 0);
			break;
		case Disc::Cpu:
			lv_obj_clear_flag(d, LV_OBJ_FLAG_HIDDEN);
			lv_obj_set_style_bg_color(d, MP_ACCENT, 0);
			break;
	}
}

void PhoneReversi::renderCursor() {
	// Reset every frame's border to the dim grid colour first (so a
	// stale cursor border on a previous cell goes away), then paint
	// the focused cell's accent border.
	for(uint8_t i = 0; i < CellCount; ++i) {
		auto* f = cellFrames[i];
		if(f == nullptr) continue;
		lv_obj_set_style_border_color(f, MP_BG_DARK, 0);
		lv_obj_set_style_border_width(f, 1, 0);
	}

	// Don't show a cursor on terminal states -- the win/loss overlay
	// is the primary visual then, and a duelling accent border just
	// adds clutter. Same goes for CPU-pending: the player isn't
	// supposed to interact with the board.
	if(state != GameState::PlayerTurn) return;

	const uint8_t cell = indexOf(cursorCol, cursorRow);
	auto* f = cellFrames[cell];
	if(f == nullptr) return;

	// Cursor border colour: cyan if the move is legal, dim cream if
	// not. The dim variant doubles as visual feedback for "you can't
	// place here" without a separate beep / shake animation.
	const bool legal = (board[cell] == Disc::Empty)
	                   && (flipsForMove(cursorCol, cursorRow, Disc::You) > 0);
	lv_obj_set_style_border_color(f,
	                              legal ? MP_HIGHLIGHT : MP_LABEL_DIM,
	                              0);
	lv_obj_set_style_border_width(f, 2, 0);
	lv_obj_move_foreground(f);
}

void PhoneReversi::refreshHud() {
	uint16_t youCount = 0, cpuCount = 0;
	countDiscs(&youCount, &cpuCount);
	if(hudYouLabel != nullptr) {
		char buf[12];
		const unsigned w = youCount > 99 ? 99 : youCount;
		snprintf(buf, sizeof(buf), "YOU %02u", w);
		lv_label_set_text(hudYouLabel, buf);
	}
	if(hudCpuLabel != nullptr) {
		char buf[12];
		const unsigned w = cpuCount > 99 ? 99 : cpuCount;
		snprintf(buf, sizeof(buf), "CPU %02u", w);
		lv_label_set_text(hudCpuLabel, buf);
	}
	if(hudTurnLabel != nullptr) {
		// Centre slot: pass note > current turn caption > tally on
		// terminal states. The pass note has top priority because it
		// is the loudest piece of in-game feedback.
		if(passNote == PassNote::YouPassed) {
			lv_label_set_text(hudTurnLabel, "YOU PASSED");
			lv_obj_set_style_text_color(hudTurnLabel, MP_LABEL_DIM, 0);
		} else if(passNote == PassNote::CpuPassed) {
			lv_label_set_text(hudTurnLabel, "CPU PASSED");
			lv_obj_set_style_text_color(hudTurnLabel, MP_LABEL_DIM, 0);
		} else {
			switch(state) {
				case GameState::PlayerTurn:
					lv_label_set_text(hudTurnLabel, "YOUR TURN");
					lv_obj_set_style_text_color(hudTurnLabel, MP_HIGHLIGHT, 0);
					break;
				case GameState::CpuPending:
					lv_label_set_text(hudTurnLabel, "CPU TURN");
					lv_obj_set_style_text_color(hudTurnLabel, MP_ACCENT, 0);
					break;
				case GameState::PlayerWon:
				case GameState::CpuWon:
				case GameState::Tie: {
					char buf[16];
					const unsigned y = winsYou > 99 ? 99 : winsYou;
					const unsigned c = winsCpu > 99 ? 99 : winsCpu;
					snprintf(buf, sizeof(buf), "%uW %uL", y, c);
					lv_label_set_text(hudTurnLabel, buf);
					lv_obj_set_style_text_color(hudTurnLabel, MP_TEXT, 0);
					break;
				}
			}
		}
	}
}

void PhoneReversi::refreshSoftKeys() {
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

void PhoneReversi::refreshOverlay() {
	if(overlayLabel == nullptr) return;
	switch(state) {
		case GameState::PlayerTurn:
		case GameState::CpuPending:
			lv_obj_add_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			return;
		case GameState::PlayerWon: {
			uint16_t y = 0, c = 0;
			countDiscs(&y, &c);
			char buf[40];
			snprintf(buf, sizeof(buf), "YOU WIN!\n%u-%u\nA TO PLAY AGAIN",
			         static_cast<unsigned>(y), static_cast<unsigned>(c));
			lv_label_set_text(overlayLabel, buf);
			lv_obj_set_style_text_color(overlayLabel, MP_HIGHLIGHT, 0);
			lv_obj_set_style_border_color(overlayLabel, MP_HIGHLIGHT, 0);
			break;
		}
		case GameState::CpuWon: {
			uint16_t y = 0, c = 0;
			countDiscs(&y, &c);
			char buf[40];
			snprintf(buf, sizeof(buf), "CPU WINS\n%u-%u\nA TO TRY AGAIN",
			         static_cast<unsigned>(c), static_cast<unsigned>(y));
			lv_label_set_text(overlayLabel, buf);
			lv_obj_set_style_text_color(overlayLabel, MP_ACCENT, 0);
			lv_obj_set_style_border_color(overlayLabel, MP_ACCENT, 0);
			break;
		}
		case GameState::Tie: {
			uint16_t y = 0, c = 0;
			countDiscs(&y, &c);
			char buf[40];
			snprintf(buf, sizeof(buf), "TIE GAME\n%u-%u\nA TO PLAY AGAIN",
			         static_cast<unsigned>(y), static_cast<unsigned>(c));
			lv_label_set_text(overlayLabel, buf);
			lv_obj_set_style_text_color(overlayLabel, MP_TEXT, 0);
			lv_obj_set_style_border_color(overlayLabel, MP_LABEL_DIM, 0);
			break;
		}
	}
	lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
	lv_obj_move_foreground(overlayLabel);
}

// ===========================================================================
// timer helpers
// ===========================================================================

void PhoneReversi::cancelCpuTimer() {
	if(cpuTimer != nullptr) {
		lv_timer_del(cpuTimer);
		cpuTimer = nullptr;
	}
}

void PhoneReversi::onCpuTimerStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneReversi*>(timer->user_data);
	if(self == nullptr) return;
	// One-shot: clear our cached pointer first so doCpuMove() can
	// reschedule (theoretically) without colliding with the dying timer.
	self->cpuTimer = nullptr;
	self->doCpuMove();
}

// ===========================================================================
// input
// ===========================================================================

void PhoneReversi::buttonPressed(uint i) {
	// BACK always pops out, regardless of state.
	if(i == BTN_BACK) {
		if(softKeys) softKeys->flashRight();
		pop();
		return;
	}

	// Reshuffle works in every state -- "give up" / "play again" share
	// the same key for muscle-memory parity with PhoneTicTacToe (S81)
	// and PhoneConnectFour (S88).
	if(i == BTN_R) {
		newRound();
		return;
	}

	switch(state) {
		case GameState::PlayerTurn: {
			// Movement -- d-pad + numpad both supported, both wrap.
			if(i == BTN_LEFT || i == BTN_4) {
				cursorCol = (cursorCol == 0)
				                ? static_cast<uint8_t>(BoardCols - 1)
				                : static_cast<uint8_t>(cursorCol - 1);
				renderCursor();
				return;
			}
			if(i == BTN_RIGHT || i == BTN_6) {
				cursorCol = static_cast<uint8_t>((cursorCol + 1) % BoardCols);
				renderCursor();
				return;
			}
			if(i == BTN_2) {
				cursorRow = (cursorRow == 0)
				                ? static_cast<uint8_t>(BoardRows - 1)
				                : static_cast<uint8_t>(cursorRow - 1);
				renderCursor();
				return;
			}
			if(i == BTN_8) {
				cursorRow = static_cast<uint8_t>((cursorRow + 1) % BoardRows);
				renderCursor();
				return;
			}

			// Place (numpad 5 + ENTER). Illegal moves just flash the
			// soft key without changing state, matching the
			// gentle-feedback approach of every other Phone* game.
			if(i == BTN_5 || i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				if(board[indexOf(cursorCol, cursorRow)] != Disc::Empty) return;
				if(flipsForMove(cursorCol, cursorRow, Disc::You) == 0) return;
				if(!placeDisc(cursorCol, cursorRow, Disc::You)) return;
				afterMove(Disc::You);
				return;
			}
			return;
		}

		case GameState::CpuPending: {
			// CPU is "thinking" or pass animation is in flight -- input
			// intentionally locked except BACK / R (handled above). We
			// don't want the player to double-place by mashing buttons
			// through the delay.
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
