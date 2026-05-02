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
 * S102 wires the icon-glyph + accent layer of the Nokia 3310 theme.
 * Phone* widgets (PhoneIconTile, PhoneSoftKeyBar, PhoneLockHint, ...)
 * stop hard-coding the synthwave MP_* values and instead call the
 * accent helpers below — `MakerphoneTheme::bgDark()`, `accent()`,
 * `highlight()`, `dim()`, `text()`, `labelDim()` — so swapping the
 * global theme propagates to every newly-built screen without each
 * widget having to know which palette to read. The helpers are
 * defensive: they fall back to the Synthwave defaults whenever the
 * current theme isn't one they handle, so an unknown future Theme
 * value can never render an undefined colour.
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

		// Game Boy DMG (Dot Matrix Game) — 4-shade green LCD homage
		// to the 1989 brick. Palette: pale-mint LCD off ("dead pixel"
		// background), light-olive mid-tone, dark-olive mid-shadow,
		// near-black olive-ink. Wallpaper is a flat 4-shade LCD panel
		// with a faint pixel-dither pattern + a small 8-bit "boy"
		// figure anchored bottom-right. Bypasses every Synthwave
		// builder so the wallpaper reads as a real DMG-01 idle
		// screen rather than a tinted Synthwave.
		GameBoyDMG = 2,

		// Amber CRT — 1980s amber-phosphor monochrome terminal homage
		// (think Apple ///, IBM 5151, vintage Wyse / DEC terminals).
		// Palette inverts the Nokia / DMG dark-on-light convention
		// back to light-on-dark: a near-black warm-brown backdrop with
		// the iconic 'burning amber' phosphor ink in the foreground.
		// Five shades — deep CRT black (panel off), a dim amber for
		// sub-bright UI chrome, classic amber phosphor for body text
		// + icon strokes, hot amber for accents / borders, and a
		// faint warm-brown gradient bottom for the panel curvature
		// cue. Wallpaper bypasses every Synthwave builder and paints
		// a flat dark panel with horizontal scanlines + a small
		// pixel-art ">_" terminal prompt anchored bottom-right - the
		// classic 1980s monochrome-terminal idle scene.
		AmberCRT = 3,

		// Reserved 4..15 for the upcoming Phase O themes:
		//   4  Sony Ericsson Aqua (S107)
		//   5  RAZR Hot Pink (S109)
		//   6  Stealth Black (S111)
		//   7  Y2K Silver   (S113)
		//   8  Cyberpunk Red (S115)
		//   9  Christmas    (S117)
		//  10  Surprise/Daily-Cycle (S119)
	};

	/** Total number of themes the picker should expose today. */
	static constexpr uint8_t ThemeCount = 4;

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

	/*
	 * S102 — accent palette resolvers.
	 *
	 * Each helper returns the colour the named *role* should take
	 * under the currently active theme. Phone* widgets call these at
	 * construction (and from the rare animation hot-path that mid-
	 * frame re-tints, e.g. PhoneSoftKeyBar's flash) so swapping the
	 * global theme propagates to every newly-built screen without
	 * each widget having to ifdef on Theme.
	 *
	 * Roles (named after the existing MP_* macros so the .cpp swap
	 * is mechanical):
	 *
	 *   bgDark    — page / slab / status-bar background fill.
	 *   accent    — primary accent (focus border, "Sent" bubble fill,
	 *               soft-key arrow tint, halo ring).
	 *   highlight — icon strokes, soft-key label, "edited" cyan.
	 *   dim       — idle borders, "Received" bubble fill, inactive
	 *               chevrons.
	 *   text      — default body text (cream).
	 *   labelDim  — timestamps, placeholders, idle tile labels.
	 *
	 * Theme mappings:
	 *
	 *   Default:    MP_BG_DARK / MP_ACCENT / MP_HIGHLIGHT /
	 *               MP_DIM / MP_TEXT / MP_LABEL_DIM
	 *   Nokia3310:  N3310_BG_LIGHT (LCD off) / N3310_FRAME (deep
	 *               olive) / N3310_PIXEL (LCD on) / N3310_PIXEL_DIM /
	 *               N3310_PIXEL / N3310_PIXEL_DIM
	 *   GameBoyDMG: GBDMG_LCD_LIGHT (LCD off) / GBDMG_INK
	 *               (darkest 4-shade) / GBDMG_INK_MID (mid-shadow) /
	 *               GBDMG_LCD_MID (lightest mid-tone) / GBDMG_INK /
	 *               GBDMG_INK_MID. The DMG palette mirrors the Nokia
	 *               mapping (dark ink on a pale LCD panel), keeping
	 *               the part-2 icon-glyph swap in S104 mechanical.
	 *   AmberCRT:   AMBER_CRT_BG_DARK (CRT panel off, near-black) /
	 *               AMBER_CRT_HOT (hot amber, accent / focus border) /
	 *               AMBER_CRT_GLOW (classic amber phosphor, icon
	 *               strokes + body text) / AMBER_CRT_DIM (sub-bright
	 *               amber, idle borders / inactive chevrons) /
	 *               AMBER_CRT_GLOW (body text) / AMBER_CRT_DIM
	 *               (timestamps + placeholders). Inverts the Nokia /
	 *               DMG mapping back to light-on-dark - the authentic
	 *               1980s phosphor-terminal reading direction (bright
	 *               amber pixels on a black panel).
	 *
	 * The Nokia mapping inverts the Synthwave dark-on-light
	 * convention: the "background" role becomes a light olive and
	 * the "text"/"highlight" roles become the dark LCD ink — that's
	 * the authentic 3310 reading direction (dark pixels on a pale
	 * green panel) and what a 1999 user would expect when they hit
	 * the menu icon on the LCD.
	 */
	static lv_color_t bgDark();
	static lv_color_t accent();
	static lv_color_t highlight();
	static lv_color_t dim();
	static lv_color_t text();
	static lv_color_t labelDim();

	/*
	 * S104 - icon-glyph accent resolvers.
	 *
	 * The Phase O themes have varying-contrast palettes, and "icon
	 * stroke" + "icon inner detail" are the two roles where that shows
	 * up most. Default/Nokia 3310 rely on the existing highlight/accent
	 * mapping (icon strokes in the highlight colour, small filled
	 * details - camera lens, gear bolt, action buttons - in the accent
	 * colour) which gives them their hue contrast (cyan-on-purple,
	 * deep-olive-on-light-olive). Game Boy DMG, with its 4-shade green
	 * LCD, needs a *brightness* hierarchy instead: darkest ink for the
	 * bulk of the icon outline, mid-shadow ink for inner details so
	 * the icon reads as a real DMG sprite (filled outlines with a
	 * lighter mid-tone shading) rather than a flat silhouette.
	 *
	 *   iconStroke - primary icon outline / bulk fill
	 *   iconDetail - secondary inner accent (lens, gear bolt, button)
	 *
	 * Mappings:
	 *   Default:    iconStroke = MP_HIGHLIGHT, iconDetail = MP_ACCENT
	 *   Nokia3310:  iconStroke = N3310_PIXEL,  iconDetail = N3310_FRAME
	 *   GameBoyDMG: iconStroke = GBDMG_INK,    iconDetail = GBDMG_INK_MID
	 *   AmberCRT:   iconStroke = AMBER_CRT_GLOW, iconDetail = AMBER_CRT_HOT
	 *
	 * Default + Nokia mappings are byte-identical to what
	 * `highlight()` / `accent()` already return for those themes, so
	 * S104 is a pure DMG visual upgrade. The DMG iconDetail is
	 * deliberately *lighter* than iconStroke (mid-shadow vs darkest)
	 * so an inner accent reads as a sprite-shading highlight inside
	 * its dark outline - the way Pokemon Red/Blue, Tetris menus and
	 * every other DMG-era game drew their iconography.
	 */
	static lv_color_t iconStroke();
	static lv_color_t iconDetail();
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

/*
 * ---------------------------------------------------------------------
 * Game Boy DMG (Dot Matrix Game) palette.
 *
 * Approximates the famous 4-shade green LCD of the 1989 DMG-01: a pale
 * pea-mint backdrop ("LCD off"), a slightly darker olive mid-tone, a
 * mid-shadow forest-olive, and a near-black olive-ink for the darkest
 * "LCD on" pixel. The four shades together are what gives any DMG
 * screenshot its distinctive 2-bit-per-pixel feel.
 *
 * Stored as lv_color_make(R, G, B) so the values render identically
 * on every LV_COLOR_DEPTH the firmware might be built against — same
 * portability rationale as the Nokia palette.
 *
 * Naming follows the MP_* / N3310_* convention so the part-2
 * icon-glyph swap in S104 only changes which header it includes,
 * not how it spells colours.
 *
 *   GBDMG_LCD_LIGHT  — LCD off (the panel itself), top of gradient
 *   GBDMG_LCD_MID    — lightest mid-tone (used for the dither pattern
 *                      and the lightest icon shade)
 *   GBDMG_INK_MID    — mid-shadow (used for icon mid-tones, frame
 *                      borders that need to stay subtle)
 *   GBDMG_INK        — LCD on (full dark "ink" pixel, used for icon
 *                      strokes, text, hard frame borders)
 *   GBDMG_LCD_DEEP   — bottom of the panel gradient (slight tilt
 *                      toward saturation so the panel reads as a
 *                      real LCD rather than a flat fill)
 *
 * RGB choices: tuned to match the DMG-01 reference grade Nintendo
 * shipped (the so-called "pea-soup green" — slightly more yellow
 * than the 3310's pea green) without veering into Game Boy Pocket
 * grayscale. The four ink shades are deliberately spaced so that
 * the lightest mid-tone is still readable as "filled" against the
 * LCD-off background, the same constraint the original DMG had.
 * ---------------------------------------------------------------------
 */
#define GBDMG_LCD_LIGHT  lv_color_make(155, 188,  15)
#define GBDMG_LCD_DEEP   lv_color_make(139, 172,  15)
#define GBDMG_LCD_MID    lv_color_make(107, 142,  15)
#define GBDMG_INK_MID    lv_color_make( 48,  98,  48)
#define GBDMG_INK        lv_color_make( 15,  56,  15)


/*
 * ---------------------------------------------------------------------
 * Amber CRT phosphor palette (S105).
 *
 * Approximates the warm amber-phosphor monochrome CRTs of the early-
 * 80s — the iconic Apple /// monitor, the IBM 5151, the Wyse 50, the
 * DEC VT320 amber variant. Unlike the Nokia and DMG palettes (dark
 * ink on a pale LCD), an amber CRT renders bright amber pixels on a
 * near-black panel, so the role mapping inverts back to the Synthwave
 * dark-on-light convention.
 *
 * Five shades, all in the warm-amber gamut:
 *
 *   AMBER_CRT_BG_DARK  — CRT panel off, near-black with a faint warm-
 *                        brown bias (the unenergised phosphor's natural
 *                        colour). Top of the panel gradient.
 *   AMBER_CRT_BG_DEEP  — slightly deeper warm-brown for the panel
 *                        gradient bottom (CRT bulge cue, same role
 *                        N3310_BG_DEEP plays for the Nokia).
 *   AMBER_CRT_DIM      — sub-bright amber for idle borders, inactive
 *                        chevrons, dim timestamps. ~40% of GLOW
 *                        intensity so it reads as legible-but-not-
 *                        shouting next to the brighter primary text.
 *   AMBER_CRT_GLOW     — classic amber phosphor — body text, icon
 *                        strokes, the LCD "on" pixel equivalent. The
 *                        signature colour of the theme. Calibrated to
 *                        the Apple /// reference grade (~580 nm
 *                        wavelength when seen on a real CRT).
 *   AMBER_CRT_HOT      — hot amber 'burn' for accents, focus borders,
 *                        the halo pulse, the focused soft-key arrow.
 *                        Slightly more saturated than GLOW so a focused
 *                        element reads as 'lit brighter' than its
 *                        neighbours.
 *
 * Stored as lv_color_make(R, G, B) so the values render identically on
 * every LV_COLOR_DEPTH the firmware might be built against — same
 * portability rationale as the Nokia and DMG palettes.
 *
 * Naming follows the MP_* / N3310_* / GBDMG_* convention so the part-2
 * icon-glyph swap in S106 only changes which header it includes, not
 * how it spells colours.
 * ---------------------------------------------------------------------
 */
#define AMBER_CRT_BG_DARK  lv_color_make( 14,   8,   2)
#define AMBER_CRT_BG_DEEP  lv_color_make( 26,  14,   4)
#define AMBER_CRT_DIM      lv_color_make(120,  68,   8)
#define AMBER_CRT_GLOW     lv_color_make(255, 176,   0)
#define AMBER_CRT_HOT      lv_color_make(255, 210,  90)

#endif // MAKERPHONE_THEME_H
