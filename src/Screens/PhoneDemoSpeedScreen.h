#ifndef MAKERPHONE_PHONEDEMOSPEEDSCREEN_H
#define MAKERPHONE_PHONEDEMOSPEEDSCREEN_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"
#include "PhoneDemoModeScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneDemoSpeedScreen
 *
 * S206 - the user-tunable slide-pace picker for PhoneDemoModeScreen
 * (S200) reachable from the new "Demo speed" row of PhoneSettingsScreen
 * (sits in the ADVANCED group, directly above the existing "Demo mode"
 * entry). The v2.0 demo deck shipped with a hard-coded 3 s slide
 * period (kSlidePeriodMs in PhoneDemoModeScreen.cpp), which is fine
 * for the marketing-video unattended shoot but feels rushed when a
 * release engineer wants to read each slide in detail at a desk or,
 * conversely, drag-y when someone is just sanity-checking the loop in
 * passing. This picker exposes that period as a three-state preset
 * (Slow / Medium / Fast) the user can flip without recompiling the
 * firmware.
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |             DEMO SPEED                 | <- pixelbasic7 cyan caption
 *   |                                        |
 *   |  o  SLOW          5 s per slide        | <- option row (selected = dot)
 *   |  o  MEDIUM        3 s per slide        |
 *   |  o  FAST          1.5 s per slide      |
 *   |                                        |
 *   |       UP / DOWN to choose              | <- pixelbasic7 dim hint
 *   |                                        |
 *   |   SAVE                          BACK   | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * The three options map 1:1 onto SettingsData::demoSpeed:
 *
 *   - Slow   (1) - 5000 ms / slide. Comfortable read-each-slide pace
 *                  for a desk demo.
 *   - Medium (0) - 3000 ms / slide. Factory default; byte-identical
 *                  to the pre-S206 hard-coded period so a freshly-
 *                  flashed device that has never touched the row
 *                  behaves exactly like every prior firmware on the
 *                  demo deck.
 *   - Fast   (2) - 1500 ms / slide. Rapid sanity-check pace for an
 *                  at-a-glance loop.
 *
 * Behaviour:
 *  - LEFT / 2 / 4 step up; RIGHT / 6 / 8 step down. Hard stops at
 *    either end (three rows is short enough that wrap-around feels
 *    less natural than for a longer list), matching the sibling
 *    PhoneLockWidgetScreen / PhoneHomeLayoutScreen pattern.
 *  - On every cursor step the soft-key labels flip to the dirty-aware
 *    "SAVE" / "CANCEL" pair, matching PhoneLockWidgetScreen so the
 *    DISPLAY / ADVANCED group reads as a single visual family.
 *  - ENTER (SAVE) writes Settings.get().demoSpeed and Settings.store();
 *    BACK pops without persisting -- the standard Sony-Ericsson
 *    "discard changes" affordance.
 *
 * Implementation notes:
 *  - Code-only, zero SPIFFS. Reuses PhoneSynthwaveBg / PhoneStatusBar /
 *    PhoneSoftKeyBar to stay visually part of the MAKERphone family.
 *  - 160x128 budget: 10 px status bar, caption strip at y=12, three
 *    18 px option rows centred between y=30..84, hint label at y=92,
 *    soft-key bar at y=118..128. Cursor highlight is 18 px tall and
 *    slides in 18 px steps.
 *  - The Speed enum is reused from PhoneDemoModeScreen so the picker
 *    and the screen it configures share a single source of truth for
 *    the encoding; the byte-aligned values match SettingsData::demoSpeed
 *    1:1 so a host or test reading the byte directly does not need a
 *    translation table.
 */
class PhoneDemoSpeedScreen : public LVScreen, private InputListener {
public:
	using Speed = PhoneDemoModeScreen::Speed;

	/** Number of selectable rows. */
	static constexpr uint8_t OptionCount = 3;

	/** Per-row height (matches the cursor rect size). */
	static constexpr lv_coord_t RowH  = 18;

	/** Top edge of the first option row inside the screen. */
	static constexpr lv_coord_t ListY = 30;

	/** Total list-area width (full screen minus 4 px margins). */
	static constexpr lv_coord_t ListW = 152;

	PhoneDemoSpeedScreen();
	virtual ~PhoneDemoSpeedScreen() override;

	void onStart() override;
	void onStop() override;

	/** Currently focused (cursor) speed. */
	Speed getFocusedSpeed() const;

	/** Speed as persisted at the moment the screen was opened. */
	Speed getInitialSpeed() const { return initialSpeed; }

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
		Speed       speed;
	};
	Row rows[OptionCount];

	uint8_t cursor       = 0;
	Speed   initialSpeed = Speed::Medium;

	void buildCaption();
	void buildListContainer();
	void buildList();
	void buildHint();

	/**
	 * Dirty-aware soft-key labels. Pristine state ("" / "BACK") while
	 * the focused speed matches the persisted one; dirty state
	 * ("SAVE" / "CANCEL") once the user has stepped onto a different
	 * speed. Same affordance PhoneLockWidgetScreen / PhoneHomeLayoutScreen
	 * use.
	 */
	void refreshSoftKeys();

	void refreshHighlight();
	void refreshCheckmarks(Speed savedSpeed);
	void moveCursorBy(int8_t delta);

	void saveAndExit();
	void cancelAndExit();

	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONEDEMOSPEEDSCREEN_H
