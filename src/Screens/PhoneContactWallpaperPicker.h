#ifndef MAKERPHONE_PHONECONTACTWALLPAPERPICKER_H
#define MAKERPHONE_PHONECONTACTWALLPAPERPICKER_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"
#include "../Types.hpp"
#include "../Elements/PhoneSynthwaveBg.h"

class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * S181 — PhoneContactWallpaperPicker
 *
 * Per-contact wallpaper selector reachable from PhoneContactDetail
 * (long-press BTN_6). Lets the user pin one of the four
 * PhoneSynthwaveBg styles (Synthwave / Plain / GridOnly / Stars) to
 * a specific contact, or fall back to the global wallpaper choice
 * via an explicit "INHERIT" row at the top of the list.
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |             WALLPAPER                  | <- pixelbasic7 cyan caption
 *   |   *  Inherit                           |
 *   |   o  Synthwave                         |
 *   |   o  Plain               <- cursor     |
 *   |   o  Grid Only                         |
 *   |   o  Stars                             |
 *   |                                        |
 *   |   PICK                          BACK   | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * Behaviour mirrors PhoneContactRingtonePicker so the muscle memory
 * transfers cleanly between the two per-contact override pickers:
 *  - BTN_2 / BTN_LEFT  -> cursor up (clamps; no wrap on a short list).
 *  - BTN_8 / BTN_RIGHT -> cursor down.
 *  - BTN_ENTER         -> live-preview the focused style by swapping
 *                         the screen's own background.  A second
 *                         press restores the saved one.
 *  - BTN_L (PICK)      -> persist the focused entry via
 *                         PhoneContacts::setWallpaper / clearWallpaper
 *                         and pop.  Picking the INHERIT row clears
 *                         the override (HasWallpaper flag off).
 *  - BTN_R / BTN_BACK  -> pop without persisting.  Long-press BTN_BACK
 *                         bails to homescreen, the convention every
 *                         Phase-D / Phase-F screen uses.
 *
 * uid == 0 is legal — onPick still fires but persistence is skipped.
 *
 * Implementation notes:
 *  - 100 % code-only.  Reuses PhoneSynthwaveBg / PhoneStatusBar /
 *    PhoneSoftKeyBar so the screen reads as part of the same
 *    MAKERphone family.
 *  - Live preview tears down the screen's PhoneSynthwaveBg and
 *    instantiates a new one with the focused Style at the bottom of
 *    LVGL's z-order.  Cheap because the background widget is
 *    self-contained — we never touch the rest of the screen tree.
 */
class PhoneContactWallpaperPicker : public LVScreen, private InputListener {
public:
	using PickHandler = void (*)(PhoneContactWallpaperPicker* self,
	                             bool inheritGlobal,
	                             uint8_t styleByte);

	/** Build a picker for `uid`. The cursor is initialised to the
	 *  contact's currently-saved wallpaper id (or INHERIT when the
	 *  contact has no override).  uid == 0 is legal: onPick still
	 *  fires but persistence is skipped. */
	explicit PhoneContactWallpaperPicker(UID_t uid);

	virtual ~PhoneContactWallpaperPicker() override;

	void onStart() override;
	void onStop() override;

	/** Bind the PICK softkey.  Default behaviour with no callback
	 *  wired is a flash + persist + pop. */
	void setOnPick(PickHandler cb);

	/** Replace the visible label of the left softkey (default "PICK"). */
	void setLeftLabel(const char* label);
	/** Replace the visible label of the right softkey (default "BACK"). */
	void setRightLabel(const char* label);

	/** Read-only accessor for the bound UID. */
	UID_t getUid() const { return uid; }

	void flashLeftSoftKey();
	void flashRightSoftKey();

	/** Total entries on the picker: INHERIT + 4 styles. */
	static constexpr uint8_t EntryCount  = 5;

	/** Pixel height of a single row. */
	static constexpr lv_coord_t RowHeight = 12;

private:
	PhoneSynthwaveBg* wallpaper      = nullptr;
	PhoneStatusBar*   statusBar      = nullptr;
	PhoneSoftKeyBar*  softKeys       = nullptr;

	lv_obj_t* captionLabel           = nullptr;
	lv_obj_t* listContainer          = nullptr;
	lv_obj_t* cursorRect             = nullptr;
	lv_obj_t* rows[EntryCount]       = {nullptr};
	lv_obj_t* savedDots[EntryCount]  = {nullptr};

	uint8_t   cursor                 = 0;
	uint8_t   savedIndex             = 0;
	bool      previewing             = false;
	uint8_t   previewedIndex         = 0;

	UID_t        uid                 = 0;
	PickHandler  pickCb              = nullptr;

	bool backHoldFired               = false;

	void buildLayout();
	void buildList();
	void rebuildEntries();

	void moveCursor(int8_t dir);
	void refreshCursor();
	void refreshSavedMarks();

	void startPreview();
	void stopPreview();
	void rebuildWallpaperFor(uint8_t entryIndex);

	void confirmPick();
	void invokeBack();

	/** Map an entry index to its raw `PhoneSynthwaveBg::Style` byte.
	 *  Entry 0 (INHERIT) returns 0xFF to flag "no override". */
	static uint8_t entryStyleByte(uint8_t entryIndex);
	static const char* entryName(uint8_t entryIndex);

	void buttonPressed(uint i) override;
	void buttonHeld(uint i) override;
	void buttonReleased(uint i) override;
};

#endif // MAKERPHONE_PHONECONTACTWALLPAPERPICKER_H
