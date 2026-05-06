#ifndef MAKERPHONE_PHONEHOMELAYOUTSCREEN_H
#define MAKERPHONE_PHONEHOMELAYOUTSCREEN_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneHomeLayoutScreen
 *
 * Phase-T sub-screen (S185): the home-screen layout-mode picker
 * reachable from the "Home layout" row of PhoneSettingsScreen
 * (S50). Lets the user pick which top-level composition the
 * PhoneHomeScreen renders on the synthwave wallpaper, so the same
 * hardware can present three distinct homescreen "vibes" without
 * rebuilding any widget tree:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |             HOME LAYOUT                | <- pixelbasic7 cyan caption
 *   |                                        |
 *   |  o  CLASSIC      Default everything    | <- option row (selected = dot)
 *   |  o  MINIMAL      Clock + wallpaper     |
 *   |  o  STACK        Adds shortcut hints   |
 *   |                                        |
 *   |       UP / DOWN to choose              | <- pixelbasic7 dim hint
 *   |                                        |
 *   |   SAVE                          BACK   | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * The three options map 1:1 onto SettingsData::homeLayoutMode:
 *
 *   - Classic  (0) - factory default; status bar + operator banner
 *                    (when populated) + big clock face + rotating
 *                    tip / idle hints + soft-key bar exactly the
 *                    way every prior firmware shipped, so a freshly-
 *                    flashed device looks byte-identical out of the
 *                    box.
 *   - Minimal  (1) - operator banner hidden and the rotating tip
 *                    banner / idle hint gated off, leaving just the
 *                    status bar, the clock face and the soft-key
 *                    bar floating on the synthwave wallpaper. Reads
 *                    as a quiet "watch face" mode for users who do
 *                    not want any text on their homescreen apart
 *                    from the time.
 *   - Stack    (2) - operator banner hidden, tip banner / idle hint
 *                    gated off, and a static "HOLD 0:DIAL HOLD #:
 *                    LOCK" shortcut hint painted in the empty
 *                    wallpaper band between the clock face and the
 *                    soft-key bar. Surfaces the long-press gestures
 *                    muscle-memory expects from a Sony-Ericsson
 *                    feature phone so a new user can discover them
 *                    without reaching for the manual.
 *
 * Behaviour:
 *  - LEFT / 2 / 4 step up; RIGHT / 6 / 8 step down. Hard stops at
 *    either end (three rows is short enough that wrap-around feels
 *    less natural than for a longer list). Mirrors the keypad map
 *    PhoneLockWidgetScreen / PhoneHapticsScreen / PhoneSoundScreen
 *    rely on so the cluster of single-list pickers reads as one
 *    visual family.
 *  - On every cursor step the soft-key labels flip to the dirty-aware
 *    "SAVE" / "CANCEL" pair, matching the sibling screens above.
 *  - ENTER (SAVE) writes Settings.get().homeLayoutMode and
 *    Settings.store(); BACK pops without persisting -- the standard
 *    Sony-Ericsson "discard changes" affordance.
 *
 * Implementation notes:
 *  - Code-only, zero SPIFFS. Reuses PhoneSynthwaveBg / PhoneStatusBar /
 *    PhoneSoftKeyBar to stay visually part of the MAKERphone family.
 *  - 160x128 budget: 10 px status bar, caption strip at y=12, three
 *    18 px option rows centred between y=30..84, hint label at y=92,
 *    soft-key bar at y=118..128. The cursor highlight is 18 px tall
 *    and slides in 18 px steps to perfectly cover a focused row.
 *  - The Mode enum mirrors the persisted byte exactly so a host /
 *    test can reason about state without reaching into Settings.
 */
class PhoneHomeLayoutScreen : public LVScreen, private InputListener {
public:
	/** Persisted-byte-aligned layout mode enum. */
	enum class Mode : uint8_t {
		Classic = 0,    // factory default: full Sony-Ericsson silhouette
		Minimal = 1,    // status bar + clock + soft-keys only
		Stack   = 2,    // adds a static shortcut hint line
	};

	/** Number of selectable rows. */
	static constexpr uint8_t OptionCount = 3;

	/** Per-row height (matches the cursor rect size). */
	static constexpr lv_coord_t RowH  = 18;

	/** Top edge of the first option row inside the screen. */
	static constexpr lv_coord_t ListY = 30;

	/** Total list-area width (full screen minus 4 px margins). */
	static constexpr lv_coord_t ListW = 152;

	PhoneHomeLayoutScreen();
	virtual ~PhoneHomeLayoutScreen() override;

	void onStart() override;
	void onStop() override;

	/** Currently focused (cursor) mode. */
	Mode getFocusedMode() const;

	/** Mode as persisted at the moment the screen was opened. */
	Mode getInitialMode() const { return initialMode; }

	/** Clamp a raw byte from Settings into a valid Mode. Out-of-range
	 *  values fall back to Classic, the factory default. */
	static Mode modeFromByte(uint8_t b);

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
		Mode        mode;
	};
	Row rows[OptionCount];

	uint8_t cursor      = 0;
	Mode    initialMode = Mode::Classic;

	void buildCaption();
	void buildListContainer();
	void buildList();
	void buildHint();

	/**
	 * Dirty-aware soft-key labels. Pristine state ("" / "BACK") while
	 * the focused mode matches the persisted one; dirty state
	 * ("SAVE" / "CANCEL") once the user has stepped onto a different
	 * mode. Same affordance PhoneLockWidgetScreen / PhoneHapticsScreen /
	 * PhoneSoundScreen use.
	 */
	void refreshSoftKeys();

	void refreshHighlight();
	void refreshCheckmarks(Mode savedMode);
	void moveCursorBy(int8_t delta);

	void saveAndExit();
	void cancelAndExit();

	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONEHOMELAYOUTSCREEN_H
