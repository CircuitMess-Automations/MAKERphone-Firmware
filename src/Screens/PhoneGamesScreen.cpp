#include "PhoneGamesScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <Chatter.h>
#include <SPIFFS.h>
#include <Loop/LoopManager.h>
#include <stdio.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../FSLVGL.h"
#include "../Games/GameEngine/Game.h"
#include "../Games/Snake/Snake.h"
#include "../Games/Pong/Bonk.h"
#include "../Games/Invaders/SpaceInvaders.h"
#include "../Games/Space/SpaceRocks.h"
#include "PhoneTetris.h"
#include "PhoneBounce.h"
#include "PhoneBrickBreaker.h"
#include "PhoneBantumi.h"
#include "PhoneBubbleSmile.h"
#include "PhoneMinesweeper.h"
#include "PhoneSlidingPuzzle.h"

// MAKERphone retro palette - kept identical to every other Phone* widget
// so the cards visually slot into the rest of the device. Inlined here
// because that is the established pattern (see PhoneMainMenu.cpp,
// PhoneAppStubScreen.cpp etc.).
#define MP_BG_DARK     lv_color_make(20, 12, 36)
#define MP_ACCENT      lv_color_make(255, 140, 30)    // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)   // cyan
#define MP_DIM         lv_color_make(70, 56, 100)     // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)   // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)   // dim purple sub-caption

// --- Glyph palette pieces -----------------------------------------------
// These are short-lived per-game palette helpers; each glyph builder picks
// the colours that read best for its game. We rely on plain lv_obj
// rectangles (no canvas/sprite cost) and accept that the glyphs are
// stylised rather than literal - on a 160x128 panel a 24x18 pixel-art
// silhouette is more legible than a faithful icon.
namespace {

// Drop a single rectangle inside the glyph parent. Coordinates are in the
// glyph's local frame so each builder can pretend it has a small canvas to
// draw on (e.g. 30x18 for the 2x2-card layout below).
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

// --- Game info table ----------------------------------------------------
// Order matters: the cursor + page index resolve to a slot in this table.
// Engine games keep their existing splash blits; Screen games (PhoneTetris
// and every Phase-N successor) skip the splash because they handle their
// own intro overlay inside the screen itself.
const PhoneGamesScreen::GameInfo PhoneGamesScreen::Games[PhoneGamesScreen::kGameCount] = {
	{ "SPACE",    "/Games/Space/splash.raw", GameKind::Engine },
	{ "INVADERZ", nullptr,                   GameKind::Engine },
	{ "SNAKE",    nullptr,                   GameKind::Engine },
	{ "BONK",     nullptr,                   GameKind::Engine },
	{ "TETRIS",   nullptr,                   GameKind::Screen },  // S71
	{ "BOUNCE",   nullptr,                   GameKind::Screen },  // S73
	{ "BRICK",    nullptr,                   GameKind::Screen },  // S75
	{ "BANTUMI",  nullptr,                   GameKind::Screen },  // S76
	{ "BUBBLES",  nullptr,                   GameKind::Screen },  // S77
	{ "MINES",    nullptr,                   GameKind::Screen },  // S79
	{ "SLIDE15",  nullptr,                   GameKind::Screen },  // S80
};

PhoneGamesScreen::PhoneGamesScreen()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  titleLabel(nullptr) {

	// Full-screen container, no scrollbars, no padding - same blank-canvas
	// pattern PhoneHomeScreen / PhoneMainMenu use. Children below either
	// pin themselves with IGNORE_LAYOUT or are LVGL primitives that we
	// anchor manually on the 160x128 display.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order. The
	// status bar, cards and soft-key bar all overlay it without any
	// per-child opacity gymnastics. Same z-order pattern PhoneHomeScreen
	// and PhoneMainMenu rely on.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: signal | clock | battery (10 px tall).
	statusBar = new PhoneStatusBar(obj);

	// Title sits under the status bar, centred. pixelbasic7 in sunset
	// orange so it pops over the synthwave gradient without competing
	// with the cyan clock face above it.
	titleLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(titleLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(titleLabel, MP_ACCENT, 0);
	lv_label_set_text(titleLabel, "GAMES");
	lv_obj_set_align(titleLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(titleLabel, 12);

	// S71: small page indicator at the right end of the title row.
	// pixelbasic7 in dim purple - just visible enough to telegraph that
	// the carousel has more than one page. Hidden when only one page
	// exists so the legacy 4-game launcher visuals stay untouched.
	pageLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(pageLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(pageLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(pageLabel, "1/1");
	lv_obj_set_pos(pageLabel, 138, 12);

	// Build the four visual slots (the 2x2 grid). The slots themselves
	// are page-agnostic; their content (glyph + label) is filled in by
	// repaintCards() once the slots exist.
	cards.reserve(SlotsPerPage);
	for(uint8_t i = 0; i < SlotsPerPage; i++){
		buildCard(i);
	}
	repaintCards();
	refreshPageLabel();

	// Initial selection visuals - card 0 (top-left) starts focused.
	applySelection(/*prev*/ 0, /*curr*/ 0);

	// Bottom: phone-style softkeys. PLAY (left) launches the focused
	// card; BACK (right) pops to the parent (PhoneMainMenu).
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("PLAY");
	softKeys->setRight("BACK");
}

PhoneGamesScreen::~PhoneGamesScreen() {
	// All children are parented to obj; LVGL frees them recursively when
	// the screen's obj is destroyed by the LVScreen base destructor.
	// The cards vector holds raw lv_obj_t pointers (not LVObject wrappers)
	// so there is nothing for us to delete manually.
}

void PhoneGamesScreen::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneGamesScreen::onStop() {
	Input::getInstance()->removeListener(this);
}

void PhoneGamesScreen::buildCard(uint8_t slotIndex) {
	// Card root: a 70x44 panel anchored at (gridX, gridY) inside the
	// 2x2 layout. The layout flag IGNORE_LAYOUT pins the card so its
	// position does not get shoved around if the parent ever decides to
	// flex its children.
	auto* root = lv_obj_create(obj);
	lv_obj_remove_style_all(root);
	lv_obj_set_size(root, CardW, CardH);
	lv_obj_set_style_bg_color(root, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(root, LV_OPA_70, 0);
	lv_obj_set_style_border_color(root, MP_DIM, 0);
	lv_obj_set_style_border_width(root, 1, 0);
	lv_obj_set_style_radius(root, 2, 0);
	lv_obj_set_style_pad_all(root, 0, 0);
	lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(root, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(root, LV_OBJ_FLAG_IGNORE_LAYOUT);

	// Position based on flat slot index - row 0 is i<2, row 1 is i>=2.
	const lv_coord_t col = slotIndex % 2;
	const lv_coord_t row = slotIndex / 2;
	const lv_coord_t x = GridX + col * (CardW + CardGapX);
	const lv_coord_t y = GridY + row * (CardH + CardGapY);
	lv_obj_set_pos(root, x, y);

	// Glyph parent - 30x22 area centred horizontally inside the card,
	// pinned 3 px from the top. Each game's builder draws into this
	// sub-canvas using the px() helper above.
	auto* glyph = lv_obj_create(root);
	lv_obj_remove_style_all(glyph);
	lv_obj_set_size(glyph, 30, 22);
	lv_obj_set_pos(glyph, (CardW - 30) / 2, 3);
	lv_obj_set_style_bg_opa(glyph, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(glyph, 0, 0);
	lv_obj_clear_flag(glyph, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(glyph, LV_OBJ_FLAG_CLICKABLE);

	// Label pinned at the bottom of the card. pixelbasic7 in warm cream
	// so it reads cleanly against the dim purple card background.
	auto* lbl = lv_label_create(root);
	lv_obj_set_style_text_font(lbl, &pixelbasic7, 0);
	lv_obj_set_style_text_color(lbl, MP_TEXT, 0);
	lv_label_set_text(lbl, "");
	lv_obj_set_align(lbl, LV_ALIGN_BOTTOM_MID);
	lv_obj_set_y(lbl, -3);

	cards.push_back({ root, glyph, lbl });
}

void PhoneGamesScreen::paintGlyph(lv_obj_t* g, uint8_t gameIndex) {
	switch(gameIndex) {
		case 0: { // SPACE - chunky asteroid + thin laser streak
			px(g,  3,  6, 7, 4, MP_LABEL_DIM);
			px(g,  2,  9, 10, 6, MP_LABEL_DIM);
			px(g,  4, 14, 8, 2, MP_LABEL_DIM);
			px(g,  6,  4, 4, 2, MP_LABEL_DIM);
			px(g, 14, 12, 4, 2, MP_HIGHLIGHT);
			px(g, 19, 10, 4, 2, MP_HIGHLIGHT);
			px(g, 24,  8, 3, 2, MP_HIGHLIGHT);
			break;
		}
		case 1: { // INVADERZ - classic tri-row pixel alien
			px(g,  6,  3, 2, 2, MP_HIGHLIGHT);
			px(g, 22,  3, 2, 2, MP_HIGHLIGHT);
			px(g,  8,  5, 14, 4, MP_HIGHLIGHT);
			px(g, 11,  6, 2, 2, MP_BG_DARK);
			px(g, 17,  6, 2, 2, MP_BG_DARK);
			px(g,  6,  9, 18, 4, MP_HIGHLIGHT);
			px(g,  6, 13, 3, 3, MP_HIGHLIGHT);
			px(g, 13, 13, 4, 3, MP_HIGHLIGHT);
			px(g, 21, 13, 3, 3, MP_HIGHLIGHT);
			break;
		}
		case 2: { // SNAKE - segmented body in classic green
			const lv_color_t snakeGreen = lv_color_make(140, 220, 110);
			px(g,  3,  4, 3, 3, snakeGreen);
			px(g,  6,  4, 3, 3, snakeGreen);
			px(g,  9,  4, 3, 3, snakeGreen);
			px(g, 12,  7, 3, 3, snakeGreen);
			px(g, 15, 10, 3, 3, snakeGreen);
			px(g, 18, 13, 3, 3, snakeGreen);
			px(g, 21, 13, 3, 3, snakeGreen);
			px(g, 24, 13, 3, 3, snakeGreen);
			px(g, 24,  4, 4, 4, snakeGreen);
			px(g, 26,  5, 1, 1, MP_BG_DARK);
			break;
		}
		case 3: { // BONK - left paddle | ball | right paddle
			px(g,  3,  4, 2, 14, MP_TEXT);
			px(g, 25,  4, 2, 14, MP_TEXT);
			px(g, 14,  4, 2, 2, MP_DIM);
			px(g, 14,  9, 2, 2, MP_DIM);
			px(g, 14, 14, 2, 2, MP_DIM);
			px(g, 19,  9, 3, 3, MP_ACCENT);
			break;
		}
		case 4: { // TETRIS - little stack of falling blocks
			// S71: a tiny T-piece floating above a pile of mixed blocks.
			// Colours match the in-game palette so the glyph "previews"
			// what the player is about to see when they hit PLAY.
			const lv_color_t cyan    = lv_color_make(122, 232, 255);
			const lv_color_t yellow  = lv_color_make(255, 220,  60);
			const lv_color_t magenta = lv_color_make(220, 130, 240);
			const lv_color_t green   = lv_color_make(120, 220, 110);
			const lv_color_t red     = lv_color_make(240,  90,  90);

			// Falling T at the top.
			px(g, 11,  3, 8, 3, magenta);
			px(g, 14,  6, 2, 3, magenta);

			// Settled stack at the bottom (six 4x3 cells in two rows).
			px(g,  3, 12, 4, 3, green);
			px(g,  7, 12, 4, 3, yellow);
			px(g, 11, 12, 4, 3, cyan);
			px(g, 15, 12, 4, 3, red);
			px(g, 19, 12, 4, 3, yellow);
			px(g, 23, 12, 4, 3, green);

			px(g,  3, 15, 4, 3, cyan);
			px(g,  7, 15, 4, 3, magenta);
			px(g, 11, 15, 4, 3, red);
			px(g, 15, 15, 4, 3, green);
			px(g, 19, 15, 4, 3, cyan);
			px(g, 23, 15, 4, 3, yellow);
			break;
		}
		case 5: { // BOUNCE - cyan ball arcing over a chunky platform
			// S73: a single bouncy ball mid-arc with motion lines, sitting
			// above a low platform so the silhouette reads as "physics game"
			// even at 30x22 px. The cyan-on-orange combo matches the actual
			// in-game ball + goal flag, so the glyph foreshadows the screen.
			const lv_color_t cyan   = lv_color_make(122, 232, 255);
			const lv_color_t orange = lv_color_make(255, 140,  30);
			const lv_color_t cream  = lv_color_make(255, 220, 180);

			// Two ground tiles + one raised step on the right.
			px(g,  3, 16, 8, 4, MP_DIM);
			px(g, 11, 16, 8, 4, MP_DIM);
			px(g, 19, 12, 8, 8, MP_DIM);

			// Ball mid-arc.
			px(g, 12,  6, 5, 5, cyan);
			px(g, 13,  7, 3, 3, cream);

			// Motion-trail dots heading up-right toward the step.
			px(g,  8, 11, 2, 2, orange);
			px(g, 19,  9, 2, 2, orange);
			break;
		}
		case 6: { // BRICK - colourful brick stack + paddle + ball
			// S75: a five-row Breakout-coloured wall sat above a thin
			// paddle with the ball mid-flight. The colours match the
			// in-game brick palette, so the glyph foreshadows the screen.
			const lv_color_t red    = lv_color_make(240,  90,  90);
			const lv_color_t orange = lv_color_make(255, 140,  30);
			const lv_color_t yellow = lv_color_make(255, 220,  60);
			const lv_color_t green  = lv_color_make(120, 220, 110);
			const lv_color_t cyan   = lv_color_make(122, 232, 255);

			// Five 24x2 brick rows (compressed to fit the 30x22 glyph).
			px(g,  3,  3, 24, 2, red);
			px(g,  3,  6, 24, 2, orange);
			px(g,  3,  9, 24, 2, yellow);
			px(g,  3, 12, 24, 2, green);
			px(g,  3, 15, 24, 2, cyan);

			// Ball mid-flight + paddle catch-strip below the brick wall.
			px(g, 13, 18, 2, 2, MP_TEXT);
			px(g,  8, 19, 14, 1, MP_TEXT);
			break;
		}
		case 7: { // BANTUMI - two pit rows + a store on each side
			// S76: a stylised Mancala board glyph -- two rows of three pits
			// with a tall store on each end. Cyan stones in the player's
			// pits/store + dim stones in the CPU's so the side asymmetry
			// reads even at 30x22 px.
			const lv_color_t cyan   = lv_color_make(122, 232, 255);
			const lv_color_t purple = lv_color_make(170, 140, 200);

			// CPU store (left) + Player store (right) frames.
			px(g,  2,  4, 3, 14, MP_DIM);
			px(g, 25,  4, 3, 14, MP_ACCENT);

			// CPU pit row (top) -- three small pit cells with dim stones.
			px(g,  7,  4, 5, 6, MP_DIM);
			px(g, 13,  4, 5, 6, MP_DIM);
			px(g, 19,  4, 5, 6, MP_DIM);
			px(g,  9,  6, 1, 2, purple);
			px(g, 15,  6, 1, 2, purple);
			px(g, 21,  6, 1, 2, purple);

			// Player pit row (bottom) -- cyan stones, ride the accent line.
			px(g,  7, 12, 5, 6, MP_DIM);
			px(g, 13, 12, 5, 6, MP_DIM);
			px(g, 19, 12, 5, 6, MP_DIM);
			px(g,  9, 14, 1, 2, cyan);
			px(g, 15, 14, 1, 2, cyan);
			px(g, 21, 14, 1, 2, cyan);

			// A couple of stones tucked in the player's store for flavour.
			px(g, 26,  8, 1, 1, cyan);
			px(g, 26, 12, 1, 1, cyan);
			break;
		}
		case 8: { // BUBBLES - cluster of three matching cyan bubbles + two strays
			// S77: a 3x3 cluster of round bubbles where the middle row
			// reads as a "match-3 line" (cyan trio) with two off-colour
			// outliers above. Foreshadows the swap-to-match mechanic.
			const lv_color_t cyan    = lv_color_make(122, 232, 255);
			const lv_color_t red     = lv_color_make(240,  90,  90);
			const lv_color_t yellow  = lv_color_make(255, 220,  60);
			const lv_color_t magenta = lv_color_make(220, 130, 240);

			// Top row: red + yellow strays.
			px(g,  4,  3, 6, 5, red);
			px(g, 12,  3, 6, 5, yellow);

			// Middle row: a cyan match-3 trio.
			px(g,  4, 10, 6, 5, cyan);
			px(g, 12, 10, 6, 5, cyan);
			px(g, 20, 10, 6, 5, cyan);

			// Bottom row: a magenta lone + cyan tail-end stray.
			px(g,  4, 17, 6, 4, magenta);
			px(g, 20, 17, 6, 4, cyan);

			// Tiny cream highlight pip on the centre cyan bubble so the
			// glyph reads as glossy spheres rather than flat squares.
			px(g, 14, 11, 1, 1, MP_TEXT);
			break;
		}
		case 9: { // MINES - peek of the Minesweeper field
			// S79: a stylised glance of three cells across two rows. The
			// top row reads as "opened-3", "flagged", "still-hidden"; the
			// bottom row finishes the scene with an opened blank, an
			// opened "1", and the struck mine. Reads as Minesweeper at a
			// glance without faking literal mine pixels at 26 px wide.
			const lv_color_t cyan   = lv_color_make(122, 232, 255);
			const lv_color_t orange = lv_color_make(255, 140,  30);
			const lv_color_t cream  = lv_color_make(255, 220, 180);
			const lv_color_t panel  = lv_color_make( 70,  56, 100);
			const lv_color_t deep   = lv_color_make( 20,  12,  36);

			// Top row.
			px(g,  4,  4, 7, 7, deep);
			px(g,  6,  5, 3, 1, cyan);
			px(g,  8,  6, 1, 1, cyan);
			px(g,  6,  7, 3, 1, cyan);
			px(g,  8,  8, 1, 1, cyan);
			px(g,  6,  9, 3, 1, cyan);

			px(g, 12,  4, 7, 7, panel);
			px(g, 14,  6, 1, 4, orange);
			px(g, 15,  6, 2, 2, orange);

			px(g, 20,  4, 7, 7, panel);
			px(g, 22,  6, 3, 3, MP_DIM);

			// Bottom row.
			px(g,  4, 12, 7, 7, deep);

			px(g, 12, 12, 7, 7, deep);
			px(g, 14, 13, 1, 1, cyan);
			px(g, 15, 13, 1, 5, cyan);

			px(g, 20, 12, 7, 7, orange);
			px(g, 22, 14, 3, 3, deep);
			px(g, 23, 13, 1, 1, cream);
			px(g, 23, 17, 1, 1, cream);
			px(g, 21, 15, 1, 1, cream);
			px(g, 25, 15, 1, 1, cream);
			break;
		}
		case 10: { // SLIDE15 - mini 3x3 grid with one tile out of place
			// S80: a stylised glance of a 3-row sliding-puzzle board.
			// Rows of three cells with a single empty slot to telegraph
			// the swap-into-blank mechanic. We tint two solved-position
			// tiles cyan + the rest cream so the "in place vs out of
			// place" cue from the real game reads at icon scale.
			const lv_color_t cyan   = lv_color_make(122, 232, 255);
			const lv_color_t cream  = lv_color_make(255, 220, 180);

			// Row 1: three "in-place" tiles in cyan.
			px(g,  4,  4, 6, 5, MP_DIM);
			px(g,  6,  5, 1, 3, cyan);
			px(g, 11,  4, 6, 5, MP_DIM);
			px(g, 13,  5, 2, 3, cyan);
			px(g, 18,  4, 6, 5, MP_DIM);
			px(g, 20,  5, 2, 3, cyan);

			// Row 2: two cream tiles + one accent (the highlighted /
			// movable tile) sitting next to the blank slot.
			px(g,  4, 10, 6, 5, MP_DIM);
			px(g,  6, 11, 2, 3, cream);
			px(g, 11, 10, 6, 5, MP_DIM);
			px(g, 13, 11, 1, 3, cream);
			px(g, 18, 10, 6, 5, MP_ACCENT);

			// Row 3: cream tile + cream tile + blank (no rectangle).
			px(g,  4, 16, 6, 5, MP_DIM);
			px(g,  6, 17, 2, 3, cream);
			px(g, 11, 16, 6, 5, MP_DIM);
			px(g, 13, 17, 2, 3, cream);
			// No row3 col3: that's the blank slot.
			break;
		}
		default:
			break;
	}
}

void PhoneGamesScreen::paintSlot(uint8_t slotIndex, int8_t gameIndex) {
	if(slotIndex >= cards.size()) return;
	Card& card = cards[slotIndex];

	// Wipe any previous glyph children so successive page flips do not
	// leave stale rectangles behind. lv_obj_clean() recursively deletes
	// all children of the glyph parent (LVGL 8.x).
	if(card.glyph != nullptr) {
		lv_obj_clean(card.glyph);
	}

	if(gameIndex < 0 || static_cast<uint8_t>(gameIndex) >= kGameCount) {
		// Empty slot - hide the card root so the user does not see a
		// disabled-looking ghost panel.
		if(card.root != nullptr) {
			lv_obj_add_flag(card.root, LV_OBJ_FLAG_HIDDEN);
		}
		if(card.label != nullptr) {
			lv_label_set_text(card.label, "");
		}
		return;
	}

	if(card.root != nullptr) {
		lv_obj_clear_flag(card.root, LV_OBJ_FLAG_HIDDEN);
	}
	if(card.label != nullptr) {
		lv_label_set_text(card.label, Games[gameIndex].name);
	}
	if(card.glyph != nullptr) {
		paintGlyph(card.glyph, static_cast<uint8_t>(gameIndex));
	}
}

void PhoneGamesScreen::repaintCards() {
	for(uint8_t i = 0; i < SlotsPerPage; ++i) {
		paintSlot(i, gameIndexFor(i));
	}
}

void PhoneGamesScreen::applySelection(uint8_t prev, uint8_t curr) {
	// Previously-selected card returns to dim border + dimmed background.
	// Newly-selected card gets the sunset-orange border + slightly
	// brighter background. Border width stays at 1 px in both states so
	// the card geometry does not jiggle on selection change.
	if(prev < cards.size()) {
		lv_obj_set_style_border_color(cards[prev].root, MP_DIM, 0);
		lv_obj_set_style_bg_opa(cards[prev].root, LV_OPA_70, 0);
	}
	if(curr < cards.size()) {
		lv_obj_set_style_border_color(cards[curr].root, MP_ACCENT, 0);
		lv_obj_set_style_bg_opa(cards[curr].root, LV_OPA_90, 0);
		lv_obj_move_foreground(cards[curr].root);
	}
}

void PhoneGamesScreen::moveCursor(int8_t dx, int8_t dy) {
	uint8_t col = cursor % 2;
	uint8_t row = cursor / 2;
	if(dx != 0) col = (col + 1) % 2;
	if(dy != 0) row = (row + 1) % 2;
	uint8_t next = static_cast<uint8_t>(row * 2 + col);

	// Clamp to a slot that actually has a game on the current page. If
	// the chosen slot is empty (last page partially full), drop back to
	// the highest valid slot on this page.
	while(next > 0 && gameIndexFor(next) < 0) --next;
	if(next == cursor) return;
	const uint8_t prev = cursor;
	cursor = next;
	applySelection(prev, cursor);
}

void PhoneGamesScreen::changePage(int8_t dir) {
	const uint8_t pages = pageCount();
	if(pages <= 1) return;
	const int next = (static_cast<int>(currentPage) + dir + pages) % pages;
	currentPage = static_cast<uint8_t>(next);

	// Clamp the cursor to a valid slot on the new page.
	uint8_t maxSlot = 0;
	for(uint8_t i = 0; i < SlotsPerPage; ++i) {
		if(gameIndexFor(i) >= 0) maxSlot = i;
	}
	const uint8_t prev = cursor;
	if(cursor > maxSlot) cursor = maxSlot;

	repaintCards();
	applySelection(prev, cursor);
	refreshPageLabel();
}

void PhoneGamesScreen::refreshPageLabel() {
	if(pageLabel == nullptr) return;
	const uint8_t pages = pageCount();
	if(pages <= 1) {
		lv_obj_add_flag(pageLabel, LV_OBJ_FLAG_HIDDEN);
		return;
	}
	lv_obj_clear_flag(pageLabel, LV_OBJ_FLAG_HIDDEN);
	char buf[8];
	snprintf(buf, sizeof(buf), "%u/%u",
	         static_cast<unsigned>(currentPage + 1),
	         static_cast<unsigned>(pages));
	lv_label_set_text(pageLabel, buf);
}

int8_t PhoneGamesScreen::gameIndexFor(uint8_t slotIndex) const {
	const uint16_t idx = static_cast<uint16_t>(currentPage) * SlotsPerPage
	                     + static_cast<uint16_t>(slotIndex);
	if(idx >= kGameCount) return -1;
	return static_cast<int8_t>(idx);
}

uint8_t PhoneGamesScreen::pageCount() const {
	return static_cast<uint8_t>((kGameCount + SlotsPerPage - 1) / SlotsPerPage);
}

void PhoneGamesScreen::launchSelected() {
	const int8_t gameIdx = gameIndexFor(cursor);
	if(gameIdx < 0) return;   // empty slot - ignore launch

	if(softKeys) softKeys->flashLeft();

	if(Games[gameIdx].kind == GameKind::Screen) {
		// LVScreen-style launch: PhoneTetris (S71) and every Phase-N
		// successor. Just push() the screen with the standard slide
		// transition. The pushed screen is responsible for its own
		// lifecycle and will pop() back to us on BACK.
		switch(gameIdx) {
			case 4: push(new PhoneTetris()); break;
			case 5: push(new PhoneBounce()); break;
			case 6: push(new PhoneBrickBreaker()); break;
			case 7: push(new PhoneBantumi()); break;
			case 8: push(new PhoneBubbleSmile()); break;
			case 9: push(new PhoneMinesweeper()); break;
			case 10: push(new PhoneSlidingPuzzle()); break;
			default:
				// Should never happen unless somebody added a Screen
				// entry to Games[] without wiring it here. Be loud
				// about it in serial so the next session catches it.
				printf("PhoneGamesScreen: no Screen-launcher for game %d\n",
				       static_cast<int>(gameIdx));
				break;
		}
		return;
	}

	// Engine-style launch (legacy Game / GameSystem path). Replicate the
	// sequence the original GamesScreen / S65 PhoneGamesScreen already
	// proved out (S65 lifted Game's host pointer to LVScreen* so we can
	// pass `this`). The deferred LoopManager call ensures we are out of
	// the LVGL event chain before unloading the LVGL cache and starting
	// the game's own render loop.
	const uint8_t idx = static_cast<uint8_t>(gameIdx);
	auto* host = this;
	stop();
	LoopManager::defer([idx, host](uint32_t /*dt*/){
		FSLVGL::unloadCache();

		uint32_t splashStart = 0;
		if(Games[idx].splash){
			auto display = Chatter.getDisplay();
			display->getBaseSprite()->drawIcon(SPIFFS.open(Games[idx].splash),
			                                   0, 0, 160, 128);
			display->commit();
			splashStart = millis();
		}

		Game* game = nullptr;
		switch(idx){
			case 0: game = new SpaceRocks(host); break;
			case 1: game = new SpaceInvaders::SpaceInvaders(host); break;
			case 2: game = new Snake::Snake(host); break;
			case 3: game = new Bonk::Bonk(host); break;
			default: break;
		}
		if(game == nullptr) return;

		game->load();
		while(!game->isLoaded()){
			delay(1);
		}

		while(splashStart != 0 && millis() - splashStart < 2000){
			delay(10);
		}

		game->start();
	});
}

void PhoneGamesScreen::buttonPressed(uint i) {
	switch(i){
		case BTN_LEFT:
		case BTN_4:
			moveCursor(-1, 0);
			break;

		case BTN_RIGHT:
		case BTN_6:
			moveCursor(+1, 0);
			break;

		case BTN_2:
			moveCursor(0, -1);
			break;

		case BTN_8:
			moveCursor(0, +1);
			break;

		// S71: bumpers paginate. Wraps in both directions so the user
		// does not have to backtrack to reach the next page.
		case BTN_L:
			changePage(-1);
			break;

		case BTN_R:
			changePage(+1);
			break;

		case BTN_ENTER:
			launchSelected();
			break;

		case BTN_BACK:
			if(softKeys) softKeys->flashRight();
			pop();
			break;

		default:
			break;
	}
}
