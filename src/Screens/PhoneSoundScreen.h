#ifndef MAKERPHONE_PHONESOUNDSCREEN_H
#define MAKERPHONE_PHONESOUNDSCREEN_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneSoundScreen
 *
 * Phase-J sub-screen (S52): the Sound + Vibration toggles page reachable
 * from PhoneSettingsScreen (S50). Replaces the SOUND placeholder stub
 * with a three-option radio chooser that swaps the device between
 * Mute / Vibrate / Loud profiles, persists the choice into the
 * Settings partition (`SettingsData::soundProfile` plus the legacy
 * `sound` flag for back-compat with BuzzerService / PhoneRingtoneEngine
 * / SettingsScreen), and previews the audible difference under the
 * user's finger so the change is felt before it is committed.
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |               SOUND                    | <- pixelbasic7 cyan caption
 *   |                                        |
 *   |  o  MUTE       Silent                  | <- option row (selected = checkmark)
 *   |  o  VIBRATE    Buzz only               |
 *   |  o  LOUD       Ringer + keys           |
 *   |                                        |
 *   |        UP / DOWN to choose             | <- pixelbasic7 dim hint
 *   |                                        |
 *   |   SAVE                          BACK   | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * Profiles map cleanly onto SettingsData fields so existing readers of
 * `Settings.get().sound` stay correct without churn:
 *
 *   Mute    -> soundProfile=0, sound=false
 *   Vibrate -> soundProfile=1, sound=false  (kept distinct so a future
 *               hardware revision with a vibration motor can light up
 *               on this profile; today the buzzer stays silent)
 *   Loud    -> soundProfile=2, sound=true
 *
 * Behavior:
 *  - LEFT / 2 / 4 step the cursor up; RIGHT / 6 / 8 step it down. No
 *    wrap-around -- a 3-element list is short enough that hard stops
 *    feel cleaner than wrapping.
 *  - On every cursor step we live-preview the chosen profile via the
 *    Piezo (a single confirmation tone for Loud, silence for Mute /
 *    Vibrate) so the user can hear the difference before committing.
 *  - ENTER (SAVE softkey) writes Settings.get().soundProfile and
 *    Settings.get().sound, calls Settings.store(), and pop()s.
 *  - BACK reverts both runtime state and the live Piezo mute setting
 *    back to the values the screen opened with -- the standard
 *    Sony-Ericsson "discard changes" affordance for option screens.
 *
 * Implementation notes:
 *  - Code-only, zero SPIFFS. Reuses PhoneSynthwaveBg / PhoneStatusBar /
 *    PhoneSoftKeyBar so the screen feels visually part of the rest of
 *    the MAKERphone family.
 *  - The "checkmark dot" indicator on the left of each row is a single
 *    LVGL rounded rect whose size + color is keyed off whether the row
 *    matches the *currently saved* profile. The cursor highlight is a
 *    separate translucent muted-purple rect that slides between rows
 *    -- saved-vs-focused are visually distinct so the user can browse
 *    without losing track of what is committed.
 *  - 160x128 budget: 10 px status bar (y = 0..10), caption strip
 *    (y = 12..20), three 18 px option rows centred between y = 28..82,
 *    hint label at y = 100, soft-key bar at y = 118..128. The cursor
 *    rect is 18 px tall and slides in 18 px steps so it perfectly
 *    covers a focused row.
 */
class PhoneSoundScreen : public LVScreen, private InputListener {
public:
	enum class Profile : uint8_t {
		Mute    = 0,
		Vibrate = 1,
		Loud    = 2,
	};

	PhoneSoundScreen();
	virtual ~PhoneSoundScreen() override;

	void onStart() override;
	void onStop() override;

	/** Number of selectable profile rows. */
	static constexpr uint8_t ProfileCount = 3;

	/** Per-row height of an option (matches the cursor rect size). */
	static constexpr lv_coord_t RowH      = 18;

	/** Top edge of the first option row inside the screen. */
	static constexpr lv_coord_t ListY     = 28;

	/** Total list-area width (full screen minus 4 px margins). */
	static constexpr lv_coord_t ListW     = 152;

	/** Profile currently under the cursor (i.e. focused). */
	Profile getFocusedProfile() const;

	/**
	 * Profile saved at the moment the screen was opened. Useful for
	 * tests + for the BACK / cancel flow which reverts to this value.
	 */
	Profile getInitialProfile() const { return initialProfile; }

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* captionLabel;
	lv_obj_t* listContainer;
	lv_obj_t* highlight;          // cursor rect (slides between rows)
	lv_obj_t* hintLabel;

	struct Row {
		lv_obj_t*   dotObj;       // saved-state checkmark dot on the left
		lv_obj_t*   nameObj;      // bold name (MUTE / VIBRATE / LOUD)
		lv_obj_t*   descObj;      // short description on the right
		lv_coord_t  y;            // top edge inside listContainer
		Profile     profile;
	};
	Row rows[ProfileCount];

	uint8_t  cursor = 0;          // 0..ProfileCount-1
	Profile  initialProfile;      // snapshot taken at construction time

	void buildCaption();
	void buildListContainer();
	void buildList();
	void buildHint();

	/**
	 * S67: rewrite the L/R softkey captions based on whether the
	 * focused profile differs from the one saved at screen-open.
	 * Pristine: "" / "BACK". Dirty: "SAVE" / "CANCEL". Lets the
	 * user see at a glance whether ENTER will commit or no-op.
	 */
	void refreshSoftKeys();

	void refreshHighlight();
	void refreshCheckmarks(Profile saved);
	void moveCursorBy(int8_t delta);

	/**
	 * Apply a profile to the live audio stack as a non-destructive
	 * preview: drives Piezo.setMute() + plays a short confirmation tone
	 * for Loud, silences for Mute / Vibrate. Does NOT touch persisted
	 * Settings -- BACK can still revert cleanly.
	 */
	void applyPreview(Profile p);

	void saveAndExit();
	void cancelAndExit();

	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONESOUNDSCREEN_H
