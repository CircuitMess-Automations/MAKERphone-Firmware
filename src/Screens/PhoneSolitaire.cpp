#include "PhoneSolitaire.h"

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
// PhoneSolitaire sits beside Phone2048 (S93), PhoneSlidingPuzzle (S80)
// and the rest of the Phase-N arcade without a visual seam.
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)  // dim purple captions

// "Card" colour set. Faces are warm cream, backs are a muted cyan trim
// over deep purple so they read against the synthwave wallpaper.
#define CARD_FACE      lv_color_make(245, 230, 200)
#define CARD_BACK      lv_color_make(45, 32, 80)
#define CARD_RED       lv_color_make(220, 60, 60)
#define CARD_BLACK     lv_color_make(20, 12, 36)
#define CARD_BORDER    lv_color_make(170, 140, 200)
#define CARD_HIGHLIGHT lv_color_make(255, 140, 30)   // selection accent

// Stride used between cards inside a tableau column. Variable strides
// (smaller after a face-down card, larger after a face-up one) let the
// player see the rank/suit of the cards they care about while keeping
// face-down stacks compact.
static constexpr int8_t kStrideFaceUp   = 6;
static constexpr int8_t kStrideFaceDown = 3;

// X coord helpers for the top pile row + tableau columns. Hardcoded so
// the layout does not subtly drift if LVGL ever re-flows the screen.
static constexpr lv_coord_t kStockX        = 4;
static constexpr lv_coord_t kWasteX        = 26;
static constexpr lv_coord_t kFoundationX0  = 56;
static constexpr lv_coord_t kFoundationDx  = 22;

static lv_coord_t foundationX(uint8_t i) {
	return static_cast<lv_coord_t>(kFoundationX0 + i * kFoundationDx);
}

static constexpr lv_coord_t kTableauX0 = 4;
static constexpr lv_coord_t kTableauDx = 22;

static lv_coord_t tableauX(uint8_t i) {
	return static_cast<lv_coord_t>(kTableauX0 + i * kTableauDx);
}

// ===========================================================================
// ctor / dtor
// ===========================================================================

PhoneSolitaire::PhoneSolitaire()
		: LVScreen() {

	for(uint8_t c = 0; c < TableauCount; ++c) {
		tableauSize[c] = 0;
		for(uint8_t i = 0; i < MaxColCards; ++i) {
			tableau[c][i] = Card();
		}
	}
	for(uint8_t f = 0; f < FoundationCount; ++f) {
		foundationSize[f] = 0;
		for(uint8_t i = 0; i < 13; ++i) {
			foundations[f][i] = Card();
		}
	}
	for(uint8_t i = 0; i < StockCap; ++i) {
		stock[i] = Card();
		waste[i] = Card();
	}

	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildHud();
	buildTopRow();
	buildTableau();
	buildOverlay();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("DRAW");
	softKeys->setRight("BACK");

	newGame();
}

PhoneSolitaire::~PhoneSolitaire() {
	// All children parented to obj; LVScreen frees them recursively.
}

void PhoneSolitaire::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneSolitaire::onStop() {
	Input::getInstance()->removeListener(this);
}

// ===========================================================================
// build helpers
// ===========================================================================

void PhoneSolitaire::buildHud() {
	hudMovesLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudMovesLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudMovesLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(hudMovesLabel, "MOVES 000");
	lv_obj_set_pos(hudMovesLabel, 4, HudY + 2);

	hudScoreLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudScoreLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudScoreLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(hudScoreLabel, "SCORE 0000");
	lv_obj_set_align(hudScoreLabel, LV_ALIGN_TOP_RIGHT);
	lv_obj_set_y(hudScoreLabel, HudY + 2);
	lv_obj_set_style_pad_right(hudScoreLabel, 4, 0);
}

void PhoneSolitaire::buildOverlay() {
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

void PhoneSolitaire::buildTopRow() {
	// Stock pile -- slot for one card (we render the back when it has
	// cards, the cyan "empty" placeholder when drained).
	stockSprite = lv_obj_create(obj);
	lv_obj_remove_style_all(stockSprite);
	lv_obj_set_size(stockSprite, CardW, TopPileH);
	lv_obj_set_pos(stockSprite, kStockX, TopPileY);
	lv_obj_set_style_bg_color(stockSprite, CARD_BACK, 0);
	lv_obj_set_style_bg_opa(stockSprite, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(stockSprite, CARD_BORDER, 0);
	lv_obj_set_style_border_width(stockSprite, 1, 0);
	lv_obj_set_style_radius(stockSprite, 1, 0);
	lv_obj_clear_flag(stockSprite, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(stockSprite, LV_OBJ_FLAG_CLICKABLE);

	stockLabel = lv_label_create(stockSprite);
	lv_obj_set_style_text_font(stockLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(stockLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(stockLabel, "");
	lv_obj_set_align(stockLabel, LV_ALIGN_CENTER);

	wasteSprite = lv_obj_create(obj);
	lv_obj_remove_style_all(wasteSprite);
	lv_obj_set_size(wasteSprite, CardW, TopPileH);
	lv_obj_set_pos(wasteSprite, kWasteX, TopPileY);
	lv_obj_set_style_bg_color(wasteSprite, CARD_FACE, 0);
	lv_obj_set_style_bg_opa(wasteSprite, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(wasteSprite, CARD_BORDER, 0);
	lv_obj_set_style_border_width(wasteSprite, 1, 0);
	lv_obj_set_style_radius(wasteSprite, 1, 0);
	lv_obj_clear_flag(wasteSprite, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(wasteSprite, LV_OBJ_FLAG_CLICKABLE);

	wasteLabel = lv_label_create(wasteSprite);
	lv_obj_set_style_text_font(wasteLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(wasteLabel, CARD_BLACK, 0);
	lv_label_set_text(wasteLabel, "");
	lv_obj_set_align(wasteLabel, LV_ALIGN_CENTER);

	for(uint8_t f = 0; f < FoundationCount; ++f) {
		auto* s = lv_obj_create(obj);
		lv_obj_remove_style_all(s);
		lv_obj_set_size(s, CardW, TopPileH);
		lv_obj_set_pos(s, foundationX(f), TopPileY);
		lv_obj_set_style_bg_color(s, MP_DIM, 0);
		lv_obj_set_style_bg_opa(s, LV_OPA_50, 0);
		lv_obj_set_style_border_color(s, CARD_BORDER, 0);
		lv_obj_set_style_border_width(s, 1, 0);
		lv_obj_set_style_radius(s, 1, 0);
		lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(s, LV_OBJ_FLAG_CLICKABLE);
		foundationSprite[f] = s;

		auto* lbl = lv_label_create(s);
		lv_obj_set_style_text_font(lbl, &pixelbasic7, 0);
		lv_obj_set_style_text_color(lbl, isRedSuit(f) ? CARD_RED : CARD_BLACK, 0);
		// Default empty foundation shows its target suit letter dimmed.
		lv_label_set_text(lbl, suitGlyph(f));
		lv_obj_set_align(lbl, LV_ALIGN_CENTER);
		foundationLabel[f] = lbl;
	}

	// Top-row caption strip: ST / WS / H / D / C / S so the player can
	// orient themselves on the first deal without reading the manual.
	auto* captionStock = lv_label_create(obj);
	lv_obj_set_style_text_font(captionStock, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionStock, MP_LABEL_DIM, 0);
	lv_label_set_text(captionStock, "ST");
	lv_obj_set_pos(captionStock, kStockX + 5, TopPileLabelY);

	auto* captionWaste = lv_label_create(obj);
	lv_obj_set_style_text_font(captionWaste, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionWaste, MP_LABEL_DIM, 0);
	lv_label_set_text(captionWaste, "WS");
	lv_obj_set_pos(captionWaste, kWasteX + 5, TopPileLabelY);

	for(uint8_t f = 0; f < FoundationCount; ++f) {
		auto* lbl = lv_label_create(obj);
		lv_obj_set_style_text_font(lbl, &pixelbasic7, 0);
		lv_obj_set_style_text_color(lbl, MP_LABEL_DIM, 0);
		lv_label_set_text(lbl, suitGlyph(f));
		lv_obj_set_pos(lbl, foundationX(f) + 8, TopPileLabelY);
	}
}

void PhoneSolitaire::buildTableau() {
	for(uint8_t col = 0; col < TableauCount; ++col) {
		auto* cont = lv_obj_create(obj);
		lv_obj_remove_style_all(cont);
		// Container is just a positioned rectangle; cards live inside it.
		lv_obj_set_size(cont, CardW, 64);
		lv_obj_set_pos(cont, tableauX(col), TableauY);
		lv_obj_set_style_pad_all(cont, 0, 0);
		lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
		lv_obj_set_style_border_width(cont, 0, 0);
		lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(cont, LV_OBJ_FLAG_CLICKABLE);
		tableauContainer[col] = cont;

		// Column number caption -- 1..7 dimmed below the column.
		auto* numLabel = lv_label_create(obj);
		lv_obj_set_style_text_font(numLabel, &pixelbasic7, 0);
		lv_obj_set_style_text_color(numLabel, MP_LABEL_DIM, 0);
		char buf[4];
		snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(col + 1));
		lv_label_set_text(numLabel, buf);
		lv_obj_set_pos(numLabel, tableauX(col) + 8, TableauLabelY);
	}
}

// ===========================================================================
// state transitions
// ===========================================================================

void PhoneSolitaire::shuffleDeck(Card deck[52]) {
	uint8_t idx = 0;
	for(uint8_t suit = 0; suit < 4; ++suit) {
		for(uint8_t rank = 1; rank <= 13; ++rank) {
			Card c = Card();
			c.rank   = rank;
			c.suit   = suit;
			c.faceUp = 0;
			c.pad    = 0;
			deck[idx++] = c;
		}
	}
	// Fisher-Yates over [0..51], seeded from millis() so consecutive
	// fresh deals are not identical.
	srand(static_cast<unsigned>(millis()));
	for(int i = 51; i > 0; --i) {
		const int j = rand() % (i + 1);
		Card tmp = deck[i];
		deck[i] = deck[j];
		deck[j] = tmp;
	}
}

void PhoneSolitaire::newGame() {
	for(uint8_t c = 0; c < TableauCount; ++c) tableauSize[c] = 0;
	for(uint8_t f = 0; f < FoundationCount; ++f) foundationSize[f] = 0;
	wasteSize = 0;
	stockSize = 0;
	score = 0;
	moves = 0;
	selectedSource = PileNone;
	state = GameState::Playing;

	Card deck[52];
	shuffleDeck(deck);

	uint8_t cursor = 0;
	// Deal 1, 2, 3, ... 7 cards into columns. The last card dealt to
	// each column is face-up, the rest are face-down. This is canonical
	// Klondike opening.
	for(uint8_t col = 0; col < TableauCount; ++col) {
		for(uint8_t i = 0; i <= col; ++i) {
			Card c = deck[cursor++];
			c.faceUp = (i == col) ? 1 : 0;
			tableau[col][i] = c;
		}
		tableauSize[col] = static_cast<uint8_t>(col + 1);
	}
	// Remaining 24 cards go into the stock face-down (cards drawn from
	// the *top* of the stock; we treat stock[stockSize-1] as the top).
	while(cursor < 52) {
		Card c = deck[cursor++];
		c.faceUp = 0;
		stock[stockSize++] = c;
	}

	renderAll();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
	refreshSelectionVisuals();
}

bool PhoneSolitaire::drawFromStock() {
	if(state != GameState::Playing) return false;

	if(stockSize > 0) {
		Card c = stock[stockSize - 1];
		stockSize--;
		c.faceUp = 1;
		waste[wasteSize++] = c;
		moves++;
		// Cancel any pending selection -- the drawn card is now the new
		// "obvious" candidate and stale selections feel surprising.
		selectedSource = PileNone;
		renderTopRow();
		refreshHud();
		refreshSelectionVisuals();
		return true;
	}

	if(wasteSize > 0) {
		// Recycle waste back into stock face-down. Reverse the order so
		// the next draw cycles the same sequence as a real deck would.
		while(wasteSize > 0) {
			Card c = waste[wasteSize - 1];
			wasteSize--;
			c.faceUp = 0;
			stock[stockSize++] = c;
		}
		moves++;
		selectedSource = PileNone;
		renderTopRow();
		refreshHud();
		refreshSelectionVisuals();
		return true;
	}

	return false;
}

bool PhoneSolitaire::tryMoveOnto(int8_t target) {
	if(state != GameState::Playing) return false;
	if(selectedSource == PileNone) return false;
	if(target == selectedSource) {
		// Tapping the source pile again cancels the selection.
		cancelSelection();
		return false;
	}

	// ---- collect the moving sub-stack ---------------------------------
	Card movingCards[13];
	uint8_t movingCount = 0;

	auto pushMoving = [&](Card c) {
		if(movingCount < 13) movingCards[movingCount++] = c;
	};

	if(selectedSource >= 0 && selectedSource < static_cast<int8_t>(TableauCount)) {
		const uint8_t srcCol = static_cast<uint8_t>(selectedSource);
		const uint8_t srcSize = tableauSize[srcCol];
		if(srcSize == 0) return false;

		// First face-up index (start of the run we'll grab).
		uint8_t startIdx = 0;
		while(startIdx < srcSize && !tableau[srcCol][startIdx].faceUp) startIdx++;
		if(startIdx >= srcSize) return false;

		// Foundation targets only ever accept the topmost card.
		if(target >= PileFoundationBase
		   && target < PileFoundationBase + static_cast<int8_t>(FoundationCount)) {
			startIdx = static_cast<uint8_t>(srcSize - 1);
		}

		for(uint8_t i = startIdx; i < srcSize; ++i) {
			pushMoving(tableau[srcCol][i]);
		}
	} else if(selectedSource == PileWaste) {
		if(wasteSize == 0) return false;
		pushMoving(waste[wasteSize - 1]);
	} else if(selectedSource >= PileFoundationBase
	          && selectedSource < PileFoundationBase + static_cast<int8_t>(FoundationCount)) {
		const uint8_t fIdx = static_cast<uint8_t>(selectedSource - PileFoundationBase);
		if(foundationSize[fIdx] == 0) return false;
		pushMoving(foundations[fIdx][foundationSize[fIdx] - 1]);
	} else {
		return false;
	}

	if(movingCount == 0) return false;

	// ---- legality check against target --------------------------------
	bool legal = false;
	if(target >= 0 && target < static_cast<int8_t>(TableauCount)) {
		const uint8_t dstCol = static_cast<uint8_t>(target);
		const uint8_t dstSize = tableauSize[dstCol];
		const Card head = movingCards[0];
		if(dstSize == 0) {
			legal = (head.rank == 13);   // King onto an empty column
		} else {
			const Card top = tableau[dstCol][dstSize - 1];
			legal = (top.faceUp
			         && top.rank > 0
			         && head.rank + 1 == top.rank
			         && (isRedSuit(head.suit) != isRedSuit(top.suit)));
		}
	} else if(target >= PileFoundationBase
	          && target < PileFoundationBase + static_cast<int8_t>(FoundationCount)) {
		if(movingCount != 1) return false;
		const uint8_t fIdx = static_cast<uint8_t>(target - PileFoundationBase);
		const Card head = movingCards[0];
		if(head.suit != fIdx) return false;
		if(foundationSize[fIdx] == 0) {
			legal = (head.rank == 1);   // Ace onto an empty foundation
		} else {
			const Card top = foundations[fIdx][foundationSize[fIdx] - 1];
			legal = (head.rank == top.rank + 1);
		}
	} else {
		// Stock / waste are never legal targets.
		return false;
	}

	if(!legal) return false;

	// ---- apply ---------------------------------------------------------
	// Remove from source.
	if(selectedSource >= 0 && selectedSource < static_cast<int8_t>(TableauCount)) {
		const uint8_t srcCol = static_cast<uint8_t>(selectedSource);
		tableauSize[srcCol] = static_cast<uint8_t>(tableauSize[srcCol] - movingCount);
		// Auto-flip the new top if it's face-down.
		if(tableauSize[srcCol] > 0
		   && !tableau[srcCol][tableauSize[srcCol] - 1].faceUp) {
			tableau[srcCol][tableauSize[srcCol] - 1].faceUp = 1;
			score += 5;   // Klondike "card flipped" bonus
		}
	} else if(selectedSource == PileWaste) {
		wasteSize--;
	} else {
		const uint8_t fIdx = static_cast<uint8_t>(selectedSource - PileFoundationBase);
		foundationSize[fIdx]--;
		// Pulling a card off the foundation is a small score penalty so
		// the player doesn't game the score by ping-pong-ing.
		if(score >= 15) score -= 15; else score = 0;
	}

	// Append to target.
	if(target >= 0 && target < static_cast<int8_t>(TableauCount)) {
		const uint8_t dstCol = static_cast<uint8_t>(target);
		for(uint8_t i = 0; i < movingCount && tableauSize[dstCol] < MaxColCards; ++i) {
			tableau[dstCol][tableauSize[dstCol]++] = movingCards[i];
		}
	} else {
		const uint8_t fIdx = static_cast<uint8_t>(target - PileFoundationBase);
		foundations[fIdx][foundationSize[fIdx]++] = movingCards[0];
		score += 10;   // Klondike "to foundation" bonus
	}

	moves++;
	selectedSource = PileNone;
	renderAll();
	refreshHud();
	refreshSelectionVisuals();
	checkWin();
	return true;
}

bool PhoneSolitaire::autoPromoteToFoundation() {
	if(state != GameState::Playing) return false;

	// Walk every legal source and try to advance any foundation. The
	// loop iterates a few times so a chain of promotions resolves in
	// one BTN_0 press if it would work in single steps.
	bool anyMoved = false;
	for(uint8_t pass = 0; pass < 8; ++pass) {
		bool movedThisPass = false;

		// Top of waste.
		if(wasteSize > 0) {
			const Card c = waste[wasteSize - 1];
			const uint8_t fIdx = c.suit;
			const bool legal = (foundationSize[fIdx] == 0)
			                   ? (c.rank == 1)
			                   : (c.rank == foundations[fIdx][foundationSize[fIdx] - 1].rank + 1);
			if(legal) {
				foundations[fIdx][foundationSize[fIdx]++] = c;
				wasteSize--;
				score += 10;
				moves++;
				anyMoved       = true;
				movedThisPass  = true;
				continue;
			}
		}

		// Top of each tableau column.
		for(uint8_t col = 0; col < TableauCount; ++col) {
			if(tableauSize[col] == 0) continue;
			const Card c = tableau[col][tableauSize[col] - 1];
			if(!c.faceUp) continue;
			const uint8_t fIdx = c.suit;
			const bool legal = (foundationSize[fIdx] == 0)
			                   ? (c.rank == 1)
			                   : (c.rank == foundations[fIdx][foundationSize[fIdx] - 1].rank + 1);
			if(!legal) continue;

			foundations[fIdx][foundationSize[fIdx]++] = c;
			tableauSize[col]--;
			if(tableauSize[col] > 0
			   && !tableau[col][tableauSize[col] - 1].faceUp) {
				tableau[col][tableauSize[col] - 1].faceUp = 1;
				score += 5;
			}
			score += 10;
			moves++;
			anyMoved       = true;
			movedThisPass  = true;
			break;
		}

		if(!movedThisPass) break;
	}

	if(anyMoved) {
		selectedSource = PileNone;
		renderAll();
		refreshHud();
		refreshSelectionVisuals();
		checkWin();
	}
	return anyMoved;
}

void PhoneSolitaire::selectSource(int8_t pile) {
	// Validate that the chosen source actually has a face-up card to
	// move. If it doesn't, refuse the selection rather than parking the
	// cursor on an empty pile.
	if(pile >= 0 && pile < static_cast<int8_t>(TableauCount)) {
		const uint8_t col = static_cast<uint8_t>(pile);
		if(tableauSize[col] == 0) return;
		// Need at least one face-up card.
		bool anyFaceUp = false;
		for(uint8_t i = 0; i < tableauSize[col]; ++i) {
			if(tableau[col][i].faceUp) { anyFaceUp = true; break; }
		}
		if(!anyFaceUp) return;
	} else if(pile == PileWaste) {
		if(wasteSize == 0) return;
	} else if(pile >= PileFoundationBase
	          && pile < PileFoundationBase + static_cast<int8_t>(FoundationCount)) {
		const uint8_t fIdx = static_cast<uint8_t>(pile - PileFoundationBase);
		if(foundationSize[fIdx] == 0) return;
	} else {
		return;
	}
	selectedSource = pile;
	refreshSelectionVisuals();
}

void PhoneSolitaire::cancelSelection() {
	selectedSource = PileNone;
	refreshSelectionVisuals();
}

void PhoneSolitaire::checkWin() {
	for(uint8_t f = 0; f < FoundationCount; ++f) {
		if(foundationSize[f] != 13) return;
	}
	state = GameState::Won;
	score += 100;   // round-clear bonus
	refreshSoftKeys();
	refreshOverlay();
}

// ===========================================================================
// rendering
// ===========================================================================

void PhoneSolitaire::renderAll() {
	renderTopRow();
	for(uint8_t col = 0; col < TableauCount; ++col) {
		renderTableauColumn(col);
	}
}

void PhoneSolitaire::renderTopRow() {
	// Stock: show back if any cards remain, "O" placeholder otherwise.
	if(stockSize > 0) {
		lv_obj_set_style_bg_color(stockSprite, CARD_BACK, 0);
		lv_obj_set_style_bg_opa(stockSprite, LV_OPA_COVER, 0);
		lv_obj_set_style_border_color(stockSprite, CARD_BORDER, 0);
		// Lattice trim: show the count in cyan so the player can see
		// how many draws are left without counting flips.
		char buf[8];
		snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(stockSize));
		lv_label_set_text(stockLabel, buf);
		lv_obj_set_style_text_color(stockLabel, MP_HIGHLIGHT, 0);
	} else {
		lv_obj_set_style_bg_color(stockSprite, MP_DIM, 0);
		lv_obj_set_style_bg_opa(stockSprite, LV_OPA_50, 0);
		lv_obj_set_style_border_color(stockSprite, CARD_BORDER, 0);
		lv_label_set_text(stockLabel, wasteSize > 0 ? "@" : "-");
		lv_obj_set_style_text_color(stockLabel, MP_LABEL_DIM, 0);
	}

	// Waste: show top card face-up if any, empty placeholder otherwise.
	if(wasteSize > 0) {
		const Card c = waste[wasteSize - 1];
		lv_obj_set_style_bg_color(wasteSprite, CARD_FACE, 0);
		lv_obj_set_style_bg_opa(wasteSprite, LV_OPA_COVER, 0);
		lv_obj_set_style_border_color(wasteSprite, CARD_BORDER, 0);
		lv_obj_set_style_text_color(wasteLabel,
		                             isRedSuit(c.suit) ? CARD_RED : CARD_BLACK, 0);
		char buf[8];
		snprintf(buf, sizeof(buf), "%s%s", rankGlyph(c.rank), suitGlyph(c.suit));
		lv_label_set_text(wasteLabel, buf);
	} else {
		lv_obj_set_style_bg_color(wasteSprite, MP_DIM, 0);
		lv_obj_set_style_bg_opa(wasteSprite, LV_OPA_50, 0);
		lv_obj_set_style_border_color(wasteSprite, CARD_BORDER, 0);
		lv_label_set_text(wasteLabel, "-");
		lv_obj_set_style_text_color(wasteLabel, MP_LABEL_DIM, 0);
	}

	// Foundations.
	for(uint8_t f = 0; f < FoundationCount; ++f) {
		auto* s = foundationSprite[f];
		auto* lbl = foundationLabel[f];
		if(foundationSize[f] > 0) {
			const Card c = foundations[f][foundationSize[f] - 1];
			lv_obj_set_style_bg_color(s, CARD_FACE, 0);
			lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
			lv_obj_set_style_border_color(s, CARD_BORDER, 0);
			lv_obj_set_style_text_color(lbl,
			                             isRedSuit(c.suit) ? CARD_RED : CARD_BLACK, 0);
			char buf[8];
			snprintf(buf, sizeof(buf), "%s%s", rankGlyph(c.rank), suitGlyph(c.suit));
			lv_label_set_text(lbl, buf);
		} else {
			lv_obj_set_style_bg_color(s, MP_DIM, 0);
			lv_obj_set_style_bg_opa(s, LV_OPA_50, 0);
			lv_obj_set_style_border_color(s, CARD_BORDER, 0);
			lv_obj_set_style_text_color(lbl, MP_LABEL_DIM, 0);
			lv_label_set_text(lbl, suitGlyph(f));
		}
	}
}

void PhoneSolitaire::renderTableauColumn(uint8_t col) {
	auto* cont = tableauContainer[col];
	if(cont == nullptr) return;
	lv_obj_clean(cont);

	const uint8_t n = tableauSize[col];
	if(n == 0) {
		// Empty column placeholder -- a faint outlined rectangle that
		// reads as "drop a King here".
		Card empty = Card();
		empty.rank = 0; empty.suit = 0; empty.faceUp = 0; empty.pad = 0;
		spawnCardSprite(cont, 0, 0, empty, /*isPlaceholder=*/true);
		return;
	}

	int16_t y = 0;
	for(uint8_t i = 0; i < n; ++i) {
		const Card c = tableau[col][i];
		spawnCardSprite(cont, 0, y, c, /*isPlaceholder=*/false);
		if(i + 1 < n) {
			y = static_cast<int16_t>(y + (c.faceUp ? kStrideFaceUp : kStrideFaceDown));
		}
	}
}

lv_obj_t* PhoneSolitaire::spawnCardSprite(lv_obj_t* parent,
                                          lv_coord_t x, lv_coord_t y,
                                          Card c, bool isPlaceholder) {
	auto* sprite = lv_obj_create(parent);
	lv_obj_remove_style_all(sprite);
	lv_obj_set_size(sprite, CardW, TableauH);
	lv_obj_set_pos(sprite, x, y);
	lv_obj_set_style_radius(sprite, 1, 0);
	lv_obj_set_style_pad_all(sprite, 0, 0);
	lv_obj_clear_flag(sprite, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(sprite, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(sprite, LV_OBJ_FLAG_IGNORE_LAYOUT);

	if(isPlaceholder) {
		lv_obj_set_style_bg_color(sprite, MP_DIM, 0);
		lv_obj_set_style_bg_opa(sprite, LV_OPA_30, 0);
		lv_obj_set_style_border_color(sprite, CARD_BORDER, 0);
		lv_obj_set_style_border_width(sprite, 1, 0);
		return sprite;
	}

	if(!c.faceUp) {
		lv_obj_set_style_bg_color(sprite, CARD_BACK, 0);
		lv_obj_set_style_bg_opa(sprite, LV_OPA_COVER, 0);
		lv_obj_set_style_border_color(sprite, CARD_BORDER, 0);
		lv_obj_set_style_border_width(sprite, 1, 0);
		// Tiny cyan diamond pip in the middle of the card-back so a
		// stack of face-down cards reads as a deck rather than a wall.
		auto* pip = lv_obj_create(sprite);
		lv_obj_remove_style_all(pip);
		lv_obj_set_size(pip, 4, 2);
		lv_obj_set_align(pip, LV_ALIGN_CENTER);
		lv_obj_set_style_bg_color(pip, MP_HIGHLIGHT, 0);
		lv_obj_set_style_bg_opa(pip, LV_OPA_COVER, 0);
		lv_obj_set_style_border_width(pip, 0, 0);
		lv_obj_clear_flag(pip, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(pip, LV_OBJ_FLAG_CLICKABLE);
		return sprite;
	}

	// Face-up card -- cream face + rank/suit label.
	lv_obj_set_style_bg_color(sprite, CARD_FACE, 0);
	lv_obj_set_style_bg_opa(sprite, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(sprite, CARD_BORDER, 0);
	lv_obj_set_style_border_width(sprite, 1, 0);

	auto* lbl = lv_label_create(sprite);
	lv_obj_set_style_text_font(lbl, &pixelbasic7, 0);
	lv_obj_set_style_text_color(lbl,
	                             isRedSuit(c.suit) ? CARD_RED : CARD_BLACK, 0);
	char buf[8];
	snprintf(buf, sizeof(buf), "%s%s", rankGlyph(c.rank), suitGlyph(c.suit));
	lv_label_set_text(lbl, buf);
	lv_obj_set_align(lbl, LV_ALIGN_CENTER);
	return sprite;
}

void PhoneSolitaire::refreshHud() {
	if(hudMovesLabel != nullptr) {
		char buf[20];
		const unsigned long m = moves > 999UL ? 999UL : moves;
		snprintf(buf, sizeof(buf), "MOVES %lu", m);
		lv_label_set_text(hudMovesLabel, buf);
	}
	if(hudScoreLabel != nullptr) {
		char buf[20];
		const unsigned long s = score > 99999UL ? 99999UL : score;
		snprintf(buf, sizeof(buf), "SCORE %lu", s);
		lv_label_set_text(hudScoreLabel, buf);
	}
}

void PhoneSolitaire::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	switch(state) {
		case GameState::Playing:
			softKeys->setLeft("DRAW");
			softKeys->setRight(selectedSource == PileNone ? "BACK" : "CANCEL");
			break;
		case GameState::Won:
			softKeys->setLeft("AGAIN");
			softKeys->setRight("BACK");
			break;
	}
}

void PhoneSolitaire::refreshOverlay() {
	if(overlayLabel == nullptr) return;
	switch(state) {
		case GameState::Playing:
			lv_obj_add_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
		case GameState::Won: {
			char buf[64];
			snprintf(buf, sizeof(buf),
			         "YOU WIN!\nMOVES %lu\nSCORE %lu\nA TO DEAL AGAIN",
			         static_cast<unsigned long>(moves),
			         static_cast<unsigned long>(score));
			lv_label_set_text(overlayLabel, buf);
			lv_obj_set_style_text_color(overlayLabel, MP_HIGHLIGHT, 0);
			lv_obj_set_style_border_color(overlayLabel, MP_HIGHLIGHT, 0);
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			lv_obj_move_foreground(overlayLabel);
			break;
		}
	}
}

void PhoneSolitaire::refreshSelectionVisuals() {
	// Reset every pile's border colour to the neutral CARD_BORDER, then
	// paint the selected pile's border in CARD_HIGHLIGHT (sunset orange)
	// so the player can see what they've grabbed.
	if(stockSprite != nullptr) {
		lv_obj_set_style_border_color(stockSprite, CARD_BORDER, 0);
	}
	if(wasteSprite != nullptr) {
		lv_obj_set_style_border_color(wasteSprite, CARD_BORDER, 0);
	}
	for(uint8_t f = 0; f < FoundationCount; ++f) {
		if(foundationSprite[f] != nullptr) {
			lv_obj_set_style_border_color(foundationSprite[f], CARD_BORDER, 0);
		}
	}
	for(uint8_t col = 0; col < TableauCount; ++col) {
		if(tableauContainer[col] == nullptr) continue;
		// Highlight the column container itself with a 1 px border when
		// it is selected as a source. The container is otherwise
		// border-less.
		const bool selected = (selectedSource == static_cast<int8_t>(col));
		lv_obj_set_style_border_color(tableauContainer[col],
		                               selected ? CARD_HIGHLIGHT : CARD_BORDER, 0);
		lv_obj_set_style_border_width(tableauContainer[col],
		                               selected ? 1 : 0, 0);
	}

	if(selectedSource == PileWaste && wasteSprite != nullptr) {
		lv_obj_set_style_border_color(wasteSprite, CARD_HIGHLIGHT, 0);
	} else if(selectedSource >= PileFoundationBase
	          && selectedSource < PileFoundationBase + static_cast<int8_t>(FoundationCount)) {
		const uint8_t fIdx = static_cast<uint8_t>(selectedSource - PileFoundationBase);
		if(foundationSprite[fIdx] != nullptr) {
			lv_obj_set_style_border_color(foundationSprite[fIdx], CARD_HIGHLIGHT, 0);
		}
	}

	refreshSoftKeys();
}

// ===========================================================================
// helpers
// ===========================================================================

const char* PhoneSolitaire::rankGlyph(uint8_t rank) {
	static char buf[4];
	switch(rank) {
		case 1:  return "A";
		case 10: return "10";
		case 11: return "J";
		case 12: return "Q";
		case 13: return "K";
		default:
			if(rank >= 2 && rank <= 9) {
				snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(rank));
				return buf;
			}
			return "?";
	}
}

const char* PhoneSolitaire::suitGlyph(uint8_t suit) {
	switch(suit) {
		case 0: return "H";
		case 1: return "D";
		case 2: return "C";
		case 3: return "S";
		default: return "?";
	}
}

// ===========================================================================
// input
// ===========================================================================

void PhoneSolitaire::buttonPressed(uint i) {
	// BACK either cancels selection or pops the screen.
	if(i == BTN_BACK) {
		if(softKeys) softKeys->flashRight();
		if(selectedSource != PileNone) {
			cancelSelection();
			return;
		}
		pop();
		return;
	}

	// Restart at any time -- handy escape hatch when the deal looks
	// hopeless. We reuse BTN_R rather than tying it to ENTER so the
	// in-game ENTER alias keeps working as the "auto-foundation" key.
	if(i == BTN_R) {
		newGame();
		return;
	}

	// Won-state input: A/Enter/0/5 deals fresh; everything else is
	// swallowed so accidental dialler presses do not leak through.
	if(state == GameState::Won) {
		if(i == BTN_ENTER || i == BTN_5 || i == BTN_0) {
			if(softKeys) softKeys->flashLeft();
			newGame();
		}
		return;
	}

	// ---- Playing state input ------------------------------------------

	// Stock draw is the only thing BTN_8 / BTN_L do.
	if(i == BTN_8 || i == BTN_L) {
		if(softKeys) softKeys->flashLeft();
		drawFromStock();
		return;
	}

	// Tableau column 1..7 -- BTN_1..BTN_7 map to col 0..6.
	if(i == BTN_1 || i == BTN_2 || i == BTN_3 || i == BTN_4
	   || i == BTN_5 || i == BTN_6 || i == BTN_7) {
		uint8_t col = 0;
		switch(i) {
			case BTN_1: col = 0; break;
			case BTN_2: col = 1; break;
			case BTN_3: col = 2; break;
			case BTN_4: col = 3; break;
			case BTN_5: col = 4; break;
			case BTN_6: col = 5; break;
			case BTN_7: col = 6; break;
			default: return;
		}
		if(selectedSource == PileNone) {
			selectSource(static_cast<int8_t>(col));
		} else {
			tryMoveOnto(static_cast<int8_t>(col));
		}
		return;
	}

	// Waste pile -- BTN_9 selects waste as the move source.
	if(i == BTN_9) {
		if(selectedSource == PileNone) {
			selectSource(PileWaste);
		} else if(selectedSource == PileWaste) {
			cancelSelection();
		}
		// No-op when a different source is selected -- waste cannot be
		// a target. The user can press BACK to cancel and re-pick.
		return;
	}

	// Foundation routing -- BTN_0 / BTN_ENTER auto-route by suit, or
	// auto-promote when no source is selected.
	if(i == BTN_0 || i == BTN_ENTER) {
		if(selectedSource == PileNone) {
			autoPromoteToFoundation();
			return;
		}

		// Determine the target foundation from the selected card's
		// suit. We only know the suit by peeking at the top of the
		// source.
		Card head = Card();
		bool haveHead = false;

		if(selectedSource >= 0 && selectedSource < static_cast<int8_t>(TableauCount)) {
			const uint8_t srcCol = static_cast<uint8_t>(selectedSource);
			if(tableauSize[srcCol] > 0) {
				head = tableau[srcCol][tableauSize[srcCol] - 1];
				haveHead = head.faceUp;
			}
		} else if(selectedSource == PileWaste) {
			if(wasteSize > 0) {
				head = waste[wasteSize - 1];
				haveHead = true;
			}
		} else if(selectedSource >= PileFoundationBase
		          && selectedSource < PileFoundationBase + static_cast<int8_t>(FoundationCount)) {
			// Foundation -> foundation moves are nonsense; drop the
			// selection so the user can re-select.
			cancelSelection();
			return;
		}

		if(!haveHead) {
			cancelSelection();
			return;
		}

		const int8_t target = static_cast<int8_t>(PileFoundationBase + head.suit);
		tryMoveOnto(target);
		return;
	}
}
