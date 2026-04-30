#ifndef MAKERPHONE_PHONEPIXELAVATAR_H
#define MAKERPHONE_PHONEPIXELAVATAR_H

#include <Arduino.h>
#include <lvgl.h>
#include "../Interface/LVObject.h"

/**
 * PhonePixelAvatar
 *
 * Reusable retro feature-phone pixel avatar (32x32) for MAKERphone 2.0.
 * The avatar is generated **deterministically** from a single seed/index
 * (uint8_t), so the same seed always renders the same character. This
 * is the foundational atom for the future contacts list, message bubble
 * sender thumbnails, incoming-call screen and "edit profile" picker.
 *
 *   +--------------+
 *   |   ########   |   <- hair (variant 0..3, color from a palette)
 *   |  ##########  |
 *   | ###      ### |   <- skin (color from a palette)
 *   | ## o    o ## |   <- eyes  (variant 0..2)
 *   | ##        ## |
 *   | ## ====== ## |   <- mouth (variant 0..2)
 *   |  ##########  |
 *   |   ########   |
 *   |   ########   |   <- shirt collar (color from a palette)
 *   +--------------+
 *
 * Implementation notes:
 *  - 100% code-only - the avatar is composed from a few dozen plain
 *    lv_obj rectangles, identical primitive to PhoneIconTile. No SPIFFS
 *    assets, no canvas backing buffers, zero data partition cost. This
 *    is the whole point of the widget: existing Chatter avatars are
 *    .bin images on the data partition, the MAKERphone roadmap wants
 *    the partition to stay tiny.
 *  - Pixels are drawn on a 14x14 logical grid where each cell is 2x2
 *    real pixels, centered inside the 32x32 frame with 2 px slack on
 *    each side. Run-length packing along each row keeps the lv_obj
 *    count modest (~30-40 rectangles per avatar).
 *  - The selected look matches every other Phone* widget: a thicker
 *    MP_ACCENT border replaces the idle MP_DIM 1 px border (no halo
 *    pulse here - the avatar is usually placed inside an already
 *    selected list row, so a second layer of pulse would compete).
 *  - The seed -> variant mapping is a small mix function in the .cpp;
 *    "salts" pick hair style, hair color, skin tone, eye style, mouth
 *    style, shirt color and background color independently, so two
 *    seeds that are 1 apart still look meaningfully different.
 *  - setSeed(uint8_t) replaces the avatar in place: it cleans the
 *    pixel layer and redraws the new variant, so callers do not need
 *    to delete + recreate the widget when a contact's profile changes.
 */
class PhonePixelAvatar : public LVObject {
public:
	/**
	 * Build a 32x32 pixel avatar deterministic in `seed`.
	 *
	 * @param parent  LVGL parent.
	 * @param seed    Any uint8_t. The same seed always produces the same
	 *                avatar - 256 distinct designs are reachable.
	 */
	PhonePixelAvatar(lv_obj_t* parent, uint8_t seed = 0);
	virtual ~PhonePixelAvatar() = default;

	/** Switch the avatar to a different seed (in-place rebuild). */
	void setSeed(uint8_t seed);
	uint8_t getSeed() const { return seed; }

	/** Toggle the highlighted/focused look (thicker orange border). */
	void setSelected(bool selected);
	bool isSelected() const { return selected; }

	static constexpr uint16_t AvatarSize  = 32;
	static constexpr uint8_t  GridSize    = 14;  // logical cells per side
	static constexpr uint8_t  CellPx      = 2;   // real px per logical cell
	// Slack between the frame border and the 28x28 painted area.
	static constexpr uint8_t  GridOriginX = (AvatarSize - GridSize * CellPx) / 2; // 2
	static constexpr uint8_t  GridOriginY = (AvatarSize - GridSize * CellPx) / 2; // 2

private:
	uint8_t seed;
	bool selected = false;
	lv_obj_t* face;     // child container that holds the per-pixel rectangles

	void buildBackground();
	void buildFaceLayer();
	void rebuild();
	void clearFace();
	void refreshSelection();

	/**
	 * Place a (gw x gh) rectangle of cells at logical (gx, gy) with the
	 * given color. Each cell is CellPx * CellPx real pixels. Out-of-grid
	 * coordinates are silently clipped.
	 */
	lv_obj_t* px(uint8_t gx, uint8_t gy, uint8_t gw, uint8_t gh, lv_color_t color);

	void drawSkin(lv_color_t skin);
	void drawHair(uint8_t style, lv_color_t color);
	void drawEyes(uint8_t style, lv_color_t color);
	void drawMouth(uint8_t style, lv_color_t color);
	void drawShirt(lv_color_t color);

	/** Tiny 8-bit mix function: `seed` x `salt` -> uint8_t spread. */
	static uint8_t hashByte(uint8_t seed, uint8_t salt);
};

#endif //MAKERPHONE_PHONEPIXELAVATAR_H
