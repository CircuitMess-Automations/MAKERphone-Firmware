#ifndef MAKERPHONE_THEME_H
#define MAKERPHONE_THEME_H

#include <Arduino.h>
#include <misc/lv_color.h>

/*
 * MakerphoneTheme — runtime theme system (Phase O kickoff).
 *
 * The MAKERphone 2.0 v1.0 ships with a single retro feature-phone skin —
 * deep purple + sunset orange + cyan, the palette captured in
 * MakerphonePalette.h. Phase O (S101–S120) wraps that skin in a runtime
 * theme switcher. Each theme bundles three things:
 *
 *   1. a palette (per-theme MP_*-equivalent constants),
 *   2. a wallpaper variant (consumed by PhoneSynthwaveBg's constructor),
 *   3. an icon-glyph + soft-key tint pass (the part-2 session for each
 *      theme — S102 for Nokia 3310, S104 for Game Boy DMG, ...).
 *
 * S101 lands the first non-default theme: Nokia 3310 Monochrome. The
 * iconic 1999 brick's pea-green LCD becomes a wallpaper variant, and
 * its palette constants (N3310_*) are exposed here for the icon-glyph
 * pass that follows in S102.
 *
 * Persistence:
 *   themeId lives in SettingsData (default 0 = Default). Read on every
 *   screen build through MakerphoneTheme::getCurrent(), so changing the
 *   theme via the picker UI takes effect the next time any screen drops
 *   a `new PhoneSynthwaveBg(obj)`.
 *
 * Defensive reading:
 *   themeFromByte() clamps any out-of-range NVS byte back to Default —
 *   the same pattern PhoneSynthwaveBg::styleFromByte and the soundProfile
 *   reader use. Keeps a corrupted NVS page from rendering a blank screen
 *   (or worse, a non-existent theme).
 */
class MakerphoneTheme {
public:
	enum class Theme : uint8_t {
		// MAKERphone Synthwave — the v1.0 retro feature-phone skin.
		// Palette: MP_BG_DARK / MP_ACCENT / MP_HIGHLIGHT / MP_DIM /
		// MP_TEXT / MP_LABEL_DIM (see MakerphonePalette.h). Wallpaper
		// is the original PhoneSynthwaveBg with the user's chosen
		// Style (Synthwave / Plain / GridOnly / Stars).
		Default   = 0,

		// Nokia 3310 Monochrome — pea-green LCD homage to the
		// 1999 brick. Palette: pale olive LCD-off background, dark
		// olive LCD-on ink, very dark olive frame. Wallpaper is a
		// flat (very subtly graded) LCD panel with a faint scanline
		// pattern and a small pixel-art antenna motif anchored
		// bottom-right (clear of the status bar / clock / soft keys).
		Nokia3310 = 1,

		// Reserved 2..15 for the upcoming Phase O themes:
		//   2  Game Boy DMG (S103)
		//   3  Amber CRT    (S105)
		//   4  Sony Ericsson Aqua (S107)
		//   5  RAZR Hot Pink (S109)
		//   6  Stealth Black (S111)
		//   7  Y2K Silver   (S113)
		//   8  Cyberpunk Red (S115)
		//   9  Christmas    (S117)
		//  10  Surprise/Daily-Cycle (S119)
	};

	/** Total number of themes the picker should expose today. */
	static constexpr uint8_t ThemeCount = 2;

	/**
	 * Resolve a raw `Settings.themeId` byte to a clamped Theme. Bytes
	 * outside the known enum range fall back to Default — the same
	 * defensive pattern PhoneSynthwaveBg::styleFromByte uses for
	 * wallpaperStyle, so a corrupted NVS page can never render a
	 * non-existent theme.
	 */
	static Theme themeFromByte(uint8_t raw);

	/** Read the persisted theme via Settings.get().themeId. */
	static Theme getCurrent();

	/**
	 * Friendly all-caps display name for a Theme. Matches the
	 * Sony-Ericsson option-list style used by PhoneSoundScreen and
	 * PhoneWallpaperScreen so the upcoming Theme picker reads as
	 * part of the same family.
	 */
	static const char* getName(Theme t);
};

/*
 * ---------------------------------------------------------------------
 * Nokia 3310 Monochrome palette.
 *
 * Approximates the famous pea-green LCD of the 1999 brick: a warm
 * light olive backdrop ("LCD off") and a near-black olive ink ("LCD
 * on"). Stored as lv_color_make(R, G, B) so the values render
 * identically on every LV_COLOR_DEPTH the firmware might be built
 * against (Chatter ships at 16 bpp; preserving portability keeps
 * the macros usable from host-side LVGL builds the test harness
 * uses).
 *
 * Naming follows the MP_* convention from MakerphonePalette.h so a
 * Phone* widget that swaps to the Nokia palette in S102 only changes
 * which header it includes, not how it spells colours.
 *
 *   N3310_BG_LIGHT  — LCD off (the panel itself), top of gradient
 *   N3310_BG_DEEP   — LCD off, bottom of gradient (slight tilt
 *                     toward saturation so the panel reads as a
 *                     real LCD rather than a flat fill)
 *   N3310_PIXEL     — LCD on (full dark pixel)
 *   N3310_PIXEL_DIM — LCD on (half-on pixel, used for fine detail)
 *   N3310_FRAME     — very dark frame around accents
 *   N3310_HIGHLIGHT — pure white, used very sparingly (rare)
 * ---------------------------------------------------------------------
 */
#define N3310_BG_LIGHT   lv_color_make(157, 192, 130)
#define N3310_BG_DEEP    lv_color_make(134, 165, 110)
#define N3310_PIXEL      lv_color_make( 31,  35,  23)
#define N3310_PIXEL_DIM  lv_color_make( 79,  94,  71)
#define N3310_FRAME      lv_color_make( 47,  54,  29)
#define N3310_HIGHLIGHT  lv_color_make(255, 255, 255)

#endif // MAKERPHONE_THEME_H
