#ifndef MAKERPHONE_PHONESETTINGSSCREEN_H
#define MAKERPHONE_PHONESETTINGSSCREEN_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneSettingsScreen
 *
 * Phase-J entry point (S50): the phone-style settings screen that takes
 * over from the legacy SettingsScreen as the destination of the Settings
 * tile on PhoneMainMenu. It is a code-only LVGL screen built around a
 * vertical list of grouped sections - every section has a small all-caps
 * dim header, and inside each section a handful of tappable rows that
 * each show a label on the left and a chevron (>) on the right:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |              SETTINGS                  | <- pixelbasic7 cyan caption
 *   |   DISPLAY                              | <- group header (dim purple)
 *   |   Brightness                       >   | <- row + chevron
 *   |   Wallpaper                        >   |
 *   |   SOUND                                |
 *   |   Sound & Vibration                >   |
 *   |   SYSTEM                               |
 *   |   Date & Time                      >   |
 *   |   About                            >   |
 *   |   OPEN                          BACK   | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * S50 ships the screen *framework* with five rows mapped 1:1 onto the
 * remaining Phase-J sub-screens (Brightness=S51, Sound & Vibration=S52,
 * Wallpaper=S53, Date & Time=S54, About=S55). Until those screens land
 * each row's ENTER action falls through to PhoneAppStubScreen("<NAME>")
 * so the framework is fully driveable today - the user can drill into
 * any row, see the placeholder, and BACK out without anything crashing.
 *
 * S51-S55 will replace the per-row stubs with the real sub-screens by
 * either binding setOnActivate (the host-supplied dispatch hook) or by
 * extending the per-row launcher inside this screen. The Item enum stays
 * stable so future commits do not have to renumber rows.
 *
 * Implementation notes:
 *  - Code-only - no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *    PhoneStatusBar / PhoneSoftKeyBar so the screen feels visually part
 *    of the same MAKERphone family. Data partition cost stays zero.
 *  - 160x128 budget: 10 px status bar (y = 0..10), 10 px caption strip
 *    (y = 11..18), list area (y = 22..118), 10 px softkey bar at the
 *    bottom (y = 118..128). The list lays out rows + headers from the
 *    top with fixed pixel offsets so the 5 rows + 3 headers fit cleanly
 *    inside the 96 px window without scrolling.
 *  - A row is RowH px tall (12 px); a header is HdrH (10 px). pixelbasic7
 *    glyphs are ~7 px tall so 12 / 10 give a crisp 2-3 px halo above and
 *    below the baseline and nothing clips on the cursor highlight.
 *  - The cursor is a single translucent muted-purple rect (same look as
 *    PhoneCallHistory's row highlight) that slides between rows in 12 px
 *    steps. Only one lv_obj position update per cursor move.
 *  - BTN_2 / BTN_LEFT walk the cursor up; BTN_8 / BTN_RIGHT walk it
 *    down; BTN_ENTER fires the OPEN softkey on the focused row;
 *    BTN_BACK pops back to whoever pushed us (PhoneMainMenu in S20).
 *    Wrap-around is on by default - feature-phone muscle memory.
 */
class PhoneSettingsScreen : public LVScreen, private InputListener {
public:
	/**
	 * Stable identifier for each row. Used by setOnActivate so a host
	 * can override the per-row dispatch with a single callback. The
	 * underlying integer is the row's position in the visible list
	 * (0-based, headers excluded), so future S51-S55 wiring can bolt
	 * on concrete sub-screens without renumbering.
	 */
	enum class Item : uint8_t {
		Brightness = 0,        // S51
		Wallpaper  = 1,        // S53
		Sound      = 2,        // S52 (sound + vibration toggles)
		Haptics    = 3,        // S68 (key-click haptic ticks toggle)
		DateTime   = 4,        // S54
		About      = 5,        // S55
		// S101 - global theme picker (Default Synthwave / Nokia 3310 today;
		// Phase O grows the list to 10 themes by S119). Sits in the
		// DISPLAY group right under Wallpaper so theme + wallpaper-style
		// edits live next to each other in the user's mental model.
		Theme      = 6,        // S101
	};

	using ActivateHandler = void (*)(PhoneSettingsScreen* self, Item item);
	using BackHandler     = void (*)(PhoneSettingsScreen* self);

	PhoneSettingsScreen();
	virtual ~PhoneSettingsScreen() override;

	void onStart() override;
	void onStop() override;

	/**
	 * Bind a callback fired when the user presses OPEN (BTN_ENTER) on
	 * a row. With no callback wired the screen falls back to the
	 * built-in default which pushes a PhoneAppStubScreen named after
	 * the focused item, so the framework is fully driveable today.
	 */
	void setOnActivate(ActivateHandler cb);

	/**
	 * Bind a callback fired on BTN_BACK. Default (when nullptr) is to
	 * pop() the screen so the user falls back to PhoneMainMenu.
	 */
	void setOnBack(BackHandler cb);

	/** Replace the visible label of the left softkey (default "OPEN"). */
	void setLeftLabel(const char* label);

	/** Replace the visible label of the right softkey (default "BACK"). */
	void setRightLabel(const char* label);

	/** Currently focused row. Useful for hosts that want to introspect. */
	Item getSelectedItem() const;

	/**
	 * Move the cursor to a specific item. Repaints the highlight rect.
	 * No-op for out-of-range items.
	 */
	void setSelectedItem(Item item);

	/**
	 * S21-style press-feedback flash on the left/right softkey. Exposed
	 * so a host can flash from outside (e.g. when a sub-screen wants
	 * to confirm "yes, opened" on its way in).
	 */
	void flashLeftSoftKey();
	void flashRightSoftKey();

	/** Number of selectable rows (excludes group headers). */
	static constexpr uint8_t ItemCount = 7;

	// --- Geometry, exposed for unit-test friendliness. -----------------

	/**
	 * Visible list-area top edge, just below the caption strip. Lifted
	 * 2 px in S68 (was 22) so the now-6-row list fits cleanly between
	 * the caption and the soft-key bar without scrolling.
	 */
	static constexpr lv_coord_t ListY  = 20;
	/**
	 * Per-row height (selectable rows). Trimmed to 10 px in S101 (was
	 * 11) so 7 rows + 3 headers (the new Theme row joins DISPLAY) tile
	 * cleanly into the 98 px window between ListY=20 and the soft-key
	 * bar at y=118 - 3*9 + 7*10 = 97 px with 1 px slack. pixelbasic7
	 * is 7 px tall so the row still has 3 px halo for the highlight
	 * rect; tighter than the pre-S101 4 px but still legible.
	 */
	static constexpr lv_coord_t RowH   = 10;
	/**
	 * Per-header height (group titles). Trimmed to 9 px in S101 (was
	 * 10) for the same fit reason as RowH above. pixelbasic7 leaves
	 * 2 px halo, enough that the dim-purple group caption stays
	 * crisp without the row labels reading as squashed.
	 */
	static constexpr lv_coord_t HdrH   = 9;
	/** Width of the list container (full screen minus 4 px margins). */
	static constexpr lv_coord_t ListW  = 152;

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* captionLabel;
	lv_obj_t* listContainer;
	lv_obj_t* highlight;

	// Per-row visuals. Headers and rows are interleaved in the on-screen
	// list, but the cursor only ever lands on rows (Item entries). We
	// track the row's Y inside the list container so the highlight rect
	// can be positioned without recomputing the layout.
	struct Row {
		lv_obj_t*   labelObj;
		lv_obj_t*   chevronObj;
		lv_coord_t  y;        // top edge inside listContainer
		Item        item;
		const char* stubName; // friendly app name shown in the fallback PhoneAppStubScreen
	};
	Row rows[ItemCount];

	uint8_t cursor = 0;       // index into rows[] (0..ItemCount-1)

	ActivateHandler activateCb = nullptr;
	BackHandler     backCb     = nullptr;

	void buildCaption();
	void buildListContainer();
	void buildList();

	void refreshHighlight();
	void moveCursorBy(int8_t delta);

	void buttonPressed(uint i) override;

	/**
	 * Default ENTER handler when the host has not bound an activate
	 * callback. Pushes a PhoneAppStubScreen named after the row's
	 * stubName so the user gets a "coming in a future session"
	 * placeholder rather than a no-op press.
	 */
	void launchDefault(Item item);
};

#endif // MAKERPHONE_PHONESETTINGSSCREEN_H
