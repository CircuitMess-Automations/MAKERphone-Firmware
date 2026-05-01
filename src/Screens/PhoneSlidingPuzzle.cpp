#include "PhoneSlidingPuzzle.h"

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
// PhoneBubbleSmile (S77/78), PhoneMinesweeper (S79) without a visual seam.
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)  // dim purple captions

// ===========================================================================
// ctor / dtor
// ===========================================================================

PhoneSlidingPuzzle::PhoneSlidingPuzzle()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr) {

	for(uint8_t i = 0; i < TileCount; ++i) {
		tileSprites[i] = nullptr;
		tileLabels[i]  = nullptr;
		board[i]       = 0;
	}

	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildHud();
	buildTiles();
	buildOverlay();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("SLIDE");
	softKeys->setRight("BACK");

	newGame();
}

PhoneSlidingPuzzle::~PhoneSlidingPuzzle() {
	stopTickTimer();
	// All children parented to obj; LVScreen frees them.
}

void PhoneSlidingPuzzle::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneSlidingPuzzle::onStop() {
	Input::getInstance()->removeListener(this);
	stopTickTimer();
}

// ===========================================================================
// build helpers
// ===========================================================================

void PhoneSlidingPuzzle::buildHud() {
	// Moves counter -- left edge of the HUD strip.
	hudMovesLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudMovesLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudMovesLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(hudMovesLabel, "MOVES 000");
	lv_obj_set_pos(hudMovesLabel, 4, HudY + 2);

	// Best-record badge -- centre.
	hudBestLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudBestLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudBestLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(hudBestLabel, "BEST ---");
	lv_obj_set_align(hudBestLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hudBestLabel, HudY + 2);

	// Timer -- right edge.
	hudTimerLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudTimerLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudTimerLabel, MP_ACCENT, 0);
	lv_label_set_text(hudTimerLabel, "00:00");
	lv_obj_set_pos(hudTimerLabel, 130, HudY + 2);
}

void PhoneSlidingPuzzle::buildOverlay() {
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

void PhoneSlidingPuzzle::buildTiles() {
	// One tile sprite + one number label per board slot. Position is
	// based on slot, not on what tile lives there -- when tiles slide,
	// we rewrite the labels rather than animating sprite positions
	// (cheaper to render at 24 px granularity, fits the rest of the
	// Phone* games' aesthetic of step-discrete moves).
	for(uint8_t row = 0; row < BoardSize; ++row) {
		for(uint8_t col = 0; col < BoardSize; ++col) {
			const uint8_t slot = indexOf(col, row);

			auto* s = lv_obj_create(obj);
			lv_obj_remove_style_all(s);
			lv_obj_set_size(s, TilePx - 1, TilePx - 1);   // 1 px gap
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
			tileSprites[slot] = s;

			auto* lbl = lv_label_create(s);
			lv_obj_set_style_text_font(lbl, &pixelbasic16, 0);
			lv_obj_set_style_text_color(lbl, MP_TEXT, 0);
			lv_obj_set_style_pad_all(lbl, 0, 0);
			lv_label_set_text(lbl, "");
			lv_obj_set_align(lbl, LV_ALIGN_CENTER);
			tileLabels[slot] = lbl;
		}
	}
}

// ===========================================================================
// state transitions
// ===========================================================================

void PhoneSlidingPuzzle::resetSolved() {
	// Solved state: tiles 1..15 in row-major order, blank at the
	// bottom-right.
	for(uint8_t i = 0; i < TileCount - 1; ++i) {
		board[i] = static_cast<uint8_t>(i + 1);
	}
	board[TileCount - 1] = 0;
	blankSlot = TileCount - 1;
}

void PhoneSlidingPuzzle::scramble() {
	// Start from the solved state and apply kScrambleSteps random legal
	// slides. This guarantees the resulting board is solvable. We avoid
	// "undo" moves (sliding the tile we just slid back into the blank)
	// so the scramble actually shuffles instead of bouncing back and
	// forth between two configurations.
	resetSolved();

	uint8_t lastBlank = 0xFF;
	for(uint16_t step = 0; step < kScrambleSteps; ++step) {
		const uint8_t bc = colOf(blankSlot);
		const uint8_t br = rowOf(blankSlot);

		// Build the list of slots adjacent to the blank, excluding the
		// "previous blank" so we do not undo the last slide.
		uint8_t candidates[4];
		uint8_t n = 0;
		const int8_t deltas[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
		for(uint8_t k = 0; k < 4; ++k) {
			const int8_t cc = static_cast<int8_t>(bc) + deltas[k][0];
			const int8_t cr = static_cast<int8_t>(br) + deltas[k][1];
			if(cc < 0 || cc >= static_cast<int8_t>(BoardSize)) continue;
			if(cr < 0 || cr >= static_cast<int8_t>(BoardSize)) continue;
			const uint8_t cand = indexOf(static_cast<uint8_t>(cc),
			                             static_cast<uint8_t>(cr));
			if(cand == lastBlank) continue;
			candidates[n++] = cand;
		}
		if(n == 0) {
			// Cornered against the previous blank with no other options;
			// allow the undo this step.
			for(uint8_t k = 0; k < 4; ++k) {
				const int8_t cc = static_cast<int8_t>(bc) + deltas[k][0];
				const int8_t cr = static_cast<int8_t>(br) + deltas[k][1];
				if(cc < 0 || cc >= static_cast<int8_t>(BoardSize)) continue;
				if(cr < 0 || cr >= static_cast<int8_t>(BoardSize)) continue;
				candidates[n++] = indexOf(static_cast<uint8_t>(cc),
				                          static_cast<uint8_t>(cr));
			}
		}
		const uint8_t pick = candidates[rand() % n];

		// Slide: swap board[blank] and board[pick].
		board[blankSlot] = board[pick];
		board[pick]      = 0;
		lastBlank        = blankSlot;
		blankSlot        = pick;
	}
}

void PhoneSlidingPuzzle::newGame() {
	scramble();

	state = GameState::Playing;
	moves = 0;
	startMillis = millis();
	finishMillis = 0;

	// Park the cursor on a tile adjacent to the blank so the first
	// move is a meaningful one button-press away. Falling back to the
	// top-left corner if for some reason the blank sits there.
	cursorCol = 0;
	cursorRow = 0;
	const uint8_t bc = colOf(blankSlot);
	const uint8_t br = rowOf(blankSlot);
	if(bc + 1 < BoardSize) { cursorCol = bc + 1; cursorRow = br; }
	else if(bc > 0)         { cursorCol = bc - 1; cursorRow = br; }
	else if(br + 1 < BoardSize) { cursorCol = bc; cursorRow = br + 1; }
	else if(br > 0)         { cursorCol = bc; cursorRow = br - 1; }

	startTickTimer();
	renderAllTiles();
	renderCursor();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneSlidingPuzzle::slideAt(uint8_t col, uint8_t row) {
	if(state != GameState::Playing) return;

	const uint8_t slot = indexOf(col, row);
	if(slot == blankSlot) return;          // cannot slide the blank
	if(!isAdjacent(slot, blankSlot)) return; // not a legal neighbour

	// Swap the picked tile into the blank's slot.
	board[blankSlot] = board[slot];
	board[slot]      = 0;
	const uint8_t oldBlank = blankSlot;
	blankSlot = slot;

	++moves;

	// Move the cursor with the tile so the next slide is also one
	// button away. This matches the "framing" UX described in the .h.
	cursorCol = colOf(oldBlank);
	cursorRow = rowOf(oldBlank);

	renderTile(oldBlank);
	renderTile(slot);
	renderCursor();
	refreshHud();

	if(isSolved()) {
		winMatch();
	}
}

bool PhoneSlidingPuzzle::isSolved() const {
	for(uint8_t i = 0; i < TileCount - 1; ++i) {
		if(board[i] != static_cast<uint8_t>(i + 1)) return false;
	}
	return board[TileCount - 1] == 0;
}

void PhoneSlidingPuzzle::winMatch() {
	state = GameState::Won;
	stopTickTimer();
	finishMillis = millis();

	// Update the in-memory best-moves record.
	if(bestMoves == 0 || moves < bestMoves) {
		bestMoves = moves;
	}

	renderAllTiles();
	renderCursor();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
}

bool PhoneSlidingPuzzle::isAdjacent(uint8_t a, uint8_t b) const {
	const int ac = colOf(a);
	const int ar = rowOf(a);
	const int bc = colOf(b);
	const int br = rowOf(b);
	const int dc = ac - bc;
	const int dr = ar - br;
	const int adc = dc < 0 ? -dc : dc;
	const int adr = dr < 0 ? -dr : dr;
	return (adc + adr) == 1;
}

// ===========================================================================
// rendering
// ===========================================================================

void PhoneSlidingPuzzle::renderAllTiles() {
	for(uint8_t i = 0; i < TileCount; ++i) {
		renderTile(i);
	}
}

void PhoneSlidingPuzzle::renderTile(uint8_t slot) {
	if(slot >= TileCount) return;
	auto* s = tileSprites[slot];
	auto* l = tileLabels[slot];
	if(s == nullptr || l == nullptr) return;

	const uint8_t value = board[slot];
	if(value == 0) {
		// Blank slot -- hide the sprite so we just see the wallpaper.
		lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
		return;
	}

	// Visible numbered tile.
	lv_obj_clear_flag(s, LV_OBJ_FLAG_HIDDEN);

	// Tint cue: tiles already in their solved slot pop in cyan, others
	// stay neutral cream. Reads as "you've parked these correctly" at a
	// glance without an explicit "completed" widget.
	const bool inPlace = (value == static_cast<uint8_t>(slot + 1));
	lv_obj_set_style_bg_color(s, inPlace ? MP_BG_DARK : MP_DIM, 0);
	lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(s, inPlace ? MP_HIGHLIGHT : MP_LABEL_DIM, 0);
	lv_obj_set_style_text_color(l, inPlace ? MP_HIGHLIGHT : MP_TEXT, 0);

	char buf[4];
	snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(value));
	lv_label_set_text(l, buf);
}

void PhoneSlidingPuzzle::renderCursor() {
	// Re-render every tile to clear stale highlights, then paint the
	// cursor border on top. Cheap enough at 16 tiles to skip a
	// "previous cursor" cache.
	for(uint8_t i = 0; i < TileCount; ++i) {
		if(i == indexOf(cursorCol, cursorRow)) continue;
		renderTile(i);
	}
	const uint8_t slot = indexOf(cursorCol, cursorRow);
	auto* s = tileSprites[slot];
	if(s == nullptr) return;

	if(board[slot] == 0) {
		// Cursor parked on the blank -- show a hint border on the
		// (hidden) sprite so we still have a visible cursor. Easiest
		// way: un-hide the sprite, give it a transparent fill, and
		// keep the accent border. We always recover the proper hidden
		// state on the next renderTile() pass.
		lv_obj_clear_flag(s, LV_OBJ_FLAG_HIDDEN);
		lv_obj_set_style_bg_opa(s, LV_OPA_TRANSP, 0);
		lv_obj_set_style_border_color(s, MP_ACCENT, 0);
		lv_label_set_text(tileLabels[slot], "");
	} else {
		lv_obj_set_style_border_color(s, MP_ACCENT, 0);
	}
	lv_obj_move_foreground(s);
}

void PhoneSlidingPuzzle::refreshHud() {
	if(hudMovesLabel != nullptr) {
		char buf[16];
		const unsigned m = moves > 999 ? 999 : moves;
		snprintf(buf, sizeof(buf), "MOVES %03u", m);
		lv_label_set_text(hudMovesLabel, buf);
	}
	if(hudBestLabel != nullptr) {
		char buf[16];
		if(bestMoves == 0) {
			snprintf(buf, sizeof(buf), "BEST ---");
		} else {
			const unsigned b = bestMoves > 999 ? 999 : bestMoves;
			snprintf(buf, sizeof(buf), "BEST %03u", b);
		}
		lv_label_set_text(hudBestLabel, buf);
	}
	if(hudTimerLabel != nullptr) {
		uint32_t elapsedMs = 0;
		if(state == GameState::Playing && startMillis > 0) {
			elapsedMs = millis() - startMillis;
		} else if(state == GameState::Won
		          && startMillis > 0 && finishMillis >= startMillis) {
			elapsedMs = finishMillis - startMillis;
		}
		uint32_t total_s = elapsedMs / 1000;
		if(total_s > 99 * 60 + 59) total_s = 99 * 60 + 59;  // cap at 99:59
		const uint32_t mm = total_s / 60;
		const uint32_t ss = total_s % 60;
		char buf[8];
		snprintf(buf, sizeof(buf), "%02lu:%02lu",
		         static_cast<unsigned long>(mm),
		         static_cast<unsigned long>(ss));
		lv_label_set_text(hudTimerLabel, buf);
	}
}

void PhoneSlidingPuzzle::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	switch(state) {
		case GameState::Playing:
			softKeys->setLeft("SLIDE");
			softKeys->setRight("BACK");
			break;
		case GameState::Won:
			softKeys->setLeft("AGAIN");
			softKeys->setRight("BACK");
			break;
	}
}

void PhoneSlidingPuzzle::refreshOverlay() {
	if(overlayLabel == nullptr) return;
	switch(state) {
		case GameState::Playing:
			lv_obj_add_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
		case GameState::Won: {
			uint32_t total_s = 0;
			if(finishMillis >= startMillis) {
				total_s = (finishMillis - startMillis) / 1000;
			}
			if(total_s > 99 * 60 + 59) total_s = 99 * 60 + 59;
			const uint32_t mm = total_s / 60;
			const uint32_t ss = total_s % 60;
			char buf[48];
			snprintf(buf, sizeof(buf),
			         "SOLVED!\n%u MOVES  %02lu:%02lu\nA TO RESHUFFLE",
			         static_cast<unsigned>(moves),
			         static_cast<unsigned long>(mm),
			         static_cast<unsigned long>(ss));
			lv_label_set_text(overlayLabel, buf);
			lv_obj_set_style_text_color(overlayLabel, MP_HIGHLIGHT, 0);
			lv_obj_set_style_border_color(overlayLabel, MP_HIGHLIGHT, 0);
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			lv_obj_move_foreground(overlayLabel);
			break;
		}
	}
}

// ===========================================================================
// timer
// ===========================================================================

void PhoneSlidingPuzzle::startTickTimer() {
	if(tickTimer != nullptr) return;
	// 250 ms cadence so the visible mm:ss transition feels snappy
	// without spamming LVGL with redraws every frame.
	tickTimer = lv_timer_create(&PhoneSlidingPuzzle::onTickStatic, 250, this);
}

void PhoneSlidingPuzzle::stopTickTimer() {
	if(tickTimer != nullptr) {
		lv_timer_del(tickTimer);
		tickTimer = nullptr;
	}
}

void PhoneSlidingPuzzle::onTickStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneSlidingPuzzle*>(timer->user_data);
	if(self == nullptr) return;
	if(self->state != GameState::Playing) return;
	self->refreshHud();
}

// ===========================================================================
// input
// ===========================================================================

void PhoneSlidingPuzzle::buttonPressed(uint i) {
	// BACK always pops out, regardless of state.
	if(i == BTN_BACK) {
		if(softKeys) softKeys->flashRight();
		pop();
		return;
	}

	// Reshuffle works in either state -- the player can opt out of an
	// unwinnable-feeling board at any time.
	if(i == BTN_R) {
		newGame();
		return;
	}

	switch(state) {
		case GameState::Playing: {
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

			// Slide (numpad 5 + ENTER).
			if(i == BTN_5 || i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				slideAt(cursorCol, cursorRow);
				return;
			}
			return;
		}

		case GameState::Won: {
			if(i == BTN_ENTER || i == BTN_5) {
				if(softKeys) softKeys->flashLeft();
				newGame();
				return;
			}
			return;
		}
	}
}
