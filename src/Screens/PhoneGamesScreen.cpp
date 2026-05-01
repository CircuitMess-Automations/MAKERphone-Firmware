#include "PhoneGamesScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <Chatter.h>
#include <SPIFFS.h>
#include <Loop/LoopManager.h>

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
// Mirrors the legacy GamesScreen list. The launch lambdas in
// `launchSelected()` build the right Game subclass per index, so this
// table is intentionally minimal (display name + splash path). Order
// matches the cursor index (0=Space, 1=Invaderz, 2=Snake, 3=Bonk).
const PhoneGamesScreen::GameInfo PhoneGamesScreen::Games[4] = {
	{ "SPACE",    "/Games/Space/splash.raw" },
	{ "INVADERZ", nullptr },
	{ "SNAKE",    nullptr },
	{ "BONK",     nullptr },
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
	// with the cyan clock face above it. Anchored manually because the
	// screen body has no flex layout.
	titleLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(titleLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(titleLabel, MP_ACCENT, 0);
	lv_label_set_text(titleLabel, "GAMES");
	lv_obj_set_align(titleLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(titleLabel, 12);

	// Build the 2x2 grid of cards. Constructor order matches the cursor
	// index so applySelection() can look the card up by index alone.
	cards.reserve(4);
	for(uint8_t i = 0; i < 4; i++){
		buildCard(i, Games[i].name);
		buildGlyph(i);
	}

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

void PhoneGamesScreen::buildCard(uint8_t index, const char* label) {
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

	// Position based on flat index - row 0 is i<2, row 1 is i>=2.
	const lv_coord_t col = index % 2;
	const lv_coord_t row = index / 2;
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
	// so it reads cleanly against the dim purple card background. The
	// label is wider than CardW would naturally allow because LVGL
	// labels expand to content; we centre-align manually using
	// LV_ALIGN_BOTTOM_MID.
	auto* lbl = lv_label_create(root);
	lv_obj_set_style_text_font(lbl, &pixelbasic7, 0);
	lv_obj_set_style_text_color(lbl, MP_TEXT, 0);
	lv_label_set_text(lbl, label);
	lv_obj_set_align(lbl, LV_ALIGN_BOTTOM_MID);
	lv_obj_set_y(lbl, -3);

	cards.push_back({ root, glyph, lbl });
}

void PhoneGamesScreen::buildGlyph(uint8_t index) {
	auto* g = cards[index].glyph;
	switch(index) {
		case 0: { // SPACE - chunky asteroid + thin laser streak
			// Asteroid silhouette (rough rock outline) at left-centre.
			px(g,  3,  6, 7, 4, MP_LABEL_DIM);
			px(g,  2,  9, 10, 6, MP_LABEL_DIM);
			px(g,  4, 14, 8, 2, MP_LABEL_DIM);
			px(g,  6,  4, 4, 2, MP_LABEL_DIM);
			// Laser streak (cyan diagonal).
			px(g, 14, 12, 4, 2, MP_HIGHLIGHT);
			px(g, 19, 10, 4, 2, MP_HIGHLIGHT);
			px(g, 24,  8, 3, 2, MP_HIGHLIGHT);
			break;
		}
		case 1: { // INVADERZ - classic tri-row pixel alien
			// Top "antennae".
			px(g,  6,  3, 2, 2, MP_HIGHLIGHT);
			px(g, 22,  3, 2, 2, MP_HIGHLIGHT);
			// Head.
			px(g,  8,  5, 14, 4, MP_HIGHLIGHT);
			// Eyes (cut-outs as accent dots).
			px(g, 11,  6, 2, 2, MP_BG_DARK);
			px(g, 17,  6, 2, 2, MP_BG_DARK);
			// Body bulge.
			px(g,  6,  9, 18, 4, MP_HIGHLIGHT);
			// Legs.
			px(g,  6, 13, 3, 3, MP_HIGHLIGHT);
			px(g, 13, 13, 4, 3, MP_HIGHLIGHT);
			px(g, 21, 13, 3, 3, MP_HIGHLIGHT);
			break;
		}
		case 2: { // SNAKE - segmented body in classic green
			const lv_color_t snakeGreen = lv_color_make(140, 220, 110);
			// Snake body segments forming an S-curve.
			px(g,  3,  4, 3, 3, snakeGreen);
			px(g,  6,  4, 3, 3, snakeGreen);
			px(g,  9,  4, 3, 3, snakeGreen);
			px(g, 12,  7, 3, 3, snakeGreen);
			px(g, 15, 10, 3, 3, snakeGreen);
			px(g, 18, 13, 3, 3, snakeGreen);
			px(g, 21, 13, 3, 3, snakeGreen);
			px(g, 24, 13, 3, 3, snakeGreen);
			// Head (slightly larger) with a single accent eye.
			px(g, 24,  4, 4, 4, snakeGreen);
			px(g, 26,  5, 1, 1, MP_BG_DARK);
			break;
		}
		case 3: { // BONK - left paddle | ball | right paddle
			// Left paddle.
			px(g,  3,  4, 2, 14, MP_TEXT);
			// Right paddle.
			px(g, 25,  4, 2, 14, MP_TEXT);
			// Centre dotted divider.
			px(g, 14,  4, 2, 2, MP_DIM);
			px(g, 14,  9, 2, 2, MP_DIM);
			px(g, 14, 14, 2, 2, MP_DIM);
			// Ball.
			px(g, 19,  9, 3, 3, MP_ACCENT);
			break;
		}
		default:
			break;
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
		// Bring the focused card to the foreground so its border draws
		// on top of any neighbour cards in case of overlap rounding.
		lv_obj_move_foreground(cards[curr].root);
	}
}

void PhoneGamesScreen::moveCursor(int8_t dx, int8_t dy) {
	// Decompose the flat 0..3 cursor onto the 2x2 grid, apply the delta
	// with wrap-around, and recompose. Two columns by two rows means
	// every move is either a column flip or a row flip - we do not need
	// the more general (dx, dy) arithmetic the grid widget does.
	uint8_t col = cursor % 2;
	uint8_t row = cursor / 2;
	if(dx > 0) col = (col + 1) % 2;
	if(dx < 0) col = (col + 1) % 2; // 2-col grid, +/-1 are the same
	if(dy > 0) row = (row + 1) % 2;
	if(dy < 0) row = (row + 1) % 2; // 2-row grid, +/-1 are the same
	const uint8_t next = static_cast<uint8_t>(row * 2 + col);
	if(next == cursor) return;
	const uint8_t prev = cursor;
	cursor = next;
	applySelection(prev, cursor);
}

void PhoneGamesScreen::launchSelected() {
	if(softKeys) softKeys->flashLeft();

	// Replicate the launch sequence the legacy GamesScreen already proved
	// out (S65 lifted Game's host pointer to LVScreen* so we can pass
	// `this`). The deferred LoopManager call ensures we are out of the
	// LVGL event chain before unloading the LVGL cache and starting the
	// game's own render loop. The Game ctor below grabs `this` as its
	// host; on game pop, the engine will `host->start()` us back.
	const uint8_t idx = cursor;
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

		// Hold the splash long enough to read - same 2 s window the
		// legacy screen used. For the games without a splash this loop
		// exits immediately because splashStart stayed at 0.
		while(splashStart != 0 && millis() - splashStart < 2000){
			delay(10);
		}

		game->start();
	});
}

void PhoneGamesScreen::buttonPressed(uint i) {
	// Same navigation chord every other phone-style screen uses - we
	// accept both the dedicated direction buttons and their numpad
	// equivalents so the user can drive the menu with either hand.
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
