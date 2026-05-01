#ifndef MAKERPHONE_PHONESOKOBAN_H
#define MAKERPHONE_PHONESOKOBAN_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneSokoban (S83)
 *
 * Phase-N arcade entry: the classic warehouse-keeper push puzzler.
 * The player walks a forklift around a tile-grid, pushing crates onto
 * marked goal pads. A level is solved when every crate sits on a goal.
 *
 *   +------------------------------------+
 *   | ||||  12:34                  ##### | <- PhoneStatusBar  (10 px)
 *   | LVL 1/5  MOVES 042  PUSHES 012   | <- HUD strip       (12 px)
 *   |                                    |
 *   |        ##########                  |
 *   |        #........#                  |
 *   |        #.@..o..*#                  |    <- 12x9 max grid,
 *   |        #........#                  |       8x8 px tiles, total
 *   |        #..o.....#                  |       96x72 px, centred.
 *   |        ##########                  |
 *   |                                    |
 *   |   PUSH                BACK         | <- PhoneSoftKeyBar (10 px)
 *   +------------------------------------+
 *
 * Controls:
 *   - BTN_2 / BTN_8       : move up / down
 *   - BTN_4 / BTN_6       : move left / right
 *   - BTN_LEFT / BTN_RIGHT: alias for left / right
 *   - BTN_5 / BTN_ENTER   : (no-op while playing - moves are direction
 *                           keys; on Won this advances to next level)
 *   - BTN_R               : reset current level (un-pushes everything)
 *   - BTN_L               : undo last move
 *   - BTN_BACK (B)        : pop back to PhoneGamesScreen
 *
 * State machine:
 *   Playing  -- normal gameplay. Player walks; pushes a crate if the
 *               tile in the move direction holds a crate AND the tile
 *               beyond that is empty (floor or goal).
 *   Won      -- every crate on a goal in this level. Overlay shows
 *               the per-level move/push counts. BTN_5 / BTN_ENTER
 *               advances to the next level. After the final level,
 *               an "ALL CLEAR" overlay appears and BTN_5 cycles back
 *               to level 1 for replay.
 *
 * Implementation notes:
 *   - 100% code-only: every tile is a plain `lv_obj` rectangle. Crates
 *     and goals are 8x8 px each, walls are flat dark blocks, the
 *     player is a small cyan "@" with eyes. No SPIFFS asset cost.
 *   - Five hand-built levels live in the .cpp as static const arrays
 *     of strings. Tile glyphs follow the de-facto Sokoban convention:
 *       '#' wall, '.' floor, ' ' outside (rendered as nothing),
 *       '@' player on floor, '+' player on goal, 'o' crate on floor,
 *       '*' crate on goal, '*' or 'O' crate on goal interchangeable,
 *       '_' goal pad on floor (rendered as a faint marker).
 *   - The level grid is at most 12 cols x 9 rows so the playfield fits
 *     comfortably inside the 96 px-wide x 96 px-high band between the
 *     HUD strip and the soft-key bar at 8 px tile size.
 *   - Per-level "best moves" lives in RAM only and resets on screen
 *     pop, matching every other Phase-N game in the v1.0 roadmap.
 *   - Undo keeps the last UndoCap moves on a tiny ring buffer; pushing
 *     past the cap drops the oldest entry (the cap is small enough
 *     that the player still feels they can recover from a typo, but
 *     not so deep that the buffer's RAM cost is worth worrying about).
 */
class PhoneSokoban : public LVScreen, private InputListener {
public:
	PhoneSokoban();
	virtual ~PhoneSokoban() override;

	void onStart() override;
	void onStop() override;

	// Layout - 160x128 panel, status/soft-key bars 10 px each.
	static constexpr lv_coord_t StatusBarH = 10;
	static constexpr lv_coord_t SoftKeyH   = 10;
	static constexpr lv_coord_t HudY       = 10;
	static constexpr lv_coord_t HudH       = 12;

	// Grid: at most 12 cols x 9 rows of 8 px tiles -> 96 x 72 px max
	// playfield. The actual level may be smaller, in which case it is
	// centred horizontally + vertically inside the play band.
	static constexpr uint8_t  GridCols  = 12;
	static constexpr uint8_t  GridRows  = 9;
	static constexpr uint16_t CellCount = GridCols * GridRows; // 108
	static constexpr lv_coord_t TilePx  = 8;

	// Vertical play band: from y = HudY+HudH (=22) to y = 128-SoftKeyH-1
	// (=117) -> 96 px. The grid is placed inside it centred.
	static constexpr lv_coord_t FieldYTop = StatusBarH + HudH;       // 22
	static constexpr lv_coord_t FieldYBot = 128 - SoftKeyH;          // 118
	static constexpr lv_coord_t FieldH    = FieldYBot - FieldYTop;   // 96

	// 5 hand-built levels.
	static constexpr uint8_t LevelCount = 5;

	// Undo ring-buffer cap.
	static constexpr uint8_t UndoCap = 32;

	// Tile kinds the engine cares about. We keep walls and floors in a
	// static "background" plane and crates / player in a "dynamic"
	// plane on top, so a push only needs to repaint the two affected
	// dynamic sprites (cheap on LVGL).
	enum Tile : uint8_t {
		TileOutside = 0,
		TileWall    = 1,
		TileFloor   = 2,
		TileGoal    = 3,
	};

private:
	// ---- LVGL node graph ----------------------------------------------
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* hudLevelLabel  = nullptr;
	lv_obj_t* hudMovesLabel  = nullptr;
	lv_obj_t* hudPushesLabel = nullptr;
	lv_obj_t* overlayLabel   = nullptr;

	// One sprite per cell for the static layer (wall/floor/goal).
	// Crates and the player are tracked separately as a single sprite
	// each that we re-position on move. CellCount sprites is 108 nodes
	// but most of them are "outside" cells we hide entirely, which
	// keeps the active LVGL node count modest.
	lv_obj_t* staticSprites[CellCount];

	// Dynamic layer: a tile-sized sprite per crate, one for the player.
	// Crate sprites are reused across levels (we just hide the surplus
	// when the next level uses fewer crates than the previous one).
	static constexpr uint8_t MaxCrates = 12;
	lv_obj_t* crateSprites[MaxCrates];
	lv_obj_t* playerSprite = nullptr;

	// ---- game state ---------------------------------------------------
	enum class GameState : uint8_t {
		Playing,
		Won,
		AllClear,
	};
	GameState state = GameState::Playing;

	// Active level index 0..LevelCount-1.
	uint8_t levelIndex = 0;

	// Per-cell static tile.
	Tile tiles[CellCount];

	// Per-cell dynamic occupant: crate index + 1, or 0 for "no crate".
	// Player position is tracked in playerCol / playerRow.
	uint8_t crateAt[CellCount];

	// Crate positions (col,row) packed into a single byte each:
	// (row << 4) | col, valid because GridCols <= 15 and GridRows <= 15.
	uint8_t cratePos[MaxCrates];
	uint8_t crateCount = 0;

	uint8_t playerCol = 0;
	uint8_t playerRow = 0;

	// Origin offsets so the level renders centred inside the play band.
	uint8_t levelCols = 0;
	uint8_t levelRows = 0;
	lv_coord_t levelOriginX = 0;
	lv_coord_t levelOriginY = 0;

	// Counters.
	uint16_t moves       = 0;
	uint16_t pushes      = 0;
	uint16_t bestMoves[LevelCount];   // 0 = no record yet

	// Undo ring buffer: each entry captures the data we need to revert
	// a single move. dir = direction we moved (0..3 = U/D/L/R).
	struct UndoEntry {
		uint8_t prevPlayerCol;
		uint8_t prevPlayerRow;
		uint8_t crateFrom;        // (row<<4)|col, 0xFF if no crate moved
		uint8_t crateTo;          // (row<<4)|col, 0xFF if no crate moved
	};
	UndoEntry undoBuf[UndoCap];
	uint8_t   undoHead  = 0;   // index where the next push will write
	uint8_t   undoCount = 0;   // valid entries in [head-undoCount .. head)

	// ---- build helpers ------------------------------------------------
	void buildHud();
	void buildOverlay();
	void buildStaticGrid();
	void buildDynamicLayer();

	// ---- state transitions --------------------------------------------
	void loadLevel(uint8_t idx);
	void resetCurrentLevel();
	void tryMove(int8_t dCol, int8_t dRow);
	void undoLast();
	bool isLevelSolved() const;
	void winLevel();
	void advanceLevel();

	// ---- helpers ------------------------------------------------------
	uint16_t indexOf(uint8_t col, uint8_t row) const {
		return static_cast<uint16_t>(row * GridCols + col);
	}
	bool inBounds(int8_t col, int8_t row) const {
		return col >= 0 && row >= 0
		    && col < static_cast<int8_t>(GridCols)
		    && row < static_cast<int8_t>(GridRows);
	}
	uint8_t crateIndexAt(uint8_t col, uint8_t row) const;

	// ---- rendering ----------------------------------------------------
	void renderStaticGrid();
	void renderCrate(uint8_t i);
	void renderAllCrates();
	void renderPlayer();
	void refreshHud();
	void refreshSoftKeys();
	void refreshOverlay();

	// ---- input --------------------------------------------------------
	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONESOKOBAN_H
