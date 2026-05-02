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
#include "PhoneTicTacToe.h"
#include "PhoneMemoryMatch.h"
#include "PhoneSokoban.h"
#include "PhonePinball.h"
#include "PhoneHangman.h"
#include "PhoneConnectFour.h"
#include "PhoneReversi.h"
#include "PhoneWhackAMole.h"
#include "PhoneLunarLander.h"
#include "PhoneHelicopter.h"
#include "Phone2048.h"
#include "PhoneSolitaire.h"
#include "PhoneSudoku.h"
#include "PhoneWordle.h"
#include "PhoneSimon.h"
#include "PhoneSnakesLadders.h"
#include "PhoneAirHockey.h"

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
	{ "TICTAC",   nullptr,                   GameKind::Screen },  // S81
	{ "MEMORY",   nullptr,                   GameKind::Screen },  // S82
	{ "SOKOBAN",  nullptr,                   GameKind::Screen },  // S83
	{ "PINBALL",  nullptr,                   GameKind::Screen },  // S85
	{ "HANGMAN",  nullptr,                   GameKind::Screen },  // S87
	{ "CONNECT4", nullptr,                   GameKind::Screen },  // S88
	{ "REVERSI",  nullptr,                   GameKind::Screen },  // S89
	{ "WHACK",    nullptr,                   GameKind::Screen },  // S90
	{ "LANDER",   nullptr,                   GameKind::Screen },  // S91
	{ "COPTER",   nullptr,                   GameKind::Screen },  // S92
	{ "2048",     nullptr,                   GameKind::Screen },  // S93
	{ "SOLITAIR", nullptr,                   GameKind::Screen },  // S94
	{ "SUDOKU",   nullptr,                   GameKind::Screen },  // S95
	{ "WORDLE",   nullptr,                   GameKind::Screen },  // S96
	{ "SIMON",    nullptr,                   GameKind::Screen },  // S97
	{ "S&L",     nullptr,                   GameKind::Screen },  // S98
	{ "AIRHOCK",  nullptr,                   GameKind::Screen },  // S99
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
		case 11: { // TICTAC - mini 3x3 board with X / O in play
			// S81: a stylised glance of the noughts-and-crosses board.
			// Three thin grid bars + a cyan X (player) in the centre and
			// an orange O (CPU) in the top-right cell so the colour
			// language matches the in-game palette without us drawing a
			// full match.
			const lv_color_t cyan   = lv_color_make(122, 232, 255);
			const lv_color_t orange = lv_color_make(255, 140,  30);

			// Two vertical grid bars + two horizontal grid bars carving
			// the 30x22 glyph into a 3x3 layout (cells are ~9x6 each).
			px(g, 12,  3, 1, 17, MP_LABEL_DIM);
			px(g, 19,  3, 1, 17, MP_LABEL_DIM);
			px(g,  3,  9, 25, 1, MP_LABEL_DIM);
			px(g,  3, 15, 25, 1, MP_LABEL_DIM);

			// Cyan X in the centre cell (rows 9-15, cols 12-19).
			px(g, 14, 10, 1, 1, cyan);
			px(g, 15, 11, 1, 1, cyan);
			px(g, 16, 12, 1, 1, cyan);
			px(g, 17, 13, 1, 1, cyan);
			px(g, 17, 11, 1, 1, cyan);
			px(g, 14, 13, 1, 1, cyan);
			px(g, 16, 12, 1, 1, cyan);

			// Orange O in the top-right cell (rows 3-9, cols 19-30).
			px(g, 22,  4, 4, 1, orange);
			px(g, 22,  8, 4, 1, orange);
			px(g, 21,  5, 1, 3, orange);
			px(g, 26,  5, 1, 3, orange);

			// Player X in the bottom-left (rows 15-22, cols 3-12) -- a
			// small cross of three pixels diagonally each way.
			px(g,  5, 16, 1, 1, cyan);
			px(g,  6, 17, 1, 1, cyan);
			px(g,  7, 18, 1, 1, cyan);
			px(g,  9, 16, 1, 1, cyan);
			px(g,  8, 17, 1, 1, cyan);
			break;
		}
		case 12: { // MEMORY - 3x2 mini-grid of face-down cards + a heart pip
			// S82: a stylised glance of a memory-match board. Three
			// face-down cards in the top row + one face-up card showing
			// a tiny red heart pip + two more face-down cards finish
			// the grid. The colour split (purple backs vs orange-bordered
			// face-up) telegraphs "concentration / pairs" at icon scale.
			const lv_color_t cyan   = lv_color_make(122, 232, 255);
			const lv_color_t cream  = lv_color_make(255, 220, 180);
			const lv_color_t red    = lv_color_make(240,  90,  90);
			const lv_color_t orange = lv_color_make(255, 140,  30);

			// Row 1 (face-down): three small cards with cyan dot pips.
			px(g,  3,  3, 7, 7, MP_DIM);
			px(g,  6,  6, 1, 1, cyan);
			px(g, 11,  3, 7, 7, MP_DIM);
			px(g, 14,  6, 1, 1, cyan);
			px(g, 19,  3, 7, 7, MP_DIM);
			px(g, 22,  6, 1, 1, cyan);

			// Row 2: a face-up card (orange border) showing a heart,
			// then two more face-down cards. Border is faked as four
			// 1 px lines so we don't pay a real lv_obj border style.
			px(g,  3, 12, 7, 7, MP_BG_DARK);
			px(g,  3, 12, 7, 1, orange);
			px(g,  3, 18, 7, 1, orange);
			px(g,  3, 12, 1, 7, orange);
			px(g,  9, 12, 1, 7, orange);
			px(g,  4, 14, 2, 1, red);
			px(g,  7, 14, 2, 1, red);
			px(g,  4, 15, 5, 1, red);
			px(g,  5, 16, 3, 1, red);
			px(g,  6, 17, 1, 1, red);

			px(g, 11, 12, 7, 7, MP_DIM);
			px(g, 14, 15, 1, 1, cyan);
			px(g, 19, 12, 7, 7, MP_DIM);
			px(g, 22, 15, 1, 1, cyan);

			// Tiny cream pip floating off the top-right corner so the
			// "you just made a match" feel reads at icon scale.
			px(g, 26,  2, 1, 1, cream);
			break;
		}
		case 13: { // SOKOBAN - tiny crate + goal pad + warehouse worker
			// S83: a stylised glance of the warehouse-keeper game. A
			// cyan worker on the left, an orange-trimmed cream crate
			// in the middle, and a faint "goal pad" outline on the
			// right -- the same colour language used inside the game.
			const lv_color_t cyan   = lv_color_make(122, 232, 255);
			const lv_color_t cream  = lv_color_make(255, 220, 180);
			const lv_color_t orange = lv_color_make(255, 140,  30);

			// Floor strip across the bottom so the scene reads as a
			// warehouse aisle rather than three floating sprites.
			px(g,  3, 18, 24, 1, MP_DIM);

			// Cyan worker (head + body) on the left.
			px(g,  4,  6, 5, 5, cyan);
			px(g,  5,  8, 1, 1, MP_BG_DARK);
			px(g,  7,  8, 1, 1, MP_BG_DARK);
			px(g,  4, 12, 5, 5, cyan);

			// Cream crate in the middle, orange-trimmed.
			px(g, 12,  9, 8, 8, cream);
			px(g, 12,  9, 8, 1, orange);
			px(g, 12, 16, 8, 1, orange);
			px(g, 12,  9, 1, 8, orange);
			px(g, 19,  9, 1, 8, orange);

			// Goal pad outline on the right (faint orange square).
			px(g, 23, 11, 4, 1, orange);
			px(g, 23, 14, 4, 1, orange);
			px(g, 23, 11, 1, 4, orange);
			px(g, 26, 11, 1, 4, orange);
			break;
		}
		case 14: { // PINBALL - bumper trio + flippers below
			// S85: a stylised glance of the single-table pinball game.
			// Three orange bumpers sit above a pair of cream flipper bars
			// flanking a tiny drain gap. Cyan ball mid-flight between the
			// upper bumpers. Reads as pinball at icon scale without us
			// drawing literal table apron geometry.
			const lv_color_t cyan   = lv_color_make(122, 232, 255);
			const lv_color_t cream  = lv_color_make(255, 220, 180);
			const lv_color_t orange = lv_color_make(255, 140,  30);

			// Three bumpers across the top half (a triangular cluster).
			px(g,  6,  4, 5, 5, orange);
			px(g,  7,  5, 3, 3, cream);
			px(g, 19,  4, 5, 5, orange);
			px(g, 20,  5, 3, 3, cream);
			px(g, 12, 10, 5, 5, orange);
			px(g, 13, 11, 3, 3, cream);

			// Cyan ball mid-flight between the upper-left bumper and the
			// centre bumper.
			px(g, 13,  6, 2, 2, cyan);

			// Two cream flipper bars near the bottom -- diagonal hint via
			// staircase-stacked rectangles. Drain gap between them.
			px(g,  4, 17, 2, 2, cream);
			px(g,  6, 16, 2, 2, cream);
			px(g,  8, 15, 2, 2, cream);
			px(g, 24, 17, 2, 2, cream);
			px(g, 22, 16, 2, 2, cream);
			px(g, 20, 15, 2, 2, cream);
			break;
		}
		case 15: { // HANGMAN - tiny gallows + stick figure + word slot
			// S87: a stylised glance of the word-guess game. A short
			// gallows post + crossbeam on the left, a hung stick figure
			// hanging from the rope, and a row of dashes representing
			// the word slot on the right. Cyan dashes match the cyan
			// "in-progress reveal" colour the screen uses.
			const lv_coord_t centerX = 9;
			const lv_color_t cream  = lv_color_make(255, 220, 180);
			const lv_color_t purple = lv_color_make(170, 140, 200);
			const lv_color_t cyan   = lv_color_make(122, 232, 255);

			// Gallows base + post + crossbeam + rope.
			px(g,  3, 18, 10, 1, purple);
			px(g,  6,  4,  1, 14, purple);
			px(g,  6,  4,  6,  1, purple);
			px(g, 11,  5,  1,  2, purple);

			// Stick figure -- head, body, two arms, two legs.
			px(g, centerX + 1,  7, 3, 3, cream);
			px(g, centerX + 2, 10, 1, 5, cream);
			px(g, centerX,     11, 2, 1, cream);
			px(g, centerX + 3, 11, 2, 1, cream);
			px(g, centerX,     16, 2, 1, cream);
			px(g, centerX + 3, 16, 2, 1, cream);

			// Word slot dashes on the right -- five short bars to
			// telegraph "5-letter word, two letters revealed in cyan".
			px(g, 16, 12, 2, 1, cyan);
			px(g, 19, 12, 2, 1, purple);
			px(g, 22, 12, 2, 1, cyan);
			px(g, 25, 12, 2, 1, purple);
			px(g, 16, 16, 2, 1, purple);
			break;
		}
		case 16: { // CONNECT4 - 7-column grid silhouette with one cyan disc
		           // and one orange disc telegraphing "you vs cpu drop".
			// S88: a stylised glance of the 7x6 board. Three rows of
			// five faint dim-purple dots sketch the empty grid; one
			// cyan disc sits low-left for the player and one orange
			// disc sits one row up to suggest the back-and-forth drop.
			const lv_color_t purple = lv_color_make(70, 56, 100);
			const lv_color_t cyan   = lv_color_make(122, 232, 255);
			const lv_color_t orange = lv_color_make(255, 140, 30);

			// 5x3 dot grid (skipping the two cells we'll fill with
			// real discs below). 4 px pitch, top-left at (4, 6).
			for(uint8_t r = 0; r < 3; ++r) {
				for(uint8_t c = 0; c < 5; ++c) {
					const uint8_t dx = static_cast<uint8_t>(4 + c * 4);
					const uint8_t dy = static_cast<uint8_t>(6 + r * 4);
					px(g, dx, dy, 2, 2, purple);
				}
			}

			// Player disc: bottom-left of the dot grid (r=2, c=1).
			px(g,  7, 13, 3, 3, cyan);
			// CPU disc: above the player (r=1, c=1).
			px(g,  7,  9, 3, 3, orange);

			// A faint "tray" line under the bottom row, hinting at the
			// gravity floor that holds dropped discs in place.
			px(g,  3, 17, 22, 1, purple);
			break;
		}
		case 17: { // REVERSI - mini board silhouette with cyan + orange discs
		           // arranged like the Othello opening cluster.
			// S89: a stylised 4x3 dot grid representing the empty
			// board, with a 2x2 cluster of alternating cyan/orange
			// discs in the centre to telegraph the standard Reversi
			// opening. A single cyan disc sits adjacent to the cluster
			// to suggest the player's next legal placement.
			const lv_color_t purple = lv_color_make(70, 56, 100);
			const lv_color_t cyan   = lv_color_make(122, 232, 255);
			const lv_color_t orange = lv_color_make(255, 140, 30);

			// Faint "felt" backdrop band so the discs read against a
			// board, not against the wallpaper. 24 px wide / 14 tall
			// matches the dot grid below.
			px(g,  3,  4, 24, 14, purple);

			// 6x4 dot grid covering the backdrop. 4 px pitch starting
			// at (4, 5) -- this leaves a 1 px frame on every side of
			// the felt and gives the discs a recognisable Othello-grid
			// feel even at 30x22 px.
			for(uint8_t r = 0; r < 4; ++r) {
				for(uint8_t c = 0; c < 6; ++c) {
					const uint8_t dx = static_cast<uint8_t>(4 + c * 4);
					const uint8_t dy = static_cast<uint8_t>(5 + r * 3);
					px(g, dx, dy, 2, 2, MP_BG_DARK);
				}
			}

			// Centre 2x2 opening cluster -- alternating colours on the
			// diagonal, exactly the standard Reversi opening.
			px(g, 12,  8, 3, 3, orange);
			px(g, 16,  8, 3, 3, cyan);
			px(g, 12, 12, 3, 3, cyan);
			px(g, 16, 12, 3, 3, orange);

			// Player's pending move: a faint cyan disc to the right of
			// the cluster, telegraphing "your next placement here".
			px(g, 20, 10, 3, 3, cyan);
			break;
		}
		case 18: { // WHACK - 3x3 hole grid with one cyan mole popping out
		           // and a sunset hammer hovering over the centre cell.
			// S90: a stylised glance of the dialer-key reaction game.
			// Three rows of three faint dim-purple holes sketch the
			// 1-9 grid; one cyan disc rises out of the top-right hole
			// and a small orange "hammer" (square + handle) hovers
			// over the centre so the silhouette reads as "whack a
			// mole" even at 30x22 px.
			const lv_color_t purple = lv_color_make(70, 56, 100);
			const lv_color_t cyan   = lv_color_make(122, 232, 255);
			const lv_color_t orange = lv_color_make(255, 140, 30);

			// 3x3 hole grid (4 px pitch, top-left at (4, 4)).
			for(uint8_t r = 0; r < 3; ++r) {
				for(uint8_t c = 0; c < 3; ++c) {
					const uint8_t dx = static_cast<uint8_t>(4 + c * 8);
					const uint8_t dy = static_cast<uint8_t>(4 + r * 6);
					px(g, dx, dy, 4, 2, purple);
				}
			}

			// Cyan mole rising out of the top-right hole.
			px(g, 20,  3, 4, 3, cyan);

			// Orange hammer head + handle hovering over the centre.
			px(g, 13, 10, 5, 3, orange);
			px(g, 15, 13, 1, 5, orange);
			break;
		}
		case 19: { // LANDER - tiny lander craft, jagged surface, cyan flame
			// S91: stylised glance of the lunar-lander game. A small
			// cream lander silhouette sits in the upper-right with a
			// short orange flame trailing beneath it; a jagged cyan
			// horizon plus a single sunset-orange flat pad anchors the
			// scene. Reads as "spacecraft over rough terrain" at 30x22.
			const lv_color_t cyan   = lv_color_make(122, 232, 255);
			const lv_color_t cream  = lv_color_make(255, 220, 180);
			const lv_color_t orange = lv_color_make(255, 140,  30);
			const lv_color_t flame  = lv_color_make(255, 200,  80);

			// Jagged terrain silhouette across the bottom: pixel slabs
			// at varying heights so the surface reads as broken rock.
			px(g,  0, 18, 3,  4, cyan);
			px(g,  3, 16, 3,  6, cyan);
			px(g,  6, 19, 2,  3, cyan);
			px(g,  8, 17, 3,  5, cyan);
			px(g, 11, 20, 4,  2, cyan);   // dip with the flat pad
			px(g, 11, 20, 4,  1, orange); // pad highlight stripe
			px(g, 15, 18, 3,  4, cyan);
			px(g, 18, 16, 2,  6, cyan);
			px(g, 20, 19, 3,  3, cyan);
			px(g, 23, 17, 4,  5, cyan);
			px(g, 27, 20, 3,  2, cyan);

			// Lander body: 4x3 cream rectangle with a 2x1 orange leg
			// strip and a single cyan dome pixel. Sits in the upper
			// right so the silhouette reads as "descending toward the
			// pad below".
			px(g, 19,  4, 5, 3, cream);
			px(g, 20,  3, 3, 1, cyan);
			px(g, 19,  7, 5, 1, orange);
			px(g, 19,  8, 1, 2, orange);
			px(g, 23,  8, 1, 2, orange);

			// Trailing flame between the legs.
			px(g, 21,  9, 1, 2, flame);
			break;
		}
		case 20: { // COPTER - tiny helicopter banking over a pillar gap
			// S92: stylised glance of the helicopter game. A small
			// cream chopper silhouette banks across the upper-left,
			// while a pair of cyan pillars flank an open gap on the
			// right. Reads as "fly through the gap" at 30x22.
			const lv_color_t cyan   = lv_color_make(122, 232, 255);
			const lv_color_t cream  = lv_color_make(255, 220, 180);
			const lv_color_t orange = lv_color_make(255, 140,  30);
			const lv_color_t purple = lv_color_make( 70,  56, 100);

			// Ceiling + floor accent bands so the silhouette reads
			// as a tunnel.
			px(g,  0,  2, 30, 1, cyan);
			px(g,  0, 19, 30, 1, orange);

			// A pair of pillars on the right with a gap in the middle.
			// Top half of pillar.
			px(g, 16,  3, 4, 6, purple);
			px(g, 16,  3, 4, 1, cyan);
			// Bottom half of pillar (gap is rows 9..13).
			px(g, 16, 14, 4, 5, purple);
			px(g, 16, 18, 4, 1, orange);

			// Second pillar farther right (just the silhouette tip).
			px(g, 25,  3, 3, 4, purple);
			px(g, 25, 16, 3, 3, purple);

			// Helicopter body banking through the gap on the left.
			// Body block (cream).
			px(g,  4,  9, 5, 3, cream);
			// Cockpit dome (cyan).
			px(g,  5, 10, 1, 1, cyan);
			// Tail boom + tail rotor.
			px(g,  9, 10, 3, 1, cream);
			px(g, 12,  9, 1, 3, orange);
			// Main rotor blur (cyan, slightly wider than body).
			px(g,  3,  8, 7, 1, cyan);
			// Skids.
			px(g,  4, 12, 5, 1, orange);
			break;
		}
		case 21: { // 2048 - stacked pixel tiles labelled with chunky digits
			// S93: a stylised glance of the merge-tile puzzle. Four 7x6
			// "tiles" arranged 2x2, each filled in a value-appropriate
			// shade so the silhouette reads as "colourful tiles". The
			// gold tile in the bottom-right hints at the 2048 prize the
			// player is chasing.
			const lv_color_t cyan   = lv_color_make(122, 232, 255);
			const lv_color_t green  = lv_color_make(140, 220, 100);
			const lv_color_t orange = lv_color_make(255, 140,  30);
			const lv_color_t gold   = lv_color_make(255, 215,  80);
			const lv_color_t bgDeep = lv_color_make( 20,  12,  36);

			// 2x2 mini-board of tiles + 1 px gaps. Each tile is 7x6 px.
			// Top-left "2" tile (cyan).
			px(g,  6,  4, 7, 6, cyan);
			px(g,  8,  6, 3, 1, bgDeep);
			px(g, 10,  7, 1, 1, bgDeep);
			px(g,  8,  8, 3, 1, bgDeep);
			// Top-right "8" tile (green).
			px(g, 14,  4, 7, 6, green);
			// Bottom-left "16" tile (orange).
			px(g,  6, 11, 7, 6, orange);
			// Bottom-right "2048" tile (gold) - the prize.
			px(g, 14, 11, 7, 6, gold);
			// Single gold-pixel sparkle to draw the eye to the prize.
			px(g, 22,  9, 2, 2, gold);
			break;
		}
		case 22: { // SOLITAIRE - tiny stack of three fanned playing cards
		           // with a heart pip + spade pip; foreshadows the deck.
			// S94: a stylised glance of the Klondike screen. Three
			// cream "cards" fanned slightly so the silhouette reads
			// as a hand of cards. The top card shows a red heart pip
			// + the rank glyph "A" (cyan), foreshadowing the Ace
			// promotion mechanic. A dim purple deck-back peeks out
			// behind the fan to suggest the stock pile.
			const lv_color_t cyan   = lv_color_make(122, 232, 255);
			const lv_color_t cream  = lv_color_make(245, 230, 200);
			const lv_color_t red    = lv_color_make(220,  60,  60);

			// Stock back peeking out top-right.
			px(g, 19,  3, 8, 8, MP_DIM);
			px(g, 22,  6, 2, 2, cyan);

			// Bottom card of the fan (left-most), tilted left.
			px(g,  3,  8, 8, 11, cream);
			px(g,  4, 13, 1, 1, red);

			// Middle card of the fan, slightly higher.
			px(g,  9,  6, 8, 11, cream);
			px(g, 10, 11, 1, 1, red);

			// Top card of the fan, highest. Cream with red heart pip
			// + cyan "A" stand-in.
			px(g, 15,  4, 8, 11, cream);
			// Heart pip (5 pixels in a small heart silhouette).
			px(g, 16,  6, 1, 1, red);
			px(g, 18,  6, 1, 1, red);
			px(g, 16,  7, 3, 1, red);
			px(g, 17,  8, 1, 1, red);
			// Cyan ace mark in the bottom-right corner of the top card.
			px(g, 20, 12, 2, 2, cyan);
			break;
		}
		case 23: { // SUDOKU - mini 3x3 box of digits + a focused cell
		           // telegraphs "fill the board" puzzle gameplay.
			// S95: a stylised mini sudoku tile. We paint a single
			// 3x3 sub-box at the centre of the glyph: dim purple
			// grid lines, two cyan "given" pixel digits, one orange
			// player-placed digit, and a sunset-orange focused cell
			// outline so the silhouette reads as "sudoku" at icon
			// scale without us trying to render real glyphs in 30x22.
			const lv_color_t cyan   = lv_color_make(122, 232, 255);
			const lv_color_t orange = lv_color_make(255, 140,  30);
			const lv_color_t purple = lv_color_make( 70,  56, 100);

			// Outer frame.
			px(g,  6,  4, 19,  1, cyan);
			px(g,  6, 18, 19,  1, cyan);
			px(g,  6,  4,  1, 15, cyan);
			px(g, 24,  4,  1, 15, cyan);

			// Two interior grid lines carving the area into a 3x3
			// of small cells.
			px(g, 12,  4,  1, 15, purple);
			px(g, 18,  4,  1, 15, purple);
			px(g,  6,  9, 19,  1, purple);
			px(g,  6, 14, 19,  1, purple);

			// Three "given" digits as cyan pips dotted into cells.
			px(g,  8,  6,  3,  2, cyan);  // top-left cell
			px(g, 20,  6,  3,  2, cyan);  // top-right cell
			px(g,  8, 16,  3,  2, cyan);  // bottom-left cell

			// One player-placed digit (orange) in the middle row.
			px(g, 14, 11,  3,  2, orange);

			// Focused-cell highlight - sunset-orange border on the
			// centre-right cell to telegraph the cursor.
			px(g, 19, 10,  5,  1, orange);
			px(g, 19, 14,  5,  1, orange);
			px(g, 19, 10,  1,  5, orange);
			px(g, 23, 10,  1,  5, orange);
			break;
		}
		case 24: { // WORDLE - five-letter row of mixed-status tiles
		           // telegraphs the daily-guess colour-feedback game.
			// S96: a stylised glance of one Wordle row -- a green hit
			// (correct slot), a yellow present (right letter, wrong
			// slot), a grey miss, plus two empty cells with a thin
			// orange focus border on the left-most empty slot to
			// telegraph "your cursor is here". Reads as Wordle even at
			// 30x22 px without us drawing real glyphs in pixel-art.
			const lv_color_t green  = lv_color_make(120, 200, 110);
			const lv_color_t yellow = lv_color_make(230, 200,  70);
			const lv_color_t grey   = lv_color_make( 70,  56, 100);
			const lv_color_t orange = lv_color_make(255, 140,  30);
			const lv_color_t cream  = lv_color_make(255, 220, 180);

			// Top row of five tiles (mid-game guess).
			px(g,  3,  4, 4, 5, green);
			px(g,  8,  4, 4, 5, yellow);
			px(g, 13,  4, 4, 5, grey);
			px(g, 18,  4, 4, 5, MP_BG_DARK);
			// orange focus border on the left empty cell.
			px(g, 18,  4, 4, 1, orange);
			px(g, 18,  8, 4, 1, orange);
			px(g, 18,  4, 1, 5, orange);
			px(g, 21,  4, 1, 5, orange);
			px(g, 23,  4, 4, 5, MP_BG_DARK);

			// Middle row -- two more tiles (one fresh hit + one yellow).
			px(g,  3, 11, 4, 5, MP_BG_DARK);
			px(g,  8, 11, 4, 5, MP_BG_DARK);
			px(g, 13, 11, 4, 5, green);
			px(g, 18, 11, 4, 5, MP_BG_DARK);
			px(g, 23, 11, 4, 5, yellow);

			// Bottom row -- five empty tiles (faintly outlined).
			for(uint8_t c = 0; c < 5; ++c) {
				const uint8_t x = static_cast<uint8_t>(3 + c * 5);
				px(g, x, 18, 4, 1, grey);
			}

			// One cream pixel highlight on the centre green tile so the
			// silhouette reads as glossy plastic rather than flat blocks.
			px(g, 14, 12, 1, 1, cream);
			break;
		}
		case 25: { // SIMON - 2x2 cluster of coloured pads, top-right pad lit
			// S97: stylised glance of the memory + buzzer-tone game.
			// A 2x2 cluster of pads (cyan, orange, magenta, yellow)
			// matches the in-game palette; the orange pad in the top
			// right gets a brighter wash + a small cream highlight pip
			// to telegraph "this pad is currently lit, mirror it".
			const lv_color_t cyanDim    = lv_color_make( 35,  90, 110);
			const lv_color_t orangeLit  = lv_color_make(255, 160,  50);
			const lv_color_t magentaDim = lv_color_make( 80,  30,  90);
			const lv_color_t yellowDim  = lv_color_make(100,  90,  20);
			const lv_color_t cream      = lv_color_make(255, 220, 180);

			// Top-left pad: cyan (dim).
			px(g,  3,  3, 11, 8, cyanDim);
			// Top-right pad: orange (lit) -- the "press me" cue.
			px(g, 16,  3, 11, 8, orangeLit);
			// Cream highlight pip on the lit pad so it reads as glossy.
			px(g, 19,  5,  2, 2, cream);
			// Bottom-left pad: magenta (dim).
			px(g,  3, 12, 11, 8, magentaDim);
			// Bottom-right pad: yellow (dim).
			px(g, 16, 12, 11, 8, yellowDim);
			break;
		}
		case 26: { // S&L - tiny board grid with a snake + ladder overlay
			// S98: a stylised glance of Snakes & Ladders -- a faint
			// 4x4 grid hint, a green ladder rail rising on the left,
			// and a red snake squiggle dropping on the right. Colours
			// match the in-game palette so the glyph foreshadows the
			// screen.
			const lv_color_t green  = lv_color_make(120, 220, 110);
			const lv_color_t red    = lv_color_make(240,  90,  90);
			const lv_color_t cream  = lv_color_make(255, 220, 180);

			// Faint 4x4 grid lines (bottom of card).
			px(g,  4,  4, 22, 1, MP_DIM);
			px(g,  4,  9, 22, 1, MP_DIM);
			px(g,  4, 14, 22, 1, MP_DIM);
			px(g,  4, 19, 22, 1, MP_DIM);
			px(g,  4,  4,  1, 16, MP_DIM);
			px(g, 25,  4,  1, 16, MP_DIM);

			// Ladder on the left -- two rails + three rungs.
			px(g,  6,  6, 1, 13, green);
			px(g,  9,  6, 1, 13, green);
			px(g,  6,  8, 4, 1, green);
			px(g,  6, 12, 4, 1, green);
			px(g,  6, 16, 4, 1, green);

			// Snake on the right -- a stair-step descent.
			px(g, 22,  6, 3, 2, red);
			px(g, 22,  8, 1, 3, red);
			px(g, 19, 11, 3, 2, red);
			px(g, 19, 13, 1, 3, red);
			px(g, 16, 16, 3, 2, red);
			// Snake "head" highlight.
			px(g, 23,  5, 2, 1, cream);
			break;
		}
		case 27: { // AIRHOCK -- vertical rink with two mallets + a puck
			// S99: a stylised glance of an air-hockey table -- a faint
			// rink frame, a centre line slicing the field in two, and
			// the cyan player mallet at the bottom facing the orange
			// CPU mallet at the top with a cream puck between them.
			const lv_color_t cream  = lv_color_make(255, 220, 180);
			const lv_color_t cyan   = lv_color_make(122, 232, 255);
			const lv_color_t orange = lv_color_make(255, 140,  30);

			// Rink outline: 22x18 hollow rectangle slightly inset from
			// the glyph's 30x22 canvas.
			px(g,  4,  3, 22, 1, MP_DIM);    // top wall (with goal hint)
			px(g,  4, 20, 22, 1, MP_DIM);    // bottom wall
			px(g,  4,  4,  1, 16, MP_DIM);   // left wall
			px(g, 25,  4,  1, 16, MP_DIM);   // right wall

			// Goal mouth highlights -- erase the centre of the top +
			// bottom walls so they read as goals rather than a closed
			// box. Orange (CPU goal) at top, cyan (player goal) at bottom.
			px(g, 11,  3,  8, 1, orange);
			px(g, 11, 20,  8, 1, cyan);

			// Centre line + centre dot.
			px(g,  5, 11,  9, 1, MP_LABEL_DIM);
			px(g, 16, 11,  9, 1, MP_LABEL_DIM);
			px(g, 14, 11,  2, 1, cream);

			// CPU mallet (top half) -- a 5x5 orange disc, picked apart
			// from a hollow circle so it reads as a mallet ring.
			px(g, 13,  6,  4, 1, orange);
			px(g, 12,  7,  6, 3, orange);
			px(g, 13, 10,  4, 1, orange);

			// Player mallet (bottom half) -- 5x5 cyan disc.
			px(g, 13, 13,  4, 1, cyan);
			px(g, 12, 14,  6, 3, cyan);
			px(g, 13, 17,  4, 1, cyan);

			// Puck -- a 2x2 cream square sitting just above the player
			// mallet so it reads as "in flight toward CPU".
			px(g, 18, 12,  2, 2, cream);
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
			case 11: push(new PhoneTicTacToe()); break;
			case 12: push(new PhoneMemoryMatch()); break;
			case 13: push(new PhoneSokoban()); break;
			case 14: push(new PhonePinball()); break;
			case 15: push(new PhoneHangman()); break;
			case 16: push(new PhoneConnectFour()); break;
			case 17: push(new PhoneReversi()); break;
			case 18: push(new PhoneWhackAMole()); break;
			case 19: push(new PhoneLunarLander()); break;
			case 20: push(new PhoneHelicopter()); break;
			case 21: push(new Phone2048()); break;
			case 22: push(new PhoneSolitaire()); break;
			case 23: push(new PhoneSudoku()); break;
			case 24: push(new PhoneWordle()); break;
			case 25: push(new PhoneSimon()); break;
			case 26: push(new PhoneSnakesLadders()); break;
			case 27: push(new PhoneAirHockey()); break;
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
