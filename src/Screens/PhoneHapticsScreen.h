#ifndef MAKERPHONE_PHONEHAPTICSSCREEN_H
#define MAKERPHONE_PHONEHAPTICSSCREEN_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneHapticsScreen
 *
 * Phase-M sub-screen (S68): the toggle for the very-short, very-quiet
 * "haptic-style" buzzer ticks that the BuzzerService emits on navigation
 * keys when the device is in Mute / Vibrate (`Settings.sound == false`).
 * Reached from PhoneSettingsScreen via the SOUND section's new
 * "Key clicks" row, this screen is a binary radio-chooser exactly
 * mirroring PhoneSoundScreen's shape so the SOUND group reads as a
 * single visual family:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |             KEY CLICKS                 | <- pixelbasic7 cyan caption
 *   |                                        |
 *   |  o  ON       Subtle ticks              | <- option row (selected = dot)
 *   |  o  OFF      No feedback               |
 *   |                                        |
 *   |       UP / DOWN to choose              | <- pixelbasic7 dim hint
 *   |                                        |
 *   |   SAVE                          BACK   | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * Behaviour:
 *  - LEFT / 2 / 4 step up; RIGHT / 6 / 8 step down. No wrap-around
 *    (two rows is short enough that hard stops feel cleaner).
 *  - On every cursor step we live-preview the chosen state by firing a
 *    single 4 ms tick when ON is focused (so the user can hear-feel
 *    what they will be opting into) and silence when OFF is focused.
 *    The preview honours the live `keyTicks` field via Piezo directly,
 *    so previewing ON works even if Settings.sound is false.
 *  - ENTER (SAVE) writes Settings.get().keyTicks and Settings.store();
 *    BACK reverts in-memory state and pop()s. Standard
 *    Sony-Ericsson "discard changes" affordance.
 *
 * Implementation notes:
 *  - Code-only, zero SPIFFS. Reuses PhoneSynthwaveBg / PhoneStatusBar /
 *    PhoneSoftKeyBar to stay visually part of the MAKERphone family.
 *  - 160x128 budget: 10 px status bar, caption strip at y=12, two 18 px
 *    option rows centred between y=36..72, hint label at y=100, soft-key
 *    bar at y=118..128. The cursor highlight is 18 px tall and slides
 *    in 18 px steps to perfectly cover a focused row.
 */
class PhoneHapticsScreen : public LVScreen, private InputListener {
public:
	PhoneHapticsScreen();
	virtual ~PhoneHapticsScreen() override;

	void onStart() override;
	void onStop() override;

	/** Number of selectable rows (ON / OFF). */
	static constexpr uint8_t OptionCount = 2;

	/** Per-row height (matches the cursor rect size). */
	static constexpr lv_coord_t RowH  = 18;

	/** Top edge of the first option row inside the screen. */
	static constexpr lv_coord_t ListY = 36;

	/** Total list-area width (full screen minus 4 px margins). */
	static constexpr lv_coord_t ListW = 152;

	/** Currently focused (cursor) state. true == ON row, false == OFF row. */
	bool getFocusedOn() const;

	/** State as persisted at the moment the screen was opened. */
	bool getInitialOn() const { return initialOn; }

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* captionLabel;
	lv_obj_t* listContainer;
	lv_obj_t* highlight;
	lv_obj_t* hintLabel;

	struct Row {
		lv_obj_t*   dotObj;
		lv_obj_t*   nameObj;
		lv_obj_t*   descObj;
		lv_coord_t  y;
		bool        on;       // true for the ON row, false for the OFF row
	};
	Row rows[OptionCount];

	uint8_t cursor   = 0;
	bool    initialOn = false;

	void buildCaption();
	void buildListContainer();
	void buildList();
	void buildHint();

	/**
	 * Dirty-aware soft-key labels (matches the PhoneSoundScreen
	 * affordance): pristine state shows "" / "BACK"; dirty state
	 * shows "SAVE" / "CANCEL". Lets the user see at a glance whether
	 * ENTER will commit a change or no-op.
	 */
	void refreshSoftKeys();

	void refreshHighlight();
	void refreshCheckmarks(bool savedOn);
	void moveCursorBy(int8_t delta);

	/**
	 * Audible preview of the focused option. Fires a single 4 ms / F6
	 * tick when ON is under the cursor, silence when OFF -- matching
	 * exactly what BuzzerService::buttonPressed will produce on real
	 * navigation keys once the user commits the choice.
	 */
	void applyPreview(bool on);

	void saveAndExit();
	void cancelAndExit();

	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONEHAPTICSSCREEN_H
