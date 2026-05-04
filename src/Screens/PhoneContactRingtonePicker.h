#ifndef MAKERPHONE_PHONECONTACTRINGTONEPICKER_H
#define MAKERPHONE_PHONECONTACTRINGTONEPICKER_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"
#include "../Types.hpp"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * S153 — PhoneContactRingtonePicker
 *
 * List-style selector that lets the user choose a ringtone for a
 * specific contact. The picker iterates `PhoneContactRingtone`'s
 * picker table (5 library tones plus any populated composer slots)
 * and shows them as a vertical list:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |             RINGTONE                   | <- pixelbasic7 cyan caption
 *   |   o  Synthwave                         |
 *   |   o  Classic                           | <- cursor (translucent dim
 *   |   *  Beep             <- saved choice  |    purple) slides over the
 *   |   o  Boss                              |    focused row; "*" marks
 *   |   o  Silent                            |    the currently saved id
 *   |   o  * MyRing                          |
 *   |   PICK                          BACK   | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * Behaviour:
 *  - BTN_2 / BTN_LEFT  -> cursor up (clamps; no wrap on a short list).
 *  - BTN_8 / BTN_RIGHT -> cursor down.
 *  - BTN_ENTER         -> preview the focused ringtone through the
 *                         shared PhoneRingtoneEngine. A second press
 *                         stops the preview.
 *  - BTN_LEFT softkey  -> persist the focused id via
 *                         `PhoneContacts::setRingtone`, fire the
 *                         registered onPick callback (if any), and
 *                         pop. Auto-stops the preview on the way out.
 *  - BTN_RIGHT softkey / BTN_BACK -> pop without persisting. Long-
 *                         press BTN_BACK bails to homescreen, the
 *                         convention every Phase-D / Phase-F screen
 *                         uses.
 *
 * The picker is uid-aware so it can persist on confirm without the
 * caller threading state. Pushing the picker with `uid == 0` is
 * still legal — the picker still fires `onPick` with the chosen id
 * but skips the persistence step, useful for placeholder rows.
 *
 * Implementation notes:
 *  - 100 % code-only. Reuses PhoneSynthwaveBg / PhoneStatusBar /
 *    PhoneSoftKeyBar so the screen reads as part of the same
 *    MAKERphone family.
 *  - The list scrolls when it grows past the visible row count
 *    (max 6 visible at the standard 12 px row height inside the
 *    body band y = 22..94). The cursor is the always-visible focus
 *    indicator; rows simply slide vertically when the cursor would
 *    leave the band.
 *  - The "saved" glyph in front of the row is a dim asterisk
 *    rendered with the existing pixelbasic7 font so we don't need
 *    a custom symbol image in SPIFFS.
 */
class PhoneContactRingtonePicker : public LVScreen, private InputListener {
public:
	using PickHandler = void (*)(PhoneContactRingtonePicker* self,
								 uint8_t pickedId);

	/** Build a picker for `uid`. The cursor is initialised to the
	 *  contact's currently-saved ringtone id (or DefaultId if the
	 *  contact has none). Caller may pass `uid == 0` for a stateless
	 *  preview / chooser — onPick still fires but persistence is
	 *  skipped. */
	explicit PhoneContactRingtonePicker(UID_t uid);

	virtual ~PhoneContactRingtonePicker() override;

	void onStart() override;
	void onStop() override;

	/** Bind the PICK softkey. Default behaviour with no callback
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

	/** Maximum entries the picker can render before the list scrolls.
	 *  Library (5) + composer slots (4) = 9 absolute max. */
	static constexpr uint8_t MaxEntries     = 9;

	/** Number of visible rows inside the body band. Anything past
	 *  this scrolls the list. */
	static constexpr uint8_t VisibleRows    = 6;

	/** Pixel height of a single row. */
	static constexpr lv_coord_t RowHeight   = 12;

private:
	PhoneSynthwaveBg* wallpaper     = nullptr;
	PhoneStatusBar*   statusBar     = nullptr;
	PhoneSoftKeyBar*  softKeys      = nullptr;

	lv_obj_t* captionLabel          = nullptr;
	lv_obj_t* listContainer         = nullptr;
	lv_obj_t* cursorRect            = nullptr;
	lv_obj_t* rows[MaxEntries]      = {nullptr};
	lv_obj_t* savedDots[MaxEntries] = {nullptr};
	uint8_t   ids[MaxEntries]       = {0};
	uint8_t   entryCount            = 0;
	uint8_t   cursor                = 0;
	uint8_t   savedId               = 0;
	uint8_t   topVisible            = 0;
	bool      previewing            = false;

	UID_t        uid                = 0;
	PickHandler  pickCb             = nullptr;

	bool backHoldFired              = false;

	void buildLayout();
	void buildList();
	void rebuildEntries();

	void moveCursor(int8_t dir);
	void refreshCursor();
	void refreshSavedMarks();
	void scrollIntoView();

	void startPreview();
	void stopPreview();

	void confirmPick();
	void invokeBack();

	void buttonPressed(uint i) override;
	void buttonHeld(uint i) override;
	void buttonReleased(uint i) override;
};

#endif // MAKERPHONE_PHONECONTACTRINGTONEPICKER_H
