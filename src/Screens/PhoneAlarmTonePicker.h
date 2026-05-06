#ifndef MAKERPHONE_PHONEALARMTONEPICKER_H
#define MAKERPHONE_PHONEALARMTONEPICKER_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"
#include "../Services/PhoneAlarmTone.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * S193 - PhoneAlarmTonePicker
 *
 * Phase-U sub-screen reachable from the SOUND group of
 * PhoneSettingsScreen via the new "Alarm tone" row. Lets the user
 * pick which melody PhoneAlarmService rings when a slot fires --
 * the "composer-fed" half of S193 lives in the picker iterating
 * PhoneAlarmTone::pickerCount() / pickerIdAt(), so a freshly-flashed
 * device shows just Factory + the five PhoneRingtoneLibrary entries
 * and a user who has saved compositions in PhoneComposer
 * automatically sees those slots appear without any extra wiring.
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |             ALARM TONE                 | <- pixelbasic7 cyan caption
 *   |   o  Factory                           | <- focused row (cursor highlight)
 *   |   *  Synthwave         <- saved row    | <- "*" = currently saved id
 *   |   o  Classic                           |
 *   |   o  Beep                              |
 *   |   o  Boss                              |
 *   |   o  Silent                            |
 *   |   o  * MyRing                          | <- composer slot row (only
 *   |                                        |    appears when populated)
 *   |   PICK                          BACK   | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * Behaviour:
 *  - BTN_2 / BTN_LEFT  -> cursor up (clamps; no wrap on a short list).
 *  - BTN_8 / BTN_RIGHT -> cursor down.
 *  - BTN_ENTER         -> toggle preview through PhoneRingtoneEngine
 *                          (a second press stops the preview).
 *  - BTN_L (PICK)      -> persist the focused id via
 *                          PhoneAlarmTone::setActiveId (also calls
 *                          Settings.store()), stop any preview, and pop.
 *  - BTN_R / BTN_BACK  -> pop without persisting. Long-press BTN_BACK
 *                          bails to homescreen, the convention every
 *                          settings sub-screen uses.
 *
 * Implementation notes:
 *  - 100 % code-only, no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *    PhoneStatusBar / PhoneSoftKeyBar so the screen reads as part of
 *    the same MAKERphone family as every other Phase-U / Phase-T
 *    sibling.
 *  - The entry table uses MaxEntries = 10 (Factory + 5 library + 4
 *    composer) so a max-populated user environment fits without
 *    paging logic; the body band's six visible rows cover the common
 *    case (Factory + 5 library) cleanly, so scrolling only kicks in
 *    when a user has saved at least one composer slot.
 *  - The "saved" glyph in front of the row is a sunset-orange dot
 *    that re-uses the same pattern PhoneSoftKeyToneScreen and
 *    PhoneContactRingtonePicker established for visual consistency
 *    across the SOUND group.
 */
class PhoneAlarmTonePicker : public LVScreen, private InputListener {
public:
	PhoneAlarmTonePicker();
	virtual ~PhoneAlarmTonePicker() override;

	void onStart() override;
	void onStop() override;

	/** Maximum entries the picker can render before the list scrolls.
	 *  Factory (1) + library (5) + composer slots (4) = 10 absolute max. */
	static constexpr uint8_t MaxEntries     = PhoneAlarmTone::MaxPickerEntries;

	/** Number of visible rows inside the body band. Anything past
	 *  this scrolls the list. */
	static constexpr uint8_t VisibleRows    = 6;

	/** Pixel height of a single row. */
	static constexpr lv_coord_t RowHeight   = 12;

	/** Body-band geometry exposed for unit-test friendliness. */
	static constexpr lv_coord_t BodyY       = 22;
	static constexpr lv_coord_t BodyW       = 152;
	static constexpr lv_coord_t BodyX       = 4;

	/** Long-press threshold for BTN_BACK (matches the rest of the
	 *  settings sub-screens). */
	static constexpr uint16_t   BackHoldMs  = 600;

	/** Currently focused id (i.e. row under the cursor). */
	uint8_t getFocusedId() const;

	/** Id saved in NVS at screen-open. BACK does NOT revert -- the
	 *  picker only persists on PICK -- so this is purely informational
	 *  for unit-test introspection. */
	uint8_t getSavedId() const { return savedId; }

private:
	PhoneSynthwaveBg* wallpaper      = nullptr;
	PhoneStatusBar*   statusBar      = nullptr;
	PhoneSoftKeyBar*  softKeys       = nullptr;

	lv_obj_t* captionLabel           = nullptr;
	lv_obj_t* listContainer          = nullptr;
	lv_obj_t* cursorRect             = nullptr;
	lv_obj_t* rows[MaxEntries]       = {nullptr};
	lv_obj_t* savedDots[MaxEntries]  = {nullptr};
	uint8_t   ids[MaxEntries]        = {0};
	uint8_t   entryCount             = 0;
	uint8_t   cursor                 = 0;
	uint8_t   savedId                = PhoneAlarmTone::DefaultId;
	uint8_t   topVisible             = 0;
	bool      previewing             = false;
	bool      backHoldFired          = false;

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

#endif // MAKERPHONE_PHONEALARMTONEPICKER_H
