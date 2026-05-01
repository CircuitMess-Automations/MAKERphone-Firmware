#ifndef MAKERPHONE_PHONEWALLPAPERSCREEN_H
#define MAKERPHONE_PHONEWALLPAPERSCREEN_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"
#include "../Elements/PhoneSynthwaveBg.h"

class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneWallpaperScreen
 *
 * Phase-J sub-screen (S53): the wallpaper picker reachable from the
 * "Wallpaper" row of PhoneSettingsScreen (S50). Replaces the WALLPAPER
 * placeholder stub with a 4-style horizontal pager:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |             WALLPAPER                  | <- pixelbasic7 cyan caption
 *   |                                        |
 *   |          +-----------------+           | <- swatch frame (80x44)
 *   |          |    *  .   .     |           |    showing flat preview
 *   |          |  --=O=--        |           |    of the chosen Style
 *   |          |   /=|=\         |           |
 *   |          |  -- + --        |           |
 *   |          +-----------------+           |
 *   |                                        |
 *   |       <    SYNTHWAVE   1/4    >        | <- pager indicator
 *   |                                        |
 *   |   SAVE                          BACK   | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * The four styles map 1:1 onto PhoneSynthwaveBg::Style:
 *
 *   - Synthwave: full retro look - sun, gradient, perspective grid,
 *                ground horizontals, twinkle stars. The default.
 *   - Plain:     just the gradient sky/ground, calmest variant.
 *   - GridOnly:  gradient + perspective rays + ground horizontals,
 *                no sun and no stars. Keeps the racing-grid motion.
 *   - Stars:     gradient + twinkle stars, no sun and no grid.
 *                Night-sky vibe.
 *
 * Behavior:
 *  - LEFT / 4 step the pager backward through the styles. RIGHT / 6
 *    steps forward. Wrap-around is on - 4 styles is small enough that
 *    cycling feels right.
 *  - 2 / 8 mirror LEFT / RIGHT for users who navigate with the numpad.
 *  - ENTER (SAVE softkey) writes Settings.get().wallpaperStyle =
 *    static_cast<uint8_t>(focusedStyle), calls Settings.store() and
 *    pop()s. The next screen that drops a `new PhoneSynthwaveBg(obj)`
 *    (e.g. PhoneHomeScreen, PhoneMainMenu) automatically picks up the
 *    new look on the very next push.
 *  - BACK pop()s without persisting - the standard Sony-Ericsson
 *    "discard changes" affordance for option screens.
 *
 * Implementation notes:
 *  - Code-only - no SPIFFS assets. The swatch is a small flat
 *    composition built out of LVGL primitives (rects + lines), not a
 *    miniaturised PhoneSynthwaveBg, because shrinking the real
 *    wallpaper would also shrink its animations and inflate the cost
 *    of every step. Each style draws ~6-8 elements; on each cursor
 *    step we tear down the swatch's children and rebuild for the new
 *    style. Cheap and animation-free.
 *  - The screen's own backdrop stays full-Synthwave PhoneSynthwaveBg
 *    so the picker is visually consistent with the rest of the
 *    settings family. The "live preview" lives entirely in the swatch.
 *  - Reuses PhoneStatusBar / PhoneSoftKeyBar so the screen feels
 *    visually part of the MAKERphone family.
 */
class PhoneWallpaperScreen : public LVScreen, private InputListener {
public:
	using Style = PhoneSynthwaveBg::Style;

	/** Number of selectable styles (so the pager reads as 1/4 .. 4/4). */
	static constexpr uint8_t StyleCount = 4;

	PhoneWallpaperScreen();
	virtual ~PhoneWallpaperScreen() override;

	void onStart() override;
	void onStop() override;

	/** Currently focused Style. Useful for tests / hosts that introspect. */
	Style getCurrentStyle() const;

	/** Index of the currently focused style (0..StyleCount-1). */
	uint8_t getCurrentIndex() const { return cursor; }

	// Swatch geometry (exposed for unit-test friendliness).
	static constexpr lv_coord_t SwatchW = 80;
	static constexpr lv_coord_t SwatchH = 44;
	static constexpr lv_coord_t SwatchY = 24;

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* captionLabel;     // "WALLPAPER"
	lv_obj_t* swatchFrame;      // outer rounded rect hosting the preview
	lv_obj_t* swatchInner;      // child container we tear down on each step
	lv_obj_t* nameLabel;        // "SYNTHWAVE"
	lv_obj_t* pagerLabel;       // "1/4"
	lv_obj_t* leftChevron;      // "<"
	lv_obj_t* rightChevron;     // ">"

	uint8_t cursor;             // 0..StyleCount-1
	uint8_t initialStyle;       // value the screen opened with (BACK ignores it)

	void buildCaption();
	void buildSwatch();
	void buildPager();

	/** Tear down swatchInner's children and redraw the focused style. */
	void rebuildSwatch();
	/** Refresh the name + pager labels for the current cursor. */
	void refreshPager();

	void stepBy(int8_t delta);

	/**
	 * S67: rewrite the L/R softkey captions based on whether the
	 * focused style differs from the one saved at screen-open.
	 * Pristine: "" / "BACK". Dirty: "SAVE" / "CANCEL".
	 */
	void refreshSoftKeys();
	void saveAndExit();
	void cancelAndExit();

	void buttonPressed(uint i) override;

	// Per-style swatch builders. Each draws into swatchInner and is
	// expected to leave behind a clean composition fitting in a
	// SwatchW x SwatchH box. No animations - the swatch is meant to
	// be a still preview, not a miniaturised wallpaper.
	void drawSynthwaveSwatch();
	void drawPlainSwatch();
	void drawGridOnlySwatch();
	void drawStarsSwatch();

	/** Friendly all-caps name for a style (matches Sony-Ericsson option list style). */
	static const char* nameForIndex(uint8_t idx);
};

#endif // MAKERPHONE_PHONEWALLPAPERSCREEN_H
