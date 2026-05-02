#ifndef MAKERPHONE_PHONETHEMESCREEN_H
#define MAKERPHONE_PHONETHEMESCREEN_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"
#include "../MakerphoneTheme.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneThemeScreen
 *
 * Phase-O entry point (S101): the global theme picker reachable from
 * `Settings → Theme`. Where PhoneWallpaperScreen lets the user pick
 * between four Synthwave wallpaper variants (Synthwave / Plain /
 * GridOnly / Stars), PhoneThemeScreen sits one level up and lets the
 * user pick the *theme*: today either the default MAKERphone
 * Synthwave skin or the Nokia 3310 Monochrome LCD homage shipped in
 * S101. Selecting a non-default theme overrides the wallpaper variant
 * and swaps the palette + wallpaper rendering on the next screen
 * push; the wallpaperStyle byte stays persisted, so flipping the
 * theme back to Default restores the user's previously chosen
 * Synthwave variant unchanged.
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |               THEME                    | <- pixelbasic7 cyan caption
 *   |                                        |
 *   |          +-----------------+           | <- swatch frame (80x44)
 *   |          | <synthwave  >   |           |    showing palette swatch
 *   |          |  or LCD pannel  |           |    of the focused theme
 *   |          +-----------------+           |
 *   |                                        |
 *   |     <    SYNTHWAVE    1/2     >        | <- pager indicator
 *   |                                        |
 *   |   SAVE                          BACK   | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * Behavior:
 *  - LEFT / 4 step the pager backward; RIGHT / 6 step forward. 2 / 8
 *    mirror LEFT / RIGHT for users who navigate with the numpad.
 *    Wrap-around is on - 2 themes is small enough that cycling reads
 *    better than clamping (and as more themes land in S103+ this
 *    stays the right UX).
 *  - ENTER (SAVE softkey) writes Settings.get().themeId =
 *    static_cast<uint8_t>(focusedTheme), calls Settings.store() and
 *    pop()s. The next screen that drops a `new PhoneSynthwaveBg(obj)`
 *    automatically picks up the new theme via the resolver in
 *    PhoneSynthwaveBg::resolveStyleFromSettings().
 *  - BACK pop()s without persisting - the standard Sony-Ericsson
 *    "discard changes" affordance.
 *
 * Implementation notes:
 *  - Code-only - no SPIFFS assets. The swatch is a tiny flat
 *    composition built out of LVGL primitives (≈ 6-8 children per
 *    theme). On each cursor step we tear down the swatch's children
 *    and rebuild for the new theme, same pattern PhoneWallpaperScreen
 *    uses. Cheap and animation-free.
 *  - The screen's own backdrop is a `new PhoneSynthwaveBg(obj)` so it
 *    visually inherits the *current* theme - users who flip to Nokia
 *    3310 see the picker itself rendered in pea green from then on.
 *    The swatch lives on top and previews whichever theme the cursor
 *    is on.
 *  - Reuses PhoneStatusBar / PhoneSoftKeyBar so the screen feels
 *    visually part of the MAKERphone family.
 */
class PhoneThemeScreen : public LVScreen, private InputListener {
public:
	using Theme = MakerphoneTheme::Theme;

	/** Number of selectable themes today (grows in S103+ as more land). */
	static constexpr uint8_t ThemeCount = MakerphoneTheme::ThemeCount;

	PhoneThemeScreen();
	virtual ~PhoneThemeScreen() override;

	void onStart() override;
	void onStop() override;

	/** Currently focused Theme. Useful for tests / hosts that introspect. */
	Theme getCurrentTheme() const;

	/** Index of the currently focused theme (0..ThemeCount-1). */
	uint8_t getCurrentIndex() const { return cursor; }

	// Swatch geometry (exposed for unit-test friendliness).
	static constexpr lv_coord_t SwatchW = 80;
	static constexpr lv_coord_t SwatchH = 44;
	static constexpr lv_coord_t SwatchY = 24;

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* captionLabel;     // "THEME"
	lv_obj_t* swatchFrame;      // outer rounded rect hosting the preview
	lv_obj_t* swatchInner;      // child container we tear down on each step
	lv_obj_t* nameLabel;        // "SYNTHWAVE" / "NOKIA 3310"
	lv_obj_t* pagerLabel;       // "1/2"
	lv_obj_t* leftChevron;      // "<"
	lv_obj_t* rightChevron;     // ">"

	uint8_t cursor;             // 0..ThemeCount-1
	uint8_t initialTheme;       // value the screen opened with (BACK ignores it)

	void buildCaption();
	void buildSwatch();
	void buildPager();

	/** Tear down swatchInner's children and redraw the focused theme. */
	void rebuildSwatch();
	/** Refresh the name + pager labels for the current cursor. */
	void refreshPager();

	void stepBy(int8_t delta);

	/**
	 * Rewrite the L/R softkey captions based on whether the focused
	 * theme differs from the one saved at screen-open. Pristine: "" /
	 * "BACK". Dirty: "SAVE" / "CANCEL". Same dirty-aware pattern
	 * PhoneSoundScreen / PhoneWallpaperScreen (S67) use.
	 */
	void refreshSoftKeys();
	void saveAndExit();
	void cancelAndExit();

	void buttonPressed(uint i) override;

	// Per-theme swatch builders. Each draws into swatchInner and is
	// expected to leave behind a clean composition fitting in a
	// SwatchW x SwatchH box.
	void drawDefaultSwatch();
	void drawNokia3310Swatch();
};

#endif // MAKERPHONE_PHONETHEMESCREEN_H
