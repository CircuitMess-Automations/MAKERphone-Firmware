#include "Phone2048.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - identical to every other Phone* widget so
// Phone2048 sits beside PhoneSlidingPuzzle (S80), PhoneTicTacToe (S81)
// and friends without a visual seam.
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)  // dim purple captions

// ===========================================================================
// ctor / dtor
// ===========================================================================

Phone2048::Phone2048()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr) {

	for(uint8_t i = 0; i < CellCount; ++i) {
		cellSprites[i] = nullptr;
		cellLabels[i]  = nullptr;
		board[i]       = 0;
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
	softKeys->setLeft("SLIDE");
	softKeys->setRight("BACK");

	newGame();
}

Phone2048::~Phone2048() {
	// All children parented to obj; LVScreen frees them recursively.
}

void Phone2048::onStart() {
	Input::getInstance()->addListener(this);
}

void Phone2048::onStop() {
	Input::getInstance()->removeListener(this);
}

// ===========================================================================
// build helpers
// ===========================================================================

void Phone2048::buildHud() {
	// SCORE on the left, BEST on the right - same HUD strip layout as
	// PhoneSlidingPuzzle / PhoneTetris so the player's eye lands in the
	// same place across games.
	hudScoreLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudScoreLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudScoreLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(hudScoreLabel, "SCORE 0000");
	lv_obj_set_pos(hudScoreLabel, 4, HudY + 2);

	hudBestLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudBestLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudBestLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(hudBestLabel, "BEST 0000");
	// Right-aligned so the best-score badge always hugs the screen edge
	// regardless of digit count - same trick PhoneTetris uses.
	lv_obj_set_align(hudBestLabel, LV_ALIGN_TOP_RIGHT);
	lv_obj_set_y(hudBestLabel, HudY + 2);
	lv_obj_set_style_pad_right(hudBestLabel, 4, 0);
}

void Phone2048::buildOverlay() {
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

void Phone2048::buildCells() {
	// One 23x23 sprite per board slot (1 px gap between cells gives the
	// grid a tidy lattice look at this resolution). Each sprite carries
	// a single centered label whose font/colour gets rewritten in
	// renderTile() per the current value.
	for(uint8_t row = 0; row < BoardSize; ++row) {
		for(uint8_t col = 0; col < BoardSize; ++col) {
			const uint8_t slot = indexOf(col, row);

			auto* s = lv_obj_create(obj);
			lv_obj_remove_style_all(s);
			lv_obj_set_size(s, TilePx - 1, TilePx - 1);
			lv_obj_set_pos(s,
			               BoardOriginX + col * TilePx,
			               BoardOriginY + row * TilePx);
			lv_obj_set_style_bg_color(s, MP_DIM, 0);
			lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
			lv_obj_set_style_border_color(s, MP_LABEL_DIM, 0);
			lv_obj_set_style_border_width(s, 1, 0);
			lv_obj_set_style_radius(s, 2, 0);
			lv_obj_set_style_pad_all(s, 0, 0);
			lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_clear_flag(s, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_flag(s, LV_OBJ_FLAG_IGNORE_LAYOUT);
			cellSprites[slot] = s;

			auto* lbl = lv_label_create(s);
			lv_obj_set_style_text_font(lbl, &pixelbasic16, 0);
			lv_obj_set_style_text_color(lbl, MP_TEXT, 0);
			lv_obj_set_style_pad_all(lbl, 0, 0);
			lv_label_set_text(lbl, "");
			lv_obj_set_align(lbl, LV_ALIGN_CENTER);
			cellLabels[slot] = lbl;
		}
	}
}

// ===========================================================================
// state transitions
// ===========================================================================

void Phone2048::newGame() {
	for(uint8_t i = 0; i < CellCount; ++i) {
		board[i] = 0;
	}
	score     = 0;
	winSticky = false;
	state     = GameState::Playing;

	// Seed the board with two starting tiles - this is the canonical
	// 2048 opening and gives the player something to work with on
	// move zero.
	spawnRandomTile();
	spawnRandomTile();

	renderAllTiles();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
}

void Phone2048::spawnRandomTile() {
	// Find every empty cell, then pick one uniformly at random. Counting
	// twice (once to size the candidate list, once to fill it) is cheap
	// at 16 cells and avoids a heap allocation.
	uint8_t empties[CellCount];
	uint8_t n = 0;
	for(uint8_t i = 0; i < CellCount; ++i) {
		if(board[i] == 0) empties[n++] = i;
	}
	if(n == 0) return;

	const uint8_t pick = empties[rand() % n];
	// 90/10 split between 2 and 4 - matches the original 2048 spec and
	// keeps the game from being too easy if "4"s spawned more often.
	board[pick] = (rand() % 10 == 0) ? 4 : 2;
}

bool Phone2048::slide(int8_t dx, int8_t dy) {
	if(state == GameState::GameOver) return false;
	if(dx == 0 && dy == 0) return false;
	// Only the four cardinal directions are supported.
	if(dx != 0 && dy != 0) return false;

	bool moved        = false;
	uint32_t scoreAdd = 0;
	bool madeWin      = false;

	// We process each "lane" independently. A lane is a 1-D sequence of
	// up to 4 cells along the slide direction. For each lane we collect
	// the non-zero values, merge adjacent equal pairs (each tile may
	// merge at most once per slide, the canonical 2048 rule), then
	// write the result back to the lane in slide order.
	//
	// Iteration order is chosen so that the outer loop walks the
	// "perpendicular" axis (each row for a horizontal slide, each
	// column for a vertical slide) and the inner loop walks "from the
	// far edge toward the near edge" along the slide axis -- i.e. the
	// first tile we encounter in each lane is the one closest to the
	// destination edge. That's the order we want for compaction.
	const bool horizontal = (dy == 0);
	const int8_t step = (dx > 0 || dy > 0) ? -1 : 1; // walk back-to-front

	for(uint8_t outer = 0; outer < BoardSize; ++outer) {
		uint16_t lane[BoardSize] = { 0, 0, 0, 0 };
		uint8_t  laneSlots[BoardSize] = { 0, 0, 0, 0 };

		// Build the lane in slide order: lane[0] is the cell closest to
		// the destination edge.
		uint8_t startInner = (step == 1) ? 0 : static_cast<uint8_t>(BoardSize - 1);
		for(uint8_t k = 0; k < BoardSize; ++k) {
			const uint8_t inner = static_cast<uint8_t>(startInner + step * static_cast<int8_t>(k));
			const uint8_t col = horizontal ? inner : outer;
			const uint8_t row = horizontal ? outer : inner;
			const uint8_t slot = indexOf(col, row);
			lane[k]      = board[slot];
			laneSlots[k] = slot;
		}

		// Compact + merge into a fresh "out" lane.
		uint16_t out[BoardSize] = { 0, 0, 0, 0 };
		uint8_t  outN = 0;
		for(uint8_t k = 0; k < BoardSize; ++k) {
			const uint16_t v = lane[k];
			if(v == 0) continue;
			if(outN > 0 && out[outN - 1] == v) {
				// Merge with the previous tile - it's the same value
				// and hasn't yet merged this slide (each "out" slot
				// represents a tile that has been placed at most once,
				// so this branch only ever fires once per slot).
				const uint16_t merged = static_cast<uint16_t>(v * 2);
				out[outN - 1] = merged;
				scoreAdd += merged;
				if(merged >= WinValue && !winSticky) {
					madeWin = true;
				}
			} else {
				out[outN++] = v;
			}
		}

		// Write the compacted lane back. If any slot's value changed we
		// flag the slide as a real move (eligible to spawn a new tile).
		for(uint8_t k = 0; k < BoardSize; ++k) {
			if(board[laneSlots[k]] != out[k]) moved = true;
			board[laneSlots[k]] = out[k];
		}
	}

	if(!moved) {
		// No tile actually moved or merged -- e.g. the player slid
		// against a wall they couldn't compress. Don't spawn, don't
		// burn a turn, don't update HUD. This matches every other 2048
		// implementation's idle-input convention.
		return false;
	}

	score += scoreAdd;
	if(score > bestScore) bestScore = score;

	spawnRandomTile();

	// Render before checking gameOver so the player sees the move that
	// closed off their last legal slide.
	renderAllTiles();
	refreshHud();

	if(madeWin) {
		// First 2048 of the round - show the celebration overlay. The
		// player presses A to keep playing toward 4096.
		winRound();
	} else if(!canMove()) {
		gameOver();
	}

	return true;
}

bool Phone2048::canMove() const {
	// Any empty cell makes another move trivially possible.
	for(uint8_t i = 0; i < CellCount; ++i) {
		if(board[i] == 0) return true;
	}
	// Otherwise look for any pair of horizontally or vertically
	// adjacent equal tiles - either one could merge on the next slide.
	for(uint8_t row = 0; row < BoardSize; ++row) {
		for(uint8_t col = 0; col < BoardSize; ++col) {
			const uint16_t v = board[indexOf(col, row)];
			if(col + 1 < BoardSize
			   && board[indexOf(static_cast<uint8_t>(col + 1), row)] == v) {
				return true;
			}
			if(row + 1 < BoardSize
			   && board[indexOf(col, static_cast<uint8_t>(row + 1))] == v) {
				return true;
			}
		}
	}
	return false;
}

void Phone2048::winRound() {
	// Sticky so we only celebrate the first 2048 of the round - any
	// subsequent 2048 merges happen quietly.
	winSticky = true;
	state     = GameState::Won;
	refreshSoftKeys();
	refreshOverlay();
}

void Phone2048::gameOver() {
	state = GameState::GameOver;
	if(score > bestScore) bestScore = score;
	refreshSoftKeys();
	refreshOverlay();
}

// ===========================================================================
// rendering
// ===========================================================================

void Phone2048::renderAllTiles() {
	for(uint8_t i = 0; i < CellCount; ++i) {
		renderTile(i);
	}
}

void Phone2048::renderTile(uint8_t slot) {
	if(slot >= CellCount) return;
	auto* s = cellSprites[slot];
	auto* l = cellLabels[slot];
	if(s == nullptr || l == nullptr) return;

	const uint16_t value = board[slot];

	// Sprite background + border tint.
	const lv_color_t bg = colorForValue(value);
	lv_obj_set_style_bg_color(s, bg, 0);
	lv_obj_set_style_bg_opa(s, value == 0 ? LV_OPA_50 : LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(s, value == 0 ? MP_LABEL_DIM
	                                              : MP_HIGHLIGHT, 0);

	if(value == 0) {
		lv_label_set_text(l, "");
		return;
	}

	// Pick the largest font that will fit. pixelbasic16 is the chunky
	// "feature font" used for tile numbers in PhoneSlidingPuzzle; we
	// keep it for 1-2 digit tiles. For 3+ digit values we fall back to
	// pixelbasic7 so the digits do not overflow the 24 px cell.
	const bool tightFit = (value >= 100);
	lv_obj_set_style_text_font(l, tightFit ? &pixelbasic7 : &pixelbasic16, 0);
	lv_obj_set_style_text_color(l, textColorForValue(value), 0);

	char buf[8];
	snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(value));
	lv_label_set_text(l, buf);
}

lv_color_t Phone2048::colorForValue(uint16_t value) {
	switch(value) {
		case 0:    return MP_DIM;                              // empty well
		case 2:    return lv_color_make( 90,  72, 132);        // light purple
		case 4:    return lv_color_make(122, 100, 168);        // brighter purple
		case 8:    return lv_color_make( 80, 160, 200);        // teal
		case 16:   return lv_color_make(122, 232, 255);        // cyan (highlight)
		case 32:   return lv_color_make( 90, 180, 130);        // sea green
		case 64:   return lv_color_make(140, 220, 100);        // bright green
		case 128:  return lv_color_make(255, 220,  60);        // soft yellow
		case 256:  return lv_color_make(255, 175,  60);        // amber
		case 512:  return lv_color_make(255, 140,  30);        // sunset orange
		case 1024: return lv_color_make(240,  90,  90);        // tomato red
		case 2048: return lv_color_make(255, 215,  80);        // gold (the prize)
		case 4096: return lv_color_make(220, 130, 240);        // magenta
		default:
			// Anything past 4096 is bonus territory - keep it readable
			// with a soft pink so the player can tell at a glance that
			// they're well into endgame.
			return lv_color_make(240, 160, 220);
	}
}

lv_color_t Phone2048::textColorForValue(uint16_t value) {
	switch(value) {
		case 2:
		case 4:
			return MP_TEXT;        // warm cream on dim purple
		case 8:
		case 16:
		case 32:
		case 64:
			return MP_BG_DARK;     // deep purple on bright tile
		case 128:
		case 256:
		case 512:
			return MP_BG_DARK;     // deep purple on warm tile
		case 1024:
			return MP_TEXT;        // warm cream on red
		case 2048:
		case 4096:
		default:
			return MP_BG_DARK;     // deep purple on gold/pink
	}
}

void Phone2048::refreshHud() {
	if(hudScoreLabel != nullptr) {
		char buf[16];
		const unsigned long s = score > 99999UL ? 99999UL : score;
		snprintf(buf, sizeof(buf), "SCORE %lu", s);
		lv_label_set_text(hudScoreLabel, buf);
	}
	if(hudBestLabel != nullptr) {
		char buf[16];
		const unsigned long b = bestScore > 99999UL ? 99999UL : bestScore;
		snprintf(buf, sizeof(buf), "BEST %lu", b);
		lv_label_set_text(hudBestLabel, buf);
	}
}

void Phone2048::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	switch(state) {
		case GameState::Playing:
			softKeys->setLeft("SLIDE");
			softKeys->setRight("BACK");
			break;
		case GameState::Won:
			softKeys->setLeft("CONT");
			softKeys->setRight("BACK");
			break;
		case GameState::GameOver:
			softKeys->setLeft("AGAIN");
			softKeys->setRight("BACK");
			break;
	}
}

void Phone2048::refreshOverlay() {
	if(overlayLabel == nullptr) return;
	switch(state) {
		case GameState::Playing:
			lv_obj_add_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
		case GameState::Won: {
			char buf[64];
			snprintf(buf, sizeof(buf),
			         "2048!\nSCORE %lu\nA TO CONTINUE",
			         static_cast<unsigned long>(score));
			lv_label_set_text(overlayLabel, buf);
			lv_obj_set_style_text_color(overlayLabel, MP_HIGHLIGHT, 0);
			lv_obj_set_style_border_color(overlayLabel, MP_HIGHLIGHT, 0);
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			lv_obj_move_foreground(overlayLabel);
			break;
		}
		case GameState::GameOver: {
			char buf[64];
			snprintf(buf, sizeof(buf),
			         "GAME OVER\nSCORE %lu\nBEST %lu\nA TO RESTART",
			         static_cast<unsigned long>(score),
			         static_cast<unsigned long>(bestScore));
			lv_label_set_text(overlayLabel, buf);
			lv_obj_set_style_text_color(overlayLabel, MP_ACCENT, 0);
			lv_obj_set_style_border_color(overlayLabel, MP_ACCENT, 0);
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			lv_obj_move_foreground(overlayLabel);
			break;
		}
	}
}

// ===========================================================================
// input
// ===========================================================================

void Phone2048::buttonPressed(uint i) {
	// BACK always pops out, regardless of state.
	if(i == BTN_BACK) {
		if(softKeys) softKeys->flashRight();
		pop();
		return;
	}

	// Cheap restart shortcut at any time.
	if(i == BTN_R) {
		newGame();
		return;
	}

	switch(state) {
		case GameState::Playing: {
			if(i == BTN_LEFT || i == BTN_4) {
				if(softKeys) softKeys->flashLeft();
				slide(-1, 0);
				return;
			}
			if(i == BTN_RIGHT || i == BTN_6) {
				if(softKeys) softKeys->flashLeft();
				slide(1, 0);
				return;
			}
			if(i == BTN_2) {
				if(softKeys) softKeys->flashLeft();
				slide(0, -1);
				return;
			}
			if(i == BTN_8) {
				if(softKeys) softKeys->flashLeft();
				slide(0, 1);
				return;
			}
			return;
		}

		case GameState::Won: {
			// CONT (left soft-key) or A dismisses the win overlay and
			// drops the player back into Playing - they keep the same
			// board and chase 4096 / 8192. The merge that triggered the
			// overlay is already on the board.
			if(i == BTN_ENTER || i == BTN_5) {
				if(softKeys) softKeys->flashLeft();
				state = GameState::Playing;
				refreshSoftKeys();
				refreshOverlay();
				return;
			}
			return;
		}

		case GameState::GameOver: {
			if(i == BTN_ENTER || i == BTN_5) {
				if(softKeys) softKeys->flashLeft();
				newGame();
				return;
			}
			return;
		}
	}
}
