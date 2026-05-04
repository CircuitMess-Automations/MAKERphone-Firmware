#ifndef MAKERPHONE_PHONESPEEDDIALSCREEN_H
#define MAKERPHONE_PHONESPEEDDIALSCREEN_H

#include <Arduino.h>
#include <lvgl.h>
#include <vector>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"
#include "../Types.hpp"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneSpeedDialScreen - S151 (configuration UI half)
 *
 * Per-slot configuration UI for the nine speed-dial slots that the S151
 * gesture half (PhoneHomeScreen long-press 1..9 -> Phone.placeCall via
 * launchSpeedDialFromHome in IntroScreen.cpp) reads from
 * Settings.speedDial[1..9]. Reached from PhoneSettingsScreen's "Speed
 * dial" row (the new SYSTEM-section row added for the S151 UI half),
 * directly below the S147 "Operator" row so the two SYSTEM-cluster
 * rows that need their own dedicated picker UI sit next to each other
 * inside the existing SYSTEM group.
 *
 *   List view (default):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |             SPEED DIAL                 | <- caption (cyan, pixelbasic7)
 *   |  1   ALICE                             | <- 9 digit rows (1..9)
 *   |  2   (unset)                           |
 *   |  3   BOB                               |
 *   |  4   (unset)                           |
 *   |  5   CARL                              |
 *   |  6   (unset)                           |
 *   |  7   (unset)                           |
 *   |  8   (unset)                           |
 *   |  9   DAD                               |
 *   |  EDIT                          BACK    | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 *   Pick view (after pressing EDIT on a slot - replaces the digit
 *   list with a paired-contacts picker plus a "(Clear)" entry that
 *   wipes the slot entirely):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |          SLOT 3 - PICK CONTACT         | <- caption (sunset orange)
 *   |  (Clear)                               | <- "(Clear)" + contacts
 *   |  ALICE                                 |
 *   |  BOB                                   |
 *   |  CARL                                  |
 *   |  DAD                                   |
 *   |  ELLA                                  |
 *   |  ...                                   |
 *   |  PICK                          BACK    | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * Persistence
 *   - Backed directly by Settings.speedDial[10] (the S151 fixed-size
 *     uint64_t array on SettingsData), persisted via the existing
 *     SettingsImpl::store() call. No separate NVS namespace -- the
 *     speedDial slots are part of the same blob that already holds
 *     soundProfile / wallpaperStyle / themeId / keyTicks /
 *     ownerName / powerOffMessage / operatorText / operatorLogo.
 *   - The slot is written + persisted at the moment the user PICKs a
 *     contact (or "(Clear)") in pick mode, so an instant edit never
 *     loses content. There is no "dirty" buffer state -- this screen
 *     mutates Settings directly on commit.
 *   - Slot 0 is reserved for the existing S22 "hold 0 to quick-dial"
 *     gesture and is intentionally not editable from this screen --
 *     the screen only ever exposes slots 1..9.
 *
 * Controls (list mode)
 *   - BTN_2 / BTN_LEFT             : cursor up (wrap).
 *   - BTN_8 / BTN_RIGHT            : cursor down (wrap).
 *   - BTN_1..BTN_9                 : jump cursor to the matching slot
 *                                    (e.g. press 5 to focus slot 5).
 *                                    Feature-phone muscle memory --
 *                                    matches the S60 PhoneCalculator's
 *                                    digit shortcut style.
 *   - BTN_ENTER                    : enter pick mode for the focused
 *                                    slot. Flashes the EDIT softkey
 *                                    on the way in.
 *   - BTN_BACK                     : pop the screen (back to settings).
 *
 * Controls (pick mode)
 *   - BTN_2 / BTN_LEFT             : cursor up (wrap).
 *   - BTN_8 / BTN_RIGHT            : cursor down (wrap).
 *   - BTN_ENTER                    : commit the focused contact (or
 *                                    "(Clear)") into the slot, persist,
 *                                    and return to list mode. Flashes
 *                                    the PICK softkey on the way out.
 *   - BTN_BACK                     : cancel the pick, return to list
 *                                    mode without mutating the slot.
 *
 * Implementation notes
 *   - 100 % code-only -- no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *     PhoneStatusBar / PhoneSoftKeyBar so the screen reads as part of
 *     the MAKERphone family. Data partition cost stays zero.
 *   - The screen owns two child containers (digitContainer +
 *     pickContainer) and only one is visible at a time. Toggling
 *     mode hides one and shows the other rather than rebuilding
 *     either, so reentering pick mode after a cancel is instant.
 *   - The pick list auto-scrolls when the contact list overflows the
 *     visible window (PickVisibleRows = 8). Same windowing pattern
 *     PhoneCallHistory and PhoneContactsScreen use, so the look is
 *     consistent across every list-style screen.
 *   - The 9-row digit list fits inside the 98 px window (ListY = 20,
 *     soft-key bar at y = 118) at DigitRowH = 10 px without scrolling
 *     (9 * 10 = 90 px; 8 px slack reserves room for the optional
 *     2 px caption baseline shift on long contact names).
 */
class PhoneSpeedDialScreen : public LVScreen, private InputListener {
public:
	PhoneSpeedDialScreen();
	virtual ~PhoneSpeedDialScreen() override;

	void onStart() override;
	void onStop() override;

	/** Number of editable speed-dial slots exposed by this screen.
	 *  Slot 0 is reserved (see header doc) so we expose slots 1..9. */
	static constexpr uint8_t SlotCount = 9;

	/** Hard cap on the visible contacts in pick mode. The Friends
	 *  repo has no formal upper bound but the on-device pairing flow
	 *  caps practical pairings well below this; the array is sized
	 *  generously so the visible window can scroll smoothly without
	 *  running off the back of the buffer. */
	static constexpr uint8_t MaxPickEntries = 32;

	/** Pick-mode visible window. Beyond this the cursor scrolls the
	 *  rows in place (same windowing pattern as PhoneCallHistory). */
	static constexpr uint8_t PickVisibleRows = 8;

	/** The first slot in the pick-mode list is always "(Clear)" so the
	 *  user can wipe a slot back to "unset" without leaving the screen.
	 *  Index 0 in pickEntries[] therefore carries uid = 0; real contacts
	 *  start at index 1. */
	static constexpr uint8_t PickClearIdx = 0;

	// ---- Geometry, exposed for unit-test friendliness. -------------

	/** List-area top edge (just below the caption strip). */
	static constexpr lv_coord_t ListY     = 20;
	/** List-area width (full screen minus 4 px margins). */
	static constexpr lv_coord_t ListW     = 152;
	/** Per-row height in the digit list view. 10 px gives pixelbasic7
	 *  (~7 px tall) a 1-2 px halo top + bottom for the highlight rect,
	 *  and 9 rows fit cleanly inside the 98 px window between ListY
	 *  and the soft-key bar at y = 118 with 8 px of slack. */
	static constexpr lv_coord_t DigitRowH = 10;
	/** Per-row height in the pick view. 11 px keeps the contact-name
	 *  list a little easier to read than the dense digit list (longer
	 *  names benefit from the extra halo); PickVisibleRows * 11 = 88 px
	 *  fits inside the same 98 px window with 10 px of slack reserved
	 *  for the small caption-vs-list separator. */
	static constexpr lv_coord_t PickRowH  = 11;

	/** Visible mode of the screen. Public so tests can introspect. */
	enum class Mode : uint8_t {
		List = 0,    // 9 digit slots
		Pick = 1,    // contact picker for the slot in editingDigit
	};

	Mode    getMode()         const { return mode; }
	uint8_t getCursor()       const { return cursor; }
	uint8_t getEditingDigit() const { return editingDigit; }

private:
	PhoneSynthwaveBg* wallpaper = nullptr;
	PhoneStatusBar*   statusBar = nullptr;
	PhoneSoftKeyBar*  softKeys  = nullptr;

	lv_obj_t* captionLabel  = nullptr;

	// ---- list (digit slots) view ----
	lv_obj_t* digitContainer = nullptr;
	lv_obj_t* digitHighlight = nullptr;
	struct DigitRow {
		lv_obj_t* numLabel  = nullptr;   // "1".."9", left column
		lv_obj_t* nameLabel = nullptr;   // contact name or "(unset)"
		lv_coord_t y        = 0;         // top edge inside digitContainer
	};
	DigitRow digitRows[SlotCount];

	// ---- pick (contact picker) view ----
	lv_obj_t* pickContainer = nullptr;
	lv_obj_t* pickHighlight = nullptr;
	struct PickRow {
		lv_obj_t* label = nullptr;       // contact name (or "(Clear)")
		lv_coord_t y    = 0;             // top edge inside pickContainer
	};
	PickRow pickRows[PickVisibleRows];   // visible window only

	struct PickEntry {
		UID_t uid       = 0;             // 0 = "(Clear)" sentinel
		char  name[24]  = { 0 };         // dot-truncates if needed
	};
	std::vector<PickEntry> pickEntries;
	uint8_t pickWindowTop = 0;           // first visible entry index

	// ---- state ----
	Mode    mode         = Mode::List;
	uint8_t cursor       = 0;            // index into digitRows[] OR pickEntries[]
	uint8_t editingDigit = 1;            // 1..9 -- which slot pick mode is editing

	// ---- builders ----
	void buildCaption();
	void buildDigitView();
	void buildPickView();

	// ---- data refresh ----
	void refreshDigitRow(uint8_t slotIdx);
	void refreshAllDigitRows();
	void refreshDigitHighlight();

	void rebuildPickEntries();
	void refreshPickRows();
	void refreshPickHighlight();

	void refreshCaption();
	void refreshSoftKeys();

	// ---- mode + cursor ----
	void enterListMode();
	void enterPickMode(uint8_t digit);
	void moveCursorBy(int8_t delta);
	void jumpCursorToDigit(uint8_t digit);

	// ---- commit ----
	void commitPickedSlot();

	// ---- input ----
	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONESPEEDDIALSCREEN_H
