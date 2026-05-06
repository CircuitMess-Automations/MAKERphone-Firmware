#ifndef MAKERPHONE_PHONEAVATAREDITOR_H
#define MAKERPHONE_PHONEAVATAREDITOR_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"
#include "../Types.hpp"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneAvatarEditor -- S182
 *
 * Pixel-paint mini editor for 32x32 contact avatars. Replaces the
 * S38 "pick one of eight generated PixelAvatars" picker as the
 * canonical way to author a contact's portrait by letting the user
 * draw their own retro feature-phone face -- think the Nokia 3310
 * picture-message editor scaled down to MAKERphone constraints.
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |          AVATAR EDITOR                | <- pixelbasic7 cyan caption
 *   |                                        |
 *   |     +------------------------+   [#]  | <- 96x96 paint surface
 *   |     |                        |   [#]  |    (32x32 logical px,
 *   |     |        canvas          |   [#]  |     3 px per logical
 *   |     |                        |   [#]  |     cell) +  4-swatch
 *   |     |                        |        |    palette on the right
 *   |     +------------------------+        |
 *   |                                        |
 *   |   14, 16  /  PEN  /  CYAN              | <- coordinate readout
 *   |   SAVE                          BACK   | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * Controls
 *  - BTN_2 / BTN_8        : move cursor up / down
 *  - BTN_4 / BTN_6        : move cursor left / right
 *  - BTN_5  or  BTN_ENTER : paint the cell under the cursor with
 *                            the currently active palette colour
 *  - BTN_0                : erase the cell under the cursor
 *  - BTN_L / BTN_R        : cycle the active palette swatch
 *  - BTN_1                : clear the entire canvas
 *  - BTN_3                : seed the canvas from the contact's
 *                            current PhonePixelAvatar hash (a
 *                            "trace over" stencil so the user has
 *                            somewhere to start beyond an empty grid)
 *  - BTN_LEFT  softkey    : SAVE  (commit + invoke the save handler)
 *  - BTN_RIGHT softkey    : BACK  (pop without committing)
 *  - BTN_BACK  long-press : bail out to the homescreen
 *
 * Persistence
 *  - The painted bitmap is a 32x32 grid of 4-colour palette indices.
 *    On SAVE the bitmap is packed (2 bits per pixel, MSB-first
 *    inside each byte) into a 256-byte buffer and written into a
 *    tiny in-RAM cache keyed by uid so the same uid re-opens onto
 *    its previous painting within a single boot session. The cache
 *    is intentionally small (capacity `MaxCachedAvatars` = 8) so it
 *    fits inside the heap budget the rest of the MAKERphone shell
 *    allows.
 *  - Persistence to flash is deliberately deferred to a follow-up
 *    session -- the bitmap layout and the static accessor API are
 *    designed so a future PhoneCustomAvatars repo can lift this
 *    cache wholesale onto the existing `Storage` layer without
 *    touching any callsite.
 *
 * Implementation notes
 *  - 100 % code-only. No SPIFFS assets. The paint surface is a grid
 *    of lazily-created lv_obj rectangles -- one per filled cell.
 *    Empty cells own nothing, so an empty canvas costs three lv_obj
 *    children (frame + cursor + grid container). A fully-painted
 *    canvas peaks at 1024 cells, comfortably inside the LVGL object
 *    budget on this device.
 *  - The cursor is a 1 px MP_HIGHLIGHT-bordered transparent
 *    rectangle that follows the logical cursor position. It sits in
 *    z-order above the painted cells and below the palette swatch
 *    bar.
 *  - Palette colours are the canonical MP_* family so every painted
 *    avatar reads as part of the same theme. Index 0 is the
 *    transparent background ("erase"), 1..3 are pen colours.
 */
class PhoneAvatarEditor : public LVScreen, private InputListener {
public:
	using ActionHandler = void (*)(PhoneAvatarEditor* self);

	/**
	 * Build an editor bound to `uid`. When `uid != 0` the editor
	 * pre-loads any previously-saved bitmap for the same uid out of
	 * the in-RAM cache, so re-opening the editor for the same
	 * contact is round-trip safe within a session. `uid == 0` is the
	 * "scratch / preview" mode -- the bitmap is still painted and
	 * SAVE still fires the registered callback, but no cache write
	 * happens.
	 */
	explicit PhoneAvatarEditor(UID_t uid = 0);
	virtual ~PhoneAvatarEditor() override;

	void onStart() override;
	void onStop() override;

	/** Bind the SAVE softkey. Default: persist + pop(). */
	void setOnSave(ActionHandler cb);
	/** Bind the BACK softkey. Default: pop() without persisting. */
	void setOnBack(ActionHandler cb);

	/** Replace the visible label of the left softkey  (default "SAVE"). */
	void setLeftLabel(const char* label);
	/** Replace the visible label of the right softkey (default "BACK"). */
	void setRightLabel(const char* label);

	/** Logical canvas dimensions in cells. */
	static constexpr uint8_t CanvasW = 32;
	static constexpr uint8_t CanvasH = 32;
	/** Cells per palette colour (transparent + 3 pens). */
	static constexpr uint8_t PaletteSize = 4;
	/** Capacity of the per-uid bitmap cache. */
	static constexpr uint8_t MaxCachedAvatars = 8;

	/**
	 * Look up the saved bitmap for `uid`. Returns the 256-byte
	 * packed buffer (2 bits per cell, MSB-first inside each byte) on
	 * hit, or nullptr on miss. The pointer is stable for the
	 * lifetime of the cache slot.
	 */
	static const uint8_t* findSavedBitmap(UID_t uid);

private:
	enum class Tool : uint8_t {
		Pen = 0,
		Erase = 1,
	};

	UID_t   uid             = 0;
	uint8_t cursorX         = CanvasW / 2;
	uint8_t cursorY         = CanvasH / 2;
	uint8_t activeColor     = 1;
	bool    backHoldFired   = false;

	uint8_t bitmap[CanvasH][CanvasW] = {{0}};

	PhoneSynthwaveBg* wallpaper   = nullptr;
	PhoneStatusBar*   statusBar   = nullptr;
	PhoneSoftKeyBar*  softKeys    = nullptr;
	lv_obj_t*         captionLbl  = nullptr;
	lv_obj_t*         frame       = nullptr;
	lv_obj_t*         gridHolder  = nullptr;
	lv_obj_t*         cursor      = nullptr;
	lv_obj_t*         readoutLbl  = nullptr;
	lv_obj_t*         swatches[PaletteSize] = {nullptr};
	lv_obj_t*         cells[CanvasH][CanvasW] = {{nullptr}};

	ActionHandler saveCb = nullptr;
	ActionHandler backCb = nullptr;

	void buildLayout();
	void buildCanvas();
	void buildPalette();
	void buildReadout();

	void paintCell(uint8_t x, uint8_t y, uint8_t colorIndex);
	void clearAllCells();
	void seedFromAvatarHash();

	void moveCursor(int8_t dx, int8_t dy);
	void cyclePalette(int8_t direction);
	void refreshCursor();
	void refreshPalette();
	void refreshReadout();

	void invokeSave();

	void buttonPressed(uint i) override;
	void buttonHeld(uint i) override;
	void buttonReleased(uint i) override;

	static lv_color_t paletteColor(uint8_t index);
	static const char* paletteName(uint8_t index);
};

#endif // MAKERPHONE_PHONEAVATAREDITOR_H
