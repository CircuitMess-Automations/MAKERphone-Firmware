#include "PhoneSokoban.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - identical to every other Phone* widget so
// the screen sits beside the rest of the Phase-N arcade entries without
// a visual seam.
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)  // dim purple captions

// ===========================================================================
// Hand-built levels
// ===========================================================================
//
// Level glyphs follow the canonical Sokoban convention:
//   '#' wall
//   ' ' outside (no tile)
//   '.' floor (no goal)
//   '_' goal pad (floor with a goal marker)
//   '@' player on floor
//   '+' player on goal
//   'o' crate on floor
//   '*' crate on goal
//
// Each row must be at most GridCols (12) characters; each level at
// most GridRows (9) rows. Shorter rows are right-padded with spaces
// during loadLevel().

namespace {

// Level 1 -- one crate, one goal. The "push it right twice" intro.
//   row width 7, height 5. Solvable in ~3 pushes.
const char* const kLevel1[] = {
	"#######",
	"#.....#",
	"#@.o._#",
	"#.....#",
	"#######",
};

// Level 2 -- two crates, two goals along the same lane. Teaches
// "push the FAR crate first or you'll block yourself".
//   row width 10, height 5. Solvable in ~6 pushes.
const char* const kLevel2[] = {
	"##########",
	"#........#",
	"#@.o.o.__#",
	"#........#",
	"##########",
};

// Level 3 -- three crates that must be pushed straight down onto
// goal pads beneath them. Teaches the "go around to the back of the
// crate" routing instinct.
//   row width 9, height 6. Solvable in ~6 pushes + walking.
const char* const kLevel3[] = {
	"#########",
	"#@......#",
	"#.o.o.o.#",
	"#.......#",
	"#._._._.#",
	"#########",
};

// Level 4 -- four crates, four goals beneath them. Player must
// pick a push order that doesn't trap a crate against a side wall.
//   row width 11, height 6. Solvable in ~8 pushes.
const char* const kLevel4[] = {
	"###########",
	"#@........#",
	"#..o.o.o.o#",
	"#.........#",
	"#.._._._._#",
	"###########",
};

// Level 5 -- five crates and five goals. Push crates leftward into
// a goal column, mixing horizontal pushes with vertical routing.
//   row width 11, height 8. Solvable in ~15 pushes.
const char* const kLevel5[] = {
	"###########",
	"#_........#",
	"#_..o.....#",
	"#_..o.....#",
	"#_..o..@..#",
	"#_..o.....#",
	"#...o.....#",
	"###########",
};

struct LevelDef {
	const char* const* rows;
	uint8_t            rowCount;
};

const LevelDef kLevels[PhoneSokoban::LevelCount] = {
	{ kLevel1, sizeof(kLevel1) / sizeof(kLevel1[0]) },
	{ kLevel2, sizeof(kLevel2) / sizeof(kLevel2[0]) },
	{ kLevel3, sizeof(kLevel3) / sizeof(kLevel3[0]) },
	{ kLevel4, sizeof(kLevel4) / sizeof(kLevel4[0]) },
	{ kLevel5, sizeof(kLevel5) / sizeof(kLevel5[0]) },
};

// Pack (col,row) into the 8-bit form used by undoBuf / cratePos.
inline uint8_t packPos(uint8_t col, uint8_t row) {
	return static_cast<uint8_t>((row << 4) | (col & 0x0F));
}
inline uint8_t unpackCol(uint8_t pp) { return pp & 0x0F; }
inline uint8_t unpackRow(uint8_t pp) { return (pp >> 4) & 0x0F; }

} // namespace

// ===========================================================================
// ctor / dtor
// ===========================================================================

PhoneSokoban::PhoneSokoban()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr) {

	for(uint16_t i = 0; i < CellCount; ++i) {
		staticSprites[i] = nullptr;
		tiles[i]   = TileOutside;
		crateAt[i] = 0;
	}
	for(uint8_t i = 0; i < MaxCrates; ++i) {
		crateSprites[i] = nullptr;
		cratePos[i]     = 0;
	}
	for(uint8_t i = 0; i < LevelCount; ++i) {
		bestMoves[i] = 0;
	}

	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildHud();
	buildStaticGrid();
	buildDynamicLayer();
	buildOverlay();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("MOVE");
	softKeys->setRight("BACK");

	loadLevel(0);
}

PhoneSokoban::~PhoneSokoban() {
	// All children parented to obj; LVScreen frees them.
}

void PhoneSokoban::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneSokoban::onStop() {
	Input::getInstance()->removeListener(this);
}

// ===========================================================================
// build helpers
// ===========================================================================

void PhoneSokoban::buildHud() {
	hudLevelLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudLevelLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudLevelLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(hudLevelLabel, "LVL 1/5");
	lv_obj_set_pos(hudLevelLabel, 4, HudY + 2);

	hudMovesLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudMovesLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudMovesLabel, MP_TEXT, 0);
	lv_label_set_text(hudMovesLabel, "M 000");
	lv_obj_set_align(hudMovesLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(hudMovesLabel, HudY + 2);

	hudPushesLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudPushesLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudPushesLabel, MP_ACCENT, 0);
	lv_label_set_text(hudPushesLabel, "P 000");
	lv_obj_set_pos(hudPushesLabel, 130, HudY + 2);
}

void PhoneSokoban::buildOverlay() {
	overlayLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(overlayLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(overlayLabel, MP_HIGHLIGHT, 0);
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

void PhoneSokoban::buildStaticGrid() {
	// One sprite per grid cell. Position is set at level-load time
	// (when we know the per-level origin offset). Sprites for cells
	// outside the active level are kept hidden.
	for(uint8_t row = 0; row < GridRows; ++row) {
		for(uint8_t col = 0; col < GridCols; ++col) {
			const uint16_t idx = indexOf(col, row);
			auto* s = lv_obj_create(obj);
			lv_obj_remove_style_all(s);
			lv_obj_set_size(s, TilePx, TilePx);
			lv_obj_set_style_bg_opa(s, LV_OPA_TRANSP, 0);
			lv_obj_set_style_border_width(s, 0, 0);
			lv_obj_set_style_radius(s, 0, 0);
			lv_obj_set_style_pad_all(s, 0, 0);
			lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_clear_flag(s, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_flag(s, LV_OBJ_FLAG_IGNORE_LAYOUT);
			lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
			staticSprites[idx] = s;
		}
	}
}

void PhoneSokoban::buildDynamicLayer() {
	// Crate sprites: solid orange-bordered cream block, 8x8 px. We
	// allocate MaxCrates of them once and hide / show them as the
	// active level uses more or fewer crates.
	for(uint8_t i = 0; i < MaxCrates; ++i) {
		auto* s = lv_obj_create(obj);
		lv_obj_remove_style_all(s);
		lv_obj_set_size(s, TilePx, TilePx);
		lv_obj_set_style_bg_color(s, MP_TEXT, 0);
		lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
		lv_obj_set_style_border_color(s, MP_ACCENT, 0);
		lv_obj_set_style_border_width(s, 1, 0);
		lv_obj_set_style_radius(s, 1, 0);
		lv_obj_set_style_pad_all(s, 0, 0);
		lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(s, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(s, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
		crateSprites[i] = s;
	}

	// Player sprite: cyan square with a darker eye-pip pair via a
	// 2-pixel border + inset child. Kept simple at 8 px so it reads
	// at a glance.
	playerSprite = lv_obj_create(obj);
	lv_obj_remove_style_all(playerSprite);
	lv_obj_set_size(playerSprite, TilePx, TilePx);
	lv_obj_set_style_bg_color(playerSprite, MP_HIGHLIGHT, 0);
	lv_obj_set_style_bg_opa(playerSprite, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(playerSprite, MP_BG_DARK, 0);
	lv_obj_set_style_border_width(playerSprite, 1, 0);
	lv_obj_set_style_radius(playerSprite, 2, 0);
	lv_obj_set_style_pad_all(playerSprite, 0, 0);
	lv_obj_clear_flag(playerSprite, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(playerSprite, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(playerSprite, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_add_flag(playerSprite, LV_OBJ_FLAG_HIDDEN);

	// Tiny dark eye dot to make the player feel like a face rather
	// than a square. Parented to the player sprite so it tracks every
	// move automatically.
	auto* eye = lv_obj_create(playerSprite);
	lv_obj_remove_style_all(eye);
	lv_obj_set_size(eye, 2, 2);
	lv_obj_set_pos(eye, 2, 2);
	lv_obj_set_style_bg_color(eye, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, 0);
	lv_obj_clear_flag(eye, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(eye, LV_OBJ_FLAG_CLICKABLE);
}

// ===========================================================================
// state transitions
// ===========================================================================

void PhoneSokoban::loadLevel(uint8_t idx) {
	if(idx >= LevelCount) idx = 0;
	levelIndex = idx;

	// Reset board state.
	for(uint16_t i = 0; i < CellCount; ++i) {
		tiles[i]   = TileOutside;
		crateAt[i] = 0;
	}
	crateCount = 0;
	moves      = 0;
	pushes     = 0;
	undoCount  = 0;
	undoHead   = 0;
	state      = GameState::Playing;
	playerCol  = 0;
	playerRow  = 0;

	const LevelDef& def = kLevels[idx];

	// Compute the actual level dimensions (max row width, row count).
	uint8_t maxCols = 0;
	for(uint8_t r = 0; r < def.rowCount; ++r) {
		const size_t len = strlen(def.rows[r]);
		if(len > maxCols) maxCols = static_cast<uint8_t>(len);
	}
	if(maxCols > GridCols) maxCols = GridCols;
	uint8_t rowCount = def.rowCount;
	if(rowCount > GridRows) rowCount = GridRows;

	levelCols = maxCols;
	levelRows = rowCount;
	levelOriginX = static_cast<lv_coord_t>((160 - maxCols * TilePx) / 2);
	levelOriginY = static_cast<lv_coord_t>(FieldYTop
	               + (FieldH - rowCount * TilePx) / 2);

	// Parse glyphs into the static + dynamic layers.
	for(uint8_t r = 0; r < rowCount; ++r) {
		const char* line = def.rows[r];
		const size_t len = strlen(line);
		for(uint8_t c = 0; c < maxCols; ++c) {
			const uint16_t idx2 = indexOf(c, r);
			const char ch = (c < len) ? line[c] : ' ';
			switch(ch) {
				case '#':
					tiles[idx2] = TileWall;
					break;
				case '.':
					tiles[idx2] = TileFloor;
					break;
				case '_':
					tiles[idx2] = TileGoal;
					break;
				case '@':
					tiles[idx2] = TileFloor;
					playerCol = c;
					playerRow = r;
					break;
				case '+':
					tiles[idx2] = TileGoal;
					playerCol = c;
					playerRow = r;
					break;
				case 'o':
					tiles[idx2] = TileFloor;
					if(crateCount < MaxCrates) {
						cratePos[crateCount] = packPos(c, r);
						crateAt[idx2] = crateCount + 1;
						++crateCount;
					}
					break;
				case '*':
				case 'O':
					tiles[idx2] = TileGoal;
					if(crateCount < MaxCrates) {
						cratePos[crateCount] = packPos(c, r);
						crateAt[idx2] = crateCount + 1;
						++crateCount;
					}
					break;
				default:
					tiles[idx2] = TileOutside;
					break;
			}
		}
	}

	renderStaticGrid();
	renderAllCrates();
	renderPlayer();
	refreshHud();
	refreshSoftKeys();
	refreshOverlay();
}

void PhoneSokoban::resetCurrentLevel() {
	loadLevel(levelIndex);
}

uint8_t PhoneSokoban::crateIndexAt(uint8_t col, uint8_t row) const {
	if(!inBounds(col, row)) return 0xFF;
	const uint8_t v = crateAt[indexOf(col, row)];
	return (v == 0) ? 0xFF : static_cast<uint8_t>(v - 1);
}

void PhoneSokoban::tryMove(int8_t dCol, int8_t dRow) {
	if(state != GameState::Playing) return;

	const int8_t nc = static_cast<int8_t>(playerCol) + dCol;
	const int8_t nr = static_cast<int8_t>(playerRow) + dRow;
	if(!inBounds(nc, nr)) return;

	const uint16_t nidx = indexOf(static_cast<uint8_t>(nc),
	                              static_cast<uint8_t>(nr));
	const Tile  nTile  = tiles[nidx];

	// Walls block movement entirely.
	if(nTile == TileWall || nTile == TileOutside) return;

	const uint8_t nCrateIdx = crateIndexAt(static_cast<uint8_t>(nc),
	                                       static_cast<uint8_t>(nr));

	UndoEntry entry;
	entry.prevPlayerCol = playerCol;
	entry.prevPlayerRow = playerRow;
	entry.crateFrom = 0xFF;
	entry.crateTo   = 0xFF;

	if(nCrateIdx != 0xFF) {
		// Crate in front of us; check the cell beyond.
		const int8_t bc = nc + dCol;
		const int8_t br = nr + dRow;
		if(!inBounds(bc, br)) return;
		const uint16_t bidx = indexOf(static_cast<uint8_t>(bc),
		                              static_cast<uint8_t>(br));
		const Tile bTile = tiles[bidx];
		if(bTile == TileWall || bTile == TileOutside) return;
		if(crateAt[bidx] != 0) return;   // blocked by another crate

		// Push.
		entry.crateFrom = packPos(static_cast<uint8_t>(nc),
		                          static_cast<uint8_t>(nr));
		entry.crateTo   = packPos(static_cast<uint8_t>(bc),
		                          static_cast<uint8_t>(br));
		crateAt[nidx]  = 0;
		crateAt[bidx]  = static_cast<uint8_t>(nCrateIdx + 1);
		cratePos[nCrateIdx] = entry.crateTo;
		renderCrate(nCrateIdx);
		++pushes;
	}

	playerCol = static_cast<uint8_t>(nc);
	playerRow = static_cast<uint8_t>(nr);
	++moves;
	renderPlayer();

	// Push the undo entry into the ring buffer.
	undoBuf[undoHead] = entry;
	undoHead = static_cast<uint8_t>((undoHead + 1) % UndoCap);
	if(undoCount < UndoCap) ++undoCount;

	refreshHud();

	if(isLevelSolved()) {
		winLevel();
	}
}

void PhoneSokoban::undoLast() {
	if(state != GameState::Playing) return;
	if(undoCount == 0) return;

	const uint8_t prevIdx = static_cast<uint8_t>((undoHead + UndoCap - 1) % UndoCap);
	const UndoEntry e = undoBuf[prevIdx];
	undoHead = prevIdx;
	--undoCount;

	// If a crate was pushed, walk it back.
	if(e.crateFrom != 0xFF && e.crateTo != 0xFF) {
		const uint8_t toCol   = unpackCol(e.crateTo);
		const uint8_t toRow   = unpackRow(e.crateTo);
		const uint8_t fromCol = unpackCol(e.crateFrom);
		const uint8_t fromRow = unpackRow(e.crateFrom);
		const uint16_t toIdx   = indexOf(toCol, toRow);
		const uint16_t fromIdx = indexOf(fromCol, fromRow);
		const uint8_t crIdx = static_cast<uint8_t>(crateAt[toIdx] - 1);
		crateAt[toIdx]   = 0;
		crateAt[fromIdx] = static_cast<uint8_t>(crIdx + 1);
		cratePos[crIdx]  = e.crateFrom;
		renderCrate(crIdx);
		if(pushes > 0) --pushes;
	}

	playerCol = e.prevPlayerCol;
	playerRow = e.prevPlayerRow;
	if(moves > 0) --moves;
	renderPlayer();
	refreshHud();
	// Undoing past a "won" never happens because state guards entry.
	refreshOverlay();
}

bool PhoneSokoban::isLevelSolved() const {
	// Solved iff every crate sits on a goal tile.
	for(uint8_t i = 0; i < crateCount; ++i) {
		const uint8_t col = unpackCol(cratePos[i]);
		const uint8_t row = unpackRow(cratePos[i]);
		if(tiles[indexOf(col, row)] != TileGoal) return false;
	}
	return true;
}

void PhoneSokoban::winLevel() {
	state = GameState::Won;

	// Update the in-memory best-moves record for this level.
	if(bestMoves[levelIndex] == 0 || moves < bestMoves[levelIndex]) {
		bestMoves[levelIndex] = moves;
	}

	refreshSoftKeys();
	refreshOverlay();
}

void PhoneSokoban::advanceLevel() {
	if(levelIndex + 1 >= LevelCount) {
		// Beat the final level -- show the all-clear overlay before
		// looping back to level 1 on the next BTN_5.
		state = GameState::AllClear;
		refreshSoftKeys();
		refreshOverlay();
		return;
	}
	loadLevel(static_cast<uint8_t>(levelIndex + 1));
}

// ===========================================================================
// rendering
// ===========================================================================

void PhoneSokoban::renderStaticGrid() {
	// Walk every cell. For "outside" tiles, hide the sprite so we get
	// the synthwave wallpaper showing through. For walls/floors/goals
	// we paint a flat 8x8 rectangle in the appropriate colour.
	for(uint8_t row = 0; row < GridRows; ++row) {
		for(uint8_t col = 0; col < GridCols; ++col) {
			const uint16_t idx = indexOf(col, row);
			auto* s = staticSprites[idx];
			if(s == nullptr) continue;

			const Tile t = tiles[idx];
			if(t == TileOutside) {
				lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
				continue;
			}

			// Position relative to the current level origin.
			lv_obj_set_pos(s,
			               static_cast<lv_coord_t>(levelOriginX + col * TilePx),
			               static_cast<lv_coord_t>(levelOriginY + row * TilePx));
			lv_obj_clear_flag(s, LV_OBJ_FLAG_HIDDEN);

			switch(t) {
				case TileWall:
					lv_obj_set_style_bg_color(s, MP_DIM, 0);
					lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
					lv_obj_set_style_border_color(s, MP_LABEL_DIM, 0);
					lv_obj_set_style_border_width(s, 1, 0);
					lv_obj_set_style_radius(s, 0, 0);
					break;
				case TileFloor:
					lv_obj_set_style_bg_color(s, MP_BG_DARK, 0);
					lv_obj_set_style_bg_opa(s, LV_OPA_60, 0);
					lv_obj_set_style_border_width(s, 0, 0);
					lv_obj_set_style_radius(s, 0, 0);
					break;
				case TileGoal:
					// Goal pad: faint cream square inside a darker
					// floor, telegraphs "park here" without a glyph.
					lv_obj_set_style_bg_color(s, MP_ACCENT, 0);
					lv_obj_set_style_bg_opa(s, LV_OPA_30, 0);
					lv_obj_set_style_border_color(s, MP_ACCENT, 0);
					lv_obj_set_style_border_width(s, 1, 0);
					lv_obj_set_style_radius(s, 1, 0);
					break;
				default:
					break;
			}
		}
	}
}

void PhoneSokoban::renderCrate(uint8_t i) {
	if(i >= MaxCrates) return;
	auto* s = crateSprites[i];
	if(s == nullptr) return;
	if(i >= crateCount) {
		lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
		return;
	}

	const uint8_t col = unpackCol(cratePos[i]);
	const uint8_t row = unpackRow(cratePos[i]);
	lv_obj_clear_flag(s, LV_OBJ_FLAG_HIDDEN);
	lv_obj_set_pos(s,
	               static_cast<lv_coord_t>(levelOriginX + col * TilePx),
	               static_cast<lv_coord_t>(levelOriginY + row * TilePx));

	// Tint a crate cyan when it is sitting on a goal -- instant
	// "you've parked this one" feedback. Otherwise warm cream.
	const bool onGoal = tiles[indexOf(col, row)] == TileGoal;
	lv_obj_set_style_bg_color(s, onGoal ? MP_HIGHLIGHT : MP_TEXT, 0);
	lv_obj_set_style_border_color(s, onGoal ? MP_BG_DARK : MP_ACCENT, 0);
	lv_obj_move_foreground(s);
}

void PhoneSokoban::renderAllCrates() {
	for(uint8_t i = 0; i < MaxCrates; ++i) {
		renderCrate(i);
	}
}

void PhoneSokoban::renderPlayer() {
	if(playerSprite == nullptr) return;
	lv_obj_clear_flag(playerSprite, LV_OBJ_FLAG_HIDDEN);
	lv_obj_set_pos(playerSprite,
	               static_cast<lv_coord_t>(levelOriginX + playerCol * TilePx),
	               static_cast<lv_coord_t>(levelOriginY + playerRow * TilePx));
	lv_obj_move_foreground(playerSprite);
}

void PhoneSokoban::refreshHud() {
	if(hudLevelLabel != nullptr) {
		char buf[16];
		snprintf(buf, sizeof(buf), "LVL %u/%u",
		         static_cast<unsigned>(levelIndex + 1),
		         static_cast<unsigned>(LevelCount));
		lv_label_set_text(hudLevelLabel, buf);
	}
	if(hudMovesLabel != nullptr) {
		char buf[16];
		const unsigned m = moves > 999 ? 999 : moves;
		snprintf(buf, sizeof(buf), "M %03u", m);
		lv_label_set_text(hudMovesLabel, buf);
	}
	if(hudPushesLabel != nullptr) {
		char buf[16];
		const unsigned p = pushes > 999 ? 999 : pushes;
		snprintf(buf, sizeof(buf), "P %03u", p);
		lv_label_set_text(hudPushesLabel, buf);
	}
}

void PhoneSokoban::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	switch(state) {
		case GameState::Playing:
			softKeys->setLeft("MOVE");
			softKeys->setRight("BACK");
			break;
		case GameState::Won:
			softKeys->setLeft("NEXT");
			softKeys->setRight("BACK");
			break;
		case GameState::AllClear:
			softKeys->setLeft("AGAIN");
			softKeys->setRight("BACK");
			break;
	}
}

void PhoneSokoban::refreshOverlay() {
	if(overlayLabel == nullptr) return;
	switch(state) {
		case GameState::Playing:
			lv_obj_add_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
		case GameState::Won: {
			char buf[64];
			const unsigned best = bestMoves[levelIndex];
			snprintf(buf, sizeof(buf),
			         "LEVEL %u CLEAR\n%u MOVES  %u PUSHES\nBEST %u  A=NEXT",
			         static_cast<unsigned>(levelIndex + 1),
			         static_cast<unsigned>(moves),
			         static_cast<unsigned>(pushes),
			         best);
			lv_label_set_text(overlayLabel, buf);
			lv_obj_set_style_text_color(overlayLabel, MP_HIGHLIGHT, 0);
			lv_obj_set_style_border_color(overlayLabel, MP_HIGHLIGHT, 0);
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			lv_obj_move_foreground(overlayLabel);
			break;
		}
		case GameState::AllClear: {
			lv_label_set_text(overlayLabel,
			                  "ALL CLEAR!\nWAREHOUSE EMPTY\nA=REPLAY");
			lv_obj_set_style_text_color(overlayLabel, MP_ACCENT, 0);
			lv_obj_set_style_border_color(overlayLabel, MP_ACCENT, 0);
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			lv_obj_move_foreground(overlayLabel);
			break;
		}
	}
}

// ===========================================================================
// input
// ===========================================================================

void PhoneSokoban::buttonPressed(uint i) {
	// BACK always pops out, regardless of state.
	if(i == BTN_BACK) {
		if(softKeys) softKeys->flashRight();
		pop();
		return;
	}

	// Reset the active level (works in any state).
	if(i == BTN_R) {
		resetCurrentLevel();
		return;
	}

	switch(state) {
		case GameState::Playing: {
			if(i == BTN_LEFT || i == BTN_4) {
				tryMove(-1, 0);
				return;
			}
			if(i == BTN_RIGHT || i == BTN_6) {
				tryMove(+1, 0);
				return;
			}
			if(i == BTN_2) {
				tryMove(0, -1);
				return;
			}
			if(i == BTN_8) {
				tryMove(0, +1);
				return;
			}
			// Undo: BTN_L bumper feels right for "step back" because
			// BTN_R already does "reset / new round" everywhere else.
			if(i == BTN_L) {
				undoLast();
				return;
			}
			// BTN_5 / BTN_ENTER are deliberately no-ops while playing
			// to avoid stealing the "advance" gesture from the win
			// overlay. The directional keys are the move primitives.
			return;
		}
		case GameState::Won: {
			if(i == BTN_5 || i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				advanceLevel();
				return;
			}
			return;
		}
		case GameState::AllClear: {
			if(i == BTN_5 || i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				loadLevel(0);
				return;
			}
			return;
		}
	}
}
