#include "PhoneMinesweeper.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - identical to every other Phone* widget so
// the screen sits beside PhoneTetris (S71/72), PhoneBantumi (S76),
// PhoneBubbleSmile (S77/78) without a visual seam.
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)  // dim purple captions

// Per-digit colours for the adjacent-mine counts. We do not literally
// match Microsoft Minesweeper hues -- we tint them so they read against
// the deep purple field while still being telegraphically distinct.
#define MS_DIGIT_1     lv_color_make(122, 232, 255)  // cyan
#define MS_DIGIT_2     lv_color_make(140, 230, 140)  // soft green
#define MS_DIGIT_3     lv_color_make(255, 180,  80)  // peach
#define MS_DIGIT_4     lv_color_make(220, 130, 240)  // magenta
#define MS_DIGIT_5     lv_color_make(255, 140,  30)  // accent
#define MS_DIGIT_6     lv_color_make(255, 220, 180)  // cream
#define MS_DIGIT_7     lv_color_make(255, 220,  60)  // yellow
#define MS_DIGIT_8     lv_color_make(240,  90,  90)  // red

// ===========================================================================
// Difficulty table
// ===========================================================================
//
// Order matters: the difficulty cycle (BTN_R) advances strictly
// EASY -> MEDIUM -> HARD -> EASY, so adding entries here changes the
// rotation. The cell counts are sized so the playfield always fits
// within the 160 x 96 px band between the HUD and soft-key bar.

const PhoneMinesweeper::DifficultyInfo
PhoneMinesweeper::Difficulties[PhoneMinesweeper::kDifficultyCount] = {
	// name        cols rows cell mines
	{ "EASY",        8,   6,  12,    6 },
	{ "MEDIUM",     10,   7,  10,   12 },
	{ "HARD",       12,   8,   8,   18 },
};

// ===========================================================================
// helpers (file-scope)
// ===========================================================================

namespace {

// Pick a colour for the adjacent-mine digit `n`. n == 0 should never be
// rendered (we leave the cell blank for opened zero cells), but we
// return cream as a safe fallback.
lv_color_t digitColour(uint8_t n) {
	switch(n) {
		case 1: return MS_DIGIT_1;
		case 2: return MS_DIGIT_2;
		case 3: return MS_DIGIT_3;
		case 4: return MS_DIGIT_4;
		case 5: return MS_DIGIT_5;
		case 6: return MS_DIGIT_6;
		case 7: return MS_DIGIT_7;
		case 8: return MS_DIGIT_8;
		default: return MP_TEXT;
	}
}

} // namespace

// ===========================================================================
// ctor / dtor
// ===========================================================================

PhoneMinesweeper::PhoneMinesweeper()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr) {

	for(uint16_t i = 0; i < MaxCells; ++i) {
		cellSprites[i]  = nullptr;
		cellLabels[i]   = nullptr;
		cellMine[i]     = 0;
		cellRevealed[i] = 0;
		cellFlagged[i]  = 0;
		cellAdjacent[i] = 0;
	}

	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildHud();
	rebuildField();
	buildOverlay();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("DIG");
	softKeys->setRight("BACK");

	enterIdle();
}

PhoneMinesweeper::~PhoneMinesweeper() {
	stopTickTimer();
	// All children parented to obj; LVScreen frees them.
}

void PhoneMinesweeper::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneMinesweeper::onStop() {
	Input::getInstance()->removeListener(this);
	stopTickTimer();
}

// ===========================================================================
// build helpers
// ===========================================================================

void PhoneMinesweeper::buildHud() {
	// Difficulty label sits at the left edge of the HUD strip.
	hudDiffLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudDiffLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudDiffLabel, MP_ACCENT, 0);
	lv_label_set_text(hudDiffLabel, "EASY");
	lv_obj_set_pos(hudDiffLabel, 6, HudY + 2);

	// Mine counter dead centre of the HUD strip so the player can see
	// "remaining mines" while keeping the sides for difficulty + timer.
	hudMineLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudMineLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudMineLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(hudMineLabel, "*00");
	lv_obj_set_align(hudMineLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hudMineLabel, HudY + 2);

	// Timer at the right edge.
	hudTimerLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudTimerLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudTimerLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(hudTimerLabel, "000s");
	lv_obj_set_pos(hudTimerLabel, 130, HudY + 2);
}

void PhoneMinesweeper::buildOverlay() {
	overlayLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(overlayLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(overlayLabel, MP_TEXT, 0);
	lv_obj_set_style_text_align(overlayLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_bg_color(overlayLabel, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(overlayLabel, LV_OPA_80, 0);
	lv_obj_set_style_border_color(overlayLabel, MP_ACCENT, 0);
	lv_obj_set_style_border_width(overlayLabel, 1, 0);
	lv_obj_set_style_radius(overlayLabel, 2, 0);
	lv_obj_set_style_pad_all(overlayLabel, 4, 0);
	lv_label_set_text(overlayLabel, "");
	lv_obj_set_align(overlayLabel, LV_ALIGN_CENTER);
}

void PhoneMinesweeper::rebuildField() {
	// Wipe any existing cell sprites + labels. We rebuild from scratch
	// on every difficulty change because the cell pixel size differs.
	for(uint16_t i = 0; i < MaxCells; ++i) {
		if(cellSprites[i] != nullptr) {
			lv_obj_del(cellSprites[i]);
			cellSprites[i] = nullptr;
			cellLabels[i]  = nullptr;
		}
	}

	const uint8_t  c = cols();
	const uint8_t  r = rows();
	const uint8_t  cp = cellPx();
	const uint16_t ox = fieldOriginX();
	const uint16_t oy = fieldOriginY();

	for(uint8_t row = 0; row < r; ++row) {
		for(uint8_t col = 0; col < c; ++col) {
			const uint16_t i = static_cast<uint16_t>(row) * c + col;
			auto* s = lv_obj_create(obj);
			lv_obj_remove_style_all(s);
			lv_obj_set_size(s, cp - 1, cp - 1);   // 1 px gap between cells
			lv_obj_set_pos(s,
			               ox + col * cp,
			               oy + row * cp);
			lv_obj_set_style_bg_color(s, MP_DIM, 0);
			lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
			lv_obj_set_style_border_color(s, MP_LABEL_DIM, 0);
			lv_obj_set_style_border_width(s, 1, 0);
			lv_obj_set_style_radius(s, 1, 0);
			lv_obj_set_style_pad_all(s, 0, 0);
			lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_clear_flag(s, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_flag(s, LV_OBJ_FLAG_IGNORE_LAYOUT);
			cellSprites[i] = s;

			auto* lbl = lv_label_create(s);
			lv_obj_set_style_text_font(lbl, &pixelbasic7, 0);
			lv_obj_set_style_text_color(lbl, MP_TEXT, 0);
			lv_obj_set_style_pad_all(lbl, 0, 0);
			lv_label_set_text(lbl, "");
			lv_obj_set_align(lbl, LV_ALIGN_CENTER);
			cellLabels[i] = lbl;
		}
	}
}

// ===========================================================================
// state transitions
// ===========================================================================

void PhoneMinesweeper::enterIdle() {
	state = GameState::Idle;
	stopTickTimer();
	finishMillis = 0;
	startMillis  = 0;

	const uint8_t cnt = cellCount();
	for(uint16_t i = 0; i < cnt; ++i) {
		cellMine[i]     = 0;
		cellRevealed[i] = 0;
		cellFlagged[i]  = 0;
		cellAdjacent[i] = 0;
	}
	flagsPlaced       = 0;
	revealedCount     = 0;
	explodedIndex     = 0xFF;
	totalMines        = Difficulties[difficulty].mines;
	needsMinePlacement = true;

	// Park the cursor at the centre of the board so the first dig
	// reveals a roomy starting region.
	cursorCol = static_cast<uint8_t>(cols() / 2);
	cursorRow = static_cast<uint8_t>(rows() / 2);

	renderAllCells();
	renderCursor();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneMinesweeper::cycleDifficulty() {
	difficulty = static_cast<uint8_t>(
			(difficulty + 1) % kDifficultyCount);
	rebuildField();
	enterIdle();
}

void PhoneMinesweeper::placeMinesAfterFirstDig(uint8_t safeCol, uint8_t safeRow) {
	// Build a list of legal mine indices: every cell that is NOT the
	// first-pick cell or one of its eight neighbours. Then sample
	// `totalMines` of them without replacement.
	const uint8_t cnt = cellCount();
	uint8_t legal[MaxCells];
	uint8_t legalCount = 0;
	for(uint16_t i = 0; i < cnt; ++i) {
		const uint8_t col = static_cast<uint8_t>(i % cols());
		const uint8_t row = static_cast<uint8_t>(i / cols());
		const int dc = static_cast<int>(col) - static_cast<int>(safeCol);
		const int dr = static_cast<int>(row) - static_cast<int>(safeRow);
		if(dc >= -1 && dc <= 1 && dr >= -1 && dr <= 1) continue;
		legal[legalCount++] = static_cast<uint8_t>(i);
	}

	// If we somehow do not have enough legal cells (impossible at the
	// shipped difficulties, but cheap to guard), cap mines at legalCount.
	uint8_t mines = totalMines;
	if(mines > legalCount) mines = legalCount;

	// Fisher-Yates partial shuffle of the legal[] prefix, picking the
	// first `mines` entries as the mine locations.
	for(uint8_t i = 0; i < mines; ++i) {
		const uint8_t j = static_cast<uint8_t>(
				i + static_cast<uint8_t>(rand() % (legalCount - i)));
		const uint8_t tmp = legal[i];
		legal[i] = legal[j];
		legal[j] = tmp;
	}
	for(uint8_t i = 0; i < mines; ++i) {
		cellMine[legal[i]] = 1;
	}

	// Pre-compute every cell's adjacent-mine count. Doing this once now
	// keeps reveal cheap; a 12x8 board is 96 cells which is well within
	// what we can scan in a single tick.
	for(uint8_t row = 0; row < rows(); ++row) {
		for(uint8_t col = 0; col < cols(); ++col) {
			cellAdjacent[indexOf(col, row)] = countAdjacentMines(col, row);
		}
	}
}

void PhoneMinesweeper::digCell(uint8_t col, uint8_t row) {
	if(state != GameState::Idle && state != GameState::Playing) return;
	if(!inBounds(col, row)) return;

	const uint16_t idx = indexOf(col, row);

	// If this is the very first dig of the game, place mines around it
	// and start the timer.
	if(state == GameState::Idle && needsMinePlacement) {
		placeMinesAfterFirstDig(col, row);
		needsMinePlacement = false;
		state              = GameState::Playing;
		startMillis        = millis();
		startTickTimer();
		refreshSoftKeys();
		refreshOverlay();
	}

	// Flagged cells are protected from accidental digs (matches every
	// modern Minesweeper variant).
	if(cellFlagged[idx]) return;

	// Already revealed -- nothing to do.
	if(cellRevealed[idx]) return;

	// Stepped on a mine -> game over.
	if(cellMine[idx]) {
		loseMatch(static_cast<uint8_t>(idx));
		return;
	}

	// Otherwise reveal this cell. If it has zero adjacent mines, run a
	// flood-fill so the open region cascades outward like classic
	// Minesweeper.
	if(cellAdjacent[idx] == 0) {
		floodReveal(col, row);
	} else {
		cellRevealed[idx] = 1;
		++revealedCount;
		renderCell(col, row);
	}

	// Check for a win: every non-mine cell has been revealed.
	const uint16_t target = static_cast<uint16_t>(cellCount()) - totalMines;
	if(revealedCount >= target) {
		winMatch();
	}
}

void PhoneMinesweeper::floodReveal(uint8_t col, uint8_t row) {
	// Iterative BFS so the (small) Arduino stack does not get blown by
	// recursion on the 12x8 HARD board.
	uint8_t qx[kMaxQueue];
	uint8_t qy[kMaxQueue];
	uint16_t head = 0;
	uint16_t tail = 0;

	const uint16_t startIdx = indexOf(col, row);
	if(cellRevealed[startIdx]) return;
	cellRevealed[startIdx] = 1;
	++revealedCount;
	renderCell(col, row);

	if(cellAdjacent[startIdx] != 0) return;

	qx[tail] = col;
	qy[tail] = row;
	++tail;

	while(head < tail) {
		const uint8_t cx = qx[head];
		const uint8_t cy = qy[head];
		++head;

		for(int8_t dy = -1; dy <= 1; ++dy) {
			for(int8_t dx = -1; dx <= 1; ++dx) {
				if(dx == 0 && dy == 0) continue;
				const int8_t nx = static_cast<int8_t>(cx) + dx;
				const int8_t ny = static_cast<int8_t>(cy) + dy;
				if(!inBounds(nx, ny)) continue;
				const uint16_t ni = indexOf(static_cast<uint8_t>(nx),
				                            static_cast<uint8_t>(ny));
				if(cellRevealed[ni]) continue;
				if(cellMine[ni])     continue;   // never trip a mine here
				if(cellFlagged[ni])  continue;   // respect the flag

				cellRevealed[ni] = 1;
				++revealedCount;
				renderCell(static_cast<uint8_t>(nx),
				           static_cast<uint8_t>(ny));

				if(cellAdjacent[ni] == 0 && tail < kMaxQueue) {
					qx[tail] = static_cast<uint8_t>(nx);
					qy[tail] = static_cast<uint8_t>(ny);
					++tail;
				}
			}
		}
	}
}

void PhoneMinesweeper::toggleFlag(uint8_t col, uint8_t row) {
	if(state != GameState::Playing && state != GameState::Idle) return;
	if(!inBounds(col, row)) return;
	const uint16_t idx = indexOf(col, row);
	if(cellRevealed[idx]) return;     // can't flag opened cells

	if(cellFlagged[idx]) {
		cellFlagged[idx] = 0;
		if(flagsPlaced > 0) --flagsPlaced;
	} else {
		cellFlagged[idx] = 1;
		++flagsPlaced;
	}
	renderCell(col, row);
	refreshHud();
}

void PhoneMinesweeper::revealAllMines() {
	const uint8_t cnt = cellCount();
	for(uint16_t i = 0; i < cnt; ++i) {
		if(cellMine[i]) cellRevealed[i] = 1;
	}
	const uint8_t c = cols();
	const uint8_t r = rows();
	for(uint8_t row = 0; row < r; ++row) {
		for(uint8_t col = 0; col < c; ++col) {
			renderCell(col, row);
		}
	}
}

void PhoneMinesweeper::winMatch() {
	state = GameState::Won;
	stopTickTimer();
	finishMillis = millis();

	// Auto-flag every remaining mine for the satisfaction "I solved it"
	// read. flagsPlaced becomes totalMines so the HUD counter ticks to
	// zero.
	const uint8_t cnt = cellCount();
	for(uint16_t i = 0; i < cnt; ++i) {
		if(cellMine[i] && !cellFlagged[i]) {
			cellFlagged[i] = 1;
			++flagsPlaced;
		}
	}
	const uint8_t c = cols();
	const uint8_t r = rows();
	for(uint8_t row = 0; row < r; ++row) {
		for(uint8_t col = 0; col < c; ++col) {
			renderCell(col, row);
		}
	}
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneMinesweeper::loseMatch(uint8_t struckIndex) {
	state = GameState::GameOver;
	stopTickTimer();
	finishMillis = millis();
	explodedIndex = struckIndex;
	cellRevealed[struckIndex] = 1;
	revealAllMines();
	refreshSoftKeys();
	refreshOverlay();
}

// ===========================================================================
// helpers
// ===========================================================================

uint8_t PhoneMinesweeper::countAdjacentMines(uint8_t col, uint8_t row) const {
	uint8_t n = 0;
	for(int8_t dy = -1; dy <= 1; ++dy) {
		for(int8_t dx = -1; dx <= 1; ++dx) {
			if(dx == 0 && dy == 0) continue;
			const int8_t nx = static_cast<int8_t>(col) + dx;
			const int8_t ny = static_cast<int8_t>(row) + dy;
			if(!inBounds(nx, ny)) continue;
			if(cellMine[indexOf(static_cast<uint8_t>(nx),
			                    static_cast<uint8_t>(ny))]) ++n;
		}
	}
	return n;
}

uint16_t PhoneMinesweeper::fieldOriginX() const {
	const uint16_t fieldW = static_cast<uint16_t>(cols()) * cellPx();
	// Centre horizontally on the 160 px panel.
	return static_cast<uint16_t>((160 - fieldW) / 2);
}

uint16_t PhoneMinesweeper::fieldOriginY() const {
	const uint16_t fieldH = static_cast<uint16_t>(rows()) * cellPx();
	const uint16_t available = FieldYBot - FieldYTop;
	// Centre vertically inside the available band so the smaller boards
	// do not feel like they are crammed against the HUD.
	if(fieldH >= available) return FieldYTop;
	return static_cast<uint16_t>(FieldYTop + (available - fieldH) / 2);
}

// ===========================================================================
// rendering
// ===========================================================================

void PhoneMinesweeper::renderAllCells() {
	const uint8_t c = cols();
	const uint8_t r = rows();
	for(uint8_t row = 0; row < r; ++row) {
		for(uint8_t col = 0; col < c; ++col) {
			renderCell(col, row);
		}
	}
}

void PhoneMinesweeper::renderCell(uint8_t col, uint8_t row) {
	if(!inBounds(col, row)) return;
	const uint16_t idx = indexOf(col, row);
	auto* s = cellSprites[idx];
	auto* l = cellLabels[idx];
	if(s == nullptr || l == nullptr) return;

	if(cellRevealed[idx]) {
		if(cellMine[idx]) {
			// Show the struck mine in accent, the rest in dim purple.
			const bool struck = (idx == explodedIndex);
			lv_obj_set_style_bg_color(s,
			                          struck ? MP_ACCENT : MP_BG_DARK, 0);
			lv_obj_set_style_border_color(s, MP_ACCENT, 0);
			lv_obj_set_style_text_color(l,
			                            struck ? MP_TEXT : MP_ACCENT, 0);
			lv_label_set_text(l, "*");
		} else {
			// Opened safe cell -- darken the background and show the
			// adjacent-mine digit (blank for zero).
			lv_obj_set_style_bg_color(s, MP_BG_DARK, 0);
			lv_obj_set_style_border_color(s, MP_DIM, 0);
			if(cellAdjacent[idx] > 0) {
				char buf[3];
				snprintf(buf, sizeof(buf), "%u",
				         static_cast<unsigned>(cellAdjacent[idx]));
				lv_label_set_text(l, buf);
				lv_obj_set_style_text_color(l,
				                            digitColour(cellAdjacent[idx]),
				                            0);
			} else {
				lv_label_set_text(l, "");
			}
		}
	} else if(cellFlagged[idx]) {
		// Unrevealed flagged cell -- dim background, accent border + tag.
		lv_obj_set_style_bg_color(s, MP_DIM, 0);
		lv_obj_set_style_border_color(s, MP_ACCENT, 0);
		lv_obj_set_style_text_color(l, MP_ACCENT, 0);
		lv_label_set_text(l, "F");
	} else {
		// Plain unrevealed cell.
		lv_obj_set_style_bg_color(s, MP_DIM, 0);
		lv_obj_set_style_border_color(s, MP_LABEL_DIM, 0);
		lv_label_set_text(l, "");
	}
}

void PhoneMinesweeper::renderCursor() {
	// Dim every other cell back to its baseline border, then highlight
	// the cursor's cell on top. Done as a full sweep so we do not have
	// to track the previous cursor cell separately.
	const uint8_t c = cols();
	const uint8_t r = rows();
	for(uint8_t row = 0; row < r; ++row) {
		for(uint8_t col = 0; col < c; ++col) {
			if(col == cursorCol && row == cursorRow) continue;
			renderCell(col, row);
		}
	}
	const uint16_t idx = indexOf(cursorCol, cursorRow);
	auto* s = cellSprites[idx];
	if(s == nullptr) return;
	lv_obj_set_style_border_color(s, MP_HIGHLIGHT, 0);
	lv_obj_move_foreground(s);
}

void PhoneMinesweeper::refreshHud() {
	if(hudDiffLabel != nullptr) {
		lv_label_set_text(hudDiffLabel, Difficulties[difficulty].name);
	}
	if(hudMineLabel != nullptr) {
		const int remaining = static_cast<int>(totalMines)
		                    - static_cast<int>(flagsPlaced);
		char buf[8];
		snprintf(buf, sizeof(buf), "*%02d", remaining);
		lv_label_set_text(hudMineLabel, buf);
	}
	if(hudTimerLabel != nullptr) {
		uint32_t elapsedMs = 0;
		if(state == GameState::Playing && startMillis > 0) {
			elapsedMs = millis() - startMillis;
		} else if((state == GameState::Won || state == GameState::GameOver)
		          && startMillis > 0 && finishMillis >= startMillis) {
			elapsedMs = finishMillis - startMillis;
		}
		uint32_t s = elapsedMs / 1000;
		if(s > 999) s = 999;
		char buf[8];
		snprintf(buf, sizeof(buf), "%03lus", static_cast<unsigned long>(s));
		lv_label_set_text(hudTimerLabel, buf);
	}
}

void PhoneMinesweeper::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	switch(state) {
		case GameState::Idle:
			softKeys->setLeft("DIG");
			softKeys->setRight("BACK");
			break;
		case GameState::Playing:
			softKeys->setLeft("DIG");
			softKeys->setRight("BACK");
			break;
		case GameState::Won:
		case GameState::GameOver:
			softKeys->setLeft("AGAIN");
			softKeys->setRight("BACK");
			break;
	}
}

void PhoneMinesweeper::refreshOverlay() {
	if(overlayLabel == nullptr) return;
	switch(state) {
		case GameState::Idle: {
			char buf[40];
			snprintf(buf, sizeof(buf),
			         "%s\nPRESS 5 TO DIG\nR FOR DIFFICULTY",
			         Difficulties[difficulty].name);
			lv_label_set_text(overlayLabel, buf);
			lv_obj_set_style_text_color(overlayLabel, MP_TEXT, 0);
			lv_obj_set_style_border_color(overlayLabel, MP_ACCENT, 0);
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
		}
		case GameState::Playing:
			lv_obj_add_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
		case GameState::Won: {
			uint32_t s = (finishMillis - startMillis) / 1000;
			if(s > 999) s = 999;
			char buf[40];
			snprintf(buf, sizeof(buf),
			         "CLEAR!\n%lus  %s\nA TO RESTART",
			         static_cast<unsigned long>(s),
			         Difficulties[difficulty].name);
			lv_label_set_text(overlayLabel, buf);
			lv_obj_set_style_text_color(overlayLabel, MP_HIGHLIGHT, 0);
			lv_obj_set_style_border_color(overlayLabel, MP_HIGHLIGHT, 0);
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
		}
		case GameState::GameOver: {
			uint32_t s = (finishMillis - startMillis) / 1000;
			if(s > 999) s = 999;
			char buf[40];
			snprintf(buf, sizeof(buf),
			         "BOOM!\n%lus  %s\nA TO RESTART",
			         static_cast<unsigned long>(s),
			         Difficulties[difficulty].name);
			lv_label_set_text(overlayLabel, buf);
			lv_obj_set_style_text_color(overlayLabel, MP_ACCENT, 0);
			lv_obj_set_style_border_color(overlayLabel, MP_ACCENT, 0);
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
		}
	}
}

// ===========================================================================
// timer
// ===========================================================================

void PhoneMinesweeper::startTickTimer() {
	if(tickTimer != nullptr) return;
	// 250 ms cadence so the visible "000s" -> "001s" transition feels
	// snappy without spamming LVGL with redraws every frame.
	tickTimer = lv_timer_create(&PhoneMinesweeper::onTickStatic, 250, this);
}

void PhoneMinesweeper::stopTickTimer() {
	if(tickTimer != nullptr) {
		lv_timer_del(tickTimer);
		tickTimer = nullptr;
	}
}

void PhoneMinesweeper::onTickStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneMinesweeper*>(timer->user_data);
	if(self == nullptr) return;
	if(self->state != GameState::Playing) return;
	self->refreshHud();
}

// ===========================================================================
// input
// ===========================================================================

void PhoneMinesweeper::buttonPressed(uint i) {
	// Always allow BACK to bail out, regardless of state.
	if(i == BTN_BACK) {
		if(softKeys) softKeys->flashRight();
		pop();
		return;
	}

	switch(state) {
		case GameState::Idle:
		case GameState::Playing: {
			// Movement (d-pad + numpad both supported).
			if(i == BTN_LEFT || i == BTN_4) {
				if(cursorCol > 0) --cursorCol;
				else cursorCol = static_cast<uint8_t>(cols() - 1);
				renderCursor();
				return;
			}
			if(i == BTN_RIGHT || i == BTN_6) {
				cursorCol = static_cast<uint8_t>((cursorCol + 1) % cols());
				renderCursor();
				return;
			}
			if(i == BTN_2) {
				if(cursorRow > 0) --cursorRow;
				else cursorRow = static_cast<uint8_t>(rows() - 1);
				renderCursor();
				return;
			}
			if(i == BTN_8) {
				cursorRow = static_cast<uint8_t>((cursorRow + 1) % rows());
				renderCursor();
				return;
			}

			// Reveal (numpad 5 + ENTER).
			if(i == BTN_5 || i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				digCell(cursorCol, cursorRow);
				renderCursor();
				return;
			}

			// Flag toggle (numpad 0).
			if(i == BTN_0) {
				toggleFlag(cursorCol, cursorRow);
				renderCursor();
				return;
			}

			// Difficulty cycle is only legal pre-game so we do not yank
			// the rug out from under an in-progress board.
			if(i == BTN_R && state == GameState::Idle) {
				cycleDifficulty();
				return;
			}
			return;
		}

		case GameState::Won:
		case GameState::GameOver: {
			if(i == BTN_ENTER || i == BTN_5) {
				if(softKeys) softKeys->flashLeft();
				enterIdle();
				return;
			}
			if(i == BTN_R) {
				cycleDifficulty();
				return;
			}
			return;
		}
	}
}
