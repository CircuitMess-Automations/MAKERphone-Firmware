#ifndef MAKERPHONE_PHONESOFTKEYTONESCREEN_H
#define MAKERPHONE_PHONESOFTKEYTONESCREEN_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"
#include "../Services/PhoneSoftKeyTone.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneSoftKeyToneScreen (S183)
 *
 * Phase-T sub-screen reachable from the SOUND group of
 * PhoneSettingsScreen via the new "Softkey tone" row. Lets the user
 * pick which of the five PhoneSoftKeyToneLib entries the BuzzerService
 * plays when the user taps BTN_LEFT or BTN_RIGHT (the two
 * Sony-Ericsson-style soft-key hardware buttons that PhoneSoftKeyBar
 * visually labels).
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |             SOFTKEY TONE               | <- pixelbasic7 cyan caption
 *   |                                        |
 *   |  o  CLASSIC    B4 / 25 ms              | <- option row (selected = dot)
 *   |  o  CLICK      Snappy                  |
 *   |  o  BLOOP      Low blip                |
 *   |  o  CHIRP      Mid chirp               |
 *   |  o  SILENT     No tone                 |
 *   |                                        |
 *   |        UP / DOWN to choose             | <- pixelbasic7 dim hint
 *   |   SAVE                          BACK   | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * Behaviour:
 *  - LEFT / 2 / 4 step the cursor up; RIGHT / 6 / 8 step it down. No
 *    wrap-around -- a 5-element list is short enough that hard stops
 *    feel cleaner than wrapping (matches PhoneSoundScreen / PhoneProfileScreen).
 *  - On every cursor step we live-preview the focused tone via
 *    PhoneSoftKeyToneLib::play(), bypassing the Settings.sound gate so
 *    the user can hear the difference even if the device is currently
 *    in Mute / Vibrate. The preview uses a single Piezo.tone() call
 *    so it never collides with an in-flight ringtone.
 *  - ENTER (SAVE softkey) writes Settings.get().softKeyTone via
 *    PhoneSoftKeyToneLib::setActive() (which calls Settings.store()),
 *    flashes the left soft-key, and pop()s. BACK reverts in-memory
 *    state and pop()s -- the standard Sony-Ericsson "discard changes"
 *    affordance for option screens.
 *
 * Implementation notes:
 *  - Code-only, zero SPIFFS. Reuses PhoneSynthwaveBg / PhoneStatusBar /
 *    PhoneSoftKeyBar so the screen feels visually part of the rest of
 *    the MAKERphone family.
 *  - 160x128 budget: 10 px status bar, caption strip at y=12, five 14 px
 *    option rows centred between y=24..94, hint label at y=100, soft-key
 *    bar at y=118..128. The cursor highlight is 14 px tall and slides
 *    in 14 px steps so it perfectly covers a focused row.
 *  - The "saved" dot indicator on the left of each row is keyed off
 *    whether the row matches the currently saved tone id, so saved-vs-
 *    focused are visually distinct (cursor highlight vs. accent dot).
 */
class PhoneSoftKeyToneScreen : public LVScreen, private InputListener {
public:
	PhoneSoftKeyToneScreen();
	virtual ~PhoneSoftKeyToneScreen() override;

	void onStart() override;
	void onStop() override;

	/** Number of selectable rows (mirrors PhoneSoftKeyToneLib::Count). */
	static constexpr uint8_t OptionCount = PhoneSoftKeyToneLib::Count;

	/** Per-row height (matches the cursor rect size). */
	static constexpr lv_coord_t RowH = 14;

	/** Top edge of the first option row inside the screen. */
	static constexpr lv_coord_t ListY = 24;

	/** Total list-area width (full screen minus 4 px margins). */
	static constexpr lv_coord_t ListW = 152;

	/** Tone id currently under the cursor (i.e. focused). */
	uint8_t getFocusedId() const;

	/** Tone id saved at screen-open (BACK reverts to this). */
	uint8_t getInitialId() const { return initialId; }

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
		uint8_t     toneId;
	};
	Row rows[OptionCount];

	uint8_t cursor    = 0;
	uint8_t initialId = PhoneSoftKeyToneLib::DefaultId;

	void buildCaption();
	void buildListContainer();
	void buildList();
	void buildHint();

	/**
	 * Dirty-aware soft-key labels (matches PhoneSoundScreen /
	 * PhoneHapticsScreen): pristine state shows "" / "BACK"; dirty
	 * state shows "SAVE" / "CANCEL". Lets the user see at a glance
	 * whether ENTER will commit a change or no-op.
	 */
	void refreshSoftKeys();

	void refreshHighlight();
	void refreshCheckmarks(uint8_t savedId);
	void moveCursorBy(int8_t delta);

	/** Audible preview of the focused tone (no Settings.sound gate). */
	void applyPreview(uint8_t toneId);

	void saveAndExit();
	void cancelAndExit();

	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONESOFTKEYTONESCREEN_H
