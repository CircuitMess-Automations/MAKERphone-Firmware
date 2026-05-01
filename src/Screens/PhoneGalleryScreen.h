#ifndef MAKERPHONE_PHONEGALLERYSCREEN_H
#define MAKERPHONE_PHONEGALLERYSCREEN_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneGalleryScreen
 *
 * S46 - the MAKERphone "Gallery" placeholder. Closes out Phase H by
 * giving the just-shipped PhoneCameraScreen (S44/S45) a destination for
 * its (currently in-memory) frame counter: a 2x2 grid of thumbnails
 * the user can browse with the d-pad, with each slot either empty
 * (the default - we have no real capture pipeline yet) or showing a
 * tiny code-only synthwave silhouette so a future session can stuff
 * faux captures in for visual polish:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |  GALLERY                       0 / 4  | <- title strip (cyan + cream)
 *   |  +-------+   +-------+                | <- 2x2 thumbnail grid
 *   |  | empty |   | empty |                |    72 x 40 px each, 2 px gap
 *   |  +-------+   +-------+                |    cyan corner brackets,
 *   |  +-------+   +-------+                |    "EMPTY" caption centred
 *   |  | empty |   | empty |                |    in pixelbasic7 (cream).
 *   |  +-------+   +-------+                |
 *   |     no captures yet                   | <- hint line (pixelbasic7 dim)
 *   |  VIEW                          BACK   | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * Input contract - feature-phone muscle memory:
 *   - BTN_LEFT  (UP)    : move cursor up   (rows wrap)
 *   - BTN_RIGHT (DOWN)  : move cursor down (rows wrap)
 *   - BTN_4              : move cursor left  (cols wrap)
 *   - BTN_6              : move cursor right (cols wrap)
 *   - BTN_2 / BTN_8      : same as UP / DOWN (numeric-pad ergonomics)
 *   - BTN_ENTER (BTN_A)  : "view" the focused thumbnail. Empty slots get
 *                          a soft-key flash + "EMPTY SLOT" caption swap;
 *                          filled slots also flash + show the slot index
 *                          (a real detail screen lands in a future
 *                          session).
 *   - BTN_BACK           : pop back to whoever pushed us.
 *
 * Why a placeholder?
 *  - PhoneCameraScreen still has no real capture pipeline (frameCount
 *    is in-memory only and resets on every onStart()), so a "gallery
 *    of nothing" is exactly the right shape for S46. The roadmap entry
 *    is explicit: "4-thumbnail grid stub for future captures."
 *  - Exposing setSlotFilled(uint8_t,uint8_t) up-front lets the camera
 *    or a later persistence layer push synthetic captures in without
 *    subclassing - same forward-looking pattern PhoneCallHistory and
 *    PhoneIncomingCall already use for their respective data feeds.
 *
 * Implementation notes:
 *  - Code-only (no SPIFFS assets). Reuses PhoneSynthwaveBg /
 *    PhoneStatusBar / PhoneSoftKeyBar so the placeholder feels visually
 *    part of the same MAKERphone family. Data partition cost stays
 *    zero. Each thumbnail is a small lv_obj container with cyan
 *    corner-bracket rects + a centred "EMPTY" pixelbasic7 label, and -
 *    when filled - up to three additional flat-colour rects forming
 *    the synthwave silhouette (dark purple sky, sunset orange horizon
 *    stripe, two pixel stars seeded from the slot's fill index).
 *  - 160x128 budget: 10 px status bar + 10 px title strip + 4 px gap +
 *    (40 + 2 + 40) = 82 px of thumbnails + 12 px hint strip + 10 px
 *    soft-key bar = 128 px exactly. Each slot is 72 px wide so two
 *    columns plus a 2 px gap fit centred (146 px, 7 px margin).
 *  - Cursor is a single MP_ACCENT-tinted 2 px-thick border that we
 *    move by repointing it at the focused thumbnail container - one
 *    style update per cursor move, not four. Same low-overhead
 *    approach PhoneCallHistory uses for its row highlight.
 *  - Long-mode label clipping keeps the thumbnail labels from spilling
 *    over the bracket frame even if a future fill-state caption grows
 *    longer than "EMPTY".
 *  - No persistent state: opening the screen always resets the cursor
 *    to slot 0 and the "fillState" to whatever the host configured
 *    via setSlotFilled() between construction and start. Closing the
 *    screen frees nothing manually (children are parented to obj and
 *    LVGL frees them recursively in the LVScreen base destructor).
 */
class PhoneGalleryScreen : public LVScreen, private InputListener {
public:
	PhoneGalleryScreen();
	virtual ~PhoneGalleryScreen() override;

	void onStart() override;
	void onStop() override;

	/** Number of thumbnail slots in the grid. Fixed at 4 for S46. */
	static constexpr uint8_t SlotCount = 4;

	/** Grid layout - 2 columns x 2 rows. */
	static constexpr uint8_t GridCols = 2;
	static constexpr uint8_t GridRows = 2;

	/** Per-thumbnail size + spacing on the 160x128 display. */
	static constexpr uint16_t ScreenW    = 160;
	static constexpr uint16_t ScreenH    = 128;
	static constexpr uint16_t ThumbW     = 72;
	static constexpr uint16_t ThumbH     = 40;
	static constexpr uint16_t ThumbGap   = 2;
	static constexpr uint16_t GridY      = 24;   // top of first row (under title strip)
	// Centred horizontally: 2*72 + 2 = 146, so the 14 px slack splits 7 / 7.
	static constexpr uint16_t GridMarginX = (ScreenW - (GridCols * ThumbW + (GridCols - 1) * ThumbGap)) / 2; // 7

	/** How long the transient hint stays on the bottom strip after a
	 *  BTN_ENTER "view" press before snapping back to the idle hint. */
	static constexpr uint32_t HintFlashMs = 900;

	/** Mark a slot as "filled" with a deterministic synthetic capture
	 *  (seed picks the silhouette variant: horizon hue + star positions).
	 *  Setting fillSeed == 0 is treated as "empty" - the same value the
	 *  default-constructed slot starts with. Safe to call before or
	 *  after onStart(); the visual is rebuilt in place either way. */
	void setSlotFilled(uint8_t slotIndex, uint8_t fillSeed);

	/** Returns the current fill seed for a slot (0 = empty). */
	uint8_t getSlotFilled(uint8_t slotIndex) const;

	/** Currently focused slot index (0 .. SlotCount - 1). */
	uint8_t getCursor() const { return cursor; }

	/** Number of currently-filled slots (fillSeed != 0). */
	uint8_t getFilledCount() const;

private:
	// ----- visual children -----
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* titleLabel;     // "GALLERY" - cyan, pixelbasic7
	lv_obj_t* countLabel;     // "0 / 4"   - cream, pixelbasic7
	lv_obj_t* hintLabel;      // "no captures yet" / "EMPTY SLOT" / "SLOT N", dim
	lv_timer_t* hintResetTimer;   // resets "EMPTY SLOT" -> "no captures yet"

	// One container per slot. The container holds the slot's bracket
	// rects, fill rects (when filled) and the "EMPTY"/"PHOTO N" caption
	// so the cursor move can repoint the highlight border at it as a
	// whole rather than walking children.
	lv_obj_t* slotContainer[SlotCount];

	// Per-slot fill seed (0 == empty). Public via setSlotFilled().
	uint8_t   slotFill[SlotCount];

	// Single moving highlight border. Repositioned on every cursor
	// move; never destroyed until the screen tears down.
	lv_obj_t* cursorBorder;

	// ----- state -----
	uint8_t cursor;          // 0 .. SlotCount - 1

	// ----- builders -----
	void buildTitleStrip();
	void buildSlots();
	void buildHint();
	void buildSlotContents(uint8_t slotIndex);   // (re)paint a slot from slotFill[]

	// Helper - flat-colour rect parented at parent, IGNORE_LAYOUT,
	// no border / no padding, like PhoneCameraScreen::makeRect but
	// reparented under an arbitrary lv_obj_t* (so we can put primitives
	// inside a slot container).
	static lv_obj_t* makeRect(lv_obj_t* parent,
							  lv_coord_t x, lv_coord_t y,
							  lv_coord_t w, lv_coord_t h,
							  lv_color_t c);

	// ----- helpers -----
	void moveCursor(int8_t dx, int8_t dy);
	void setCursor(uint8_t newCursor);
	void refreshCursorBorder();
	void refreshCountLabel();
	void flashHint(const char* msg);
	void clearSlotContents(uint8_t slotIndex);

	// LVGL timer callback for the transient hint message.
	static void onHintResetTick(lv_timer_t* t);

	// ----- input -----
	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONEGALLERYSCREEN_H
