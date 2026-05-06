#ifndef MAKERPHONE_PHONEOWNEREMOJISCREEN_H
#define MAKERPHONE_PHONEOWNEREMOJISCREEN_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"
#include "../Services/PhoneOwnerEmoji.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneOwnerEmojiScreen (S188)
 *
 * SYSTEM-section sub-screen reachable from PhoneSettingsScreen's "Owner
 * emoji" row (S188 inserts it just below "Owner name"). Lets the user
 * scrub through the curated PhoneOwnerEmoji catalogue (None / Heart /
 * Star / Smile / Music / Crown / Skull / Bolt / Cat / Coffee / Pizza /
 * Dice / Rocket) and pin one to Settings.ownerEmoji so the LockScreen
 * paints it just under the status bar (next to any owner-name text).
 *
 *   View:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |             OWNER EMOJI                | <- caption (cyan, pixelbasic7)
 *   |                                        |
 *   |          +---------------+             |
 *   |          |               |             | <- preview slab (4x scale)
 *   |          |    HEART      |             |
 *   |          |               |             |
 *   |          +---------------+             |
 *   |                                        |
 *   |              HEART                     | <- name caption
 *   |             2 / 13                     | <- index caption
 *   |                                        |
 *   |   PICK                          BACK   | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Behaviour:
 *  - LEFT / 4 / 2  : previous emoji  (wraps)
 *  - RIGHT / 6 / 8 : next emoji      (wraps)
 *  - ENTER         : PICK -> persist Settings.ownerEmoji + pop
 *  - BTN_LEFT (PICK softkey)  : same as ENTER
 *  - BTN_RIGHT (BACK softkey) : pop without persisting
 *  - BTN_BACK                 : same as BACK softkey
 *
 * Implementation notes:
 *  - 100 % code-only -- no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *    PhoneStatusBar / PhoneSoftKeyBar so the screen reads as part of
 *    the MAKERphone family. Data partition cost stays zero.
 *  - The big preview is a grid of plain lv_obj cells (9 x 9 = 81
 *    children at 4x scale) sitting inside a fixed-size container.
 *    On every cursor scrub the cells get recoloured in place rather
 *    than recreated, so the preview repaint stays one style call per
 *    cell with no LVGL alloc churn.
 *  - The cursor wraps both ends -- a 13-entry catalogue is short
 *    enough that a hard stop at either end feels punitive. The
 *    PICK soft-key flashes on commit so the user gets the same
 *    visual confirmation every other personalisation screen ships.
 *  - Defensive read: getInitialId() returns
 *    PhoneOwnerEmoji::clampedId(Settings.ownerEmoji) so a corrupt
 *    persisted byte opens the picker on None rather than walking
 *    past the end of the catalogue.
 */
class PhoneOwnerEmojiScreen : public LVScreen, private InputListener {
public:
	PhoneOwnerEmojiScreen();
	virtual ~PhoneOwnerEmojiScreen() override;

	void onStart() override;
	void onStop() override;

	/** Currently focused (cursor) catalogue index. */
	uint8_t getFocusedId() const { return cursor; }

	/** Catalogue index as persisted at the moment the screen opened.
	 *  BACK reverts the in-flight cursor visually but PICK is what
	 *  actually mutates the persisted byte. */
	uint8_t getInitialId() const { return initialId; }

	/** Pixel scale (cell-px per emoji-px) used for the preview slab. */
	static constexpr lv_coord_t PreviewScale = 4;

	/** Pixel footprint of the preview slab (Width x PreviewScale). */
	static constexpr lv_coord_t PreviewSize = 9 * PreviewScale;  // 36

	/** Long-press threshold reused from the other personalisation screens. */
	static constexpr uint16_t BackHoldMs = 600;

private:
	PhoneSynthwaveBg* wallpaper = nullptr;
	PhoneStatusBar*   statusBar = nullptr;
	PhoneSoftKeyBar*  softKeys  = nullptr;

	lv_obj_t* captionLabel = nullptr;     // "OWNER EMOJI"
	lv_obj_t* previewBox   = nullptr;     // 36 x 36 frame container
	lv_obj_t* nameLabel    = nullptr;     // "HEART"
	lv_obj_t* indexLabel   = nullptr;     // "2 / 13"
	lv_obj_t* hintLabel    = nullptr;     // "LEFT / RIGHT to choose"

	// Pre-allocated cell grid for the preview slab. Created once in
	// the constructor; the cursor scrub just recolours each cell in
	// place so the LVGL alloc churn stays at zero.
	lv_obj_t* cells[PhoneOwnerEmoji::Height][PhoneOwnerEmoji::Width] = {{nullptr}};

	uint8_t cursor    = 0;
	uint8_t initialId = 0;

	// ---- builders ----
	void buildCaption();
	void buildPreview();
	void buildNameRow();
	void buildHint();

	// ---- repainters ----
	void refreshPreview();
	void refreshNameRow();
	void refreshSoftKeys();

	// ---- input ----
	void moveCursorBy(int8_t delta);
	void saveAndExit();
	void cancelAndExit();

	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONEOWNEREMOJISCREEN_H
