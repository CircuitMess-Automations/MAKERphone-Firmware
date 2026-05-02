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

		// Y2K Silver - turn-of-the-millennium "translucent chrome"
		// homage (iMac G3 Snow, iPod 1G click-wheel, Sony Discman
		// MZ-E700, Sony VAIO PCG-505, Nokia 8210, Sharp J-SH04
		// "Frost Silver" - the brushed-silver-and-translucent-blue
		// industrial-design vocabulary that defined the late-1990s
		// to early-2000s "Y2K" consumer-electronics aesthetic). The
		// skin that came right after the late-2000s tactical
		// blacked-out era visually but actually predated it
		// historically - the polished-aluminium / frosted-Lucite
		// gadget every kid in 2001 wanted before the 2005 RAZR flip
		// phone reset the design conversation. Six shades - pearl-
		// silver panel off, brushed-aluminium gradient bottom,
		// frosted-grey idle borders, electric-blue accent (the iMac
		// G3 Bondi-blue / iPod scroll-wheel ring colour), charcoal-
		// blue body text (the cool slate every Y2K UI printed
		// against pearl), icy-white shine highlights / etched-
		// aluminium ripples. Reads dark-on-light like the Nokia 3310
		// + Game Boy DMG themes - the authentic Y2K reading
		// direction (cool dark text on a polished silver panel) -
		// giving the theme picker a physical-world feel that
		// contrasts with the light-on-dark Synthwave / Amber CRT /
		// Aqua / RAZR / Stealth Black themes. Wallpaper bypasses
		// every Synthwave builder and paints a flat pearl-silver
		// gradient panel with a few horizontal brushed-aluminium
		// grain striations + scattered Lucite frost specks + a
		// stylised translucent-Lucite "raindrop" motif anchored
		// bottom-right (the universal "Y2K frost" brand cue,
		// copyright-safe vs the iMac G3 Bondi droplet or the iPod
		// scroll-wheel ring).
		Y2KSilver = 7,

		// Cyberpunk Red — neon-on-void homage to the late-1980s /
		// early-2020s "neo-Tokyo at night" cyberpunk aesthetic
		// (Akira's red-on-black title plate, Blade Runner's neon-saturated
		// alley signage, Ghost in the Shell's wired-circuit trace lines,
		// the Cyberpunk 2077 brand identity, and the 1980s Tokyo arcade
		// neon-tube glow that defined the whole genre's visual
		// vocabulary). Six shades calibrated for a high-contrast neon
		// red focus accent against an obsidian void: warm void-black
		// panel-off, slightly red-shifted blood-black gradient bottom,
		// muted maroon idle borders, neon-red focus accent (the single
		// splash of glowing colour that defines every cyberpunk
		// signage cue), hot magenta-pink rimlight highlights, and a
		// near-white phosphor-pink body-text colour (the "neon glow
		// burning into the retina" effect every cyberpunk UI prints in).
		// Reads light-on-dark like the Default Synthwave + Amber CRT
		// + Sony Ericsson Aqua + RAZR Hot Pink + Stealth Black themes
		// — the authentic late-1980s / early-2020s cyberpunk reading
		// direction (neon menu items on a void-black panel, with a
		// glowing neon-red accent standing in for the era's signature
		// neon-tube signage). Wallpaper bypasses every Synthwave
		// builder and paints a flat near-void gradient panel with a
		// few faint horizontal circuit-trace bus lines + a sparse
		// constellation of neon glitch pixels + a small triangular
		// hazard / "DANGER" chevron motif anchored bottom-right (the
		// trademark-safe equivalent of the era's signature corporate-
		// warning iconography).
		CyberpunkRed = 8,

		// Reserved 9..15 for the upcoming Phase O themes:
		//   9  Christmas    (S117)
		//  10  Surprise/Daily-Cycle (S119)
	};

	/** Total number of themes the picker should expose today. */
	static constexpr uint8_t ThemeCount = 9;

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
	/*
	 * S112 - Stealth Black status-LED helpers (icon-glyph pass).
	 *
	 * The defining visual cue of every early-2010s "blacked-out" tactical
	 * handset (Vertu Constellation Black, BlackBerry Bold 9900 Stealth,
	 * Nokia 8800 Carbon Arte, the obsidian-slab generation that bridged
	 * the late-2000s feature phone and the late-2010s glass-sandwich
	 * smartphone) was a single tactical-red status LED indicator -
	 * usually a 1-2 mm pinprick in the top corner of the bezel that
	 * stayed lit whenever the device was armed. Everything else about
	 * the phone read as a sea of obsidian + bone-white menu chrome;
	 * the LED was the only chromatic accent, and the only thing that
	 * told you the device was actually on rather than a slab of glass.
	 * Without that LED, an obsidian tile with bone-white icon strokes
	 * just reads as 'a dark icon on a black background', missing the
	 * single visual that defined the era's tactical aesthetic.
	 *
	 * Phase O folds that effect into PhoneIconTile via a per-theme
	 * 'status-LED' overlay - a 2x2 STEALTH_LED dot rendered in the
	 * top-right corner of every tile body, capped with a 1x1
	 * STEALTH_BONE highlight pixel in the upper-left of that dot
	 * (the LED's emission peak, the bright spec every photo of an
	 * armed status LED captures). On themes that don't have a
	 * tactical-LED convention (Default Synthwave purple, Nokia 3310
	 * LCD, Game Boy DMG LCD, Amber CRT phosphor, Sony Ericsson Aqua
	 * glass, RAZR Hot Pink EL backlight) the dot stays fully
	 * transparent, byte-identical to the previous behaviour. On
	 * Stealth Black, the dot rests at a moderate idle opacity - the
	 * always-on status LED cue - and the selected tile burns the
	 * dot to full intensity (the 'this key is the active selection,
	 * status LED at full intensity' cue every armed tactical
	 * handset's UI used to mark its focused row).
	 *
	 *   statusLedEnabled()        - true only for StealthBlack
	 *   statusLedColor()          - STEALTH_LED under StealthBlack,
	 *                               MP_ACCENT otherwise (the latter is
	 *                               a fallback that's never observed
	 *                               because callers gate on
	 *                               statusLedEnabled() first via the
	 *                               opacity helpers below)
	 *   statusLedHighlightColor() - STEALTH_BONE under StealthBlack,
	 *                               MP_TEXT otherwise (same fallback
	 *                               semantics; never observed outside
	 *                               Stealth Black because the highlight
	 *                               pixel rides the same opacity as
	 *                               the LED dot)
	 *   statusLedIdleOpa()        - LV_OPA_70 under StealthBlack,
	 *                               LV_OPA_TRANSP otherwise (so the
	 *                               existing tiles render byte-
	 *                               identically on every other theme)
	 *   statusLedSelectedOpa()    - LV_OPA_COVER under StealthBlack,
	 *                               LV_OPA_TRANSP otherwise
	 *
	 * The selected-vs-idle opacity gap (70% -> 100%) is intentionally
	 * smaller than the S108 Aqua chrome-shine (50% -> 100%) and the
	 * S110 RAZR EL-bleed (40% -> 100%) because a real tactical-handset
	 * status LED never went dark - it stayed lit at near-full intensity
	 * whenever the device was armed, and only pulsed slightly on
	 * activity. The 70%/100% gap captures that 'always-armed, briefly
	 * burning hotter on focus' behaviour without flashing the dot from
	 * faint to bright (which would read as a notification LED rather
	 * than a status LED). PhoneIconTile applies this static (non-
	 * pulsing) overlay rather than animating it because the existing
	 * halo already pulses on selection; layering a second pulsing
	 * element on top would make the focused tile read as jittery
	 * rather than 'armed', which contradicts the cool composure of
	 * the Stealth Black aesthetic.
	 *
	 * Why top-right corner (vs S108's top edge / S110's bottom edge):
	 * the status LED on a real tactical handset always sat in the
	 * top-right of the bezel (the convention shared by every
	 * blacked-out feature phone of the era - the LED was placed where
	 * a glance at the device while it was face-down on a desk would
	 * still catch the indicator). Anchoring the dot to the top-right
	 * corner of the tile keeps the cue physically faithful and gives
	 * the Stealth Black theme a visual axis that's distinct from the
	 * Aqua top-shine and the RAZR bottom-bleed - so a user flipping
	 * between Aqua, RAZR, and Stealth Black sees the highlight move
	 * from top edge to bottom edge to top-right corner, reinforcing
	 * that they're three genuinely different lighting models rather
	 * than three recoloured versions of the same overlay.
	 */
	static bool       statusLedEnabled();
	static lv_color_t statusLedColor();
	static lv_color_t statusLedHighlightColor();
	static uint8_t    statusLedIdleOpa();
	static uint8_t    statusLedSelectedOpa();

	/*
	 * S114 - Y2K Silver translucent-Lucite jewel helpers (icon-glyph pass).
	 *
	 * The defining visual cue of the turn-of-the-millennium Y2K
	 * consumer-electronics aesthetic (iMac G3 Snow / Bondi / Tangerine,
	 * iPod 1G click-wheel, Sony Discman MZ-E700, Sony VAIO PCG-505,
	 * Nokia 8210 'Frost Silver', Sharp J-SH04 - the brushed-aluminium-
	 * and-translucent-blue gadgets that defined the late-1990s to early-
	 * 2000s era of consumer electronics) was a small translucent-Lucite
	 * accent - usually a coloured Bondi-blue jewel or a frosted polycarb
	 * insert - tucked into one corner of the gadget's polished pearl-
	 * silver shell. The iMac G3's translucent handle, the iPod 1G's
	 * scroll-wheel ring, the Sony Discman's circular Lucite power LED,
	 * the VAIO PCG-505's translucent-blue badge: every Y2K-era device
	 * paired its brushed-aluminium body with one carefully-placed
	 * Lucite accent, and that pairing was the era's single defining
	 * visual cue. Without that translucent-Lucite jewel, a pearl-silver
	 * tile with charcoal-blue icon strokes just reads as 'a flat icon
	 * on a silver panel' rather than 'a Y2K-era polished-aluminium
	 * gadget with a Bondi-blue Lucite accent', missing the entire point
	 * of the era's industrial-design vocabulary.
	 *
	 * Phase O folds that effect into PhoneIconTile via a per-theme
	 * 'Lucite-jewel' overlay - a 3 x 3 Y2K_BONDI translucent jewel
	 * rendered in the bottom-left corner of every tile body, capped
	 * with a 1 x 1 Y2K_SHINE highlight pixel in the upper-left of the
	 * jewel (the iconic Lucite 'spec' - the way every photographed
	 * translucent-Lucite gadget always exhibited a near-white reflection
	 * peak in the upper-left of the jewel, the cue your eye uses to
	 * resolve 'is this a flat painted dot or a translucent volumetric
	 * jewel'). On themes that don't have a Lucite-jewel convention
	 * (Default Synthwave purple, Nokia 3310 LCD, Game Boy DMG LCD,
	 * Amber CRT phosphor, Sony Ericsson Aqua glass, RAZR Hot Pink EL
	 * backlight, Stealth Black tactical handset) the jewel stays fully
	 * transparent, byte-identical to the previous behaviour. On Y2K
	 * Silver, the jewel rests at a moderate translucent-Lucite idle
	 * opacity - the always-cloudy 'frosted Bondi-blue accent' cue every
	 * Y2K-era gadget displayed when ambient light caught its Lucite
	 * insert - and the selected tile burns the jewel to full saturation
	 * (the 'this row is the active selection, Lucite jewel catching a
	 * direct light beam' cue every iMac G3 / iPod 1G UI used to mark
	 * its focused row, where the translucent accent suddenly read as
	 * fully-saturated Bondi blue rather than its usual frosted idle
	 * shade).
	 *
	 *   luciteJewelEnabled()         - true only for Y2KSilver
	 *   luciteJewelColor()           - Y2K_BONDI under Y2KSilver, falls
	 *                                  back to MP_ACCENT otherwise (the
	 *                                  fallback is never observed
	 *                                  because callers gate on
	 *                                  luciteJewelEnabled() first via
	 *                                  the opacity helpers below)
	 *   luciteJewelHighlightColor()  - Y2K_SHINE under Y2KSilver, falls
	 *                                  back to MP_TEXT otherwise (same
	 *                                  fallback semantics; never
	 *                                  observed outside Y2K Silver
	 *                                  because the highlight pixel
	 *                                  rides the same opacity as the
	 *                                  jewel)
	 *   luciteJewelIdleOpa()         - LV_OPA_30 under Y2KSilver,
	 *                                  LV_OPA_TRANSP otherwise (so the
	 *                                  existing tiles render byte-
	 *                                  identically on every other
	 *                                  theme)
	 *   luciteJewelSelectedOpa()     - LV_OPA_COVER under Y2KSilver,
	 *                                  LV_OPA_TRANSP otherwise
	 *
	 * The selected-vs-idle opacity gap (30% -> 100%) is intentionally
	 * the widest of the four Phase O icon-glyph overlays - wider than
	 * S108's 50% -> 100% (Aqua chrome shine), S110's 40% -> 100% (RAZR
	 * EL bleed), and S112's 70% -> 100% (Stealth Black status LED) -
	 * because a real Y2K-era translucent-Lucite jewel exhibited the
	 * widest dynamic range of any of the four era-defining lighting
	 * cues: idle Lucite was almost imperceptibly tinted (the polycarb
	 * cloudiness diffused the colour to a near-pearl shade), but a
	 * direct light beam would saturate the jewel to its full Bondi
	 * intensity in a single perceptual step - the ' Lucite glows when
	 * the light hits it just right' phenomenon every iMac G3 owner
	 * remembered. The 30%->100% gap captures that 'frosted to
	 * fully-saturated' transition without animating the jewel through
	 * an in-between state. PhoneIconTile applies this static (non-
	 * pulsing) overlay rather than animating it because the existing
	 * halo already pulses on selection; layering a second pulsing
	 * element on top would make the focused tile read as jittery
	 * rather than 'lit', which contradicts the still-photograph
	 * polish of the Y2K aesthetic (where the gadget always read as a
	 * carefully-lit product shot, never an animated UI element).
	 *
	 * Why bottom-left corner (vs S108's top edge / S110's bottom edge /
	 * S112's top-right corner): the four Phase O icon-glyph overlays
	 * are deliberately anchored to four disjoint geometric axes - top
	 * edge, bottom edge, top-right corner, bottom-left corner - so a
	 * future theme can layer any subset of the four cues without
	 * overpainting, and so a user flipping between Aqua, RAZR, Stealth
	 * Black, and Y2K Silver sees the highlight move from top edge to
	 * bottom edge to top-right corner to bottom-left corner,
	 * reinforcing that they're four genuinely different lighting
	 * models rather than four recoloured versions of the same
	 * overlay. The bottom-left anchor is also faithful to the era's
	 * industrial-design vocabulary: the iMac G3's translucent handle
	 * sat at the bottom-left of the cabinet, the iPod 1G's Apple logo
	 * sat at the bottom-left of the polished face, and the Sony
	 * Discman's Lucite power LED sat in the bottom-left corner of the
	 * lid - the bottom-left was where Y2K-era industrial designers
	 * placed their signature-Lucite accents, so anchoring the jewel
	 * to the bottom-left of the tile captures that placement
	 * directly.
	 */
	static bool       luciteJewelEnabled();
	static lv_color_t luciteJewelColor();
	static lv_color_t luciteJewelHighlightColor();
	static uint8_t    luciteJewelIdleOpa();
	static uint8_t    luciteJewelSelectedOpa();
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


/*
 * ---------------------------------------------------------------------
 * Y2K Silver palette (S113).
 *
 * Approximates the iconic turn-of-the-millennium "translucent chrome"
 * industrial-design vocabulary - the iMac G3 Snow, iPod 1G click-wheel,
 * Sony Discman MZ-E700, Sony VAIO PCG-505, Nokia 8210 "Frost Silver",
 * Sharp J-SH04, the brushed-aluminium-and-translucent-blue gadgets that
 * defined the late-1990s to early-2000s "Y2K" consumer-electronics era
 * before the mid-2000s RAZR flip phone and the late-2000s blacked-out
 * tactical handset reset the design conversation. Like the Nokia 3310
 * and Game Boy DMG palettes, Y2K Silver reads dark-on-light: cool
 * charcoal-blue menu items + electric-blue accents on a pearl-silver
 * panel, the authentic Y2K-era menu reading direction (and the visual
 * pole opposite the Synthwave / Amber CRT / Aqua / RAZR / Stealth
 * Black light-on-dark themes).
 *
 * Six shades, all in the cool silver / electric-blue gamut:
 *
 *   Y2K_BG_PEARL  - pearl-silver, the panel-off colour. Top of the
 *                   vertical gradient. A bright cool grey with a faint
 *                   blue bias - matches the polished-aluminium /
 *                   frosted-Lucite back panel of every Y2K-era gadget
 *                   when the screen is off and ambient light catches
 *                   the brushed surface.
 *   Y2K_BG_CHROME - brushed-aluminium chrome, the panel gradient
 *                   bottom. Slightly darker / more saturated cool grey
 *                   - the cue every iMac G3 Snow / iPod 1G owner saw
 *                   when the panel curved away from the light source
 *                   and the brushed grain caught a deeper shade.
 *   Y2K_FROST     - frosted-grey idle border / inactive chevron
 *                   colour. ~40 % darker than BG_PEARL so it reads as
 *                   legible-but-not-shouting next to the brighter
 *                   panel - the etched-aluminium trim every Y2K UI
 *                   used for inactive affordances.
 *   Y2K_BONDI     - electric Bondi-blue, the focus / accent colour.
 *                   The signature "iMac G3 Bondi" / iPod scroll-wheel
 *                   ring colour - the single splash of saturated blue
 *                   against the cool pearl panel. Calibrated toward
 *                   the iMac G3 Snow's translucent-blue ring rather
 *                   than the deeper Bondi original so it stays
 *                   readable on a 16 bpp panel.
 *   Y2K_INK       - charcoal-blue body text + icon-stroke colour.
 *                   The cool dark slate every Y2K UI printed against
 *                   pearl - bluer than the Nokia 3310 olive-ink, more
 *                   saturated than the DMG forest-green, but still in
 *                   the dark-on-light hierarchy. Matches the iPod
 *                   1G's menu-item ink and the Sony VAIO PCG-505's
 *                   etched-keyboard label colour.
 *   Y2K_SHINE     - icy-white shine highlight + ripple accent. Used
 *                   for top-edge gloss strips, etched-aluminium
 *                   ripples, dim labels (timestamps, placeholders),
 *                   and the Lucite-frost specks on the wallpaper.
 *                   Almost pure white with a hint of cool-blue bias
 *                   so a SHINE pixel reads as 'reflected ambient
 *                   light' rather than a focus accent.
 *
 * Stored as lv_color_make(R, G, B) so the values render identically on
 * every LV_COLOR_DEPTH the firmware might be built against - same
 * portability rationale as the prior palettes.
 *
 * Naming follows the MP_* / N3310_* / GBDMG_* / AMBER_CRT_* / AQUA_* /
 * RAZR_* / STEALTH_* convention so the part-2 icon-glyph swap in S114
 * only changes which header it includes, not how it spells colours.
 * ---------------------------------------------------------------------
 */
#define Y2K_BG_PEARL  lv_color_make(216, 222, 230)
#define Y2K_BG_CHROME lv_color_make(176, 184, 196)
#define Y2K_FROST     lv_color_make(140, 152, 168)
#define Y2K_BONDI     lv_color_make( 30, 130, 220)
#define Y2K_INK       lv_color_make( 32,  44,  68)
#define Y2K_SHINE     lv_color_make(248, 252, 255)



/*
 * ---------------------------------------------------------------------
 * Cyberpunk Red palette (S115).
 *
 * Approximates the iconic late-1980s / early-2020s "neo-Tokyo at night"
 * cyberpunk aesthetic — the Akira red-on-black title plate, Blade
 * Runner's neon-saturated rain-soaked alley signage, Ghost in the
 * Shell's wired-circuit trace lines, the Cyberpunk 2077 brand identity,
 * and the 1980s Tokyo arcade neon-tube glow that defined the whole
 * genre's visual vocabulary. Like the Default Synthwave + Amber CRT
 * + Sony Ericsson Aqua + RAZR Hot Pink + Stealth Black palettes,
 * Cyberpunk Red reads light-on-dark: neon-red menu accents + bright
 * phosphor-pink body text on a void-black panel, the authentic
 * cyberpunk reading direction (and the visual pole opposite the
 * Nokia 3310 + Game Boy DMG + Y2K Silver dark-on-light themes).
 *
 * Six shades, all in the void-black / neon-red gamut:
 *
 *   CYBER_BG_VOID   — warm void-black, the panel-off colour. Top of
 *                     the vertical gradient. Almost pure black with a
 *                     barely-perceptible warm-red bias — matches the
 *                     "neo-Tokyo midnight" panel-off cue every
 *                     cyberpunk handset rendered when the screen was
 *                     idle (the void you want a neon-red accent to
 *                     pop against, not the cool obsidian of the
 *                     Stealth Black palette).
 *   CYBER_BG_BLOOD  — deeper blood-shifted black, the panel gradient
 *                     bottom. Pulls the lower half of the panel
 *                     toward a faint blood-red subsurface glow without
 *                     the menu chrome having to fight for contrast —
 *                     the cue every Akira / Blade Runner / Cyberpunk
 *                     2077 frame buffer rendered when the city's
 *                     ambient red signage bled into the lower edge.
 *   CYBER_DIM       — muted maroon, used for idle borders and
 *                     inactive chevrons. ~40 % of NEON intensity so
 *                     it reads as legible-but-not-shouting next to
 *                     the brighter primary chrome — the etched-metal
 *                     trim every cyberpunk UI used for inactive
 *                     affordances, calibrated cooler than the RAZR
 *                     dim-magenta but warmer than the Stealth
 *                     gunmetal so the theme reads as "warning red,
 *                     idle" rather than "tactical red, idle".
 *   CYBER_NEON      — neon red, the focus / accent colour. The single
 *                     splash of glowing colour that defines every
 *                     cyberpunk signage cue — the Akira title-plate
 *                     red, the Cyberpunk 2077 logo red, the Blade
 *                     Runner replicant-warning red. Calibrated toward
 *                     the saturated end of the red gamut so the
 *                     accent reads as a glowing neon tube rather than
 *                     a status LED, and bright enough that it pops
 *                     off the void panel at every reading distance.
 *   CYBER_HOT       — hot magenta-pink, used for rimlight highlights
 *                     and anti-aliasing fringes around neon strokes.
 *                     The "neon glow blooming around a glass tube"
 *                     pixel every cyberpunk UI used to suggest the
 *                     accent is glowing rather than flat-painted.
 *                     Lighter and pinker than CYBER_NEON so a HOT
 *                     pixel reads as 'reflected neon scatter' rather
 *                     than a body fill.
 *   CYBER_TEXT      — phosphor-pink body text + icon-stroke colour.
 *                     The "neon glow burning into the retina" off-
 *                     white every cyberpunk UI printed against the
 *                     void — pinker than the Stealth bone-white,
 *                     warmer than the MP cream — so the text reads
 *                     as 'stained by the ambient neon' rather than
 *                     freshly engraved on bone or printed in cream.
 *                     Matches the Akira-logo phosphor-pink and the
 *                     Blade Runner subtitle phosphor-cream blend.
 *
 * Stored as lv_color_make(R, G, B) so the values render identically on
 * every LV_COLOR_DEPTH the firmware might be built against — same
 * portability rationale as the prior palettes.
 *
 * Naming follows the MP_* / N3310_* / GBDMG_* / AMBER_CRT_* / AQUA_* /
 * RAZR_* / STEALTH_* / Y2K_* convention so the part-2 icon-glyph swap
 * in S116 only changes which header it includes, not how it spells
 * colours.
 * ---------------------------------------------------------------------
 */
#define CYBER_BG_VOID   lv_color_make(  8,   2,   6)
#define CYBER_BG_BLOOD  lv_color_make( 28,   4,  10)
#define CYBER_DIM       lv_color_make( 96,  18,  32)
#define CYBER_NEON      lv_color_make(255,  30,  60)
#define CYBER_HOT       lv_color_make(255, 110, 150)
#define CYBER_TEXT      lv_color_make(255, 220, 224)

#endif // MAKERPHONE_THEME_H
