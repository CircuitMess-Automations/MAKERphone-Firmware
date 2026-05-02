#ifndef MAKERPHONE_PHONEGAMESSCREEN_H
#define MAKERPHONE_PHONEGAMESSCREEN_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include <vector>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneGamesScreen (S65, paginated for the Phase-N arcade in S71)
 *
 * Phone-styled launcher for the built-in games. Originally seeded
 * with the four engine games (Space Rocks, Invaderz, Snake, Bonk),
 * S71 introduces PhoneTetris -- the first of many LVScreen-style
 * games scheduled in the Phase-N "more games" arc. To make room
 * without breaking the existing 2x2 visual grid, the screen now
 * paginates: the visible 2x2 slot grid stays exactly as the user
 * remembers it, but L / R bumpers cycle through additional pages
 * of four games each.
 *
 *   +------------------------------------+
 *   | ||||  12:34         1/2     ##### | <- PhoneStatusBar + page hint
 *   |               GAMES                |
 *   |  +-----------+   +-----------+    |
 *   |  |   ##      |   |   <\\>    |    |
 *   |  |   ##      |   |  /  \\    |    |  <- 2x2 game cards
 *   |  | SPACE     |   |  INVADERZ |    |     each card = code-only
 *   |  +-----------+   +-----------+    |     pixel-art glyph + name
 *   |  +-----------+   +-----------+    |
 *   |  |  ~~~ <    |   |   |--|    |    |
 *   |  |   ~~~~    |   |  |    |   |    |
 *   |  |  SNAKE    |   |   BONK    |    |
 *   |  +-----------+   +-----------+    |
 *   |     PLAY               BACK       | <- PhoneSoftKeyBar
 *   +------------------------------------+
 *
 * Pagination semantics (S71):
 *   - SlotsPerPage stays at 4 so the existing 2x2 selection logic does
 *     not change. The cursor is always 0..3 within the current page.
 *   - L bumper / R bumper cycle pages with wrap. The L/R bumpers are
 *     unused on every other Phone* screen so the gesture is "free"
 *     muscle-memory-wise and avoids stealing keys we already use for
 *     selection (LEFT / RIGHT / UP / DOWN, plus 4 / 6 / 2 / 8).
 *   - A small `1/2` hint sits at the top-right of the title row so
 *     the player can see the carousel exists. It hides itself when
 *     there is only one page so the unaltered visual budget for the
 *     classic four-game launcher is preserved.
 *   - When a page is partially full (e.g. page 2 with one Tetris card
 *     after the four engine games on page 1), unused slots are
 *     hidden. The cursor clamps to the highest valid slot on the
 *     current page when paging in.
 *
 * Implementation notes:
 *  - 100% code-only - every card is composed from a handful of plain
 *    `lv_obj` rectangles, the same primitive every other Phone* widget
 *    uses. No SPIFFS asset cost (the data partition is precious).
 *  - The selected card gets a sunset-orange border + a faintly brighter
 *    background. We deliberately do NOT introduce a new pulsing halo -
 *    the cards are already lively enough with their pixel-art glyphs,
 *    and a second source of motion next to the synthwave wallpaper
 *    starts to feel busy on the 160x128 panel.
 *  - SELECT (`BTN_ENTER`) launches the focused game. Two launch
 *    flavours coexist: legacy Game-engine games keep their existing
 *    `unloadCache + optional splash + new Game(this) + load + start`
 *    sequence, and the new LVScreen-style games (PhoneTetris and
 *    every Phase-N successor) just `push()` themselves like any other
 *    phone screen. The dispatch is data-driven via GameInfo::kind.
 *  - BACK (`BTN_BACK`) pops the screen. We do not swallow the press or
 *    flash anything else - the soft-key bar takes care of the visual
 *    cue via `flashRight()`, matching every other phone-style screen.
 */
class PhoneGamesScreen : public LVScreen, private InputListener {
public:
	PhoneGamesScreen();
	virtual ~PhoneGamesScreen();

	void onStart() override;
	void onStop() override;

	// Public (kGameCount only) so the file-scope `Games` table in the
	// .cpp file can size itself off the same constant the class uses
	// internally without a friend declaration. Bump this whenever a
	// Phase-N session adds another game to the carousel.
	static constexpr uint8_t kGameCount = 18;

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;
	lv_obj_t*         titleLabel;
	lv_obj_t*         pageLabel = nullptr;   // S71: small "1/2" hint near title

	struct Card {
		lv_obj_t*   root;     // 70x44 container with border + glyph + label
		lv_obj_t*   glyph;    // glyph parent (centered top of card)
		lv_obj_t*   label;    // pixelbasic7 caption pinned to card bottom
	};

	// Four cards in a 2x2 grid - flat slot index 0..3:
	//   0 = top-left, 1 = top-right, 2 = bot-left, 3 = bot-right.
	// The cards are visual slots; the actual game shown in each slot
	// depends on the current page (slot index `i` shows game index
	// `currentPage * SlotsPerPage + i`).
	std::vector<Card> cards;
	uint8_t           cursor = 0;       // 0..3, slot within current page

	// S71: pagination state.
	static constexpr uint8_t SlotsPerPage = 4;
	uint8_t           currentPage = 0;

	// Layout constants - hard-coded so the screen does not subtly drift
	// when LVGL re-flows things behind us. 160x128 panel, status/soft-key
	// bars 10 px each, title label 10 px tall, cards 2x2 with 4 px gaps.
	static constexpr lv_coord_t CardW    = 70;
	static constexpr lv_coord_t CardH    = 44;
	static constexpr lv_coord_t CardGapX = 4;
	static constexpr lv_coord_t CardGapY = 4;
	static constexpr lv_coord_t GridX    = (160 - (2 * CardW + CardGapX)) / 2; // = 8
	static constexpr lv_coord_t GridY    = 23; // under status bar + title

	void buildCard(uint8_t slotIndex);  // slot frame + empty glyph + label
	// S71: paint a single slot with the chosen game (clears any previously
	// painted glyph children + rewrites the label). gameIndex < 0 hides
	// the slot entirely (used when the last page has fewer than 4 games).
	void paintSlot(uint8_t slotIndex, int8_t gameIndex);
	void paintGlyph(lv_obj_t* glyphParent, uint8_t gameIndex);
	void repaintCards();   // calls paintSlot for every visible slot

	void applySelection(uint8_t prev, uint8_t curr);
	void moveCursor(int8_t dx, int8_t dy);
	void changePage(int8_t dir);
	void refreshPageLabel();
	void launchSelected();

	// Compute the global game index for a card slot on the current page.
	// Returns -1 if the slot is past the end of the global game list.
	int8_t gameIndexFor(uint8_t slotIndex) const;
	// Total number of paginated pages, derived from kGameCount.
	uint8_t pageCount() const;

	// S71: each entry pairs a label with a launch flavour.
	enum class GameKind : uint8_t { Engine, Screen };
	struct GameInfo {
		const char*  name;
		const char*  splash;   // optional engine splash blit; nullptr = none
		GameKind     kind;     // Engine vs Screen launch path
	};
	static const GameInfo Games[kGameCount];

	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONEGAMESSCREEN_H
