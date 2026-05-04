#ifndef MAKERPHONE_PHONEPROFILESCREEN_H
#define MAKERPHONE_PHONEPROFILESCREEN_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneProfileScreen
 *
 * Phase-R sub-screen (S159): the classic Sony-Ericsson / Nokia
 * five-state phone profile picker (General / Silent / Meeting /
 * Outdoor / Headset) reachable from PhoneSettingsScreen's SOUND
 * group. Replaces the S52 three-state Mute / Vibrate / Loud picker
 * (PhoneSoundScreen) as the user-facing surface for "what kind of
 * phone do I want today" -- the underlying soundProfile + sound
 * fields are still written on save so every existing reader
 * (BuzzerService, PhoneRingtoneEngine, SettingsScreen) keeps
 * working without churn, but the user now sees the same five-name
 * vocabulary every feature-phone of the era used.
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |              PROFILE                   | <- pixelbasic7 cyan caption
 *   |  o  GENERAL    Ring + buzz             | <- option row 0
 *   |  o  SILENT     No sound                | <- option row 1
 *   |  o  MEETING    Vibrate only            | <- option row 2
 *   |  o  OUTDOOR    Loud + buzz             | <- option row 3
 *   |  o  HEADSET    Ring, no buzz           | <- option row 4
 *   |       UP / DOWN to choose              | <- pixelbasic7 dim hint
 *   |   SAVE                          BACK   | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * Profiles map onto the legacy SettingsData fields so existing
 * readers of `Settings.get().sound` and `soundProfile` stay correct
 * without churn:
 *
 *   General  -> phoneProfile=0, soundProfile=2 (Loud),    sound=true
 *   Silent   -> phoneProfile=1, soundProfile=0 (Mute),    sound=false
 *   Meeting  -> phoneProfile=2, soundProfile=1 (Vibrate), sound=false
 *   Outdoor  -> phoneProfile=3, soundProfile=2 (Loud),    sound=true
 *   Headset  -> phoneProfile=4, soundProfile=2 (Loud),    sound=true
 *
 * Behavior:
 *  - LEFT / 2 / 4 step the cursor up; RIGHT / 6 / 8 step it down.
 *    Clamps at the ends -- a 5-element list is short enough that
 *    hard stops feel cleaner than wrapping (matches PhoneSoundScreen
 *    convention).
 *  - On every cursor step we live-preview the chosen profile via
 *    the Piezo (a single confirmation tone for "rings", silence
 *    for Silent / Meeting) so the user can hear the difference
 *    before committing.
 *  - ENTER (SAVE softkey) writes Settings.get().phoneProfile,
 *    soundProfile and sound atomically, calls Settings.store(),
 *    and pop()s.
 *  - BACK reverts the live Piezo mute setting back to the
 *    profile the screen opened with -- the standard
 *    Sony-Ericsson "discard changes" affordance for option
 *    screens. (PhoneSoundScreen / PhoneHapticsScreen / PhoneWallpaperScreen
 *    all behave the same.)
 *
 * Implementation notes:
 *  - Code-only, zero SPIFFS. Reuses PhoneSynthwaveBg /
 *    PhoneStatusBar / PhoneSoftKeyBar so the screen feels visually
 *    part of the same MAKERphone family as every Phase-J / Phase-R
 *    sibling.
 *  - The "checkmark dot" indicator on the left of each row lights
 *    up sunset-orange on whichever row matches the *currently
 *    saved* profile. The cursor highlight is a separate translucent
 *    muted-purple rect that slides between rows -- saved-vs-focused
 *    are visually distinct so the user can browse without losing
 *    track of what is committed.
 *  - 160x128 budget: 10 px status bar (y = 0..10), caption strip
 *    (y = 12..20), five 14 px option rows centred between
 *    y = 24..94, hint label at y = 100, soft-key bar at
 *    y = 118..128. The cursor rect is 14 px tall and slides in
 *    14 px steps so it perfectly covers a focused row.
 */
class PhoneProfileScreen : public LVScreen, private InputListener {
public:
	enum class Profile : uint8_t {
		General = 0,
		Silent  = 1,
		Meeting = 2,
		Outdoor = 3,
		Headset = 4,
	};

	PhoneProfileScreen();
	virtual ~PhoneProfileScreen() override;

	void onStart() override;
	void onStop() override;

	/** Number of selectable profile rows. */
	static constexpr uint8_t ProfileCount = 5;

	/** Per-row height of an option (matches the cursor rect size). */
	static constexpr lv_coord_t RowH      = 14;

	/** Top edge of the first option row inside the screen. */
	static constexpr lv_coord_t ListY     = 24;

	/** Total list-area width (full screen minus 4 px margins). */
	static constexpr lv_coord_t ListW     = 152;

	/** Profile currently under the cursor (i.e. focused). */
	Profile getFocusedProfile() const;

	/**
	 * Profile saved at the moment the screen was opened. Useful for
	 * tests + for the BACK / cancel flow which reverts to this value.
	 */
	Profile getInitialProfile() const { return initialProfile; }

	/**
	 * Map a profile to the three-state legacy soundProfile byte
	 * (0 = Mute, 1 = Vibrate, 2 = Loud) so callers that have not
	 * been ported to the new five-state vocabulary keep working.
	 * Public + static so PhoneSettingsScreen / IntroScreen and
	 * future test harnesses can derive the fan-out without
	 * dragging a live PhoneProfileScreen instance into the call
	 * site.
	 */
	static uint8_t legacySoundProfile(Profile p);

	/**
	 * Map a profile to the legacy `bool sound` field. Same
	 * back-compat rationale as legacySoundProfile() above.
	 */
	static bool legacySoundOn(Profile p);

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
		lv_obj_t*   nameObj;      // bold name (GENERAL / SILENT / ...)
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
	 * S67-style: rewrite the L/R softkey captions based on whether
	 * the focused profile differs from the one saved at screen-open.
	 * Pristine: "" / "BACK". Dirty: "SAVE" / "CANCEL". Lets the
	 * user see at a glance whether ENTER will commit or no-op.
	 */
	void refreshSoftKeys();

	void refreshHighlight();
	void refreshCheckmarks(Profile saved);
	void moveCursorBy(int8_t delta);

	/**
	 * Apply a profile to the live audio stack as a non-destructive
	 * preview: drives Piezo.setMute() + plays a short confirmation
	 * tone for the "rings" profiles, silences for Silent / Meeting.
	 * Does NOT touch persisted Settings -- BACK can still revert
	 * cleanly.
	 */
	void applyPreview(Profile p);

	void saveAndExit();
	void cancelAndExit();

	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONEPROFILESCREEN_H
