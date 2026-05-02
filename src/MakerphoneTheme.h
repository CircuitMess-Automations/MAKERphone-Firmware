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

		// Sony Ericsson Aqua — early-2000s Sony Ericsson "Aqua" UI
		// homage (W-series Walkman / W910i / W995 / K850i family).
		// The skin that defined the late-feature-phone era: a deep
		// ocean-blue panel pulling into a vivid aqua mid, glossy
		// white-chrome icon strokes, bright cyan accents, and a
		// signature water-droplet glyph anchored bottom-right. Six
		// shades — deep navy (panel off), mid-ocean blue (gradient
		// bottom), muted blue (idle borders), bright cyan glow
		// (focus / accent), white chrome (icon strokes + body
		// text), icy foam (timestamps + ripple highlights). Reads
		// light-on-dark like the Default Synthwave + Amber CRT
		// themes — the authentic 2007 Sony Ericsson reading
		// direction (bright chrome icons on a deep aqua panel).
		// Wallpaper bypasses every Synthwave builder and paints a
		// flat ocean-gradient panel with a few horizontal current
		// streaks + scattered bubble specks + the water-droplet
		// motif anchored bottom-right.
		SonyEricssonAqua = 4,

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

		// RAZR Hot Pink — 2004-2006 Motorola RAZR V3 / V3i Pink
		// homage. The phone that defined the mid-2000s "ultra-thin
		// flip" era, and the first feature phone whose colour
		// branding (Valentine's-Day pink anodised aluminium)
		// became the product itself. Six shades - deep night-
		// magenta panel off, warmer dark magenta gradient bottom,
		// muted plum idle borders, hot magenta-pink accent / focus,
		// warm silver chrome body text, light pink ripple highlights
		// / etched-metal accents. Reads light-on-dark like the
		// Default Synthwave + Amber CRT + Sony Ericsson Aqua themes
		// - the authentic mid-2000s RAZR reading direction (bright
		// chrome menu items on a hot-pink anodised back panel).
		// Wallpaper bypasses every Synthwave builder and paints a
		// flat dark-magenta gradient panel with a few horizontal
		// "anodised aluminium" striation lines + scattered LED-
		// backlight specks + a stylised lightning-bolt motif
		// anchored bottom-right (the universal "RAZR sharpness"
		// brand cue, copyright-safe vs the RAZR wordmark itself).
		RazrHotPink = 5,

		// Stealth Black - early-2010s "blacked-out" tactical-handset
		// homage (Vertu Constellation Black, BlackBerry Bold 9900
		// Stealth, Nokia 8800 Carbon Arte, the obsidian-slab era
		// before the late-2010s glass-sandwich smartphone took over).
		// The skin that defined the "phone as concealed instrument"
		// look - a slab of obsidian glass with a single warm-red
		// tactical LED indicator and ghosted gunmetal menu chrome.
		// Six shades - pure obsidian (panel off), warm charcoal
		// (panel gradient bottom, suggesting faint subsurface
		// circuit-board glow), gunmetal (idle borders), tactical-red
		// LED (focus / accent - the single splash of colour against
		// a sea of black), bright bone-white (icon strokes + body
		// text), cool steel (dim labels / etched-bezel highlights).
		// Reads light-on-dark like the Default Synthwave + Amber CRT
		// + Sony Ericsson Aqua + RAZR Hot Pink themes - the
		// authentic early-2010s reading direction (bone-white menu
		// items on a void-black panel, with a single red LED
		// standing in for the device's status indicator). Wallpaper
		// bypasses every Synthwave builder and paints a flat
		// near-black panel with a faint subsurface gradient + a
		// few barely-visible horizontal carbon-fibre-weave rasters
		// + a tiny solid red status LED motif anchored bottom-right
		// (the trademark-safe equivalent of the era's signature
		// red status indicator).
		StealthBlack = 6,

		// Reserved 7..15 for the upcoming Phase O themes:
		//   7  Y2K Silver   (S113)
		//   8  Cyberpunk Red (S115)
		//   9  Christmas    (S117)
		//  10  Surprise/Daily-Cycle (S119)
	};

	/** Total number of themes the picker should expose today. */
	static constexpr uint8_t ThemeCount = 7;

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
	 *   SonyEricssonAqua:
	 *               AQUA_BG_DEEP (deep navy panel) / AQUA_GLOW (bright
	 *               cyan, accent / focus border) / AQUA_CHROME (white
	 *               chrome, icon strokes + soft-key labels) / AQUA_DIM
	 *               (muted blue, idle borders) / AQUA_CHROME (body
	 *               text, the bright always-on chrome the W910i uses
	 *               for its menu items) / AQUA_FOAM (icy mint,
	 *               timestamps + placeholders). Light-on-dark like
	 *               Default + Amber CRT - the authentic 2007 Sony
	 *               Ericsson reading direction (bright chrome icons
	 *               on a deep aqua panel).
	 *   RazrHotPink:
	 *               RAZR_BG_DARK (deep night-magenta panel) /
	 *               RAZR_GLOW (hot magenta-pink, accent / focus
	 *               border) / RAZR_CHROME (warm silver chrome, icon
	 *               strokes + soft-key labels) / RAZR_DIM (muted
	 *               plum, idle borders) / RAZR_CHROME (body text,
	 *               the bright chrome RAZR menu items used) /
	 *               RAZR_SHINE (light pink, timestamps + placeholders
	 *               + etched-metal ripple highlights). Light-on-dark
	 *               like Default + Amber CRT + Sony Ericsson Aqua -
	 *               the authentic 2005 RAZR reading direction (bright
	 *               chrome menu items on a hot-pink anodised panel).
	 *   StealthBlack:
	 *               STEALTH_BG_OBSIDIAN (pure obsidian panel) /
	 *               STEALTH_LED (tactical-red LED, accent / focus
	 *               border) / STEALTH_BONE (bone-white, icon strokes
	 *               + soft-key labels) / STEALTH_GUNMETAL (cool
	 *               gunmetal, idle borders) / STEALTH_BONE (body
	 *               text, the bright off-white tactical-handset
	 *               menu chrome) / STEALTH_STEEL (cool steel,
	 *               timestamps + placeholders + etched-bezel
	 *               highlights). Light-on-dark like Default +
	 *               Amber CRT + Sony Ericsson Aqua + RAZR Hot Pink -
	 *               the authentic early-2010s tactical-handset
	 *               reading direction (bone-white menu items on a
	 *               void-black panel, with a single warm-red LED
	 *               standing in for accent).
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
	 *   SonyEricssonAqua:
	 *               iconStroke = AQUA_CHROME, iconDetail = AQUA_GLOW
	 *   RazrHotPink:
	 *               iconStroke = RAZR_CHROME, iconDetail = RAZR_GLOW
	 *   StealthBlack:
	 *               iconStroke = STEALTH_BONE,  iconDetail = STEALTH_LED
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

	/*
	 * S106 - phosphor-bloom halo (Amber CRT icon-glyph pass).
	 *
	 * Real 1980s amber-phosphor CRTs exhibit a faint always-on halo
	 * around bright pixels: the energised phosphor 'bleeds' about a
	 * pixel into surrounding panel area, so an icon never sits as a
	 * hard-edged graphic against a dead-black panel - it always has
	 * a soft warm edge spreading into the bezel. That bloom is the
	 * single defining visual cue of the era; without it, an "amber"
	 * icon just looks like an orange icon on a black background.
	 *
	 * Phase O folds that effect into PhoneIconTile via a per-theme
	 * idle-halo mode. On themes that don't have a phosphor (Default
	 * Synthwave, Nokia 3310 LCD, Game Boy DMG LCD) the idle halo
	 * stays fully transparent, byte-identical to the previous
	 * behaviour. On Amber CRT, the halo always rests at a low
	 * opacity in the dim-amber colour so every tile - whether
	 * selected or not - reads as 'lit phosphor bleeding into the
	 * surrounding panel'. Selecting the tile then pulses the halo
	 * at a brighter pulse range, mimicking the 'energised phosphor
	 * burns harder' cue you see on a real CRT when the cursor row
	 * is drawn at full beam intensity.
	 *
	 *   phosphorGlowEnabled() - true only for AmberCRT
	 *   phosphorGlow()        - dim halo colour (AMBER_CRT_DIM under
	 *                           AmberCRT, MP_DIM otherwise - the
	 *                           latter is a fallback that's never
	 *                           observed because callers gate on
	 *                           phosphorGlowEnabled() first)
	 *   phosphorGlowOpa()     - LV_OPA_20 under AmberCRT, _TRANSP
	 *                           otherwise
	 *
	 * Selected-pulse range:
	 *   phosphorPulseLow()  - LV_OPA_30 default, LV_OPA_50 AmberCRT
	 *   phosphorPulseHigh() - LV_OPA_80 default, LV_OPA_COVER AmberCRT
	 *
	 * The pulse-range bump (50/100% vs 30/80%) makes a selected
	 * Amber CRT tile read as visibly 'hotter' than its idle
	 * neighbours.
	 */
	static bool       phosphorGlowEnabled();
	static lv_color_t phosphorGlow();
	static uint8_t    phosphorGlowOpa();
	static uint8_t    phosphorPulseLow();
	static uint8_t    phosphorPulseHigh();

	/*
	 * S108 - Sony Ericsson Aqua chrome-shine helpers (icon-glyph pass).
	 *
	 * The defining visual cue of the late-2000s Sony Ericsson "Aqua"
	 * UI was its glossy menu chrome: every tile, every soft-key, every
	 * focus row carried a thin bright highlight along its upper edge,
	 * suggesting reflected light from above-right. That highlight is
	 * what made an Aqua menu icon read as a polished glass / chrome
	 * panel rather than a flat coloured square - the W910i, W995,
	 * K850i and the C-series Cyber-shot phones all leaned on the same
	 * cue, and it's the single visual that separates the Aqua look
	 * from a generic dark-blue tile.
	 *
	 * Phase O folds that effect into PhoneIconTile via a per-theme
	 * 'chrome-shine' overlay - a 1 px AQUA_FOAM strip rendered along
	 * the very top of every tile body. On themes that don't have a
	 * chrome-shine convention (Default Synthwave purple, Nokia 3310
	 * LCD, Game Boy DMG LCD, Amber CRT phosphor) the strip stays
	 * fully transparent, byte-identical to the previous behaviour.
	 * On Sony Ericsson Aqua, the strip rests at a moderate idle
	 * opacity so every tile - whether selected or not - reads as
	 * 'glass tile catching ambient light from above', and the
	 * selected tile snaps the strip to full intensity so the focused
	 * tile reads as 'a single tile catching a direct sunbeam' against
	 * its softly-glowing neighbours - the same wet-shine cue Sony
	 * Ericsson used to mark the focused row on the W910i menu
	 * carousel.
	 *
	 *   chromeShineEnabled()     - true only for SonyEricssonAqua
	 *   chromeShineColor()       - AQUA_FOAM under SonyEricssonAqua,
	 *                              MP_HIGHLIGHT otherwise (the latter
	 *                              is a fallback that's never observed
	 *                              because callers gate on
	 *                              chromeShineEnabled() first via the
	 *                              opacity helpers below)
	 *   chromeShineIdleOpa()     - LV_OPA_50 under SonyEricssonAqua,
	 *                              LV_OPA_TRANSP otherwise (so the
	 *                              existing tiles render byte-
	 *                              identically on every other theme)
	 *   chromeShineSelectedOpa() - LV_OPA_COVER under SonyEricssonAqua,
	 *                              LV_OPA_TRANSP otherwise
	 *
	 * The selected-vs-idle opacity gap is intentionally large
	 * (50% -> 100%): an Aqua-era focused tile read as visibly 'wet
	 * with light' next to the rest of the menu, not just slightly
	 * brighter, and the doubled opacity captures that without needing
	 * a per-tile colour shift. PhoneIconTile applies this static
	 * (non-pulsing) overlay rather than animating it because the
	 * existing halo already pulses on selection - layering a second
	 * pulsing element on top would make the focused tile read as
	 * jittery rather than 'lit', which contradicts the Aqua aesthetic.
	 */
	static bool       chromeShineEnabled();
	static lv_color_t chromeShineColor();
	static uint8_t    chromeShineIdleOpa();
	static uint8_t    chromeShineSelectedOpa();

	/*
	 * S110 - RAZR Hot Pink edge-glow helpers (icon-glyph pass).
	 *
	 * The defining visual cue of the mid-2000s Motorola RAZR V3 / V3i
	 * keypad was its electroluminescent (EL) backlight: a thin pink-
	 * white panel sat under the etched-chrome keypad, and when the
	 * phone woke the EL bled hot magenta-pink light around the
	 * boundaries of every chrome character. The icons + numerals
	 * weren't 'lit' so much as 'haloed from below' - the chrome was
	 * dark, the pink leak was bright, and the result was a fingerprint
	 * silhouette of every key against a glowing pink panel. That
	 * bottom-edge bleed is the single visual that defined the RAZR
	 * keypad look; without it, an etched-chrome icon on a dark
	 * magenta panel just reads as 'an icon on dark purple', missing
	 * the entire point of the era's signature flip phone.
	 *
	 * Phase O folds that effect into PhoneIconTile via a per-theme
	 * 'edge-glow' overlay - a 1 px RAZR_GLOW strip rendered along the
	 * very bottom of every tile body. Mechanically the mirror of the
	 * S108 chrome-shine top strip, but tuned to read as 'EL backlight
	 * leaking up from beneath the icon' rather than 'glass catching
	 * sunlight from above'. On themes that don't have an EL-bleed
	 * convention (Default Synthwave purple, Nokia 3310 LCD, Game Boy
	 * DMG LCD, Amber CRT phosphor, Sony Ericsson Aqua glass) the strip
	 * stays fully transparent, byte-identical to the previous
	 * behaviour. On RAZR Hot Pink, the strip rests at a moderate idle
	 * opacity (the always-on EL keypad backlight cue) and the selected
	 * tile burns the strip to full intensity (the 'this key is
	 * pressed - panel lit at full' cue every RAZR owner saw whenever
	 * they thumbed the d-pad).
	 *
	 *   edgeGlowEnabled()     - true only for RazrHotPink
	 *   edgeGlowColor()       - RAZR_GLOW under RazrHotPink, MP_ACCENT
	 *                           otherwise (the latter is a fallback
	 *                           that's never observed because callers
	 *                           gate on edgeGlowEnabled() first via
	 *                           the opacity helpers below)
	 *   edgeGlowIdleOpa()     - LV_OPA_40 under RazrHotPink,
	 *                           LV_OPA_TRANSP otherwise (so the
	 *                           existing tiles render byte-identically
	 *                           on every other theme)
	 *   edgeGlowSelectedOpa() - LV_OPA_COVER under RazrHotPink,
	 *                           LV_OPA_TRANSP otherwise
	 *
	 * The selected-vs-idle opacity gap (40% -> 100%) captures the
	 * 'EL keypad lit at full' cue that defined the RAZR press-feedback
	 * loop: every key on a real V3 went from dimly-haloed-pink to
	 * white-pink-glowing the instant your thumb made contact. The
	 * doubled-plus opacity reproduces that snap without needing a
	 * per-tile colour shift. PhoneIconTile applies this static (non-
	 * pulsing) overlay rather than animating it because the existing
	 * halo already pulses on selection - layering a second pulsing
	 * element on top would make the focused tile read as jittery
	 * rather than 'pressed', which contradicts the snappy RAZR
	 * feel.
	 *
	 * Why bottom edge (vs S108's top edge): the EL backlight panel
	 * sat *under* the keypad, so the bleed-through always read as
	 * 'lifting up from below' on a real V3. Mirroring the strip from
	 * top to bottom keeps the cue physically faithful and gives the
	 * RAZR theme a visual axis that's distinct from the SE Aqua
	 * top-shine - so a user flipping between the two themes sees
	 * the highlight strip swap edges, not just colours, reinforcing
	 * that they're two genuinely different lighting models rather
	 * than two re-coloured versions of the same overlay.
	 */
	static bool       edgeGlowEnabled();
	static lv_color_t edgeGlowColor();
	static uint8_t    edgeGlowIdleOpa();
	static uint8_t    edgeGlowSelectedOpa();
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


/*
 * ---------------------------------------------------------------------
 * Sony Ericsson Aqua palette (S107).
 *
 * Approximates the iconic late-2000s Sony Ericsson "Aqua" UI shipped on
 * the W-series Walkman line (W910i, W995), the K850i, and the C-series
 * Cyber-shot phones — the skin that defined what a "premium feature
 * phone" looked like in the years before iOS / Android took over.
 * Unlike the Nokia and DMG palettes (dark ink on a pale LCD), Aqua
 * renders bright chrome icons + cyan accents on a deep ocean-blue
 * panel, so the role mapping aligns with the Synthwave / Amber CRT
 * light-on-dark convention.
 *
 * Six shades, all in the cool ocean-blue gamut:
 *
 *   AQUA_BG_DEEP — deep navy, the panel-off colour. Top of the
 *                  vertical gradient. Richer than synthwave purple,
 *                  more saturated than the typical iOS navy. The
 *                  signature panel colour of the W910i menu screen.
 *   AQUA_BG_MID  — mid-ocean blue, the panel gradient bottom. Pulls
 *                  the panel toward the iconic Sony Ericsson "aqua"
 *                  hue without the menu chrome having to fight for
 *                  contrast.
 *   AQUA_DIM     — muted slate-blue, used for idle borders and
 *                  inactive chevrons. ~40% of GLOW intensity so it
 *                  reads as legible-but-not-shouting next to the
 *                  brighter primary chrome.
 *   AQUA_GLOW    — bright cyan glow, the focus / accent colour. The
 *                  signature "energised highlight" hue Sony Ericsson
 *                  used for the focused menu row, the active soft-key
 *                  arrow, and the always-on Walkman LED. Calibrated
 *                  toward the W995 reference shade — slightly more
 *                  saturated than a generic sky blue.
 *   AQUA_CHROME  — white chrome, the body-text + icon-stroke colour.
 *                  The 2007 Sony Ericsson menu items were rendered
 *                  in a bright off-white that read as polished
 *                  metal rather than pure paper-white. Matches that.
 *   AQUA_FOAM    — icy mint-cyan, used for ripple highlights, drop-
 *                  shine pixels, and dim labels (timestamps,
 *                  placeholders). Lighter than GLOW so a foam ripple
 *                  reads as 'reflected light' rather than a focus
 *                  accent.
 *
 * Stored as lv_color_make(R, G, B) so the values render identically on
 * every LV_COLOR_DEPTH the firmware might be built against — same
 * portability rationale as the Nokia / DMG / Amber CRT palettes.
 *
 * Naming follows the MP_* / N3310_* / GBDMG_* / AMBER_CRT_* convention
 * so the part-2 icon-glyph swap in S108 only changes which header it
 * includes, not how it spells colours.
 * ---------------------------------------------------------------------
 */
#define AQUA_BG_DEEP  lv_color_make(  6,  22,  60)
#define AQUA_BG_MID   lv_color_make( 20,  78, 142)
#define AQUA_DIM      lv_color_make( 60, 110, 170)
#define AQUA_GLOW     lv_color_make( 60, 200, 255)
#define AQUA_CHROME   lv_color_make(220, 240, 255)
#define AQUA_FOAM     lv_color_make(160, 230, 255)


/*
 * ---------------------------------------------------------------------
 * RAZR Hot Pink palette (S109).
 *
 * Approximates the iconic mid-2000s Motorola RAZR V3 / V3i Pink — the
 * Valentine's-Day pink anodised-aluminium ultra-thin flip phone that
 * defined the era between the early-2000s feature phone and the late-
 * 2000s touch-screen smartphone. Like the Sony Ericsson Aqua and Amber
 * CRT palettes, RAZR Hot Pink reads light-on-dark: bright chrome menu
 * items + hot-pink accents on a deep night-magenta back panel, the
 * authentic RAZR-era menu reading direction.
 *
 * Six shades, all in the warm magenta-pink gamut:
 *
 *   RAZR_BG_DARK — deep night-magenta, the panel-off colour. Top of
 *                  the vertical gradient. Almost black with a faint
 *                  warm magenta bias - matches the V3i Pink's interior
 *                  housing colour with the screen unlit.
 *   RAZR_BG_DEEP — warmer dark magenta, the panel gradient bottom.
 *                  Pulls the panel toward the iconic anodised-pink
 *                  hue without the menu chrome having to fight for
 *                  contrast.
 *   RAZR_DIM     — muted plum, used for idle borders, sub-bright UI
 *                  chrome, inactive chevrons. ~40 % of GLOW intensity
 *                  so it reads as legible-but-not-shouting next to
 *                  the brighter primary chrome.
 *   RAZR_GLOW    — hot magenta-pink, the focus / accent colour. The
 *                  signature "Valentine's-Day pink" hue Motorola used
 *                  for the V3i Pink anodisation, calibrated so a
 *                  focused menu row reads as 'lit by the back panel'
 *                  rather than 'painted on top of it'.
 *   RAZR_CHROME  — warm silver, the body-text + icon-stroke colour.
 *                  The 2005 RAZR menu items rendered in a bright
 *                  warm-toned silver that read as polished metal
 *                  with a faint pink undertone (the back-panel
 *                  colour bleeding through the keypad backlight).
 *                  Matches that reading.
 *   RAZR_SHINE   — light pink, used for ripple highlights, etched-
 *                  aluminium striation accents, dim labels
 *                  (timestamps, placeholders), and the LED-backlight
 *                  bleed-through specks on the wallpaper. Lighter
 *                  than GLOW so a SHINE pixel reads as 'reflected
 *                  light' rather than a focus accent.
 *
 * Stored as lv_color_make(R, G, B) so the values render identically on
 * every LV_COLOR_DEPTH the firmware might be built against — same
 * portability rationale as the Nokia / DMG / Amber CRT / Aqua palettes.
 *
 * Naming follows the MP_* / N3310_* / GBDMG_* / AMBER_CRT_* / AQUA_*
 * convention so the part-2 icon-glyph swap in S110 only changes which
 * header it includes, not how it spells colours.
 * ---------------------------------------------------------------------
 */
#define RAZR_BG_DARK  lv_color_make( 22,   8,  18)
#define RAZR_BG_DEEP  lv_color_make( 48,  16,  36)
#define RAZR_DIM      lv_color_make(120,  60,  92)
#define RAZR_GLOW     lv_color_make(255,  40, 140)
#define RAZR_CHROME   lv_color_make(232, 220, 224)
#define RAZR_SHINE    lv_color_make(255, 120, 180)



/*
 * ---------------------------------------------------------------------
 * Stealth Black palette (S111).
 *
 * Approximates the early-2010s "blacked-out" tactical-handset aesthetic
 * - the obsidian-slab era of the Vertu Constellation Black, the
 * BlackBerry Bold 9900 Stealth, the Nokia 8800 Carbon Arte, and the
 * jet-black-anodised concept hardware that bridged the late-2000s
 * feature phone to the late-2010s glass-sandwich smartphone. Like the
 * Sony Ericsson Aqua, Amber CRT, and RAZR Hot Pink palettes, Stealth
 * Black reads light-on-dark: bone-white menu chrome + a single
 * tactical-red LED accent on a pure obsidian panel, the authentic
 * blacked-out-handset reading direction.
 *
 * Six shades, all in the cool obsidian / warm-LED gamut:
 *
 *   STEALTH_BG_OBSIDIAN — pure obsidian, the panel-off colour. Top of
 *                         the vertical gradient. Almost pure black with
 *                         a hint of cool-blue bias - matches the
 *                         polished obsidian glass of a powered-off
 *                         tactical handset (the screen has no light
 *                         leakage at all when the device is asleep).
 *   STEALTH_BG_CHARCOAL — warm charcoal, the panel gradient bottom.
 *                         Pulls the panel toward a faint subsurface
 *                         circuit-board glow without the menu chrome
 *                         having to fight for contrast. The cue every
 *                         8800 Carbon Arte owner saw when the phone
 *                         woke and the OLED panel began drawing power
 *                         into the lower edge first.
 *   STEALTH_GUNMETAL    — cool gunmetal, used for idle borders and
 *                         inactive chevrons. ~40 % of BONE intensity so
 *                         it reads as legible-but-not-shouting next to
 *                         the brighter primary chrome - the etched-
 *                         metal trim every blacked-out handset used
 *                         for inactive UI affordances.
 *   STEALTH_LED         — tactical-red LED, the focus / accent colour.
 *                         The single splash of colour against the sea
 *                         of black; everything else in the palette is
 *                         monochrome, so the LED accent reads as a
 *                         status indicator rather than a hue accent.
 *                         Calibrated toward the warm-red of an OLED
 *                         standby pixel - hot enough to feel "armed",
 *                         not so saturated that it becomes cherry red.
 *   STEALTH_BONE        — bone-white, the body-text + icon-stroke
 *                         colour. The early-2010s tactical menus
 *                         rendered in a slightly warm off-white that
 *                         read as "engraved bone on obsidian" rather
 *                         than the cool paper-white of a modern OS.
 *                         Matches that.
 *   STEALTH_STEEL       — cool steel, used for ripple highlights,
 *                         etched-bezel accents, and dim labels
 *                         (timestamps, placeholders). Lighter than
 *                         GUNMETAL but cooler than BONE so a STEEL
 *                         pixel reads as 'reflected ambient light'
 *                         rather than a body-text fill.
 *
 * Stored as lv_color_make(R, G, B) so the values render identically on
 * every LV_COLOR_DEPTH the firmware might be built against - same
 * portability rationale as the Nokia / DMG / Amber CRT / Aqua / RAZR
 * palettes.
 *
 * Naming follows the MP_* / N3310_* / GBDMG_* / AMBER_CRT_* / AQUA_* /
 * RAZR_* convention so the part-2 icon-glyph swap in S112 only changes
 * which header it includes, not how it spells colours.
 * ---------------------------------------------------------------------
 */
#define STEALTH_BG_OBSIDIAN  lv_color_make(  6,   8,  10)
#define STEALTH_BG_CHARCOAL  lv_color_make( 18,  22,  28)
#define STEALTH_GUNMETAL     lv_color_make( 78,  86,  98)
#define STEALTH_LED          lv_color_make(220,  40,  30)
#define STEALTH_BONE         lv_color_make(228, 226, 220)
#define STEALTH_STEEL        lv_color_make(160, 168, 180)

#endif // MAKERPHONE_THEME_H
