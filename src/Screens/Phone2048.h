#ifndef MAKERPHONE_PHONE2048_H
#define MAKERPHONE_PHONE2048_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * Phone2048 (S93)
 *
 * Phase-N arcade entry: the classic 4x4 "merge to 2048" tile puzzle
 * popularised by Gabriele Cirulli's 2014 web game. The player slides
 * the entire board up / down / left / right; every tile travels as far
 * as it can in that direction and pairs of equal tiles fuse into a
 * single tile of double their value. After every successful move (one
 * that actually relocated or merged at least one tile) a fresh "2"
 * (or, 10% of the time, a "4") spawns in a random empty cell. Reaching
 * 2048 wins the round; running out of legal moves ends the round.
 *
 *   +------------------------------------+
 *   | ||||  12:34                  ##### | <- PhoneStatusBar  (10 px)
 *   | SCORE 0420         BEST 02048      | <- HUD strip       (12 px)
 *   |        +------+------+------+------+
 *   |        |      |  2   |      |   4  |
 *   |        +------+------+------+------+
 *   |        |  4   |      |  16  |   8  |
 *   |        +------+------+------+------+    <- 4x4 board, 24x24 px
 *   |        |      |  64  |  32  |      |       cells, total 96x96
 *   |        +------+------+------+------+
 *   |        |  2   | 128  |      |  64  |
 *   |        +------+------+------+------+
 *   |   SLIDE              BACK          | <- PhoneSoftKeyBar (10 px)
 *   +------------------------------------+
 *
 * Controls:
 *   - BTN_LEFT / BTN_RIGHT : slide whole board left / right
 *   - BTN_2  / BTN_8       : slide whole board up / down (numpad north/south)
 *   - BTN_4  / BTN_6       : alias for left / right
 *   - BTN_R                : restart (cheap "I give up" reset)
 *   - BTN_ENTER (A)        : restart when in Won / GameOver overlay;
 *                            no-op while playing (the dialler-pad
 *                            slide buttons are the four direction keys)
 *   - BTN_BACK (B)         : pop back to PhoneGamesScreen
 *
 * State machine:
 *   Playing  -- normal slide / merge gameplay.
 *   Won      -- the player just produced a 2048 tile for the first time.
 *               An overlay congratulates them; pressing A continues
 *               playing (the win flag is sticky for the rest of the
 *               round so the overlay does not pop up again on every
 *               additional 2048 merge).
 *   GameOver -- the board is full and no merges are possible. Overlay
 *               shows final score + the player's best across the
 *               session; pressing A starts a fresh round.
 *
 * Implementation notes:
 *   - 100% code-only -- every cell is a plain `lv_obj` rectangle with a
 *     single label inside it. No SPIFFS asset cost.
 *   - The "Continue after winning" affordance is intentional. Many
 *     2048 implementations get this wrong and force the player back to
 *     a menu after the first 2048; we keep playing so the player can
 *     chase 4096 / 8192. The Won overlay is a one-shot celebration.
 *   - Tile colours scale with the exponent so the player can read the
 *     board at a glance: low values are dim purples, mid values are
 *     cyan / green, high values are sunset orange and gold. This is
 *     deliberately quieter than the popular pastel-and-red palette
 *     because it has to share the screen with the synthwave wallpaper.
 *   - Tile values up to 2 digits use the larger pixelbasic16 font, 3+
 *     digit values fall back to pixelbasic7 so they fit inside the
 *     24 px cell. The transition is automatic per-tile.
 *   - Score / best are kept in-memory only -- matches every other
 *     Phone* game in the v1.0 roadmap. Surviving a reboot is a future
 *     persistence-layer concern.
 */
class Phone2048 : public LVScreen, private InputListener {
public:
	Phone2048();
	virtual ~Phone2048() override;

	void onStart() override;
	void onStop() override;

	// Screen layout - mirrors PhoneSlidingPuzzle so the two grid games
	// feel like siblings on the 160x128 panel.
	static constexpr lv_coord_t StatusBarH = 10;
	static constexpr lv_coord_t SoftKeyH   = 10;
	static constexpr lv_coord_t HudY       = 10;
	static constexpr lv_coord_t HudH       = 12;

	static constexpr uint8_t   BoardSize = 4;
	static constexpr uint8_t   CellCount = BoardSize * BoardSize;
	static constexpr lv_coord_t TilePx   = 24;
	static constexpr lv_coord_t BoardPx  = TilePx * BoardSize;        // 96
	static constexpr lv_coord_t BoardOriginX = (160 - BoardPx) / 2;   // 32
	static constexpr lv_coord_t BoardOriginY = StatusBarH + HudH;     // 22

	static constexpr uint16_t WinValue = 2048;

private:
	// ---- LVGL node graph ----------------------------------------------
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* hudScoreLabel = nullptr;
	lv_obj_t* hudBestLabel  = nullptr;
	lv_obj_t* overlayLabel  = nullptr;

	// One sprite + label per board cell. Index = row * 4 + col.
	lv_obj_t* cellSprites[CellCount];
	lv_obj_t* cellLabels[CellCount];

	// ---- game state ---------------------------------------------------
	enum class GameState : uint8_t {
		Playing,
		Won,
		GameOver,
	};
	GameState state = GameState::Playing;

	// board[i] = tile value (0 means empty, otherwise a power of two).
	uint16_t board[CellCount];

	uint32_t score      = 0;
	uint32_t bestScore  = 0;   // 0 = no record yet
	bool     winSticky  = false;  // true once the player has been shown
	                              // the Won overlay this round

	// ---- build helpers ------------------------------------------------
	void buildHud();
	void buildOverlay();
	void buildCells();

	// ---- state transitions --------------------------------------------
	void newGame();
	void spawnRandomTile();
	bool slide(int8_t dx, int8_t dy);   // returns true if anything moved
	bool canMove() const;
	void winRound();      // first 2048 produced this round
	void gameOver();      // board full + no merges

	// ---- helpers ------------------------------------------------------
	uint8_t indexOf(uint8_t col, uint8_t row) const {
		return static_cast<uint8_t>(row * BoardSize + col);
	}

	// ---- rendering ----------------------------------------------------
	void renderAllTiles();
	void renderTile(uint8_t slot);
	void refreshHud();
	void refreshSoftKeys();
	void refreshOverlay();

	// Pick the sprite background colour for a given tile value. Pure
	// function -- empty cells use the dim-purple "well" colour.
	static lv_color_t colorForValue(uint16_t value);
	// Pick the label text colour for a given tile value -- light tiles
	// pair with deep-purple text, dark tiles pair with warm cream text.
	static lv_color_t textColorForValue(uint16_t value);

	// ---- input --------------------------------------------------------
	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONE2048_H
