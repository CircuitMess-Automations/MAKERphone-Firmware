#ifndef MAKERPHONE_PHONELOCKWIDGETSCREEN_H
#define MAKERPHONE_PHONELOCKWIDGETSCREEN_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneLockWidgetScreen
 *
 * Phase-T sub-screen (S184): the lock-screen widget composition picker
 * reachable from the "Lock widget" row of PhoneSettingsScreen (S50).
 * Lets the user pick which secondary line(s) the LockScreen renders
 * below the big clock face, so the same hardware can present three
 * distinct "lock-screen vibes" without rebuilding any widget tree:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |             LOCK WIDGET                | <- pixelbasic7 cyan caption
 *   |                                        |
 *   |  o  CLOCK ONLY    HH:MM only           | <- option row (selected = dot)
 *   |  o  CLOCK + DATE  Adds weekday & date  |
 *   |  o  CLOCK + EVENT Next alarm preview   |
 *   |                                        |
 *   |       UP / DOWN to choose              | <- pixelbasic7 dim hint
 *   |                                        |
 *   |   SAVE                          BACK   | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * The three options map 1:1 onto SettingsData::lockWidgetMode:
 *
 *   - ClockOnly  (1) - hides the weekday + date rows entirely so the
 *                      lock screen reads as a minimalist HH:MM watch
 *                      face. Useful for users who want the synthwave
 *                      wallpaper to breathe without the date typography
 *                      stealing focus.
 *   - ClockDate  (0) - factory default; the classic feature-phone
 *                      lock screen with weekday + day-of-month + month
 *                      + year underneath the big clock.
 *   - ClockEvent (2) - hides the date rows and instead paints the next
 *                      armed PhoneAlarmService alarm ("NEXT ALARM 07:00")
 *                      in their place, so the lock screen previews the
 *                      user's closest pending event the way a Sony-
 *                      Ericsson agenda widget would. When no alarm is
 *                      armed the line falls back to "NO ALARMS SET" in
 *                      the dim caption colour rather than going blank.
 *
 * Behaviour:
 *  - LEFT / 2 / 4 step up; RIGHT / 6 / 8 step down. Hard stops at
 *    either end (three rows is short enough that wrap-around feels
 *    less natural than for a longer list).
 *  - On every cursor step the soft-key labels flip to the dirty-aware
 *    "SAVE" / "CANCEL" pair, matching PhoneHapticsScreen / PhoneSoundScreen
 *    so the SOUND / DISPLAY group reads as a single visual family.
 *  - ENTER (SAVE) writes Settings.get().lockWidgetMode and Settings.store();
 *    BACK pops without persisting -- the standard Sony-Ericsson "discard
 *    changes" affordance.
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
class PhoneLockWidgetScreen : public LVScreen, private InputListener {
public:
	/** Persisted-byte-aligned widget mode enum. */
	enum class Mode : uint8_t {
		ClockDate  = 0,    // factory default: clock + weekday + date
		ClockOnly  = 1,    // HH:MM only
		ClockEvent = 2,    // clock + next alarm preview
	};

	/** Number of selectable rows. */
	static constexpr uint8_t OptionCount = 3;

	/** Per-row height (matches the cursor rect size). */
	static constexpr lv_coord_t RowH  = 18;

	/** Top edge of the first option row inside the screen. */
	static constexpr lv_coord_t ListY = 30;

	/** Total list-area width (full screen minus 4 px margins). */
	static constexpr lv_coord_t ListW = 152;

	PhoneLockWidgetScreen();
	virtual ~PhoneLockWidgetScreen() override;

	void onStart() override;
	void onStop() override;

	/** Currently focused (cursor) mode. */
	Mode getFocusedMode() const;

	/** Mode as persisted at the moment the screen was opened. */
	Mode getInitialMode() const { return initialMode; }

	/** Clamp a raw byte from Settings into a valid Mode. Out-of-range
	 *  values fall back to ClockDate, the factory default. */
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
	Mode    initialMode = Mode::ClockDate;

	void buildCaption();
	void buildListContainer();
	void buildList();
	void buildHint();

	/**
	 * Dirty-aware soft-key labels. Pristine state ("" / "BACK") while
	 * the focused mode matches the persisted one; dirty state
	 * ("SAVE" / "CANCEL") once the user has stepped onto a different
	 * mode. Same affordance PhoneHapticsScreen / PhoneSoundScreen use.
	 */
	void refreshSoftKeys();

	void refreshHighlight();
	void refreshCheckmarks(Mode savedMode);
	void moveCursorBy(int8_t delta);

	void saveAndExit();
	void cancelAndExit();

	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONELOCKWIDGETSCREEN_H
