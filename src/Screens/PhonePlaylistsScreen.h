#ifndef MAKERPHONE_PHONEPLAYLISTSSCREEN_H
#define MAKERPHONE_PHONEPLAYLISTSSCREEN_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"
#include "../Services/PhoneMusicPlaylists.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhonePlaylistsScreen (S189)
 *
 * Phase-U entry point: the picker that sits in front of S42's
 * PhoneMusicPlayer. Lets the user choose between the four built-in
 * playlists (PhoneMusicPlaylists::Id) before drilling into the player
 * with that playlist's track set.
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |             PLAYLISTS                  | <- pixelbasic7 cyan caption
 *   |                                        |
 *   |  o  All Tracks         10 tracks      | <- highlight = focused row
 *   |  o  Chill Vibes         4 tracks      |
 *   |  o  Energy Boost        4 tracks      |
 *   |  o  Synthwave Drive     3 tracks      |
 *   |                                        |
 *   |  Slow / dreamy                         | <- per-row caption (focused)
 *   |   PLAY                          BACK   | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * Behaviour:
 *  - LEFT / 2 / 4 step the cursor up; RIGHT / 6 / 8 step it down.
 *    No wrap-around — four rows is short enough that hard stops feel
 *    cleaner than wrapping (matches PhoneSoftKeyToneScreen).
 *  - ENTER (PLAY softkey) creates a PhoneMusicPlayer, calls
 *    setTracks() with the focused playlist's pointer-array and
 *    setPlaylistName() with its display name, then push()es it.
 *    The player auto-routes through PhoneTransitions::Drill so the
 *    push reads as a proper "open-app" slide.
 *  - BACK pop()s back to the parent screen (the main menu).
 *
 * Implementation notes:
 *  - 100 % code-only — no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *    PhoneStatusBar / PhoneSoftKeyBar and the same shared MP_*
 *    palette every other phone screen uses.
 *  - 160x128 budget: 10 px status bar, caption strip at y=12, four
 *    14 px rows centred between y=24..80, focused-caption strip at
 *    y=98, soft-key bar at y=118..128.
 *  - The cursor "highlight" is a thin 14 px rounded rectangle that
 *    slides over the focused row. Same widget pattern S183's
 *    PhoneSoftKeyToneScreen ships, kept identical so the two screens
 *    look like part of the same family.
 */
class PhonePlaylistsScreen : public LVScreen, private InputListener {
public:
	PhonePlaylistsScreen();
	virtual ~PhonePlaylistsScreen() override;

	void onStart() override;
	void onStop() override;

	/** Number of rows (== PhoneMusicPlaylists::Count). */
	static constexpr uint8_t RowCount = PhoneMusicPlaylists::Count;

	/** Per-row height (matches the cursor rect size). */
	static constexpr lv_coord_t RowH = 14;

	/** Top edge of the first row inside the screen. */
	static constexpr lv_coord_t ListY = 24;

	/** Total list-area width (full screen minus 4 px margins). */
	static constexpr lv_coord_t ListW = 152;

	/** Playlist id currently under the cursor. */
	uint8_t getFocusedId() const { return cursor; }

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* captionLabel;
	lv_obj_t* listContainer;
	lv_obj_t* highlight;
	lv_obj_t* hintLabel;

	struct Row {
		lv_obj_t* dotObj;
		lv_obj_t* nameObj;
		lv_obj_t* countObj;
		lv_coord_t y;
	};
	Row rows[RowCount];

	uint8_t cursor;

	void buildCaption();
	void buildListContainer();
	void buildList();
	void buildHint();

	void refreshHighlight();
	void refreshFocusedHint();

	void stepBy(int8_t delta);
	void launchSelection();

	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONEPLAYLISTSSCREEN_H
