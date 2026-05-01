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
 * PhoneGamesScreen (S65)
 *
 * Phone-styled launcher for the four built-in games (Space Rocks,
 * Invaderz, Snake, Bonk). Replaces the legacy ListItem-based
 * `GamesScreen` for the MAKERphone main-menu Games tile - the legacy
 * screen still exists and is used by the legacy carousel `MainMenu`,
 * so this is purely an additive re-skin for the phone-style flow.
 *
 *   +------------------------------------+
 *   | ||||      12:34            ##### | <- PhoneStatusBar
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
 * Implementation notes:
 *  - 100% code-only - every card is composed from a handful of plain
 *    `lv_obj` rectangles, the same primitive every other Phone* widget
 *    uses. No SPIFFS asset cost (the data partition is precious).
 *  - Cursor is a flat 0..3 index that maps onto a 2x2 grid; navigation
 *    is the standard MAKERphone keypad chord (LEFT/RIGHT/UP/DOWN, plus
 *    the 4/6/2/8 numpad equivalents). Wraps in both axes.
 *  - The selected card gets a sunset-orange border + a faintly brighter
 *    background. We deliberately do NOT introduce a new pulsing halo -
 *    the cards are already lively enough with their pixel-art glyphs,
 *    and a second source of motion next to the synthwave wallpaper
 *    starts to feel busy on the 160x128 panel.
 *  - SELECT (`BTN_ENTER`) launches the focused game. Launch reuses the
 *    sequence the legacy `GamesScreen` already proved out (unload cache
 *    -> optional splash blit -> new Game(this) -> load -> wait -> start),
 *    just adapted to use `this` (a phone-styled LVScreen) as the host
 *    so the Game engine restarts THIS screen on game pop instead of
 *    bouncing back to the legacy list view. This works because the
 *    same-session refactor of `Game` lifted its host pointer from
 *    `GamesScreen*` to `LVScreen*`.
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

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;
	lv_obj_t*         titleLabel;

	struct Card {
		lv_obj_t*   root;     // 70x44 container with border + glyph + label
		lv_obj_t*   glyph;    // glyph parent (centered top of card)
		lv_obj_t*   label;    // pixelbasic7 caption pinned to card bottom
	};

	// Four cards in a 2x2 grid - flat index 0..3:
	//   0 = top-left  (Space Rocks)
	//   1 = top-right (Invaderz)
	//   2 = bot-left  (Snake)
	//   3 = bot-right (Bonk)
	std::vector<Card> cards;
	uint8_t           cursor = 0;

	// Layout constants - hard-coded so the screen does not subtly drift
	// when LVGL re-flows things behind us. 160x128 panel, status/soft-key
	// bars 10 px each, title label 10 px tall, cards 2x2 with 4 px gaps.
	static constexpr lv_coord_t CardW    = 70;
	static constexpr lv_coord_t CardH    = 44;
	static constexpr lv_coord_t CardGapX = 4;
	static constexpr lv_coord_t CardGapY = 4;
	static constexpr lv_coord_t GridX    = (160 - (2 * CardW + CardGapX)) / 2; // = 6
	static constexpr lv_coord_t GridY    = 23; // under status bar + title

	void buildCard(uint8_t index, const char* label);
	void buildGlyph(uint8_t index);
	void applySelection(uint8_t prev, uint8_t curr);
	void moveCursor(int8_t dx, int8_t dy);
	void launchSelected();

	// Mirror the GameInfo table from GamesScreen so we can drop it into
	// the launch sequence without taking a dependency on the legacy
	// screen's class. Each entry pairs a label with a launch lambda; the
	// optional splash path is rendered for ~2s before the game starts,
	// preserving the boot-feel the legacy screen already had.
	struct GameInfo {
		const char* name;
		const char* splash;
	};
	static const GameInfo Games[4];

	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONEGAMESSCREEN_H
