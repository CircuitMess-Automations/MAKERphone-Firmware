#ifndef MAKERPHONE_PHONEMENUGRID_H
#define MAKERPHONE_PHONEMENUGRID_H

#include <Arduino.h>
#include <lvgl.h>
#include <vector>
#include <functional>
#include <utility>
#include "../Interface/LVObject.h"
#include "PhoneIconTile.h"

/**
 * PhoneMenuGrid
 *
 * Reusable retro feature-phone main-menu grid for MAKERphone 2.0. Builds on
 * PhoneIconTile (the foundational atom) to compose a navigable 2D grid of
 * Sony-Ericsson-style icons - the visual centerpiece of the future
 * PhoneMainMenu screen and the natural successor to Chatter's vertical
 * carousel:
 *
 *      +-----+   +-----+   +-----+   +-----+
 *      |[##] |   |[##] |   |[##] |   |[##] |
 *      |Phone|   |Mess.|   |Cont.|   |Music|
 *      +-----+   +-----+   +-----+   +-----+
 *      +-----+   +-----+   +-----+
 *      |[##] |   |[##] |   |[##] |
 *      |Cam. |   |Games|   |Sett.|
 *      +-----+   +-----+   +-----+
 *
 * The grid owns a cursor that maps a flat index onto the (col, row) layout.
 * Moving the cursor highlights one tile (the existing PhoneIconTile halo
 * pulse provides the "selected" animation - the grid does NOT introduce a
 * second source of motion). Wrap-around is on by default and matches what
 * a feature-phone user expects: pressing Right past the last column lands
 * on the first item of the next row, pressing Down past the last row
 * wraps back to the top.
 *
 * Implementation notes:
 *  - 100% code-only - no SPIFFS assets, no canvas backing buffers, zero
 *    data partition cost. Same constraint that drove every other Phone*
 *    widget in MAKERphone 2.0.
 *  - Uses LVGL flex layout (LV_FLEX_FLOW_ROW_WRAP) so tiles flow naturally
 *    when the entry list grows or shrinks. The container width is sized
 *    to exactly fit `cols` tiles plus their gaps; height is content-sized.
 *  - Tiles are stored as raw PhoneIconTile* pointers but lifecycle is
 *    handled by the LVObject base class - when the grid's lv_obj is
 *    deleted, every child tile's LV_EVENT_DELETE handler runs and the
 *    matching C++ wrapper deletes itself (see LVObject.cpp).
 *  - Anchored with LV_OBJ_FLAG_IGNORE_LAYOUT so it cooperates with parents
 *    that already use flex/grid layouts (consistent with the other Phone*
 *    widgets). The host screen typically just sets its align/x/y to slot
 *    the grid between the status bar and the soft-key bar.
 *  - moveCursor(dx, dy) is the only navigation primitive the host needs;
 *    a typical screen wires BTN_LEFT/RIGHT/UP/DOWN (or 4/6/2/8) directly
 *    into it and lets the grid manage selection visuals.
 *  - An optional onSelectionChanged callback fires after the cursor lands
 *    on a new index so the host can update soft-key labels, play a click
 *    on the buzzer, etc. The callback is std::function so it can capture
 *    the host screen via lambda - same pattern Chatter already uses for
 *    its press handlers.
 */
class PhoneMenuGrid : public LVObject {
public:
	struct Entry {
		PhoneIconTile::Icon icon;
		const char* label; // pixelbasic7 caption shown under the tile (nullable)
	};

	using SelectionChangedCb = std::function<void(uint8_t prevIndex, uint8_t newIndex)>;

	/**
	 * Build a grid populated with the given entries.
	 *
	 * @param parent  LVGL parent.
	 * @param entries List of (icon, label) pairs - rendered left-to-right,
	 *                top-to-bottom, in order. Must be non-empty.
	 * @param cols    Number of tiles per row. Defaults to 4 (the SonyEricsson
	 *                main-menu layout). Clamped to >= 1.
	 */
	PhoneMenuGrid(lv_obj_t* parent, const std::vector<Entry>& entries, uint8_t cols = 4);
	virtual ~PhoneMenuGrid() = default;

	/** Move the cursor by (dx columns, dy rows). Wraps around per setWrap(). */
	void moveCursor(int8_t dx, int8_t dy);

	/** Jump the cursor directly to an index. Out-of-range indices are ignored. */
	void setCursor(uint8_t index);

	/** Currently focused flat index. */
	uint8_t getCursor() const { return cursor; }

	/** Total number of entries (== number of tiles). */
	uint8_t getCount() const { return (uint8_t) tiles.size(); }

	/** Icon enum of the currently focused tile (handy for press dispatch). */
	PhoneIconTile::Icon getSelectedIcon() const;

	/** Direct access to the focused tile (useful to flash/animate it on press). */
	PhoneIconTile* getSelectedTile() const;

	/** Toggle wrap-around navigation. Default: true. */
	void setWrap(bool wrap) { this->wrap = wrap; }
	bool isWrap() const { return wrap; }

	/**
	 * Register a callback fired AFTER the cursor lands on a new index.
	 * Pass nullptr to clear. Not invoked when setCursor() is called with
	 * the current index (no-op moves don't fire).
	 */
	void setOnSelectionChanged(SelectionChangedCb cb) { onSelChanged = std::move(cb); }

	uint8_t getCols() const { return cols; }
	uint8_t getRows() const;

	// Layout constants - tile geometry must match PhoneIconTile.
	static constexpr uint16_t TileW         = PhoneIconTile::TileWidth;
	static constexpr uint16_t TileH         = PhoneIconTile::TileHeight;
	static constexpr uint8_t  TileGapX      = 2;   // px between tiles horizontally
	static constexpr uint8_t  TileGapY      = 2;   // px between tiles vertically
	static constexpr uint8_t  ContainerPad  = 1;   // px around the whole grid

private:
	uint8_t cols;
	bool    wrap = true;
	uint8_t cursor = 0;

	std::vector<PhoneIconTile*> tiles;
	SelectionChangedCb onSelChanged;

	void buildContainer();
	void populate(const std::vector<Entry>& entries);

	/** Apply selection visuals: previously-selected tile off, newly-selected on. */
	void applySelection(uint8_t prev, uint8_t curr);
};

#endif //MAKERPHONE_PHONEMENUGRID_H
