#include "PhoneConnectFour.h"

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
// PhoneTicTacToe (S81), PhoneHangman (S87) without a visual seam.
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)  // dim purple captions

namespace {

// The four straight-line directions we scan for a 4-in-a-row run from
// any given cell. Stored as (dCol, dRow). Each direction is checked in
// both polarities (forward + backward) by scanWinFromCell so we don't
// also need (-1,0), (0,-1), etc.
struct Dir { int8_t dc; int8_t dr; };
constexpr Dir kDirs[4] = {
	{ 1,  0 },   // horizontal
	{ 0,  1 },   // vertical
	{ 1,  1 },   // diagonal down-right
	{ 1, -1 },   // diagonal up-right
};

// Column-preference ordering: centre column first, fanning outwards in
// alternating ±1 steps. For BoardCols=7 this yields {3,2,4,1,5,0,6}.
// Kept as a constexpr so it lives in flash and we don't rebuild it on
// every CPU pick. If BoardCols ever changes we'd extend this table.
constexpr uint8_t kCenterPriority[7] = { 3, 2, 4, 1, 5, 0, 6 };

} // namespace

// ===========================================================================
// ctor / dtor
// ===========================================================================

PhoneConnectFour::PhoneConnectFour()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr) {

	for(uint8_t i = 0; i < CellCount; ++i) {
		cellDiscs[i] = nullptr;
		board[i]     = Disc::Empty;
	}
	for(uint8_t i = 0; i < WinRun; ++i) {
		winLine[i] = 0xFF;
	}

	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildHud();
	buildBoard();
	buildColumnCursor();
	buildOverlay();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("DROP");
	softKeys->setRight("BACK");

	newRound();
}

PhoneConnectFour::~PhoneConnectFour() {
	cancelCpuTimer();
	// All children parented to obj; LVScreen frees them.
}

void PhoneConnectFour::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneConnectFour::onStop() {
	Input::getInstance()->removeListener(this);
	cancelCpuTimer();
}

// ===========================================================================
// build helpers
// ===========================================================================

void PhoneConnectFour::buildHud() {
	// Three small score badges arranged left-mid-right inside the HUD
	// strip. pixelbasic7 in cyan / orange / dim cream so YOU vs CPU vs
	// TIE read distinctly without a separator glyph -- consistent with
	// PhoneTicTacToe (S81) and PhoneMemoryMatch (S82).
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

void PhoneConnectFour::buildOverlay() {
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

void PhoneConnectFour::buildBoard() {
	// Frame panel: a single rounded rectangle behind every cell. The
	// dim-purple fill gives the empty cells a "drilled hole" look once
	// the discs paint over it.
	boardPanel = lv_obj_create(obj);
	lv_obj_remove_style_all(boardPanel);
	lv_obj_set_size(boardPanel, BoardW, BoardH);
	lv_obj_set_pos(boardPanel, BoardOriginX, BoardOriginY);
	lv_obj_set_style_bg_color(boardPanel, MP_DIM, 0);
	lv_obj_set_style_bg_opa(boardPanel, LV_OPA_90, 0);
	lv_obj_set_style_border_color(boardPanel, MP_HIGHLIGHT, 0);
	lv_obj_set_style_border_width(boardPanel, 1, 0);
	lv_obj_set_style_radius(boardPanel, 3, 0);
	lv_obj_set_style_pad_all(boardPanel, 0, 0);
	lv_obj_clear_flag(boardPanel, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(boardPanel, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(boardPanel, LV_OBJ_FLAG_IGNORE_LAYOUT);

	// One disc obj per cell. 11x11 rounded square == circle on this
	// pixel scale. Centred inside the 14x14 cell, leaving a 1.5 px
	// gutter on every side so the empty grid reads as discrete holes.
	for(uint8_t row = 0; row < BoardRows; ++row) {
		for(uint8_t col = 0; col < BoardCols; ++col) {
			const uint8_t cell = indexOf(col, row);

			auto* disc = lv_obj_create(boardPanel);
			lv_obj_remove_style_all(disc);
			lv_obj_set_size(disc, 11, 11);
			lv_obj_set_pos(disc,
			               static_cast<lv_coord_t>(col * CellPx + 2),
			               static_cast<lv_coord_t>(row * CellPx + 2));
			lv_obj_set_style_bg_color(disc, MP_BG_DARK, 0);
			lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
			lv_obj_set_style_radius(disc, 6, 0);   // 6 ~= 11/2 -> circle
			lv_obj_set_style_border_width(disc, 0, 0);
			lv_obj_set_style_pad_all(disc, 0, 0);
			lv_obj_clear_flag(disc, LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_clear_flag(disc, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_flag(disc, LV_OBJ_FLAG_IGNORE_LAYOUT);

			cellDiscs[cell] = disc;
		}
	}
}

void PhoneConnectFour::buildColumnCursor() {
	// A short cyan bar that rides above the column the player is about
	// to drop into. Sits in the 6 px strip between HUD and board, and
	// gets re-positioned on every cursor move.
	columnCursor = lv_obj_create(obj);
	lv_obj_remove_style_all(columnCursor);
	lv_obj_set_size(columnCursor, 10, 4);
	lv_obj_set_pos(columnCursor,
	               BoardOriginX + 2,
	               CursorStripY + (CursorStripH - 4) / 2);
	lv_obj_set_style_bg_color(columnCursor, MP_HIGHLIGHT, 0);
	lv_obj_set_style_bg_opa(columnCursor, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(columnCursor, 1, 0);
	lv_obj_set_style_border_width(columnCursor, 0, 0);
	lv_obj_clear_flag(columnCursor, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(columnCursor, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(columnCursor, LV_OBJ_FLAG_IGNORE_LAYOUT);
}

// ===========================================================================
// state transitions
// ===========================================================================

void PhoneConnectFour::newRound() {
	cancelCpuTimer();

	for(uint8_t i = 0; i < CellCount; ++i) {
		board[i] = Disc::Empty;
	}
	for(uint8_t i = 0; i < WinRun; ++i) {
		winLine[i] = 0xFF;
	}

	// Park the cursor on the centre column -- the strongest opening
	// move and the friendliest default for first-time players.
	cursorCol = BoardCols / 2;

	if(playerStartsNext) {
		state = GameState::PlayerTurn;
	} else {
		state = GameState::CpuPending;
	}
	playerStartsNext = !playerStartsNext;

	renderAllCells();
	renderColumnCursor();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();

	if(state == GameState::CpuPending) {
		scheduleCpuMove();
	}
}

bool PhoneConnectFour::dropDisc(uint8_t col, Disc who, uint8_t* outCell) {
	if(col >= BoardCols) return false;
	if(who == Disc::Empty) return false;
	const int8_t row = lowestEmptyRow(col);
	if(row < 0) return false;

	const uint8_t cell = indexOf(col, static_cast<uint8_t>(row));
	board[cell] = who;
	renderCell(cell);
	if(outCell != nullptr) *outCell = cell;
	return true;
}

void PhoneConnectFour::afterMove(uint8_t lastCell, Disc who) {
	// Did `who` just complete a 4-in-a-row?
	uint8_t line[WinRun];
	if(scanWinFromCell(lastCell, who, line)) {
		for(uint8_t i = 0; i < WinRun; ++i) winLine[i] = line[i];
		state = (who == Disc::You) ? GameState::PlayerWon
		                           : GameState::CpuWon;
		if(who == Disc::You) ++winsYou; else ++winsCpu;
		renderAllCells();
		renderColumnCursor();
		refreshHud();
		refreshSoftKeys();
		refreshOverlay();
		return;
	}

	// Out of squares?
	if(isBoardFull()) {
		state = GameState::Tie;
		++ties;
		renderColumnCursor();
		refreshHud();
		refreshSoftKeys();
		refreshOverlay();
		return;
	}

	// Game continues -- swap turns. If we just resolved a player move,
	// it's the CPU's turn now.
	if(who == Disc::You) {
		state = GameState::CpuPending;
		renderColumnCursor();
		refreshSoftKeys();
		scheduleCpuMove();
	} else {
		state = GameState::PlayerTurn;
		renderColumnCursor();
		refreshSoftKeys();
	}
}

void PhoneConnectFour::scheduleCpuMove() {
	cancelCpuTimer();
	cpuTimer = lv_timer_create(&PhoneConnectFour::onCpuTimerStatic,
	                           kCpuThinkMs, this);
	if(cpuTimer != nullptr) {
		// One-shot semantics: the callback deletes the timer itself,
		// but we also set repeat_count=1 so a stray run after deletion
		// (e.g. during teardown) is harmless.
		lv_timer_set_repeat_count(cpuTimer, 1);
	}
}

void PhoneConnectFour::doCpuMove() {
	// Defensive: only act if we're still in the pending state. If the
	// player popped the screen or hit reshuffle while the timer was in
	// flight, cancelCpuTimer() may not have caught the racing fire.
	if(state != GameState::CpuPending) return;

	const uint8_t pickCol = pickCpuMove();
	uint8_t placedCell = 0xFF;
	if(!dropDisc(pickCol, Disc::Cpu, &placedCell)) {
		// pickCpuMove() never returns a full column, but be defensive:
		// scan again for the first column with room and drop there.
		for(uint8_t c = 0; c < BoardCols; ++c) {
			if(columnHasRoom(c) && dropDisc(c, Disc::Cpu, &placedCell)) {
				break;
			}
		}
	}
	if(placedCell != 0xFF) {
		afterMove(placedCell, Disc::Cpu);
	}
}

uint8_t PhoneConnectFour::pickCpuMove() {
	// 1. If the CPU has a winning drop, take it.
	const int8_t winCol = findWinningColFor(Disc::Cpu);
	if(winCol >= 0) return static_cast<uint8_t>(winCol);

	// 2. If the player threatens to win on their next move, block it.
	const int8_t blockCol = findWinningColFor(Disc::You);
	if(blockCol >= 0) return static_cast<uint8_t>(blockCol);

	// 3. Avoid columns that would let the player win on top of our
	//    piece. Simulate dropping a CPU disc, then test whether the
	//    resulting board hands the player a free win on the same
	//    column. We restore the board after the probe.
	bool unsafe[BoardCols] = { false, false, false, false, false, false, false };
	for(uint8_t col = 0; col < BoardCols; ++col) {
		if(!columnHasRoom(col)) { unsafe[col] = true; continue; }
		const int8_t row = lowestEmptyRow(col);
		if(row < 1) {
			// Even if we drop here, there's no row above for the player
			// to stack onto, so this column can't be made unsafe by the
			// "stack on top" attack.
			continue;
		}
		const uint8_t myCell = indexOf(col, static_cast<uint8_t>(row));
		const uint8_t opCell = indexOf(col, static_cast<uint8_t>(row - 1));
		// Probe: place CPU disc, then place player disc above it.
		board[myCell] = Disc::Cpu;
		board[opCell] = Disc::You;
		uint8_t dummy[WinRun];
		const bool playerWins = scanWinFromCell(opCell, Disc::You, dummy);
		// Restore.
		board[opCell] = Disc::Empty;
		board[myCell] = Disc::Empty;
		if(playerWins) unsafe[col] = true;
	}

	// 4. Pick the safest centre-preferred column. If every column is
	//    unsafe, fall back to centre-preferred without the safety
	//    filter -- losing on principle is better than refusing to play.
	for(uint8_t pass = 0; pass < 2; ++pass) {
		// Build a small candidate list from kCenterPriority filtered by
		// "has room" and (on pass 0) "is not unsafe".
		uint8_t cands[BoardCols];
		uint8_t nCands = 0;
		for(uint8_t i = 0; i < BoardCols; ++i) {
			const uint8_t c = kCenterPriority[i];
			if(!columnHasRoom(c)) continue;
			if(pass == 0 && unsafe[c]) continue;
			cands[nCands++] = c;
		}
		if(nCands == 0) continue;

		// Slight random jitter: with ~1/4 odds, swap the top pick with
		// the runner-up so the CPU's openings don't feel scripted.
		if(nCands >= 2 && (rand() & 0x3) == 0) {
			const uint8_t tmp = cands[0];
			cands[0] = cands[1];
			cands[1] = tmp;
		}
		return cands[0];
	}

	// Should never reach here (board full -> doCpuMove won't be called),
	// but fall back to the first column with room.
	for(uint8_t c = 0; c < BoardCols; ++c) {
		if(columnHasRoom(c)) return c;
	}
	return 0;
}

// ===========================================================================
// board queries
// ===========================================================================

bool PhoneConnectFour::columnHasRoom(uint8_t col) const {
	if(col >= BoardCols) return false;
	// Top row is the first row -- if it's empty the column has room.
	return board[indexOf(col, 0)] == Disc::Empty;
}

int8_t PhoneConnectFour::lowestEmptyRow(uint8_t col) const {
	if(col >= BoardCols) return -1;
	// Scan from the bottom row up; the first empty slot is where a
	// dropped disc lands under simulated gravity.
	for(int8_t row = static_cast<int8_t>(BoardRows) - 1; row >= 0; --row) {
		if(board[indexOf(col, static_cast<uint8_t>(row))] == Disc::Empty) {
			return row;
		}
	}
	return -1;
}

bool PhoneConnectFour::isBoardFull() const {
	for(uint8_t i = 0; i < CellCount; ++i) {
		if(board[i] == Disc::Empty) return false;
	}
	return true;
}

bool PhoneConnectFour::scanWinFromCell(uint8_t cell, Disc who,
                                       uint8_t outLine[WinRun]) const {
	if(cell >= CellCount) return false;
	if(who == Disc::Empty) return false;
	if(board[cell] != who) return false;

	const int8_t startCol = static_cast<int8_t>(cell % BoardCols);
	const int8_t startRow = static_cast<int8_t>(cell / BoardCols);

	for(uint8_t d = 0; d < 4; ++d) {
		const int8_t dc = kDirs[d].dc;
		const int8_t dr = kDirs[d].dr;

		// Walk backward from `cell` until we step off the board or hit
		// a non-`who` cell, then walk forward, collecting indices. If
		// the contiguous run is at least WinRun long we have a winner.
		int8_t col = startCol;
		int8_t row = startRow;
		while(true) {
			const int8_t pc = static_cast<int8_t>(col - dc);
			const int8_t pr = static_cast<int8_t>(row - dr);
			if(pc < 0 || pc >= static_cast<int8_t>(BoardCols)) break;
			if(pr < 0 || pr >= static_cast<int8_t>(BoardRows)) break;
			if(board[indexOf(static_cast<uint8_t>(pc),
			                 static_cast<uint8_t>(pr))] != who) break;
			col = pc;
			row = pr;
		}

		// Walk forward until we leave the run.
		uint8_t line[BoardCols + BoardRows]; // generous upper bound
		uint8_t lineLen = 0;
		while(col >= 0 && col < static_cast<int8_t>(BoardCols)
		      && row >= 0 && row < static_cast<int8_t>(BoardRows)
		      && board[indexOf(static_cast<uint8_t>(col),
		                       static_cast<uint8_t>(row))] == who) {
			line[lineLen++] = indexOf(static_cast<uint8_t>(col),
			                          static_cast<uint8_t>(row));
			col = static_cast<int8_t>(col + dc);
			row = static_cast<int8_t>(row + dr);
		}

		if(lineLen >= WinRun) {
			// Find the contiguous WinRun-window that contains `cell`,
			// or just take the first WinRun cells. Either way the line
			// is valid; we use the first window for determinism.
			for(uint8_t i = 0; i < WinRun; ++i) {
				outLine[i] = line[i];
			}
			return true;
		}
	}
	return false;
}

int8_t PhoneConnectFour::findWinningColFor(Disc who) const {
	// For each column with room, simulate the drop, scan from the
	// landing cell, and report the first column that wins.
	for(uint8_t col = 0; col < BoardCols; ++col) {
		if(!columnHasRoom(col)) continue;
		const int8_t row = lowestEmptyRow(col);
		if(row < 0) continue;
		const uint8_t cell = indexOf(col, static_cast<uint8_t>(row));
		// We can't write to `board` from a const method, so cheat with
		// a local copy of the relevant cell. The scan only reads from
		// the board, so we temporarily mutate via const_cast and
		// restore before returning.
		Disc* mut = const_cast<Disc*>(board);
		mut[cell] = who;
		uint8_t dummy[WinRun];
		const bool wins = scanWinFromCell(cell, who, dummy);
		mut[cell] = Disc::Empty;
		if(wins) return static_cast<int8_t>(col);
	}
	return -1;
}

// ===========================================================================
// rendering
// ===========================================================================

void PhoneConnectFour::renderAllCells() {
	for(uint8_t i = 0; i < CellCount; ++i) {
		renderCell(i);
	}
}

void PhoneConnectFour::renderCell(uint8_t cell) {
	if(cell >= CellCount) return;
	auto* d = cellDiscs[cell];
	if(d == nullptr) return;

	const Disc disc = board[cell];

	// Default fill: empty hole = MP_BG_DARK, player = cyan, CPU = orange.
	// We tint the same widget rather than swapping in/out so the disc
	// position is visually stable from "empty" to "filled".
	switch(disc) {
		case Disc::Empty:
			lv_obj_set_style_bg_color(d, MP_BG_DARK, 0);
			break;
		case Disc::You:
			lv_obj_set_style_bg_color(d, MP_HIGHLIGHT, 0);
			break;
		case Disc::Cpu:
			lv_obj_set_style_bg_color(d, MP_ACCENT, 0);
			break;
	}
	lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);

	// Win-line highlight: discs that participated in the winning line
	// get a 1 px cream border so the result reads at a glance even
	// before the player notices the centre overlay text.
	bool winning = false;
	if(state == GameState::PlayerWon || state == GameState::CpuWon) {
		for(uint8_t i = 0; i < WinRun; ++i) {
			if(winLine[i] == cell) { winning = true; break; }
		}
	}
	if(winning) {
		lv_obj_set_style_border_color(d, MP_TEXT, 0);
		lv_obj_set_style_border_width(d, 1, 0);
	} else {
		lv_obj_set_style_border_width(d, 0, 0);
	}
}

void PhoneConnectFour::renderColumnCursor() {
	if(columnCursor == nullptr) return;

	// Hide the cursor on terminal states -- the win-line is the primary
	// visual then, and a duelling indicator just adds clutter.
	if(state == GameState::PlayerWon
	   || state == GameState::CpuWon
	   || state == GameState::Tie) {
		lv_obj_add_flag(columnCursor, LV_OBJ_FLAG_HIDDEN);
		return;
	}

	// Likewise, hide it during the CPU's turn -- it would lie to the
	// player about whose move resolves next.
	if(state == GameState::CpuPending) {
		lv_obj_add_flag(columnCursor, LV_OBJ_FLAG_HIDDEN);
		return;
	}

	lv_obj_clear_flag(columnCursor, LV_OBJ_FLAG_HIDDEN);

	// Position over the centre of the cursorCol cell. The cursor bar is
	// 10 px wide; the cell is 14; (14 - 10) / 2 = 2 px gutter.
	const lv_coord_t x = static_cast<lv_coord_t>(
	    BoardOriginX + cursorCol * CellPx + 2);
	lv_obj_set_x(columnCursor, x);

	// Tint the cursor red-ish if the column is full -- visual feedback
	// that pressing DROP will not do anything.
	if(!columnHasRoom(cursorCol)) {
		lv_obj_set_style_bg_color(columnCursor, MP_LABEL_DIM, 0);
	} else {
		lv_obj_set_style_bg_color(columnCursor, MP_HIGHLIGHT, 0);
	}
}

void PhoneConnectFour::refreshHud() {
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

void PhoneConnectFour::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	switch(state) {
		case GameState::PlayerTurn:
			softKeys->setLeft("DROP");
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

void PhoneConnectFour::refreshOverlay() {
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

void PhoneConnectFour::cancelCpuTimer() {
	if(cpuTimer != nullptr) {
		lv_timer_del(cpuTimer);
		cpuTimer = nullptr;
	}
}

void PhoneConnectFour::onCpuTimerStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneConnectFour*>(timer->user_data);
	if(self == nullptr) return;
	// One-shot: clear our cached pointer first so doCpuMove() can
	// reschedule (theoretically) without colliding with the dying timer.
	self->cpuTimer = nullptr;
	self->doCpuMove();
}

// ===========================================================================
// input
// ===========================================================================

void PhoneConnectFour::buttonPressed(uint i) {
	// BACK always pops out, regardless of state.
	if(i == BTN_BACK) {
		if(softKeys) softKeys->flashRight();
		pop();
		return;
	}

	// Reshuffle works in every state -- "give up" / "play again" share
	// the same key for muscle-memory parity with PhoneTicTacToe (S81).
	if(i == BTN_R) {
		newRound();
		return;
	}

	switch(state) {
		case GameState::PlayerTurn: {
			// Movement (d-pad + numpad both supported, both wrap).
			if(i == BTN_LEFT || i == BTN_4) {
				cursorCol = (cursorCol == 0)
				                ? static_cast<uint8_t>(BoardCols - 1)
				                : static_cast<uint8_t>(cursorCol - 1);
				renderColumnCursor();
				return;
			}
			if(i == BTN_RIGHT || i == BTN_6) {
				cursorCol = static_cast<uint8_t>((cursorCol + 1) % BoardCols);
				renderColumnCursor();
				return;
			}

			// Drop (numpad 5 + ENTER). Illegal drops (column full) just
			// flash the soft key without changing state, matching the
			// gentle-feedback approach of every other Phone* game.
			if(i == BTN_5 || i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				if(!columnHasRoom(cursorCol)) return;
				uint8_t placedCell = 0xFF;
				if(!dropDisc(cursorCol, Disc::You, &placedCell)) return;
				afterMove(placedCell, Disc::You);
				return;
			}
			return;
		}

		case GameState::CpuPending: {
			// CPU is "thinking" -- input intentionally locked except
			// BACK / R (handled above). We don't want the player to
			// double-drop by mashing buttons through the delay.
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
