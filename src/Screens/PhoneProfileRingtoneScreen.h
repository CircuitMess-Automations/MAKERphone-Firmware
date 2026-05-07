#ifndef MAKERPHONE_PHONEPROFILERINGTONESCREEN_H
#define MAKERPHONE_PHONEPROFILERINGTONESCREEN_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"
#include "../Services/PhoneContactRingtone.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * S160 - PhoneProfileRingtoneScreen
 *
 * Per-profile ringtone selection UI -- the "let me hear MEETING ring
 * differently from OUTDOOR" affordance every Sony-Ericsson / Nokia of
 * the era exposed under Settings -> Sounds -> Profiles. Reached from
 * PhoneSettingsScreen's SOUND group ("Profile ring" row, directly
 * below "Profile" so the two profile-related rows cluster together).
 *
 * Two-mode screen, one .cpp file, in the same shape PhoneSpeedDialScreen
 * pioneered (LIST + PICK):
 *
 *   List view (default):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |          PROFILE RING                  | <- caption (cyan, pixelbasic7)
 *   |  GENERAL    Classic                    | <- 5 profile rows
 *   |  SILENT     Silent                     |
 *   |  MEETING    Silent                     |
 *   |  OUTDOOR    Boss                       |
 *   |  HEADSET    Synthwave                  |
 *   |  EDIT                          BACK    | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 *   Pick view (after pressing EDIT on a profile - replaces the profile
 *   list with the standard PhoneContactRingtone picker layout, plus a
 *   tiny "PROFILE: XYZ" caption so the user knows which slot they are
 *   editing):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |        PROFILE - MEETING               | <- caption (sunset orange)
 *   |    Synthwave                           |
 *   |  * Classic              <- saved row   |
 *   |    Beep                                |
 *   |    Boss                                |
 *   |    Silent                              |
 *   |    *Slot 1                             |
 *   |  PICK                          BACK    | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * Behaviour:
 *  - List mode:
 *      BTN_2 / BTN_LEFT  -> profile cursor up   (clamps -- 5 rows is short)
 *      BTN_8 / BTN_RIGHT -> profile cursor down
 *      BTN_ENTER / BTN_L -> open the pick view for the focused profile
 *      BTN_R / BTN_BACK  -> pop back to PhoneSettingsScreen
 *      Long-press BTN_BACK -> bail to homescreen (matches the rest of
 *                              the SYSTEM-cluster screens)
 *
 *  - Pick mode:
 *      BTN_2 / BTN_LEFT  -> ringtone cursor up
 *      BTN_8 / BTN_RIGHT -> ringtone cursor down
 *      BTN_ENTER         -> toggle preview through PhoneRingtoneEngine
 *      BTN_L (PICK)      -> persist the focused id into
 *                            Settings.profileRingtones[focusedProfile]
 *                            via PhoneContactRingtone::setProfileRingtoneId,
 *                            return to list mode (auto-stops preview)
 *      BTN_R / BTN_BACK  -> return to list mode without persisting
 *
 * Implementation notes:
 *  - 100 % code-only. Reuses PhoneSynthwaveBg / PhoneStatusBar /
 *    PhoneSoftKeyBar so the screen reads as part of the same
 *    MAKERphone family as every Phase-J / Phase-R sibling.
 *  - 160x128 budget. Vertical slices:
 *      y =   0 ..  9  - PhoneStatusBar (10 px)
 *      y =  12 .. 19  - caption strip
 *      y =  22 .. 93  - body band (72 px = 6 visible rows of 12 px)
 *      y =  96 ..115  - slack (currently empty)
 *      y = 118 ..127  - PhoneSoftKeyBar (10 px)
 *  - The pick-mode list iterates PhoneContactRingtone::pickerCount() /
 *    pickerIdAt() so a freshly-flashed device with no composer slots
 *    shows just the five library tones, and a user who has saved
 *    composed ringtones automatically sees them appear without an
 *    additional code path.
 *  - Profile-name strings stay inline literal (no SPIFFS / no
 *    asset cost). Names match PhoneProfileScreen ("GENERAL",
 *    "SILENT", "MEETING", "OUTDOOR", "HEADSET") for vocabulary
 *    consistency across the SOUND group.
 */
class PhoneProfileRingtoneScreen : public LVScreen, private InputListener {
public:
	PhoneProfileRingtoneScreen();
	virtual ~PhoneProfileRingtoneScreen() override;

	void onStart() override;
	void onStop() override;

	/** Number of profile rows. Mirrors PhoneContactRingtone::ProfileCount
	 *  / PhoneProfileScreen::ProfileCount so all three layers stay
	 *  numerically in lockstep. */
	static constexpr uint8_t ProfileCount   = 5;

	/** Body-band geometry exposed for unit-test friendliness. Pick
	 *  view tops up the same buffer the list view paints into so the
	 *  two modes can never visually overlap. */
	static constexpr lv_coord_t BodyY       = 22;
	static constexpr lv_coord_t BodyW       = 152;
	static constexpr lv_coord_t BodyX       = 4;
	static constexpr lv_coord_t RowHeight   = 12;
	static constexpr uint8_t    VisibleRows = 6;

	/** Maximum number of pick-mode entries: 5 library tones plus up
	 *  to 4 composer slots (S153 ringtone-encoding budget). */
	static constexpr uint8_t    MaxPickEntries = 9;

	/** Which mode the screen is currently in. Public for unit tests. */
	enum class Mode : uint8_t {
		List = 0,
		Pick = 1,
	};
	Mode getMode() const { return mode; }

	/** Profile currently focused (in either mode). 0..ProfileCount-1. */
	uint8_t getFocusedProfile() const { return profileCursor; }

	/** S223 -- returns true when the active sound profile (SILENT or
	 *  MEETING) has muted Settings.get().sound. Static so the
	 *  engine-skip check in startPreview() can call it without
	 *  indirecting through a live screen instance, and so unit tests
	 *  can stamp Settings.get().sound directly and observe the same
	 *  decision the screen would. Mirrors the helper landed in S205
	 *  (PhoneRadio), S219 (PhoneComposer), S220 (PhoneMusicPlayer),
	 *  S221 (PhoneAlarmTonePicker), and S222
	 *  (PhoneContactRingtonePicker). */
	static bool isSilenced();

private:
	PhoneSynthwaveBg* wallpaper      = nullptr;
	PhoneStatusBar*   statusBar      = nullptr;
	PhoneSoftKeyBar*  softKeys       = nullptr;

	lv_obj_t* captionLabel           = nullptr;

	// List mode (profile rows).
	lv_obj_t* listContainer          = nullptr;
	lv_obj_t* listHighlight          = nullptr;
	lv_obj_t* nameLabels[ProfileCount]  = {nullptr};
	lv_obj_t* tuneLabels[ProfileCount]  = {nullptr};

	// Pick mode (ringtone rows for the focused profile).
	lv_obj_t* pickContainer          = nullptr;
	lv_obj_t* pickHighlight          = nullptr;
	lv_obj_t* pickRows[MaxPickEntries]   = {nullptr};
	lv_obj_t* pickSavedDots[MaxPickEntries] = {nullptr};
	uint8_t   pickIds[MaxPickEntries]    = {0};
	uint8_t   pickEntryCount         = 0;
	uint8_t   pickCursor             = 0;
	uint8_t   pickTopVisible         = 0;
	uint8_t   pickSavedId            = PhoneContactRingtone::DefaultId;
	bool      previewing             = false;

	Mode    mode                     = Mode::List;
	uint8_t profileCursor            = 0;
	bool    backHoldFired            = false;

	// Build helpers.
	void buildLayout();
	void buildCaption();
	void buildListView();
	void buildPickView();

	// Mode transitions.
	void enterListMode();
	void enterPickMode();

	void refreshCaption();
	void refreshListLabels();
	void refreshListHighlight();

	void rebuildPickEntries();
	void refreshPickRows();
	void refreshPickHighlight();
	void refreshPickSavedMarks();

	/** S223 -- repurposes captionLabel as a "MUTED -- SOUND OFF"
	 *  badge while a silenced preview is "live" so the user reads
	 *  the silence as deliberate rather than wondering whether the
	 *  picked tone happens to be near-silent. When `muted` is false
	 *  the helper delegates to refreshCaption() so the per-mode
	 *  caption ("PROFILE RING" in list mode, "PROFILE - <NAME>" in
	 *  pick mode) is restored verbatim. */
	void setMutedCaption(bool muted);

	void moveProfileCursor(int8_t dir);
	void movePickCursor(int8_t dir);

	void startPreview();
	void stopPreview();

	void confirmPick();

	void invokeBack();

	void buttonPressed(uint i) override;
	void buttonHeld(uint i) override;
	void buttonReleased(uint i) override;
};

#endif // MAKERPHONE_PHONEPROFILERINGTONESCREEN_H
