#include "PhoneBubbleSmile.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - identical to every other Phone* widget so the
// arcade screen slots in beside its predecessors (PhoneTetris, PhoneBounce,
// PhoneBrickBreaker, PhoneBantumi). Inlined per the established pattern.
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)  // dim purple captions

namespace {

// Compact pad / bubble proportions. The cell itself is 18x12; the visible
// "bubble" is inset 2 px on each side so adjacent bubbles read as separate
// disks rather than merging into a colour stripe.
constexpr lv_coord_t BubbleInsetX = 2;
constexpr lv_coord_t BubbleInsetY = 1;
constexpr lv_coord_t BubbleW      = PhoneBubbleSmile::CellW - 2 * BubbleInsetX; // 14
constexpr lv_coord_t BubbleH      = PhoneBubbleSmile::CellH - 2 * BubbleInsetY; // 10
constexpr lv_coord_t HighlightW   = 3;
constexpr lv_coord_t HighlightH   = 2;

} // namespace

// ===========================================================================
// ctor / dtor
// ===========================================================================

PhoneBubbleSmile::PhoneBubbleSmile()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr) {

	// Defensive zero-init.
	for(uint8_t i = 0; i < CellCount; ++i) {
		cellPads[i]      = nullptr;
		bubbleSprites[i] = nullptr;
		highlightDots[i] = nullptr;
		grid[i]          = Bubble::Empty;
		matched[i]       = false;
	}

	// Full-screen container, no scrollbars, no padding.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper at the bottom of the z-order.
	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildHud();
	buildPlayfield();
	buildCells();
	buildOverlay();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("START");
	softKeys->setRight("BACK");

	enterIdle();
}

PhoneBubbleSmile::~PhoneBubbleSmile() {
	stopResolveTimer();
	// Children are parented to obj; LVGL frees them recursively when the
	// LVScreen base destructor disposes obj.
}

void PhoneBubbleSmile::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneBubbleSmile::onStop() {
	Input::getInstance()->removeListener(this);
	stopResolveTimer();
}

// ===========================================================================
// build helpers
// ===========================================================================

void PhoneBubbleSmile::buildHud() {
	hudLeftLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudLeftLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudLeftLabel, MP_ACCENT, 0);
	lv_label_set_text(hudLeftLabel, "BUBBLES");
	lv_obj_set_pos(hudLeftLabel, 4, 12);

	// Right-anchored "score / target" pair. We keep the target dim and
	// the score in cream so the two read as label + value.
	scoreLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(scoreLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(scoreLabel, MP_TEXT, 0);
	lv_label_set_text(scoreLabel, "0");
	lv_obj_set_align(scoreLabel, LV_ALIGN_TOP_RIGHT);
	lv_obj_set_pos(scoreLabel, -34, 12);

	targetLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(targetLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(targetLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(targetLabel, "/1000");
	lv_obj_set_align(targetLabel, LV_ALIGN_TOP_RIGHT);
	lv_obj_set_pos(targetLabel, -3, 12);
}

void PhoneBubbleSmile::buildPlayfield() {
	playfield = lv_obj_create(obj);
	lv_obj_remove_style_all(playfield);
	lv_obj_set_size(playfield, 160, 98);
	lv_obj_set_pos(playfield, 0, 20);
	lv_obj_set_style_bg_color(playfield, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(playfield, LV_OPA_50, 0);
	lv_obj_set_style_border_color(playfield, MP_DIM, 0);
	lv_obj_set_style_border_width(playfield, 1, 0);
	lv_obj_set_style_pad_all(playfield, 0, 0);
	lv_obj_clear_flag(playfield, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(playfield, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(playfield, LV_OBJ_FLAG_IGNORE_LAYOUT);
}

void PhoneBubbleSmile::buildCells() {
	for(uint8_t i = 0; i < CellCount; ++i) {
		const uint8_t  col = cellCol(i);
		const uint8_t  row = cellRow(i);
		const lv_coord_t x = GridX + col * (CellW + CellGapX);
		const lv_coord_t y = GridY + row * (CellH + CellGapY);

		// Cell pad - hosts the cursor / selection border. Transparent
		// background by default so the synthwave wallpaper bleeds through
		// the gaps between bubbles.
		auto* pad = lv_obj_create(obj);
		lv_obj_remove_style_all(pad);
		lv_obj_set_size(pad, CellW, CellH);
		lv_obj_set_pos(pad, x, y);
		lv_obj_set_style_bg_opa(pad, LV_OPA_TRANSP, 0);
		lv_obj_set_style_border_color(pad, MP_DIM, 0);
		lv_obj_set_style_border_width(pad, 0, 0);
		lv_obj_set_style_radius(pad, 1, 0);
		lv_obj_set_style_pad_all(pad, 0, 0);
		lv_obj_clear_flag(pad, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(pad, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(pad, LV_OBJ_FLAG_IGNORE_LAYOUT);
		cellPads[i] = pad;

		// Coloured bubble inner.
		auto* bub = lv_obj_create(pad);
		lv_obj_remove_style_all(bub);
		lv_obj_set_size(bub, BubbleW, BubbleH);
		lv_obj_set_pos(bub, BubbleInsetX, BubbleInsetY);
		lv_obj_set_style_bg_color(bub, MP_DIM, 0);
		lv_obj_set_style_bg_opa(bub, LV_OPA_COVER, 0);
		lv_obj_set_style_border_width(bub, 0, 0);
		lv_obj_set_style_radius(bub, 4, 0);
		lv_obj_clear_flag(bub, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(bub, LV_OBJ_FLAG_CLICKABLE);
		bubbleSprites[i] = bub;

		// Tiny highlight pixel - 3x2 cream block, top-left of bubble.
		auto* hi = lv_obj_create(bub);
		lv_obj_remove_style_all(hi);
		lv_obj_set_size(hi, HighlightW, HighlightH);
		lv_obj_set_pos(hi, 2, 2);
		lv_obj_set_style_bg_color(hi, MP_TEXT, 0);
		lv_obj_set_style_bg_opa(hi, LV_OPA_COVER, 0);
		lv_obj_set_style_border_width(hi, 0, 0);
		lv_obj_set_style_radius(hi, 1, 0);
		lv_obj_clear_flag(hi, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(hi, LV_OBJ_FLAG_CLICKABLE);
		highlightDots[i] = hi;
	}
}

void PhoneBubbleSmile::buildOverlay() {
	overlayLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(overlayLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(overlayLabel, MP_TEXT, 0);
	lv_obj_set_style_text_align(overlayLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(overlayLabel, "");
	lv_obj_set_align(overlayLabel, LV_ALIGN_CENTER);
	lv_obj_set_y(overlayLabel, 4);
}

// ===========================================================================
// state transitions
// ===========================================================================

void PhoneBubbleSmile::enterIdle() {
	state = GameState::Idle;
	stopResolveTimer();
	score        = 0;
	cascadeLevel = 0;
	cursor       = cellIndex(GridCols / 2, GridRows / 2);
	selected     = -1;
	revertPending= false;
	rngState     = static_cast<uint16_t>(millis() & 0xFFFFu);
	if(rngState == 0) rngState = 0xACE1u;
	seedBoard();
	render();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneBubbleSmile::startGame() {
	state = GameState::Playing;
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneBubbleSmile::winGame() {
	state = GameState::GameOver;
	stopResolveTimer();
	selected = -1;
	refreshSoftKeys();
	refreshOverlay();
	render();
}

// ===========================================================================
// PRNG
// ===========================================================================

uint16_t PhoneBubbleSmile::xorshift16() {
	uint16_t x = rngState ? rngState : 0xACE1u;
	x ^= x << 7;
	x ^= x >> 9;
	x ^= x << 8;
	rngState = x;
	return x;
}

PhoneBubbleSmile::Bubble PhoneBubbleSmile::randomColor() {
	const uint16_t r = xorshift16();
	const uint8_t  i = static_cast<uint8_t>(r % ColorCount);
	// Bubble enum: 0 = Empty, 1..5 = colours.
	return static_cast<Bubble>(i + 1);
}

// ===========================================================================
// game ops
// ===========================================================================

void PhoneBubbleSmile::seedBoard() {
	// Fill the grid avoiding any immediate runs of three. We just keep
	// rolling for any cell whose chosen colour matches both predecessors
	// in either direction. With 5 colours and a 6x7 grid this terminates
	// in ~one extra roll per cell on average.
	for(uint8_t i = 0; i < CellCount; ++i) {
		const uint8_t col = cellCol(i);
		const uint8_t row = cellRow(i);

		Bubble c = Bubble::Empty;
		uint8_t attempts = 0;
		do {
			c = randomColor();
			++attempts;
			if(attempts > 12) break;   // bail-out safety; uniqueness will
			                           // be repaired by post-seed scan.
			bool conflictRow = false;
			if(col >= 2) {
				const Bubble a = grid[cellIndex(col - 1, row)];
				const Bubble b = grid[cellIndex(col - 2, row)];
				if(a == c && b == c) conflictRow = true;
			}
			bool conflictCol = false;
			if(row >= 2) {
				const Bubble a = grid[cellIndex(col, row - 1)];
				const Bubble b = grid[cellIndex(col, row - 2)];
				if(a == c && b == c) conflictCol = true;
			}
			if(!conflictRow && !conflictCol) break;
		} while(true);
		grid[i] = c;
	}

	// Belt-and-braces: in case the bail-out above accepted a collision,
	// run one detect+clear+drop+fill sweep silently so the board the
	// player sees is guaranteed match-free without crediting them score.
	uint16_t cleared = 0;
	uint8_t  guard = 0;
	while(detectMatches(cleared) && guard < 8) {
		clearMatches();
		dropBubbles();
		fillTop();
		++guard;
	}
	// Reset the per-cell match flags so render() doesn't draw stale halos.
	for(uint8_t i = 0; i < CellCount; ++i) matched[i] = false;
}

bool PhoneBubbleSmile::detectMatches(uint16_t& cleared) {
	cleared = 0;
	for(uint8_t i = 0; i < CellCount; ++i) matched[i] = false;

	// Row scan.
	for(uint8_t row = 0; row < GridRows; ++row) {
		uint8_t runStart = 0;
		uint8_t runLen   = 1;
		Bubble  runColor = grid[cellIndex(0, row)];
		for(uint8_t col = 1; col <= GridCols; ++col) {
			Bubble c = (col < GridCols) ? grid[cellIndex(col, row)] : Bubble::Empty;
			if(col < GridCols && c == runColor && c != Bubble::Empty) {
				++runLen;
			} else {
				if(runLen >= 3 && runColor != Bubble::Empty) {
					for(uint8_t k = 0; k < runLen; ++k) {
						matched[cellIndex(runStart + k, row)] = true;
					}
				}
				runStart = col;
				runLen   = 1;
				runColor = c;
			}
		}
	}

	// Column scan.
	for(uint8_t col = 0; col < GridCols; ++col) {
		uint8_t runStart = 0;
		uint8_t runLen   = 1;
		Bubble  runColor = grid[cellIndex(col, 0)];
		for(uint8_t row = 1; row <= GridRows; ++row) {
			Bubble c = (row < GridRows) ? grid[cellIndex(col, row)] : Bubble::Empty;
			if(row < GridRows && c == runColor && c != Bubble::Empty) {
				++runLen;
			} else {
				if(runLen >= 3 && runColor != Bubble::Empty) {
					for(uint8_t k = 0; k < runLen; ++k) {
						matched[cellIndex(col, runStart + k)] = true;
					}
				}
				runStart = row;
				runLen   = 1;
				runColor = c;
			}
		}
	}

	for(uint8_t i = 0; i < CellCount; ++i) {
		if(matched[i]) ++cleared;
	}
	return cleared > 0;
}

void PhoneBubbleSmile::clearMatches() {
	for(uint8_t i = 0; i < CellCount; ++i) {
		if(matched[i]) {
			grid[i]    = Bubble::Empty;
			matched[i] = false;
		}
	}
}

void PhoneBubbleSmile::dropBubbles() {
	// Column-major gravity: for each column, walk from the bottom up.
	// For each empty cell, find the nearest non-empty cell above it
	// and pull it down.
	for(uint8_t col = 0; col < GridCols; ++col) {
		int8_t writeRow = static_cast<int8_t>(GridRows) - 1;
		for(int8_t readRow = static_cast<int8_t>(GridRows) - 1; readRow >= 0; --readRow) {
			const Bubble c = grid[cellIndex(col, static_cast<uint8_t>(readRow))];
			if(c != Bubble::Empty) {
				if(writeRow != readRow) {
					grid[cellIndex(col, static_cast<uint8_t>(writeRow))] = c;
					grid[cellIndex(col, static_cast<uint8_t>(readRow))]  = Bubble::Empty;
				}
				--writeRow;
			}
		}
	}
}

void PhoneBubbleSmile::fillTop() {
	for(uint8_t i = 0; i < CellCount; ++i) {
		if(grid[i] == Bubble::Empty) {
			grid[i] = randomColor();
		}
	}
}

bool PhoneBubbleSmile::isAdjacent(uint8_t a, uint8_t b) {
	if(a == b) return false;
	const int dCol = static_cast<int>(cellCol(a)) - static_cast<int>(cellCol(b));
	const int dRow = static_cast<int>(cellRow(a)) - static_cast<int>(cellRow(b));
	return (dCol == 0 && (dRow == 1 || dRow == -1)) ||
	       (dRow == 0 && (dCol == 1 || dCol == -1));
}

bool PhoneBubbleSmile::wouldMatch(uint8_t a, uint8_t b) const {
	// Simulate the swap; check for any 3-in-a-row centred on either cell.
	auto colorAt = [&](uint8_t idx) -> Bubble {
		if(idx == a) return grid[b];
		if(idx == b) return grid[a];
		return grid[idx];
	};
	auto check3 = [&](uint8_t cellIdx) -> bool {
		const uint8_t col = cellCol(cellIdx);
		const uint8_t row = cellRow(cellIdx);
		const Bubble c = colorAt(cellIdx);
		if(c == Bubble::Empty) return false;
		// Horizontal run length passing through (col,row).
		uint8_t hLen = 1;
		for(int8_t cx = static_cast<int8_t>(col) - 1; cx >= 0; --cx) {
			if(colorAt(cellIndex(static_cast<uint8_t>(cx), row)) == c) ++hLen; else break;
		}
		for(int8_t cx = static_cast<int8_t>(col) + 1; cx < static_cast<int8_t>(GridCols); ++cx) {
			if(colorAt(cellIndex(static_cast<uint8_t>(cx), row)) == c) ++hLen; else break;
		}
		if(hLen >= 3) return true;
		// Vertical run length.
		uint8_t vLen = 1;
		for(int8_t ry = static_cast<int8_t>(row) - 1; ry >= 0; --ry) {
			if(colorAt(cellIndex(col, static_cast<uint8_t>(ry))) == c) ++vLen; else break;
		}
		for(int8_t ry = static_cast<int8_t>(row) + 1; ry < static_cast<int8_t>(GridRows); ++ry) {
			if(colorAt(cellIndex(col, static_cast<uint8_t>(ry))) == c) ++vLen; else break;
		}
		return vLen >= 3;
	};
	return check3(a) || check3(b);
}

bool PhoneBubbleSmile::hasAnyValidMove() const {
	for(uint8_t i = 0; i < CellCount; ++i) {
		// Right neighbour.
		if(cellCol(i) + 1 < GridCols) {
			if(wouldMatch(i, static_cast<uint8_t>(i + 1))) return true;
		}
		// Below neighbour.
		if(cellRow(i) + 1 < GridRows) {
			if(wouldMatch(i, static_cast<uint8_t>(i + GridCols))) return true;
		}
	}
	return false;
}

void PhoneBubbleSmile::shuffleBoard() {
	// Cheap shuffle: re-roll every cell, then sweep until no immediate
	// matches remain. If after several attempts no valid move exists we
	// still hand the board over -- the next swap may resolve it, and the
	// "no move" detection will simply fire again.
	for(uint8_t attempt = 0; attempt < 4; ++attempt) {
		for(uint8_t i = 0; i < CellCount; ++i) {
			grid[i] = randomColor();
		}
		uint16_t cleared = 0;
		uint8_t  guard = 0;
		while(detectMatches(cleared) && guard < 8) {
			clearMatches();
			dropBubbles();
			fillTop();
			++guard;
		}
		for(uint8_t i = 0; i < CellCount; ++i) matched[i] = false;
		if(hasAnyValidMove()) return;
	}
}

void PhoneBubbleSmile::doSwap(uint8_t a, uint8_t b) {
	const Bubble tmp = grid[a];
	grid[a] = grid[b];
	grid[b] = tmp;
}

void PhoneBubbleSmile::onPlayerSwap() {
	// Begin the cascade pipeline. Phase starts at Match because we want
	// to detect / score / clear before any drop.
	cascadeLevel = 1;
	phase        = ResolvePhase::Match;
	state        = GameState::Resolving;
	startResolveTimer();
	refreshSoftKeys();
}

// ===========================================================================
// rendering
// ===========================================================================

lv_color_t PhoneBubbleSmile::bubbleColor(Bubble b) const {
	switch(b) {
		case Bubble::Cyan:    return lv_color_make(122, 232, 255);
		case Bubble::Red:     return lv_color_make(240,  90,  90);
		case Bubble::Yellow:  return lv_color_make(255, 220,  60);
		case Bubble::Green:   return lv_color_make(120, 220, 110);
		case Bubble::Magenta: return lv_color_make(220, 130, 240);
		case Bubble::Empty:
		default:              return MP_DIM;
	}
}

void PhoneBubbleSmile::renderCell(uint8_t idx) {
	if(idx >= CellCount) return;
	auto* pad = cellPads[idx];
	auto* bub = bubbleSprites[idx];
	auto* hi  = highlightDots[idx];
	if(pad == nullptr || bub == nullptr || hi == nullptr) return;

	const Bubble c = grid[idx];

	if(c == Bubble::Empty) {
		lv_obj_add_flag(bub, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_clear_flag(bub, LV_OBJ_FLAG_HIDDEN);
		lv_obj_set_style_bg_color(bub, bubbleColor(c), 0);
		// Matched cells flash bright (cream) for a tick before they get
		// cleared on the next phase.
		if(matched[idx]) {
			lv_obj_set_style_bg_color(bub, MP_TEXT, 0);
		}
	}

	// Cursor + selection borders. Selection wins over cursor when both
	// would land on the same cell.
	if(static_cast<int8_t>(idx) == selected) {
		lv_obj_set_style_border_color(pad, MP_HIGHLIGHT, 0);
		lv_obj_set_style_border_width(pad, 1, 0);
	} else if(idx == cursor) {
		lv_obj_set_style_border_color(pad, MP_ACCENT, 0);
		lv_obj_set_style_border_width(pad, 1, 0);
	} else {
		lv_obj_set_style_border_width(pad, 0, 0);
	}
}

void PhoneBubbleSmile::render() {
	for(uint8_t i = 0; i < CellCount; ++i) renderCell(i);
}

void PhoneBubbleSmile::refreshHud() {
	if(scoreLabel == nullptr) return;
	char buf[12];
	snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(score));
	lv_label_set_text(scoreLabel, buf);
}

void PhoneBubbleSmile::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	switch(state) {
		case GameState::Idle:
			softKeys->setLeft("START");
			softKeys->setRight("BACK");
			break;
		case GameState::Playing:
			softKeys->setLeft(selected >= 0 ? "SWAP" : "SELECT");
			softKeys->setRight("BACK");
			break;
		case GameState::Resolving:
			softKeys->setLeft("...");
			softKeys->setRight("BACK");
			break;
		case GameState::GameOver:
			softKeys->setLeft("AGAIN");
			softKeys->setRight("BACK");
			break;
	}
}

void PhoneBubbleSmile::refreshOverlay() {
	if(overlayLabel == nullptr) return;
	switch(state) {
		case GameState::Idle:
			lv_label_set_text(overlayLabel, "PRESS START");
			lv_obj_set_style_text_color(overlayLabel, MP_TEXT, 0);
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
		case GameState::Playing:
		case GameState::Resolving:
			lv_obj_add_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
		case GameState::GameOver:
			lv_label_set_text(overlayLabel, "WELL DONE!");
			lv_obj_set_style_text_color(overlayLabel, MP_ACCENT, 0);
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
	}
}

// ===========================================================================
// timers
// ===========================================================================

void PhoneBubbleSmile::startResolveTimer() {
	if(resolveTimer != nullptr) return;
	resolveTimer = lv_timer_create(&PhoneBubbleSmile::onResolveTickStatic,
	                                ResolveStepMs, this);
}

void PhoneBubbleSmile::stopResolveTimer() {
	if(resolveTimer != nullptr) {
		lv_timer_del(resolveTimer);
		resolveTimer = nullptr;
	}
}

void PhoneBubbleSmile::onResolveTickStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneBubbleSmile*>(timer->user_data);
	if(self == nullptr) return;
	if(self->state != PhoneBubbleSmile::GameState::Resolving) return;

	// One pipeline phase per tick. Each tick re-renders so the player
	// sees the cascade flow.
	switch(self->phase) {
		case PhoneBubbleSmile::ResolvePhase::Match: {
			uint16_t cleared = 0;
			const bool any = self->detectMatches(cleared);
			if(!any) {
				// Cascade complete. Handle the "did the player swap
				// produce a match?" revert, then settle.
				if(self->revertPending) {
					// First-time revert: swap back, clear pending, and
					// keep cascading off (no score, no match was made).
					self->doSwap(self->revertA, self->revertB);
					self->revertPending = false;
					self->render();
					// No matches expected after revert -- go straight
					// back to Playing.
				}
				if(self->score >= self->TargetScore) {
					self->winGame();
					return;
				}
				if(!self->hasAnyValidMove()) {
					self->shuffleBoard();
					self->render();
				}
				self->stopResolveTimer();
				self->state = PhoneBubbleSmile::GameState::Playing;
				self->refreshSoftKeys();
				return;
			}
			// We have matches. Score them and render the flash before
			// the next tick clears them.
			self->revertPending = false;     // a real match landed
			self->score += static_cast<uint32_t>(cleared) * 10u
			               * static_cast<uint32_t>(self->cascadeLevel);
			self->refreshHud();
			self->phase = PhoneBubbleSmile::ResolvePhase::Drop;
			self->render();
			break;
		}
		case PhoneBubbleSmile::ResolvePhase::Drop: {
			self->clearMatches();
			self->dropBubbles();
			self->phase = PhoneBubbleSmile::ResolvePhase::Fill;
			self->render();
			break;
		}
		case PhoneBubbleSmile::ResolvePhase::Fill: {
			self->fillTop();
			++self->cascadeLevel;            // chain bonus for next round
			self->phase = PhoneBubbleSmile::ResolvePhase::Match;
			self->render();
			break;
		}
	}
}

// ===========================================================================
// input
// ===========================================================================

void PhoneBubbleSmile::buttonPressed(uint i) {
	switch(state) {
		case GameState::Idle:
			if(i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				startGame();
			} else if(i == BTN_BACK) {
				if(softKeys) softKeys->flashRight();
				pop();
			}
			return;

		case GameState::Resolving:
			// Only BACK is allowed during cascade; no input until settle.
			if(i == BTN_BACK) {
				if(softKeys) softKeys->flashRight();
				pop();
			}
			return;

		case GameState::GameOver:
			if(i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				enterIdle();
				startGame();
			} else if(i == BTN_BACK) {
				if(softKeys) softKeys->flashRight();
				pop();
			}
			return;

		case GameState::Playing:
			break;   // fall through to per-key handling below
	}

	// ---- Playing-state input ----------------------------------------
	const uint8_t col = cellCol(cursor);
	const uint8_t row = cellRow(cursor);

	switch(i) {
		case BTN_LEFT:
		case BTN_4:
			if(col > 0) {
				cursor = cellIndex(col - 1, row);
				render();
			}
			return;

		case BTN_RIGHT:
		case BTN_6:
			if(col + 1 < GridCols) {
				cursor = cellIndex(col + 1, row);
				render();
			}
			return;

		case BTN_2:
			if(row > 0) {
				cursor = cellIndex(col, row - 1);
				render();
			}
			return;

		case BTN_8:
			if(row + 1 < GridRows) {
				cursor = cellIndex(col, row + 1);
				render();
			}
			return;

		case BTN_ENTER: {
			if(softKeys) softKeys->flashLeft();
			if(selected < 0) {
				// First press: select the current cell.
				selected = static_cast<int8_t>(cursor);
				refreshSoftKeys();
				render();
				return;
			}
			if(static_cast<uint8_t>(selected) == cursor) {
				// Second press on same cell: deselect.
				selected = -1;
				refreshSoftKeys();
				render();
				return;
			}
			if(!isAdjacent(static_cast<uint8_t>(selected), cursor)) {
				// Not adjacent -- treat as "switch selection" so the
				// player can re-aim without an explicit deselect.
				selected = static_cast<int8_t>(cursor);
				render();
				return;
			}
			// Adjacent swap. Try the swap; if it produces no match, set
			// up a revert so the cascade pipeline puts the bubbles back
			// after the player sees the no-op feedback.
			const uint8_t a = static_cast<uint8_t>(selected);
			const uint8_t b = cursor;
			doSwap(a, b);
			// Run the live match detector on the post-swap grid. We do
			// not use wouldMatch() here because that helper simulates a
			// swap on the unswapped grid; we already committed the swap.
			uint16_t cleared = 0;
			const bool ok = detectMatches(cleared);
			// Clear the per-cell match flags so render() doesn't pre-flash
			// the matched cells before the resolve timer fires.
			for(uint8_t k = 0; k < CellCount; ++k) matched[k] = false;
			if(!ok) {
				// No match -- queue a revert and let the resolve loop
				// run a single Match-phase tick that will swap back.
				revertPending = true;
				revertA       = a;
				revertB       = b;
			}
			selected = -1;
			onPlayerSwap();
			render();
			return;
		}

		case BTN_BACK:
			if(softKeys) softKeys->flashRight();
			pop();
			return;

		default:
			return;
	}
}
