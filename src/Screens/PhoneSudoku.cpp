#include "PhoneSudoku.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - kept identical to every other Phone* widget
// so the screen sits beside PhoneTetris (S71/72), PhoneTicTacToe (S81),
// Phone2048 (S93), PhoneSolitaire (S94) without a visual seam.
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)  // dim purple captions

namespace {

// Swap two values in place. We avoid <utility> to stay leaner on the
// header / module budget; sudoku only needs a uint8_t swap.
inline void swapU8(uint8_t& a, uint8_t& b) {
	const uint8_t t = a;
	a = b;
	b = t;
}

} // namespace

// ===========================================================================
// ctor / dtor
// ===========================================================================

PhoneSudoku::PhoneSudoku()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr) {

	for(uint8_t i = 0; i < CellCount; ++i) {
		cellSprites[i] = nullptr;
		cellLabels[i]  = nullptr;
		solution[i]    = 0;
		puzzle[i]      = 0;
		given[i]       = false;
	}

	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildHud();
	buildBoard();
	buildOverlay();
	buildPicker();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("PLAY");
	softKeys->setRight("BACK");

	enterPicker();
}

PhoneSudoku::~PhoneSudoku() {
	cancelTickTimer();
	// All children parented to obj; LVScreen frees them recursively.
}

void PhoneSudoku::onStart() {
	Input::getInstance()->addListener(this);
	if(state == State::Playing) {
		scheduleTickTimer();
	}
}

void PhoneSudoku::onStop() {
	Input::getInstance()->removeListener(this);
	cancelTickTimer();
}

// ===========================================================================
// build helpers
// ===========================================================================

void PhoneSudoku::buildHud() {
	// Three small text badges arranged left | mid | right. Difficulty is
	// the cyan badge on the left; timer is the cream badge in the middle;
	// error counter is the orange badge on the right. pixelbasic7 keeps
	// the strip readable inside the 10 px HUD slice without crowding.
	hudDifficultyLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudDifficultyLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudDifficultyLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(hudDifficultyLabel, "EASY");
	lv_obj_set_pos(hudDifficultyLabel, 4, HudY + 2);

	hudTimerLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudTimerLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudTimerLabel, MP_TEXT, 0);
	lv_label_set_text(hudTimerLabel, "00:00");
	lv_obj_set_align(hudTimerLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hudTimerLabel, HudY + 2);

	hudErrLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudErrLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudErrLabel, MP_ACCENT, 0);
	lv_label_set_text(hudErrLabel, "ERR 00");
	lv_obj_set_pos(hudErrLabel, 122, HudY + 2);
}

void PhoneSudoku::buildBoard() {
	// Outer board container: 92x92 to fit a 1 px cyan border around the
	// 90x90 cell grid. Pinned at (BoardOriginX-1, BoardOriginY-1) so the
	// cell origin inside the container is (1, 1).
	boardContainer = lv_obj_create(obj);
	lv_obj_remove_style_all(boardContainer);
	lv_obj_set_size(boardContainer, BoardPx + 2, BoardPx + 2);
	lv_obj_set_pos(boardContainer, BoardOriginX - 1, BoardOriginY - 1);
	lv_obj_set_style_bg_color(boardContainer, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(boardContainer, LV_OPA_70, 0);
	lv_obj_set_style_border_color(boardContainer, MP_HIGHLIGHT, 0);
	lv_obj_set_style_border_width(boardContainer, 1, 0);
	lv_obj_set_style_radius(boardContainer, 0, 0);
	lv_obj_set_style_pad_all(boardContainer, 0, 0);
	lv_obj_clear_flag(boardContainer, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(boardContainer, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(boardContainer, LV_OBJ_FLAG_IGNORE_LAYOUT);

	// 81 cells. Each cell is a 10x10 lv_obj with a 1 px dim border + a
	// pixelbasic7 label parked dead-centre. Cells are pinned with
	// IGNORE_LAYOUT so the parent container never re-flows them.
	for(uint8_t row = 0; row < BoardSize; ++row) {
		for(uint8_t col = 0; col < BoardSize; ++col) {
			const uint8_t cell = indexOf(col, row);

			lv_obj_t* s = lv_obj_create(boardContainer);
			lv_obj_remove_style_all(s);
			lv_obj_set_size(s, CellPx, CellPx);
			lv_obj_set_pos(s, 1 + col * CellPx, 1 + row * CellPx);
			lv_obj_set_style_bg_color(s, MP_BG_DARK, 0);
			lv_obj_set_style_bg_opa(s, LV_OPA_TRANSP, 0);
			lv_obj_set_style_border_color(s, MP_DIM, 0);
			lv_obj_set_style_border_width(s, 1, 0);
			lv_obj_set_style_radius(s, 0, 0);
			lv_obj_set_style_pad_all(s, 0, 0);
			lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_clear_flag(s, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_flag(s, LV_OBJ_FLAG_IGNORE_LAYOUT);
			cellSprites[cell] = s;

			lv_obj_t* lbl = lv_label_create(s);
			lv_obj_set_style_text_font(lbl, &pixelbasic7, 0);
			lv_obj_set_style_text_color(lbl, MP_TEXT, 0);
			lv_obj_set_style_pad_all(lbl, 0, 0);
			lv_label_set_text(lbl, "");
			lv_obj_set_align(lbl, LV_ALIGN_CENTER);
			cellLabels[cell] = lbl;
		}
	}

	// Sub-box dividers: four 1 px cyan bars carving the board into
	// 3x3 boxes. Drawn inside the boardContainer so they share the
	// IGNORE_LAYOUT/clip behavior with the cells.
	auto addDivider = [this](lv_coord_t x, lv_coord_t y,
	                          lv_coord_t w, lv_coord_t h) {
		lv_obj_t* d = lv_obj_create(boardContainer);
		lv_obj_remove_style_all(d);
		lv_obj_set_size(d, w, h);
		lv_obj_set_pos(d, x, y);
		lv_obj_set_style_bg_color(d, MP_HIGHLIGHT, 0);
		lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
		lv_obj_set_style_border_width(d, 0, 0);
		lv_obj_set_style_radius(d, 0, 0);
		lv_obj_set_style_pad_all(d, 0, 0);
		lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(d, LV_OBJ_FLAG_IGNORE_LAYOUT);
	};

	// Two horizontal dividers between the 3rd-4th and 6th-7th rows.
	addDivider(0, 1 + 3 * CellPx, BoardPx + 2, 1);
	addDivider(0, 1 + 6 * CellPx, BoardPx + 2, 1);
	// Two vertical dividers between the 3rd-4th and 6th-7th columns.
	addDivider(1 + 3 * CellPx, 0, 1, BoardPx + 2);
	addDivider(1 + 6 * CellPx, 0, 1, BoardPx + 2);
}

void PhoneSudoku::buildOverlay() {
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

void PhoneSudoku::buildPicker() {
	// Centred panel that fills the board area when the picker is open.
	// We deliberately overlay the empty board grid so the visual jump
	// into Playing feels instant -- the cells are already there, only
	// their content (the starting puzzle) appears.
	pickerPanel = lv_obj_create(obj);
	lv_obj_remove_style_all(pickerPanel);
	lv_obj_set_size(pickerPanel, 110, 76);
	lv_obj_set_pos(pickerPanel, (160 - 110) / 2, BoardOriginY + 6);
	lv_obj_set_style_bg_color(pickerPanel, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(pickerPanel, LV_OPA_90, 0);
	lv_obj_set_style_border_color(pickerPanel, MP_HIGHLIGHT, 0);
	lv_obj_set_style_border_width(pickerPanel, 1, 0);
	lv_obj_set_style_radius(pickerPanel, 2, 0);
	lv_obj_set_style_pad_all(pickerPanel, 0, 0);
	lv_obj_clear_flag(pickerPanel, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(pickerPanel, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(pickerPanel, LV_OBJ_FLAG_IGNORE_LAYOUT);

	// Title ("DIFFICULTY") at the top of the panel.
	lv_obj_t* title = lv_label_create(pickerPanel);
	lv_obj_set_style_text_font(title, &pixelbasic7, 0);
	lv_obj_set_style_text_color(title, MP_ACCENT, 0);
	lv_label_set_text(title, "DIFFICULTY");
	lv_obj_set_align(title, LV_ALIGN_TOP_MID);
	lv_obj_set_y(title, 4);

	// Three options stacked vertically. Each is its own pixelbasic7
	// label so we can recolour the focused entry without affecting the
	// surrounding text.
	pickerEasyLabel = lv_label_create(pickerPanel);
	lv_obj_set_style_text_font(pickerEasyLabel, &pixelbasic7, 0);
	lv_label_set_text(pickerEasyLabel, "1  EASY");
	lv_obj_set_align(pickerEasyLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(pickerEasyLabel, 18);

	pickerMediumLabel = lv_label_create(pickerPanel);
	lv_obj_set_style_text_font(pickerMediumLabel, &pixelbasic7, 0);
	lv_label_set_text(pickerMediumLabel, "2  MEDIUM");
	lv_obj_set_align(pickerMediumLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(pickerMediumLabel, 30);

	pickerHardLabel = lv_label_create(pickerPanel);
	lv_obj_set_style_text_font(pickerHardLabel, &pixelbasic7, 0);
	lv_label_set_text(pickerHardLabel, "3  HARD");
	lv_obj_set_align(pickerHardLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(pickerHardLabel, 42);

	pickerHintLabel = lv_label_create(pickerPanel);
	lv_obj_set_style_text_font(pickerHintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(pickerHintLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(pickerHintLabel, "4/6 OR 1-3, A");
	lv_obj_set_align(pickerHintLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(pickerHintLabel, 58);

	lv_obj_add_flag(pickerPanel, LV_OBJ_FLAG_HIDDEN);
}

// ===========================================================================
// generator
// ===========================================================================

void PhoneSudoku::generateSolution() {
	// Canonical Latin-square base that always satisfies row, column and
	// 3x3-box constraints. We then apply a chain of sudoku-symmetry-
	// preserving transforms to randomise it.
	for(uint8_t r = 0; r < BoardSize; ++r) {
		for(uint8_t c = 0; c < BoardSize; ++c) {
			solution[r * BoardSize + c] =
			    static_cast<uint8_t>(((r * 3 + r / 3 + c) % 9) + 1);
		}
	}

	// 1) Relabel: pick two distinct digits and swap them throughout the
	//    grid. Repeated several times so the digit identities scramble.
	for(uint8_t k = 0; k < 16; ++k) {
		const uint8_t a = static_cast<uint8_t>((rand() % 9) + 1);
		const uint8_t b = static_cast<uint8_t>((rand() % 9) + 1);
		if(a == b) continue;
		for(uint8_t i = 0; i < CellCount; ++i) {
			if(solution[i] == a) solution[i] = b;
			else if(solution[i] == b) solution[i] = a;
		}
	}

	// 2) Swap two rows within the same band (rows 0..2, 3..5, 6..8).
	//    Allowed because rows in the same band cover the same set of
	//    boxes -- swapping them keeps every constraint intact.
	for(uint8_t k = 0; k < 12; ++k) {
		const uint8_t band = static_cast<uint8_t>(rand() % 3);
		const uint8_t r1 = static_cast<uint8_t>(band * 3 + (rand() % 3));
		const uint8_t r2 = static_cast<uint8_t>(band * 3 + (rand() % 3));
		if(r1 == r2) continue;
		for(uint8_t c = 0; c < BoardSize; ++c) {
			swapU8(solution[r1 * BoardSize + c],
			       solution[r2 * BoardSize + c]);
		}
	}

	// 3) Swap two columns within the same stack. Same reasoning as the
	//    row-within-band swap, just transposed.
	for(uint8_t k = 0; k < 12; ++k) {
		const uint8_t stack = static_cast<uint8_t>(rand() % 3);
		const uint8_t c1 = static_cast<uint8_t>(stack * 3 + (rand() % 3));
		const uint8_t c2 = static_cast<uint8_t>(stack * 3 + (rand() % 3));
		if(c1 == c2) continue;
		for(uint8_t r = 0; r < BoardSize; ++r) {
			swapU8(solution[r * BoardSize + c1],
			       solution[r * BoardSize + c2]);
		}
	}

	// 4) Swap two whole bands (groups of 3 rows). Allowed for the same
	//    constraint-preservation reasons.
	for(uint8_t k = 0; k < 6; ++k) {
		const uint8_t b1 = static_cast<uint8_t>(rand() % 3);
		const uint8_t b2 = static_cast<uint8_t>(rand() % 3);
		if(b1 == b2) continue;
		for(uint8_t i = 0; i < 3; ++i) {
			for(uint8_t c = 0; c < BoardSize; ++c) {
				swapU8(solution[(b1 * 3 + i) * BoardSize + c],
				       solution[(b2 * 3 + i) * BoardSize + c]);
			}
		}
	}

	// 5) Swap two whole stacks (groups of 3 columns).
	for(uint8_t k = 0; k < 6; ++k) {
		const uint8_t s1 = static_cast<uint8_t>(rand() % 3);
		const uint8_t s2 = static_cast<uint8_t>(rand() % 3);
		if(s1 == s2) continue;
		for(uint8_t i = 0; i < 3; ++i) {
			for(uint8_t r = 0; r < BoardSize; ++r) {
				swapU8(solution[r * BoardSize + s1 * 3 + i],
				       solution[r * BoardSize + s2 * 3 + i]);
			}
		}
	}

	// 6) Optional transpose. Half the rolls flip the grid along its
	//    main diagonal; the resulting grid still satisfies every
	//    sudoku constraint but completely re-shuffles the row / column
	//    relationships.
	if((rand() & 1) == 0) {
		uint8_t tmp[CellCount];
		for(uint8_t r = 0; r < BoardSize; ++r) {
			for(uint8_t c = 0; c < BoardSize; ++c) {
				tmp[c * BoardSize + r] = solution[r * BoardSize + c];
			}
		}
		for(uint8_t i = 0; i < CellCount; ++i) {
			solution[i] = tmp[i];
		}
	}
}

void PhoneSudoku::carvePuzzle() {
	for(uint8_t i = 0; i < CellCount; ++i) {
		puzzle[i] = solution[i];
		given[i]  = true;
	}

	const uint8_t holes = holesFor(difficulty);

	// Fisher-Yates shuffle of the cell indices, then blank the first
	// `holes` cells in the shuffled order. This gives every cell an
	// equal chance of being a hole independently of difficulty.
	uint8_t indices[CellCount];
	for(uint8_t i = 0; i < CellCount; ++i) {
		indices[i] = i;
	}
	for(uint8_t i = CellCount - 1; i > 0; --i) {
		const uint8_t j = static_cast<uint8_t>(rand() % (i + 1));
		swapU8(indices[i], indices[j]);
	}
	for(uint8_t i = 0; i < holes; ++i) {
		const uint8_t cell = indices[i];
		puzzle[cell] = 0;
		given[cell]  = false;
	}
}

// ===========================================================================
// state transitions
// ===========================================================================

void PhoneSudoku::enterPicker() {
	cancelTickTimer();
	state = State::Picker;
	pickerChoice = difficulty;
	cursor = 40;
	errors = 0;
	startMs = 0;
	solvedMs = 0;

	// Wipe the on-screen board so the player does not see stale numbers
	// from a previous game while the picker is open.
	for(uint8_t i = 0; i < CellCount; ++i) {
		puzzle[i] = 0;
		given[i]  = false;
	}
	renderAllCells();
	renderCursor();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
	refreshPicker();

	if(pickerPanel != nullptr) {
		lv_obj_clear_flag(pickerPanel, LV_OBJ_FLAG_HIDDEN);
		lv_obj_move_foreground(pickerPanel);
	}
}

void PhoneSudoku::startGame(Difficulty d) {
	difficulty = d;
	if(pickerPanel != nullptr) {
		lv_obj_add_flag(pickerPanel, LV_OBJ_FLAG_HIDDEN);
	}

	generateSolution();
	carvePuzzle();

	state = State::Playing;
	cursor = 40;
	errors = 0;
	startMs = millis();
	solvedMs = 0;

	renderAllCells();
	renderCursor();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();

	scheduleTickTimer();
}

void PhoneSudoku::onTickStaticTrampoline(lv_timer_t* t) {
	auto* self = static_cast<PhoneSudoku*>(t->user_data);
	if(self == nullptr) return;
	self->onTickStatic();
}

void PhoneSudoku::onTickStatic() {
	if(state != State::Playing) return;
	refreshHud();
}

// ===========================================================================
// rendering
// ===========================================================================

void PhoneSudoku::renderAllCells() {
	for(uint8_t i = 0; i < CellCount; ++i) {
		renderCell(i);
	}
	renderCursor();
}

void PhoneSudoku::renderCell(uint8_t cell) {
	if(cell >= CellCount) return;
	lv_obj_t* s = cellSprites[cell];
	lv_obj_t* l = cellLabels[cell];
	if(s == nullptr || l == nullptr) return;

	// Default styling - dim border, transparent fill. The cursor pass
	// repaints the focused cell on top.
	lv_obj_set_style_bg_color(s, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(s, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_color(s, MP_DIM, 0);
	lv_obj_set_style_border_width(s, 1, 0);

	const uint8_t v = puzzle[cell];
	if(v == 0) {
		lv_label_set_text(l, "");
		return;
	}

	char buf[2] = { static_cast<char>('0' + v), '\0' };
	lv_label_set_text(l, buf);

	if(given[cell]) {
		// Locked starting clue - cyan so the player can tell at a glance
		// which cells are non-editable.
		lv_obj_set_style_text_color(l, MP_HIGHLIGHT, 0);
	} else if(v == solution[cell]) {
		// Player-placed correct digit - cream.
		lv_obj_set_style_text_color(l, MP_TEXT, 0);
	} else {
		// Player-placed wrong digit - sunset orange tint + faint
		// wash on the cell so the mistake reads even at a glance.
		lv_obj_set_style_text_color(l, MP_ACCENT, 0);
		lv_obj_set_style_bg_color(s, MP_ACCENT, 0);
		lv_obj_set_style_bg_opa(s, LV_OPA_30, 0);
	}
}

void PhoneSudoku::renderCursor() {
	// Reset every cell first so a stale cursor border on the previous
	// cell goes away cleanly. Then redraw the focused cell with the
	// accent border on top.
	for(uint8_t i = 0; i < CellCount; ++i) {
		renderCell(i);
	}

	if(state != State::Playing) return;

	if(cursor >= CellCount) return;
	lv_obj_t* s = cellSprites[cursor];
	if(s == nullptr) return;

	// Cursor border: cyan when the cell is editable, dim cream when the
	// cell is a locked given (informs "you can't change this" without
	// stealing focus).
	lv_obj_set_style_border_color(s,
	                              given[cursor] ? MP_LABEL_DIM
	                                             : MP_HIGHLIGHT,
	                              0);
	lv_obj_set_style_border_width(s, 2, 0);
	lv_obj_move_foreground(s);
}

void PhoneSudoku::refreshHud() {
	if(hudDifficultyLabel != nullptr) {
		lv_label_set_text(hudDifficultyLabel, difficultyName(difficulty));
	}

	if(hudTimerLabel != nullptr) {
		uint32_t elapsedSec = 0;
		if(state == State::Playing && startMs != 0) {
			elapsedSec = (millis() - startMs) / 1000U;
		} else if(state == State::Solved && startMs != 0) {
			elapsedSec = (solvedMs - startMs) / 1000U;
		}
		const uint32_t mm = elapsedSec / 60U;
		const uint32_t ss = elapsedSec % 60U;
		char buf[8];
		if(mm > 99) {
			lv_label_set_text(hudTimerLabel, "99:59");
		} else {
			snprintf(buf, sizeof(buf), "%02u:%02u",
			         static_cast<unsigned>(mm),
			         static_cast<unsigned>(ss));
			lv_label_set_text(hudTimerLabel, buf);
		}
	}

	if(hudErrLabel != nullptr) {
		char buf[12];
		const unsigned e = errors > 99 ? 99 : errors;
		snprintf(buf, sizeof(buf), "ERR %02u", e);
		lv_label_set_text(hudErrLabel, buf);
	}
}

void PhoneSudoku::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	switch(state) {
		case State::Picker:
			softKeys->setLeft("PLAY");
			softKeys->setRight("BACK");
			break;
		case State::Playing:
			softKeys->setLeft("PLACE");
			softKeys->setRight("BACK");
			break;
		case State::Solved:
			softKeys->setLeft("AGAIN");
			softKeys->setRight("BACK");
			break;
	}
}

void PhoneSudoku::refreshOverlay() {
	if(overlayLabel == nullptr) return;
	if(state != State::Solved) {
		lv_obj_add_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
		return;
	}

	uint32_t elapsedSec = 0;
	if(startMs != 0 && solvedMs >= startMs) {
		elapsedSec = (solvedMs - startMs) / 1000U;
	}
	const uint32_t mm = elapsedSec / 60U;
	const uint32_t ss = elapsedSec % 60U;
	char buf[40];
	const unsigned e = errors > 99 ? 99 : errors;
	if(mm > 99) {
		snprintf(buf, sizeof(buf), "SOLVED!\n99:59  ERR %02u", e);
	} else {
		snprintf(buf, sizeof(buf), "SOLVED!\n%02u:%02u  ERR %02u",
		         static_cast<unsigned>(mm),
		         static_cast<unsigned>(ss),
		         e);
	}
	lv_label_set_text(overlayLabel, buf);
	lv_obj_set_style_text_color(overlayLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_style_border_color(overlayLabel, MP_HIGHLIGHT, 0);
	lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
	lv_obj_move_foreground(overlayLabel);
}

void PhoneSudoku::refreshPicker() {
	auto colorise = [this](lv_obj_t* lbl, Difficulty target) {
		if(lbl == nullptr) return;
		if(pickerChoice == target) {
			lv_obj_set_style_text_color(lbl, MP_ACCENT, 0);
		} else {
			lv_obj_set_style_text_color(lbl, MP_TEXT, 0);
		}
	};
	colorise(pickerEasyLabel,   Difficulty::Easy);
	colorise(pickerMediumLabel, Difficulty::Medium);
	colorise(pickerHardLabel,   Difficulty::Hard);
}

// ===========================================================================
// helpers
// ===========================================================================

bool PhoneSudoku::boardComplete() const {
	for(uint8_t i = 0; i < CellCount; ++i) {
		if(puzzle[i] == 0) return false;
	}
	return true;
}

bool PhoneSudoku::boardMatchesSolution() const {
	for(uint8_t i = 0; i < CellCount; ++i) {
		if(puzzle[i] != solution[i]) return false;
	}
	return true;
}

void PhoneSudoku::cancelTickTimer() {
	if(tickTimer != nullptr) {
		lv_timer_del(tickTimer);
		tickTimer = nullptr;
	}
}

void PhoneSudoku::scheduleTickTimer() {
	cancelTickTimer();
	tickTimer = lv_timer_create(&PhoneSudoku::onTickStaticTrampoline,
	                            1000, this);
}

const char* PhoneSudoku::difficultyName(Difficulty d) {
	switch(d) {
		case Difficulty::Easy:   return "EASY";
		case Difficulty::Medium: return "MED";
		case Difficulty::Hard:   return "HARD";
	}
	return "EASY";
}

uint8_t PhoneSudoku::holesFor(Difficulty d) {
	switch(d) {
		case Difficulty::Easy:   return 40;  // 41 clues
		case Difficulty::Medium: return 48;  // 33 clues
		case Difficulty::Hard:   return 54;  // 27 clues
	}
	return 40;
}

// ===========================================================================
// input
// ===========================================================================

void PhoneSudoku::buttonPressed(uint i) {
	// BACK always pops out of the screen, regardless of state.
	if(i == BTN_BACK) {
		if(softKeys) softKeys->flashRight();
		pop();
		return;
	}

	switch(state) {
		case State::Picker: {
			// Cycle / direct-pick the difficulty.
			if(i == BTN_LEFT || i == BTN_4) {
				const uint8_t cur = static_cast<uint8_t>(pickerChoice);
				pickerChoice = static_cast<Difficulty>((cur + 2) % 3);
				refreshPicker();
				return;
			}
			if(i == BTN_RIGHT || i == BTN_6) {
				const uint8_t cur = static_cast<uint8_t>(pickerChoice);
				pickerChoice = static_cast<Difficulty>((cur + 1) % 3);
				refreshPicker();
				return;
			}
			if(i == BTN_1) {
				pickerChoice = Difficulty::Easy;
				refreshPicker();
				return;
			}
			if(i == BTN_2) {
				pickerChoice = Difficulty::Medium;
				refreshPicker();
				return;
			}
			if(i == BTN_3) {
				pickerChoice = Difficulty::Hard;
				refreshPicker();
				return;
			}
			if(i == BTN_5 || i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				startGame(pickerChoice);
				return;
			}
			return;
		}

		case State::Playing: {
			// Reroll a fresh puzzle without going back to the picker.
			if(i == BTN_R) {
				startGame(difficulty);
				return;
			}

			// Movement (d-pad + numpad both supported, both wrap).
			if(i == BTN_LEFT || i == BTN_4) {
				uint8_t col = cursor % BoardSize;
				const uint8_t row = cursor / BoardSize;
				col = (col == 0) ? (BoardSize - 1) : (col - 1);
				cursor = indexOf(col, row);
				renderCursor();
				return;
			}
			if(i == BTN_RIGHT || i == BTN_6) {
				const uint8_t col = static_cast<uint8_t>(((cursor % BoardSize) + 1) % BoardSize);
				const uint8_t row = cursor / BoardSize;
				cursor = indexOf(col, row);
				renderCursor();
				return;
			}
			if(i == BTN_2) {
				const uint8_t col = cursor % BoardSize;
				uint8_t row = cursor / BoardSize;
				row = (row == 0) ? (BoardSize - 1) : (row - 1);
				cursor = indexOf(col, row);
				renderCursor();
				return;
			}
			if(i == BTN_8) {
				const uint8_t col = cursor % BoardSize;
				const uint8_t row = static_cast<uint8_t>(((cursor / BoardSize) + 1) % BoardSize);
				cursor = indexOf(col, row);
				renderCursor();
				return;
			}

			// Clear current cell. BTN_0 is the canonical "delete digit"
			// key; BTN_ENTER doubles up as "confirm / commit clear" so
			// players who muscle-memory ENTER after navigating still get
			// the friendly behaviour.
			if(i == BTN_0 || i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				if(cursor < CellCount && !given[cursor]) {
					puzzle[cursor] = 0;
					renderCell(cursor);
					renderCursor();
				}
				return;
			}

			// 1..9: place that digit in the focused cell. We consider a
			// placement "wrong" only when it differs from the solution
			// (sudoku has at most one valid digit per cell in a unique
			// puzzle, so this is the same as the row/col/box conflict
			// check for our generated grids).
			uint8_t digit = 0;
			switch(i) {
				case BTN_1: digit = 1; break;
				case BTN_2: digit = 2; break;
				case BTN_3: digit = 3; break;
				case BTN_4: digit = 4; break;
				case BTN_5: digit = 5; break;
				case BTN_6: digit = 6; break;
				case BTN_7: digit = 7; break;
				case BTN_8: digit = 8; break;
				case BTN_9: digit = 9; break;
				default: break;
			}
			if(digit == 0) return;
			if(cursor >= CellCount) return;
			if(given[cursor]) {
				if(softKeys) softKeys->flashLeft();
				return;
			}
			if(softKeys) softKeys->flashLeft();

			const uint8_t prev = puzzle[cursor];
			puzzle[cursor] = digit;
			if(digit != solution[cursor] && digit != prev) {
				if(errors < 0xFFFFu) ++errors;
			}
			renderCell(cursor);
			renderCursor();
			refreshHud();

			if(boardComplete() && boardMatchesSolution()) {
				state = State::Solved;
				solvedMs = millis();
				cancelTickTimer();
				renderAllCells();
				refreshHud();
				refreshSoftKeys();
				refreshOverlay();
			}
			return;
		}

		case State::Solved: {
			// Reroll a brand-new puzzle at the same difficulty.
			if(i == BTN_R || i == BTN_ENTER || i == BTN_5) {
				if(softKeys) softKeys->flashLeft();
				startGame(difficulty);
				return;
			}
			return;
		}
	}
}
