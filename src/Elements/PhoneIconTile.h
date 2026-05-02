#ifndef MAKERPHONE_PHONEICONTILE_H
#define MAKERPHONE_PHONEICONTILE_H

#include <Arduino.h>
#include <lvgl.h>
#include "../Interface/LVObject.h"

/**
 * PhoneIconTile
 *
 * Reusable retro feature-phone menu icon tile (36x36) for MAKERphone 2.0.
 * It is the foundational atom of the future Phase-3 main menu grid: a
 * single Sony-Ericsson-style icon (Phone, Messages, Contacts, Music,
 * Camera, Games, Settings, Mail) drawn in pixel-art with an optional
 * label below and a "selected" highlight that pulses to attract the eye.
 *
 *   +----------+
 *   |  ##  ##  |       <- 16x16 icon area
 *   |  ######  |
 *   |    ##    |
 *   +----------+
 *   |  PHONE   |       <- pixelbasic7 label
 *   +----------+
 *
 * Implementation notes:
 *  - 100% code-only - every icon is composed from a handful of plain
 *    lv_obj rectangles (the same primitive PhoneSoftKeyBar uses for its
 *    arrows). No SPIFFS assets, no canvas backing buffers, zero data
 *    partition cost. A typical icon is 5-10 rectangles.
 *  - When the parent is a LV_LAYOUT_FLEX or LV_LAYOUT_GRID container,
 *    the tiles flow naturally - PhoneIconTile does NOT use
 *    LV_OBJ_FLAG_IGNORE_LAYOUT (unlike StatusBar/SoftKeyBar) because
 *    the future PhoneMainMenu wants to lay tiles out automatically.
 *  - Selection state is purely visual: setSelected(true) brightens the
 *    border to MP_ACCENT and fades a soft halo ring in/out behind the
 *    tile. The halo's opacity ping-pongs forever once started, then
 *    auto-tears down with the tile via LVGL's normal child cleanup.
 *  - The icon is chosen via the Icon enum and drawn once at construction.
 *    Tiles are intended to be immutable after creation - if you need a
 *    different icon, delete and recreate the tile (the future menu does
 *    that anyway when its model changes).
 *  - Pixel art is anchored to a 16x16 canvas-of-ideas - each per-icon
 *    builder pretends it has a 16x16 grid and uses the px() helper to
 *    drop opaque rectangles. The icon layer is centered inside the tile
 *    so cosmetic offsets don't matter to consumers.
 */
class PhoneIconTile : public LVObject {
public:
	enum class Icon : uint8_t {
		Phone,
		Messages,
		Contacts,
		Music,
		Camera,
		Games,
		Settings,
		Mail
	};

	PhoneIconTile(lv_obj_t* parent, Icon icon, const char* label = nullptr);
	virtual ~PhoneIconTile() = default;

	/** Toggle the highlighted/focused look (pulsing halo + orange border). */
	void setSelected(bool selected);
	bool isSelected() const { return selected; }

	Icon getIcon() const { return icon; }

	static constexpr uint16_t TileWidth   = 36;
	static constexpr uint16_t TileHeight  = 36;
	static constexpr uint16_t IconSize    = 16;
	static constexpr uint16_t LabelHeight = 9;   // pixelbasic7 cap height + 2

	// Halo "breath" period (ms). Full ping-pong cycle = 2 * HaloPulsePeriod.
	// Long enough to feel like a calm pulse, short enough to be noticeable.
	static constexpr uint16_t HaloPulsePeriod = 900;

private:
	Icon icon;
	bool selected = false;

	lv_obj_t* halo;        // outer glow ring (visible only when selected)
	lv_obj_t* shine;       // S108 - Sony Ericsson Aqua chrome-shine strip across the top edge
	                        //          of the tile body. Fully transparent under every other theme.
	lv_obj_t* edgeGlow;    // S110 - RAZR Hot Pink EL-backlight bleed strip across the bottom
	                        //          edge of the tile body. Mirrors `shine` (top edge) on the
	                        //          opposite axis so the two cues read as physically distinct
	                        //          lighting models. Fully transparent under every non-RAZR theme.
	lv_obj_t* iconLayer;   // 16x16 transparent container that holds the pixel rectangles
	lv_obj_t* labelEl;     // optional pixelbasic7 label below the icon (nullable)

	void buildBackground();
	void buildHalo();
	void buildShine();
	void buildEdgeGlow();
	void buildIconLayer();
	void buildLabel(const char* label);
	void refreshSelection();

	/**
	 * Place a 1+ pixel rectangle at (x, y) with size (w, h) inside iconLayer.
	 * The whole composition primitive used by every per-icon builder.
	 */
	lv_obj_t* px(int16_t x, int16_t y, uint16_t w, uint16_t h, lv_color_t color);

	// Per-icon builders. Each pretends iconLayer is a 16x16 grid and
	// drops opaque rectangles to compose the silhouette.
	void drawPhone();
	void drawMessages();
	void drawContacts();
	void drawMusic();
	void drawCamera();
	void drawGames();
	void drawSettings();
	void drawMail();

	// Drives the halo's opacity ping-pong - the visible "selected pulse".
	// Free function semantics (matches LVGL's lv_anim_exec_xcb_t signature).
	static void haloPulseExec(void* var, int32_t v);
};

#endif //MAKERPHONE_PHONEICONTILE_H
