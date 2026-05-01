#include "PhoneMemoryMatch.h"

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
// (S80) and PhoneTicTacToe (S81) without a visual seam.
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)  // dim purple captions

namespace {

// ---- pixel helper -------------------------------------------------------
//
// Drop a single rectangle into the icon parent at local coords (x, y).
// Mirrors the px() helper used in PhoneGamesScreen / PhoneBubbleSmile so
// the call sites in paintIcon() stay tiny and readable.
void px(lv_obj_t* parent, lv_coord_t x, lv_coord_t y,
        lv_coord_t w, lv_coord_t h, lv_color_t color) {
	auto* p = lv_obj_create(parent);
	lv_obj_remove_style_all(p);
	lv_obj_set_size(p, w, h);
	lv_obj_set_pos(p, x, y);
	lv_obj_set_style_bg_color(p, color, 0);
	lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(p, 0, 0);
	lv_obj_set_style_radius(p, 0, 0);
	lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(p, LV_OBJ_FLAG_CLICKABLE);
}

} // namespace

// =========================================================================
// ctor / dtor
// =========================================================================

PhoneMemoryMatch::PhoneMemoryMatch()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr) {

	for(uint8_t i = 0; i < CardCount; ++i) {
		cardSprites[i] = nullptr;
		cardIcons[i]   = nullptr;
		kinds[i]       = 0;
		states[i]      = CardState::Hidden;
	}

	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildHud();
	buildCards();
	buildOverlay();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("FLIP");
	softKeys->setRight("BACK");

	newRound();
}

PhoneMemoryMatch::~PhoneMemoryMatch() {
	stopTickTimer();
	cancelResolveTimer();
	// All children parented to obj; LVScreen frees them.
}

void PhoneMemoryMatch::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneMemoryMatch::onStop() {
	Input::getInstance()->removeListener(this);
	stopTickTimer();
	cancelResolveTimer();
}

// =========================================================================
// build helpers
// =========================================================================

void PhoneMemoryMatch::buildHud() {
	// Three small badges arranged left / centre / right inside the
	// 12 px HUD strip. pixelbasic7 in cyan / dim cream / orange so the
	// triplet reads as discrete items at a glance even though they sit
	// on the same row.
	hudFlipsLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudFlipsLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudFlipsLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(hudFlipsLabel, "FLIPS 000");
	lv_obj_set_pos(hudFlipsLabel, 4, HudY + 2);

	hudPairsLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudPairsLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudPairsLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(hudPairsLabel, "0/8");
	lv_obj_set_align(hudPairsLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hudPairsLabel, HudY + 2);

	hudTimerLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudTimerLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudTimerLabel, MP_ACCENT, 0);
	lv_label_set_text(hudTimerLabel, "00:00");
	lv_obj_set_pos(hudTimerLabel, 132, HudY + 2);
}

void PhoneMemoryMatch::buildOverlay() {
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

void PhoneMemoryMatch::buildCards() {
	// One sprite per card. Each sprite is a 22x22 rounded panel; its
	// interior holds either the "?" face-down hint or the pixel-art
	// icon (drawn under cardIcons[i]).
	for(uint8_t row = 0; row < BoardRows; ++row) {
		for(uint8_t col = 0; col < BoardCols; ++col) {
			const uint8_t idx = indexOf(col, row);

			auto* s = lv_obj_create(obj);
			lv_obj_remove_style_all(s);
			lv_obj_set_size(s, CardPx, CardPx);
			lv_obj_set_pos(s,
			               BoardOriginX + col * (CardPx + CardGap),
			               BoardOriginY + row * (CardPx + CardGap));
			lv_obj_set_style_bg_color(s, MP_DIM, 0);
			lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
			lv_obj_set_style_border_color(s, MP_LABEL_DIM, 0);
			lv_obj_set_style_border_width(s, 1, 0);
			lv_obj_set_style_radius(s, 2, 0);
			lv_obj_set_style_pad_all(s, 0, 0);
			lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_clear_flag(s, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_flag(s, LV_OBJ_FLAG_IGNORE_LAYOUT);
			cardSprites[idx] = s;

			// Icon parent - 14x14 area centred inside the 22x22 card
			// (4 px padding all around). The actual icon rectangles
			// are children of this parent, so a flip-back can wipe
			// the icon by clearing this single sub-tree.
			auto* g = lv_obj_create(s);
			lv_obj_remove_style_all(g);
			lv_obj_set_size(g, 14, 14);
			lv_obj_set_pos(g, 4, 4);
			lv_obj_set_style_bg_opa(g, LV_OPA_TRANSP, 0);
			lv_obj_set_style_border_width(g, 0, 0);
			lv_obj_clear_flag(g, LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_clear_flag(g, LV_OBJ_FLAG_CLICKABLE);
			cardIcons[idx] = g;
		}
	}
}

// =========================================================================
// state transitions
// =========================================================================

void PhoneMemoryMatch::shuffleDeck() {
	// Lay the eight pairs out in slot order, then Fisher-Yates shuffle
	// the kinds[] array in place. With only 16 entries this is well
	// below the cost of a single LVGL refresh.
	for(uint8_t i = 0; i < CardCount; ++i) {
		kinds[i] = static_cast<uint8_t>(i / 2);
	}
	for(uint8_t i = CardCount - 1; i > 0; --i) {
		const uint8_t j = static_cast<uint8_t>(rand() % (i + 1));
		const uint8_t tmp = kinds[i];
		kinds[i] = kinds[j];
		kinds[j] = tmp;
	}
}

void PhoneMemoryMatch::newRound() {
	cancelResolveTimer();
	stopTickTimer();

	shuffleDeck();
	for(uint8_t i = 0; i < CardCount; ++i) {
		states[i] = CardState::Hidden;
	}

	state          = GameState::Idle;
	firstFlipped   = -1;
	secondFlipped  = -1;
	flips          = 0;
	pairsCleared   = 0;
	startMillis    = 0;     // the first flip will start the clock
	finishMillis   = 0;
	cursorCol      = 0;
	cursorRow      = 0;

	renderAllCards();
	renderCursor();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneMemoryMatch::ensureTimerStarted() {
	if(startMillis != 0) return;
	startMillis = millis();
	if(startMillis == 0) startMillis = 1; // avoid the "0 = unstarted" sentinel
	startTickTimer();
}

void PhoneMemoryMatch::flipAt(uint8_t col, uint8_t row) {
	if(state == GameState::Resolving || state == GameState::Won) return;

	const uint8_t idx = indexOf(col, row);
	if(idx >= CardCount) return;
	if(states[idx] != CardState::Hidden) return;

	// First interaction kicks the clock off.
	ensureTimerStarted();

	states[idx] = CardState::Shown;
	++flips;

	if(state == GameState::Idle) {
		firstFlipped = static_cast<int8_t>(idx);
		state = GameState::Showing;
	} else /* GameState::Showing */ {
		secondFlipped = static_cast<int8_t>(idx);
		state = GameState::Resolving;
		scheduleResolve();
	}

	renderCard(idx);
	renderCursor();
	refreshHud();
	refreshSoftKeys();
}

void PhoneMemoryMatch::scheduleResolve() {
	cancelResolveTimer();
	resolveTimer = lv_timer_create(&PhoneMemoryMatch::onResolveStatic,
	                                kRevealMs, this);
	if(resolveTimer != nullptr) {
		// One-shot - the callback frees the timer itself, but we also
		// cap repeat_count so a stray fire after teardown is harmless.
		lv_timer_set_repeat_count(resolveTimer, 1);
	}
}

void PhoneMemoryMatch::resolvePair() {
	if(state != GameState::Resolving) return;
	if(firstFlipped < 0 || secondFlipped < 0) {
		// Shouldn't happen, but recover gracefully by reverting to Idle.
		state = GameState::Idle;
		firstFlipped = secondFlipped = -1;
		refreshSoftKeys();
		return;
	}

	const uint8_t a = static_cast<uint8_t>(firstFlipped);
	const uint8_t b = static_cast<uint8_t>(secondFlipped);
	const bool match = (kinds[a] == kinds[b]);

	if(match) {
		states[a] = CardState::Matched;
		states[b] = CardState::Matched;
		++pairsCleared;
	} else {
		states[a] = CardState::Hidden;
		states[b] = CardState::Hidden;
	}

	firstFlipped  = -1;
	secondFlipped = -1;
	state = GameState::Idle;

	renderCard(a);
	renderCard(b);
	renderCursor();
	refreshHud();
	refreshSoftKeys();

	if(pairsCleared >= PairCount) {
		winMatch();
	}
}

void PhoneMemoryMatch::winMatch() {
	state = GameState::Won;
	stopTickTimer();
	finishMillis = millis();
	if(finishMillis < startMillis) finishMillis = startMillis;

	// In-memory leaderboard: smallest flips wins; ties broken by time.
	const uint32_t elapsed = finishMillis - startMillis;
	if(bestFlips == 0 || flips < bestFlips ||
	   (flips == bestFlips && (bestMillis == 0 || elapsed < bestMillis))) {
		bestFlips  = flips;
		bestMillis = elapsed;
	}

	renderAllCards();
	renderCursor();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
}

// =========================================================================
// rendering
// =========================================================================

void PhoneMemoryMatch::renderAllCards() {
	for(uint8_t i = 0; i < CardCount; ++i) {
		renderCard(i);
	}
}

void PhoneMemoryMatch::renderCard(uint8_t cell) {
	if(cell >= CardCount) return;
	auto* s = cardSprites[cell];
	auto* g = cardIcons[cell];
	if(s == nullptr || g == nullptr) return;

	// Wipe any existing icon children so the redraw path is single-shot
	// regardless of the previous state. Cheap (at most ~10 children).
	lv_obj_clean(g);

	const CardState st = states[cell];
	switch(st) {
		case CardState::Hidden: {
			// Face-down card: solid purple panel with a subtle cyan
			// dot pattern (3-pixel triangle tucked in the middle) so
			// the back reads as "decorated" rather than blank.
			lv_obj_set_style_bg_color(s, MP_DIM, 0);
			lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
			lv_obj_set_style_border_color(s, MP_LABEL_DIM, 0);
			lv_obj_set_style_border_width(s, 1, 0);

			// Three small dots inside the icon area to suggest the
			// "back-of-card" pattern. Centred in the 14x14 region.
			px(g,  3,  3, 2, 2, MP_LABEL_DIM);
			px(g,  9,  3, 2, 2, MP_LABEL_DIM);
			px(g,  6,  9, 2, 2, MP_LABEL_DIM);
			break;
		}
		case CardState::Shown: {
			// Face-up un-matched card: dark panel + cyan border so the
			// reveal pops. Icon drawn in its native palette below.
			lv_obj_set_style_bg_color(s, MP_BG_DARK, 0);
			lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
			lv_obj_set_style_border_color(s, MP_HIGHLIGHT, 0);
			lv_obj_set_style_border_width(s, 1, 0);
			paintIcon(g, kinds[cell]);
			break;
		}
		case CardState::Matched: {
			// Locked-in pair: dimmer panel + dim purple border so the
			// matched cards visually retreat into the board, drawing
			// the eye toward what's still hidden.
			lv_obj_set_style_bg_color(s, MP_BG_DARK, 0);
			lv_obj_set_style_bg_opa(s, LV_OPA_70, 0);
			lv_obj_set_style_border_color(s, MP_DIM, 0);
			lv_obj_set_style_border_width(s, 1, 0);
			paintIcon(g, kinds[cell]);
			break;
		}
	}
}

void PhoneMemoryMatch::renderCursor() {
	// The cursor is a sunset-orange border on the focused card, drawn
	// on top of whatever state-tint that card is wearing. We always
	// re-render the focused card from scratch first so we don't bake
	// the cursor border into a state we'll later have to clean up.
	const uint8_t focused = indexOf(cursorCol, cursorRow);
	if(focused >= CardCount) return;
	auto* s = cardSprites[focused];
	if(s == nullptr) return;

	// Re-render to pick up the proper state tint, then overlay the
	// cursor border + bring the sprite to the front of the z-order so
	// the orange edge isn't clipped by neighbour borders.
	renderCard(focused);
	lv_obj_set_style_border_color(s, MP_ACCENT, 0);
	lv_obj_move_foreground(s);
}

void PhoneMemoryMatch::refreshHud() {
	if(hudFlipsLabel != nullptr) {
		char buf[16];
		const unsigned f = flips > 999 ? 999 : flips;
		snprintf(buf, sizeof(buf), "FLIPS %03u", f);
		lv_label_set_text(hudFlipsLabel, buf);
	}
	if(hudPairsLabel != nullptr) {
		char buf[16];
		snprintf(buf, sizeof(buf), "%u/%u",
		         static_cast<unsigned>(pairsCleared),
		         static_cast<unsigned>(PairCount));
		lv_label_set_text(hudPairsLabel, buf);
	}
	if(hudTimerLabel != nullptr) {
		uint32_t elapsedMs = 0;
		if(state == GameState::Won
		   && startMillis > 0 && finishMillis >= startMillis) {
			elapsedMs = finishMillis - startMillis;
		} else if(startMillis > 0) {
			elapsedMs = millis() - startMillis;
		}
		uint32_t total_s = elapsedMs / 1000;
		if(total_s > 99 * 60 + 59) total_s = 99 * 60 + 59; // cap at 99:59
		const uint32_t mm = total_s / 60;
		const uint32_t ss = total_s % 60;
		char buf[8];
		snprintf(buf, sizeof(buf), "%02lu:%02lu",
		         static_cast<unsigned long>(mm),
		         static_cast<unsigned long>(ss));
		lv_label_set_text(hudTimerLabel, buf);
	}
}

void PhoneMemoryMatch::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	switch(state) {
		case GameState::Idle:
		case GameState::Showing:
			softKeys->setLeft("FLIP");
			softKeys->setRight("BACK");
			break;
		case GameState::Resolving:
			// Inputs other than R / BACK are swallowed during the
			// reveal window; tell the player so via the soft-key
			// caption.
			softKeys->setLeft("WAIT");
			softKeys->setRight("BACK");
			break;
		case GameState::Won:
			softKeys->setLeft("AGAIN");
			softKeys->setRight("BACK");
			break;
	}
}

void PhoneMemoryMatch::refreshOverlay() {
	if(overlayLabel == nullptr) return;
	if(state != GameState::Won) {
		lv_obj_add_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
		return;
	}

	uint32_t elapsedMs = 0;
	if(finishMillis >= startMillis) elapsedMs = finishMillis - startMillis;
	uint32_t total_s = elapsedMs / 1000;
	if(total_s > 99 * 60 + 59) total_s = 99 * 60 + 59;
	const uint32_t mm = total_s / 60;
	const uint32_t ss = total_s % 60;

	char buf[64];
	snprintf(buf, sizeof(buf),
	         "CLEARED!\n%u FLIPS  %02lu:%02lu\nA TO PLAY AGAIN",
	         static_cast<unsigned>(flips),
	         static_cast<unsigned long>(mm),
	         static_cast<unsigned long>(ss));
	lv_label_set_text(overlayLabel, buf);
	lv_obj_set_style_text_color(overlayLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_style_border_color(overlayLabel, MP_HIGHLIGHT, 0);
	lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
	lv_obj_move_foreground(overlayLabel);
}

// =========================================================================
// pixel-art icons
// =========================================================================
//
// Each icon is drawn into the 14x14 interior of a card. The eight kinds
// pick distinct silhouettes + colours so a glance at any flipped card
// is enough to tell the kind apart, even on the 160x128 panel.

void PhoneMemoryMatch::paintIcon(lv_obj_t* g, uint8_t kind) {
	// Local palette aliases - each icon picks the colours that read
	// best against MP_BG_DARK at icon scale.
	const lv_color_t cyan    = lv_color_make(122, 232, 255);
	const lv_color_t orange  = lv_color_make(255, 140,  30);
	const lv_color_t cream   = lv_color_make(255, 220, 180);
	const lv_color_t yellow  = lv_color_make(255, 220,  60);
	const lv_color_t red     = lv_color_make(240,  90,  90);
	const lv_color_t magenta = lv_color_make(220, 130, 240);
	const lv_color_t green   = lv_color_make(120, 220, 110);

	switch(kind) {
		case 0: {
			// Heart - two top lobes + a tapering body. Classic 8-bit
			// heart silhouette, recoloured red.
			px(g,  2,  3, 4, 3, red);
			px(g,  8,  3, 4, 3, red);
			px(g,  1,  5, 12, 3, red);
			px(g,  3,  8, 8, 2, red);
			px(g,  5, 10, 4, 2, red);
			px(g,  6, 12, 2, 1, red);
			break;
		}
		case 1: {
			// Star - five-point pixel star with a cream highlight pip
			// in the middle so the silhouette reads even when the
			// border tint changes.
			px(g,  6,  1, 2, 2, yellow);
			px(g,  5,  3, 4, 2, yellow);
			px(g,  1,  5, 12, 2, yellow);
			px(g,  3,  7, 8, 2, yellow);
			px(g,  4,  9, 6, 2, yellow);
			px(g,  3, 11, 3, 2, yellow);
			px(g,  8, 11, 3, 2, yellow);
			px(g,  6,  6, 2, 2, cream);
			break;
		}
		case 2: {
			// Crescent moon - two arcs of cyan rectangles offset to
			// produce a crescent. Plus a tiny cream sparkle pip.
			px(g,  3,  2, 4, 2, cyan);
			px(g,  2,  4, 4, 2, cyan);
			px(g,  1,  6, 4, 2, cyan);
			px(g,  1,  8, 4, 2, cyan);
			px(g,  2, 10, 4, 2, cyan);
			px(g,  3, 12, 4, 1, cyan);
			px(g, 10,  3, 1, 1, cream);
			px(g, 11,  6, 1, 1, cream);
			break;
		}
		case 3: {
			// Sun - centre dot + four orthogonal rays + four diagonal
			// pip rays. Orange palette to echo the MAKERphone accent.
			px(g,  4,  4, 6, 6, orange);
			px(g,  6,  1, 2, 2, orange); // up
			px(g,  6, 11, 2, 2, orange); // down
			px(g,  1,  6, 2, 2, orange); // left
			px(g, 11,  6, 2, 2, orange); // right
			px(g,  2,  2, 1, 1, yellow);
			px(g, 11,  2, 1, 1, yellow);
			px(g,  2, 11, 1, 1, yellow);
			px(g, 11, 11, 1, 1, yellow);
			break;
		}
		case 4: {
			// Lightning bolt - jagged zig-zag in yellow with a cream
			// "spark" pip at the tail to break the otherwise-flat
			// silhouette.
			px(g,  6,  1, 4, 3, yellow);
			px(g,  4,  4, 5, 2, yellow);
			px(g,  6,  6, 5, 2, yellow);
			px(g,  3,  8, 5, 2, yellow);
			px(g,  4, 10, 3, 3, yellow);
			px(g,  3, 11, 1, 1, cream);
			break;
		}
		case 5: {
			// Diamond / gem - rhombus silhouette in magenta with a
			// cyan top-bevel and cream highlight pip.
			px(g,  6,  1, 2, 2, cyan);
			px(g,  4,  3, 6, 2, cyan);
			px(g,  2,  5, 10, 2, magenta);
			px(g,  3,  7, 8, 2, magenta);
			px(g,  4,  9, 6, 2, magenta);
			px(g,  5, 11, 4, 2, magenta);
			px(g,  6,  4, 1, 1, cream);
			break;
		}
		case 6: {
			// Music note - vertical stem + filled note-head + stem
			// flag. Cyan, the same accent we use for "received" UI.
			px(g,  4,  1, 6, 2, cyan); // top of flag
			px(g,  9,  1, 2, 5, cyan); // descending flag stem
			px(g,  4,  3, 2, 7, cyan); // main vertical stem
			px(g,  2,  9, 5, 4, cyan); // round-ish note head
			px(g,  3,  8, 4, 1, cyan);
			px(g,  3, 12, 1, 1, cream);
			break;
		}
		case 7: {
			// Smiley face - circle frame in green with two cyan eyes
			// and a cream smile. Reads as "happy" at icon scale.
			px(g,  4,  1, 6, 1, green);
			px(g,  2,  2, 2, 2, green);
			px(g, 10,  2, 2, 2, green);
			px(g,  1,  4, 2, 6, green);
			px(g, 11,  4, 2, 6, green);
			px(g,  2, 10, 2, 2, green);
			px(g, 10, 10, 2, 2, green);
			px(g,  4, 12, 6, 1, green);
			// Eyes.
			px(g,  4,  5, 2, 2, cyan);
			px(g,  8,  5, 2, 2, cyan);
			// Smile.
			px(g,  4,  9, 6, 1, cream);
			px(g,  3,  8, 1, 1, cream);
			px(g, 10,  8, 1, 1, cream);
			break;
		}
		default:
			// Defensive fallback - a single dim dot. Should never fire
			// because shuffleDeck() only produces kinds 0..7.
			px(g,  6,  6, 2, 2, MP_LABEL_DIM);
			break;
	}
}

// =========================================================================
// timer helpers
// =========================================================================

void PhoneMemoryMatch::startTickTimer() {
	if(tickTimer != nullptr) return;
	// 250 ms cadence: matches PhoneSlidingPuzzle, sweet spot between
	// snappy mm:ss redraws and LVGL refresh cost.
	tickTimer = lv_timer_create(&PhoneMemoryMatch::onTickStatic, 250, this);
}

void PhoneMemoryMatch::stopTickTimer() {
	if(tickTimer != nullptr) {
		lv_timer_del(tickTimer);
		tickTimer = nullptr;
	}
}

void PhoneMemoryMatch::onTickStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneMemoryMatch*>(timer->user_data);
	if(self == nullptr) return;
	if(self->state == GameState::Won) return;
	self->refreshHud();
}

void PhoneMemoryMatch::cancelResolveTimer() {
	if(resolveTimer != nullptr) {
		lv_timer_del(resolveTimer);
		resolveTimer = nullptr;
	}
}

void PhoneMemoryMatch::onResolveStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneMemoryMatch*>(timer->user_data);
	if(self == nullptr) return;
	// LVGL deletes one-shot timers automatically after the call returns,
	// but we null our handle proactively so the dtor doesn't double-free.
	self->resolveTimer = nullptr;
	self->resolvePair();
}

// =========================================================================
// input
// =========================================================================

void PhoneMemoryMatch::buttonPressed(uint i) {
	// BACK always pops out, regardless of state.
	if(i == BTN_BACK) {
		if(softKeys) softKeys->flashRight();
		pop();
		return;
	}

	// Reshuffle / start a new round at any time.
	if(i == BTN_R) {
		newRound();
		return;
	}

	// Movement always works, even during the reveal window, so the
	// player can pre-frame their next pair while watching the resolve
	// finish. The actual flip is gated below.
	if(i == BTN_LEFT || i == BTN_4) {
		if(cursorCol > 0) --cursorCol;
		else cursorCol = static_cast<uint8_t>(BoardCols - 1);
		renderCursor();
		return;
	}
	if(i == BTN_RIGHT || i == BTN_6) {
		cursorCol = static_cast<uint8_t>((cursorCol + 1) % BoardCols);
		renderCursor();
		return;
	}
	if(i == BTN_2) {
		if(cursorRow > 0) --cursorRow;
		else cursorRow = static_cast<uint8_t>(BoardRows - 1);
		renderCursor();
		return;
	}
	if(i == BTN_8) {
		cursorRow = static_cast<uint8_t>((cursorRow + 1) % BoardRows);
		renderCursor();
		return;
	}

	// Flip / commit. In Won state the same key starts a fresh round
	// (matching the PhoneSlidingPuzzle / PhoneTicTacToe convention).
	if(i == BTN_5 || i == BTN_ENTER) {
		switch(state) {
			case GameState::Idle:
			case GameState::Showing:
				if(softKeys) softKeys->flashLeft();
				flipAt(cursorCol, cursorRow);
				return;
			case GameState::Resolving:
				// Swallow the press - the reveal is in flight.
				return;
			case GameState::Won:
				if(softKeys) softKeys->flashLeft();
				newRound();
				return;
		}
		return;
	}
}
