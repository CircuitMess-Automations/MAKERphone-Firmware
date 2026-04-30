#ifndef MAKERPHONE_PHONEDIALERPAD_H
#define MAKERPHONE_PHONEDIALERPAD_H

#include <Arduino.h>
#include <lvgl.h>
#include <vector>
#include <functional>
#include <utility>
#include "../Interface/LVObject.h"
#include "PhoneDialerKey.h"

/**
 * PhoneDialerPad
 *
 * Reusable retro feature-phone 3x4 numpad for MAKERphone 2.0. Builds on
 * PhoneDialerKey (the foundational atom) the same way PhoneMenuGrid
 * builds on PhoneIconTile - one composing widget that owns 12 keys laid
 * out in the classic Sony-Ericsson numpad geometry:
 *
 *      +----+ +----+ +----+
 *      | 1  | | 2  | | 3  |
 *      |    | |ABC | |DEF |
 *      +----+ +----+ +----+
 *      +----+ +----+ +----+
 *      | 4  | | 5  | | 6  |
 *      |GHI | |JKL | |MNO |
 *      +----+ +----+ +----+
 *      +----+ +----+ +----+
 *      | 7  | | 8  | | 9  |
 *      |PQRS| |TUV | |WXYZ|
 *      +----+ +----+ +----+
 *      +----+ +----+ +----+
 *      | *  | | 0  | | #  |
 *      +----+ +----+ +----+
 *
 * The pad owns a flat-index cursor (0..11) that maps onto (col, row)
 * with row-major ordering. moveCursor(dx, dy) wraps both axes by default
 * - the same behaviour PhoneMenuGrid uses, so the two widgets navigate
 * identically when a screen mixes them.
 *
 * Implementation notes:
 *  - 100% code-only - just composes 12 PhoneDialerKey atoms into a flex
 *    row-wrap container. No SPIFFS assets, no canvas backing buffers,
 *    zero data partition cost. Same constraint that drives every other
 *    Phone* widget in MAKERphone 2.0.
 *  - Fixed-size geometry so the numpad slots cleanly into the eventual
 *    PhoneDialer screen between the status bar (top 10 px) and the soft
 *    key bar (bottom 10 px). Width: 3 * 36 + 2 * 2 + 2 = 114 px. Height:
 *    4 * 20 + 3 * 2 + 2 = 88 px - fits 160x128 with margin to spare.
 *  - Anchored with LV_OBJ_FLAG_IGNORE_LAYOUT so it cooperates with parent
 *    screens that already use flex/grid layouts (consistent with every
 *    other Phone* widget). The host typically just sets the pad's align
 *    + x/y to slot it into the right spot under the dialed-number entry.
 *  - Two callbacks fire in sequence on a press:
 *      1. onSelectionChanged(prev, curr) - whenever the cursor moves.
 *      2. onPress(glyph, letters)        - whenever pressSelected() or
 *         pressGlyph(c) lands a press. The host wires this up to append
 *         the glyph to its number entry, play a buzzer click, etc.
 *  - pressGlyph(c) lets a host respond to the physical numpad keys
 *    (BTN_0..BTN_9) by routing each press through the matching tile so
 *    the user sees the same flash + selection visuals whether they used
 *    arrow-keys + Enter or punched the numpad directly. Returns true if
 *    a tile matched, false otherwise (no tile for the requested glyph).
 *  - Lifecycle: tiles are stored as raw PhoneDialerKey* but cleanup is
 *    handled by the LVObject base class - when the pad's lv_obj is
 *    deleted, every child tile's LV_EVENT_DELETE handler runs and the
 *    matching C++ wrapper deletes itself (see LVObject.cpp).
 */
class PhoneDialerPad : public LVObject {
public:
	using SelectionChangedCb = std::function<void(uint8_t prevIndex, uint8_t newIndex)>;
	/**
	 * Press callback.
	 * @param glyph   Big character of the pressed key ('0'..'9', '*', '#').
	 * @param letters NUL-terminated caption string ("ABC", "DEF", ...) or
	 *                empty string for keys without letters ('1', '0', '*', '#').
	 */
	using PressCb = std::function<void(char glyph, const char* letters)>;

	/**
	 * Build the standard 3x4 numpad. The keymap is fixed (1, 2/ABC, 3/DEF,
	 * 4/GHI, ... *, 0, #) - matching every Sony-Ericsson handset of the era
	 * and the implicit T9 contract Phase 5 will rely on.
	 */
	PhoneDialerPad(lv_obj_t* parent);
	virtual ~PhoneDialerPad() = default;

	/** Move the cursor by (dx columns, dy rows). Wraps per setWrap(). */
	void moveCursor(int8_t dx, int8_t dy);

	/** Jump the cursor directly to an index (0..11). Out-of-range is ignored. */
	void setCursor(uint8_t index);

	/** Currently focused flat index (0..11). */
	uint8_t getCursor() const { return cursor; }

	/** Total key count (always 12 for the standard numpad). */
	uint8_t getCount() const { return (uint8_t) keys.size(); }

	/** Big glyph of the focused key ('0'..'9', '*', '#'). */
	char getSelectedGlyph() const;

	/** Letters caption of the focused key, or "" when none. */
	const char* getSelectedLetters() const;

	/** Direct access to the focused key (handy for ad-hoc flashes). */
	PhoneDialerKey* getSelectedKey() const;

	/**
	 * Press the currently focused key: triggers the key's press flash and
	 * fires onPress(glyph, letters). Does nothing on an empty pad.
	 */
	void pressSelected();

	/**
	 * Find the key matching `glyph` and press it (visual flash + onPress).
	 * Useful for direct-numpad input where the user hits BTN_0..BTN_9
	 * instead of navigating with arrows. The cursor jumps to the matched
	 * key so a follow-up arrow-key works from the expected position.
	 *
	 * @return true if a key matched glyph, false otherwise.
	 */
	bool pressGlyph(char glyph);

	void setWrap(bool wrap) { this->wrap = wrap; }
	bool isWrap() const { return wrap; }

	/**
	 * Register a callback fired AFTER the cursor lands on a new index.
	 * Pass nullptr to clear. Not invoked when setCursor() is called with
	 * the current index (no-op moves don't fire).
	 */
	void setOnSelectionChanged(SelectionChangedCb cb) { onSelChanged = std::move(cb); }

	/**
	 * Register a callback fired on every press (pressSelected /
	 * pressGlyph). Pass nullptr to clear.
	 */
	void setOnPress(PressCb cb) { onPress = std::move(cb); }

	// ----- layout constants -----
	static constexpr uint8_t  Cols           = 3;
	static constexpr uint8_t  Rows           = 4;
	static constexpr uint8_t  KeyCount       = Cols * Rows; // 12
	static constexpr uint16_t KeyW           = PhoneDialerKey::KeyWidth;
	static constexpr uint16_t KeyH           = PhoneDialerKey::KeyHeight;
	static constexpr uint8_t  KeyGapX        = 2;
	static constexpr uint8_t  KeyGapY        = 2;
	static constexpr uint8_t  ContainerPad   = 1;

	// Resolved outer dimensions: the host can use these to position the
	// pad without poking at LVGL geometry directly.
	static constexpr uint16_t PadWidth =
			(uint16_t) Cols * KeyW
			+ (uint16_t) (Cols - 1) * KeyGapX
			+ (uint16_t) (2 * ContainerPad);
	static constexpr uint16_t PadHeight =
			(uint16_t) Rows * KeyH
			+ (uint16_t) (Rows - 1) * KeyGapY
			+ (uint16_t) (2 * ContainerPad);

private:
	bool    wrap = true;
	uint8_t cursor = 0;

	std::vector<PhoneDialerKey*> keys;
	SelectionChangedCb           onSelChanged;
	PressCb                      onPress;

	void buildContainer();
	void populate();

	/** Apply selection visuals: previous tile off, new tile on. */
	void applySelection(uint8_t prev, uint8_t curr);
};

#endif //MAKERPHONE_PHONEDIALERPAD_H
