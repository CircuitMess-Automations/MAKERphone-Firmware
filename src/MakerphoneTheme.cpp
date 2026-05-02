#include "MakerphoneTheme.h"
#include "MakerphonePalette.h"
#include <Settings.h>

MakerphoneTheme::Theme MakerphoneTheme::themeFromByte(uint8_t raw){
	// Defensive clamp - any persisted byte outside the known enum range
	// falls back to Default so a corrupted NVS page can never render
	// against a non-existent theme. Same pattern PhoneSynthwaveBg uses
	// for wallpaperStyle.
	switch(raw){
		case static_cast<uint8_t>(Theme::Nokia3310):  return Theme::Nokia3310;
		case static_cast<uint8_t>(Theme::GameBoyDMG): return Theme::GameBoyDMG;
		case static_cast<uint8_t>(Theme::AmberCRT):   return Theme::AmberCRT;
		case static_cast<uint8_t>(Theme::SonyEricssonAqua):
		                                              return Theme::SonyEricssonAqua;
		case static_cast<uint8_t>(Theme::RazrHotPink):
		                                              return Theme::RazrHotPink;
		case static_cast<uint8_t>(Theme::StealthBlack):
		                                              return Theme::StealthBlack;
		case static_cast<uint8_t>(Theme::Y2KSilver):
		                                              return Theme::Y2KSilver;
		case static_cast<uint8_t>(Theme::CyberpunkRed):
		                                              return Theme::CyberpunkRed;
		case static_cast<uint8_t>(Theme::Default):
		default:                                      return Theme::Default;
	}
}

MakerphoneTheme::Theme MakerphoneTheme::getCurrent(){
	return themeFromByte(Settings.get().themeId);
}

const char* MakerphoneTheme::getName(Theme t){
	switch(t){
		case Theme::Default:    return "SYNTHWAVE";
		case Theme::Nokia3310:  return "NOKIA 3310";
		case Theme::GameBoyDMG: return "GAME BOY";
		case Theme::AmberCRT:        return "AMBER CRT";
		case Theme::SonyEricssonAqua: return "SE AQUA";
		case Theme::RazrHotPink: return "RAZR PINK";
		case Theme::StealthBlack: return "STEALTH";
		case Theme::Y2KSilver:  return "Y2K SILVER";
		case Theme::CyberpunkRed: return "CYBER RED";
		default:                return "DEFAULT";
	}
}

// ---------------------------------------------------------------------
// S102 — accent palette resolvers.
//
// Each helper switches on the active theme and returns the role-coloured
// lv_color_t. The Default branch uses the Synthwave MP_* values from
// MakerphonePalette.h so existing screens render identically when the
// user has not picked a theme; the Nokia3310 branch swaps to the
// pea-green LCD palette, inverting the dark-on-light convention so
// labels read as dark olive ink on a pale-olive panel — the authentic
// 1999 reading direction. The GameBoyDMG branch (added in S103) takes
// the same dark-on-light approach with the DMG-01's 4-shade green
// palette: pale-mint LCD backdrop, dark olive-ink strokes, plus a
// mid-shadow shade reserved for the icon-glyph dither pass that
// follows in S104. Highlight + label-dim are deliberately routed
// to the *mid* ink shades rather than the deepest one so future
// secondary text and idle borders read as legible-but-not-shouting
// against the bright LCD panel - the authentic DMG reading
// hierarchy.
//
// Reads MakerphoneTheme::getCurrent() each call rather than caching:
// the values are dirt-cheap (an `lv_color_make` literal) and theme
// switches in the picker take effect on the next screen build, which
// is exactly what we want.
// ---------------------------------------------------------------------

lv_color_t MakerphoneTheme::bgDark(){
	switch(getCurrent()){
		case Theme::Nokia3310:  return N3310_BG_LIGHT;
		case Theme::GameBoyDMG: return GBDMG_LCD_LIGHT;
		case Theme::AmberCRT:   return AMBER_CRT_BG_DARK;
		case Theme::SonyEricssonAqua: return AQUA_BG_DEEP;
		case Theme::RazrHotPink: return RAZR_BG_DARK;
		case Theme::StealthBlack: return STEALTH_BG_OBSIDIAN;
		case Theme::Y2KSilver:  return Y2K_BG_PEARL;
		case Theme::CyberpunkRed:  return CYBER_BG_VOID;
		case Theme::Default:
		default:                return MP_BG_DARK;
	}
}

lv_color_t MakerphoneTheme::accent(){
	switch(getCurrent()){
		case Theme::Nokia3310:  return N3310_FRAME;
		case Theme::GameBoyDMG: return GBDMG_INK;
		case Theme::AmberCRT:   return AMBER_CRT_HOT;
		case Theme::SonyEricssonAqua: return AQUA_GLOW;
		case Theme::RazrHotPink: return RAZR_GLOW;
		case Theme::StealthBlack: return STEALTH_LED;
		case Theme::Y2KSilver:  return Y2K_BONDI;
		case Theme::CyberpunkRed:  return CYBER_NEON;
		case Theme::Default:
		default:                return MP_ACCENT;
	}
}

lv_color_t MakerphoneTheme::highlight(){
	switch(getCurrent()){
		case Theme::Nokia3310:  return N3310_PIXEL;
		case Theme::GameBoyDMG: return GBDMG_INK_MID;
		case Theme::AmberCRT:   return AMBER_CRT_GLOW;
		case Theme::SonyEricssonAqua: return AQUA_CHROME;
		case Theme::RazrHotPink: return RAZR_CHROME;
		case Theme::StealthBlack: return STEALTH_BONE;
		case Theme::Y2KSilver:  return Y2K_INK;
		case Theme::CyberpunkRed:  return CYBER_TEXT;
		case Theme::Default:
		default:                return MP_HIGHLIGHT;
	}
}

lv_color_t MakerphoneTheme::dim(){
	switch(getCurrent()){
		case Theme::Nokia3310:  return N3310_PIXEL_DIM;
		case Theme::GameBoyDMG: return GBDMG_LCD_MID;
		case Theme::AmberCRT:   return AMBER_CRT_DIM;
		case Theme::SonyEricssonAqua: return AQUA_DIM;
		case Theme::RazrHotPink: return RAZR_DIM;
		case Theme::StealthBlack: return STEALTH_GUNMETAL;
		case Theme::Y2KSilver:  return Y2K_FROST;
		case Theme::CyberpunkRed:  return CYBER_DIM;
		case Theme::Default:
		default:                return MP_DIM;
	}
}

lv_color_t MakerphoneTheme::text(){
	switch(getCurrent()){
		case Theme::Nokia3310:  return N3310_PIXEL;
		case Theme::GameBoyDMG: return GBDMG_INK;
		case Theme::AmberCRT:   return AMBER_CRT_GLOW;
		case Theme::SonyEricssonAqua: return AQUA_CHROME;
		case Theme::RazrHotPink: return RAZR_CHROME;
		case Theme::StealthBlack: return STEALTH_BONE;
		case Theme::Y2KSilver:  return Y2K_INK;
		case Theme::CyberpunkRed:  return CYBER_TEXT;
		case Theme::Default:
		default:                return MP_TEXT;
	}
}

lv_color_t MakerphoneTheme::labelDim(){
	switch(getCurrent()){
		case Theme::Nokia3310:  return N3310_PIXEL_DIM;
		case Theme::GameBoyDMG: return GBDMG_INK_MID;
		case Theme::AmberCRT:   return AMBER_CRT_DIM;
		case Theme::SonyEricssonAqua: return AQUA_FOAM;
		case Theme::RazrHotPink: return RAZR_SHINE;
		case Theme::StealthBlack: return STEALTH_STEEL;
		case Theme::Y2KSilver:  return Y2K_FROST;
		case Theme::CyberpunkRed:  return CYBER_DIM;
		case Theme::Default:
		default:                return MP_LABEL_DIM;
	}
}

// ---------------------------------------------------------------------
// S104 - icon-glyph accent resolvers.
//
// Each helper switches on the active theme and returns the role-coloured
// lv_color_t for the icon-glyph layer. Default + Nokia 3310 mappings
// are deliberately byte-identical to what `highlight()` / `accent()`
// already return for those themes - the only theme that benefits from
// the new helpers is Game Boy DMG, where the 4-shade green LCD needs a
// brightness hierarchy (darkest ink for outlines, mid-shadow for inner
// details) rather than a hue hierarchy. Wiring PhoneIconTile.cpp to
// `iconStroke()` / `iconDetail()` therefore changes nothing for the
// existing Synthwave + Nokia tiles, and turns the DMG tile glyphs from
// flat-mid-shadow silhouettes into properly-shaded DMG-era sprites.
// ---------------------------------------------------------------------

lv_color_t MakerphoneTheme::iconStroke(){
	switch(getCurrent()){
		case Theme::Nokia3310:  return N3310_PIXEL;
		case Theme::GameBoyDMG: return GBDMG_INK;
		case Theme::AmberCRT:   return AMBER_CRT_GLOW;
		case Theme::SonyEricssonAqua: return AQUA_CHROME;
		case Theme::RazrHotPink: return RAZR_CHROME;
		case Theme::StealthBlack: return STEALTH_BONE;
		case Theme::Y2KSilver:  return Y2K_INK;
		case Theme::CyberpunkRed:  return CYBER_TEXT;
		case Theme::Default:
		default:                return MP_HIGHLIGHT;
	}
}

lv_color_t MakerphoneTheme::iconDetail(){
	switch(getCurrent()){
		case Theme::Nokia3310:  return N3310_FRAME;
		case Theme::GameBoyDMG: return GBDMG_INK_MID;
		case Theme::AmberCRT:   return AMBER_CRT_HOT;
		case Theme::SonyEricssonAqua: return AQUA_GLOW;
		case Theme::RazrHotPink: return RAZR_GLOW;
		case Theme::StealthBlack: return STEALTH_LED;
		case Theme::Y2KSilver:  return Y2K_BONDI;
		case Theme::CyberpunkRed:  return CYBER_NEON;
		case Theme::Default:
		default:                return MP_ACCENT;
	}
}


// ---------------------------------------------------------------------
// S106 - Amber CRT phosphor-bloom halo helpers.
//
// Real 1980s amber-phosphor CRTs exhibit a faint always-on halo around
// bright pixels: the energised phosphor 'bleeds' about a pixel into
// surrounding panel area, so an icon never sits as a hard-edged
// graphic against a dead-black panel - it always has a soft warm edge
// spreading into the bezel. PhoneIconTile consumes these helpers at
// idle (gated on phosphorGlowEnabled()) so every tile under Amber CRT
// rests with a faint AMBER_CRT_DIM border peeking past the tile body,
// and the selected-pulse range bumps to 50%-100% so a selected tile
// reads as 'hotter' than its idle neighbours - the same beam-intensity
// cue you'd see on a real Apple ///, IBM 5151, or Wyse 50 terminal
// when the cursor row redraws at full energy.
//
// Default / Nokia 3310 / Game Boy DMG return values that produce the
// previous byte-identical behaviour: phosphorGlowEnabled() == false,
// phosphorGlowOpa() == LV_OPA_TRANSP (so phosphorGlow()'s colour is
// never observed), pulse range stays 30%-80% so the existing tile
// pulse cadence is unchanged.
// ---------------------------------------------------------------------

bool MakerphoneTheme::phosphorGlowEnabled(){
	return getCurrent() == Theme::AmberCRT;
}

lv_color_t MakerphoneTheme::phosphorGlow(){
	switch(getCurrent()){
		case Theme::AmberCRT:   return AMBER_CRT_DIM;
		case Theme::Nokia3310:  return N3310_PIXEL_DIM;
		case Theme::GameBoyDMG: return GBDMG_LCD_MID;
		case Theme::SonyEricssonAqua: return AQUA_DIM;
		case Theme::RazrHotPink: return RAZR_DIM;
		case Theme::StealthBlack: return STEALTH_GUNMETAL;
		case Theme::Y2KSilver:  return Y2K_FROST;
		case Theme::CyberpunkRed:  return CYBER_DIM;
		case Theme::Default:
		default:                return MP_DIM;
	}
}

uint8_t MakerphoneTheme::phosphorGlowOpa(){
	// LV_OPA_20 is intentionally low: the bloom should be visible
	// but never compete with the icon strokes themselves. On a real
	// CRT the bloom is brightest immediately adjacent to the lit
	// pixel and falls off rapidly with distance; LVGL only gives us
	// a single border layer so a single dim opacity is the closest
	// approximation we can achieve without a per-pixel canvas pass.
	return phosphorGlowEnabled() ? LV_OPA_20 : LV_OPA_TRANSP;
}

uint8_t MakerphoneTheme::phosphorPulseLow(){
	return phosphorGlowEnabled() ? LV_OPA_50 : LV_OPA_30;
}

uint8_t MakerphoneTheme::phosphorPulseHigh(){
	return phosphorGlowEnabled() ? LV_OPA_COVER : LV_OPA_80;
}


// ---------------------------------------------------------------------
// S108 - Sony Ericsson Aqua chrome-shine helpers.
//
// The late-2000s Sony Ericsson "Aqua" UI rendered every menu tile, soft-
// key and focus row with a thin bright highlight along its upper edge -
// a reflected-light cue that suggested polished glass / chrome rather
// than a flat coloured panel. That's the single visual that defined the
// Aqua look on the W910i, W995, K850i and C-series Cyber-shot phones,
// and it's what separates the Aqua skin from a generic dark-blue tile.
//
// PhoneIconTile consumes these helpers at idle (gated on
// chromeShineEnabled()) so every tile under SonyEricssonAqua rests with
// a faint AQUA_FOAM strip across the top of its body - the always-on
// 'glass catching ambient light' cue. Selecting the tile then snaps the
// strip to LV_OPA_COVER so the focused tile reads as 'lit by a direct
// sunbeam' against its softly-shining neighbours - the same wet-shine
// cue Sony Ericsson used to mark the focused row on the W910i menu
// carousel.
//
// Default / Nokia 3310 / Game Boy DMG / Amber CRT return values that
// produce the previous byte-identical behaviour: chromeShineEnabled()
// == false, both opacity helpers return LV_OPA_TRANSP (so
// chromeShineColor()'s value is never observed), keeping the existing
// tile silhouette unchanged on every non-Aqua theme.
// ---------------------------------------------------------------------

bool MakerphoneTheme::chromeShineEnabled(){
	return getCurrent() == Theme::SonyEricssonAqua;
}

lv_color_t MakerphoneTheme::chromeShineColor(){
	switch(getCurrent()){
		case Theme::SonyEricssonAqua: return AQUA_FOAM;
		// The fallbacks below are never observed - chromeShineIdleOpa()
		// and chromeShineSelectedOpa() both return LV_OPA_TRANSP on
		// every non-Aqua theme, so the strip's colour can't reach the
		// framebuffer. The values still resolve to a sensible per-theme
		// 'lightest accent' so a future caller that probes the colour
		// outside the opacity gate (e.g. a debug overlay) reads
		// something coherent rather than an undefined value.
		case Theme::Nokia3310:  return N3310_HIGHLIGHT;
		case Theme::GameBoyDMG: return GBDMG_LCD_LIGHT;
		case Theme::AmberCRT:   return AMBER_CRT_HOT;
		case Theme::RazrHotPink: return RAZR_SHINE;
		case Theme::StealthBlack: return STEALTH_STEEL;
		case Theme::Y2KSilver:        return Y2K_SHINE;
		case Theme::CyberpunkRed:        return CYBER_HOT;
		case Theme::Default:
		default:                return MP_HIGHLIGHT;
	}
}

uint8_t MakerphoneTheme::chromeShineIdleOpa(){
	// LV_OPA_50 is the calibrated 'always-on glass shine' opacity:
	// bright enough that the strip reads as a deliberate highlight
	// rather than a stray pixel, dim enough that the tile body
	// underneath stays the dominant colour. On a real W910i the
	// idle chrome edge sat roughly half-way between the panel
	// colour and pure white; LV_OPA_50 with AQUA_FOAM as the source
	// is the closest one-pixel-strip approximation of that
	// blend on a 16 bpp panel.
	return chromeShineEnabled() ? LV_OPA_50 : LV_OPA_TRANSP;
}

uint8_t MakerphoneTheme::chromeShineSelectedOpa(){
	// LV_OPA_COVER (full intensity) on selection - the focused Aqua
	// tile snaps to a 'wet bright top edge' that the eye reads as
	// 'this tile is currently catching the light'. This is a
	// non-pulsing, on/off overlay because the existing halo already
	// pulses on selection; layering a second pulsing element on top
	// would make the focused tile read as jittery rather than
	// confidently 'lit'.
	return chromeShineEnabled() ? LV_OPA_COVER : LV_OPA_TRANSP;
}


// ---------------------------------------------------------------------
// S110 - RAZR Hot Pink edge-glow helpers.
//
// The mid-2000s Motorola RAZR V3 / V3i keypad sat over an
// electroluminescent (EL) backlight panel: a thin pink-white sheet that
// always lit when the phone was awake and bled hot magenta-pink around
// the boundaries of every etched-chrome character. The result was a
// distinctive 'icon haloed from below' look - the chrome strokes stayed
// dark, the EL leak burned bright pink underneath them, and every key
// read as a fingerprint silhouette against a glowing panel. That
// bottom-edge bleed is the single visual that defined the RAZR keypad,
// and it's what separates the RAZR Hot Pink skin from a generic dark-
// magenta tile.
//
// PhoneIconTile consumes these helpers at idle (gated on
// edgeGlowEnabled()) so every tile under RazrHotPink rests with a faint
// RAZR_GLOW strip across the bottom of its body - the always-on EL
// backlight cue. Selecting the tile then snaps the strip to LV_OPA_COVER
// so the focused tile reads as 'this key is pressed and the panel is
// lit at full intensity' against its softly-haloed neighbours - the
// same press-feedback cue every RAZR owner felt when their thumb hit
// the d-pad.
//
// Default / Nokia 3310 / Game Boy DMG / Amber CRT / Sony Ericsson Aqua
// return values that produce the previous byte-identical behaviour:
// edgeGlowEnabled() == false, both opacity helpers return LV_OPA_TRANSP
// (so edgeGlowColor()'s value is never observed), keeping the existing
// tile silhouette unchanged on every non-RAZR theme.
//
// Mechanically the strip is the mirror of S108's chrome-shine top
// strip, but the bottom-edge anchor + RAZR_GLOW colour give the RAZR
// theme a visual axis that's distinct from the Aqua look - so a user
// flipping between Aqua and RAZR sees the highlight strip swap edges
// (top -> bottom) and hue (foam -> hot pink), reinforcing that they're
// two genuinely different lighting models rather than two recoloured
// versions of the same overlay.
// ---------------------------------------------------------------------

bool MakerphoneTheme::edgeGlowEnabled(){
	return getCurrent() == Theme::RazrHotPink;
}

lv_color_t MakerphoneTheme::edgeGlowColor(){
	switch(getCurrent()){
		case Theme::RazrHotPink: return RAZR_GLOW;
		// The fallbacks below are never observed - edgeGlowIdleOpa()
		// and edgeGlowSelectedOpa() both return LV_OPA_TRANSP on every
		// non-RAZR theme, so the strip's colour can't reach the
		// framebuffer. The values still resolve to a sensible per-theme
		// 'brightest accent' so a future caller that probes the colour
		// outside the opacity gate (e.g. a debug overlay) reads
		// something coherent rather than an undefined value.
		case Theme::Nokia3310:        return N3310_FRAME;
		case Theme::GameBoyDMG:       return GBDMG_INK;
		case Theme::AmberCRT:         return AMBER_CRT_HOT;
		case Theme::SonyEricssonAqua: return AQUA_GLOW;
		case Theme::StealthBlack:     return STEALTH_LED;
		case Theme::Y2KSilver:        return Y2K_BONDI;
		case Theme::CyberpunkRed:        return CYBER_NEON;
		case Theme::Default:
		default:                      return MP_ACCENT;
	}
}

uint8_t MakerphoneTheme::edgeGlowIdleOpa(){
	// LV_OPA_40 is the calibrated 'always-on EL backlight' opacity:
	// bright enough that the strip reads as a deliberate halo - the
	// pink panel light visibly leaking out from under the chrome key -
	// dim enough that the tile body underneath stays the dominant
	// dark-magenta colour. On a real V3 the EL keypad rested at
	// roughly 35-45% of full brightness when idle (the panel never
	// fully dimmed during use - that was the whole reason RAZR
	// owners loved typing in the dark) and LV_OPA_40 is the closest
	// 1-pixel-strip approximation of that bleed on a 16 bpp panel.
	// One step deeper than S108's LV_OPA_50 chrome-shine idle so the
	// idle RAZR halo reads as 'soft EL bleed' rather than 'lit chrome'
	// - the visual difference between an electroluminescent panel
	// (warm, diffuse, soft) and a polished chrome reflection (cool,
	// crisp, bright).
	return edgeGlowEnabled() ? LV_OPA_40 : LV_OPA_TRANSP;
}

uint8_t MakerphoneTheme::edgeGlowSelectedOpa(){
	// LV_OPA_COVER (full intensity) on selection - the focused RAZR
	// tile snaps to a 'fully lit pink panel under the icon' that the
	// eye reads as 'this key is currently pressed'. This is a non-
	// pulsing, on/off overlay because the existing halo already pulses
	// on selection; layering a second pulsing element on top would
	// make the focused tile read as jittery rather than 'pressed',
	// which contradicts the snappy RAZR feel (a real V3's keypad
	// transitioned cleanly between idle-EL-bleed and full-press
	// brightness, no soft fade in between - the EL panel had no
	// gradient, only on-or-off behind each key region).
	return edgeGlowEnabled() ? LV_OPA_COVER : LV_OPA_TRANSP;
}


// ---------------------------------------------------------------------
// S112 - Stealth Black status-LED helpers.
//
// The early-2010s "blacked-out" tactical-handset aesthetic (Vertu
// Constellation Black, BlackBerry Bold 9900 Stealth, Nokia 8800 Carbon
// Arte, the obsidian-slab generation that bridged the late-2000s
// feature phone to the late-2010s glass-sandwich smartphone) shipped a
// single defining accent: a tactical-red status LED indicator in the
// top corner of the bezel that stayed lit whenever the device was
// armed. Everything else about the phone read as a sea of obsidian +
// bone-white menu chrome; the LED was the only chromatic accent, and
// the only thing that told you the device was actually on rather than
// a slab of glass. Without that LED, an obsidian tile with bone-white
// icon strokes just reads as 'a dark icon on a black background',
// missing the single visual that defined the era.
//
// PhoneIconTile consumes these helpers at idle (gated on
// statusLedEnabled()) so every tile under StealthBlack rests with a
// faint STEALTH_LED dot in its top-right corner - the always-on
// status-LED cue. Selecting the tile then snaps the dot to LV_OPA_COVER
// so the focused tile reads as 'this row is the active selection,
// status LED at full intensity' against its softly-armed neighbours -
// the same focus-feedback cue every armed tactical handset's UI used
// to mark its current row.
//
// Default / Nokia 3310 / Game Boy DMG / Amber CRT / Sony Ericsson Aqua
// / RAZR Hot Pink return values that produce the previous byte-
// identical behaviour: statusLedEnabled() == false, both opacity
// helpers return LV_OPA_TRANSP (so statusLedColor() and
// statusLedHighlightColor() values are never observed), keeping the
// existing tile silhouette unchanged on every non-Stealth-Black theme.
//
// Mechanically a corner-anchored 2x2 dot (with a 1x1 highlight pixel),
// not the top-edge / bottom-edge strip patterns S108 + S110 used. The
// corner-anchor + STEALTH_LED colour give the Stealth Black theme a
// visual axis that's distinct from both the Aqua top-shine and the
// RAZR bottom-bleed - so a user flipping between Aqua, RAZR, and
// Stealth Black sees the highlight move from top edge to bottom edge
// to top-right corner, reinforcing that they're three genuinely
// different lighting models rather than three recoloured versions of
// the same overlay.
// ---------------------------------------------------------------------

bool MakerphoneTheme::statusLedEnabled(){
	return getCurrent() == Theme::StealthBlack;
}

lv_color_t MakerphoneTheme::statusLedColor(){
	switch(getCurrent()){
		case Theme::StealthBlack: return STEALTH_LED;
		// The fallbacks below are never observed - statusLedIdleOpa()
		// and statusLedSelectedOpa() both return LV_OPA_TRANSP on every
		// non-Stealth-Black theme, so the dot's colour can't reach the
		// framebuffer. The values still resolve to a sensible per-theme
		// 'brightest accent' so a future caller that probes the colour
		// outside the opacity gate (e.g. a debug overlay) reads
		// something coherent rather than an undefined value.
		case Theme::Nokia3310:        return N3310_FRAME;
		case Theme::GameBoyDMG:       return GBDMG_INK;
		case Theme::AmberCRT:         return AMBER_CRT_HOT;
		case Theme::SonyEricssonAqua: return AQUA_GLOW;
		case Theme::RazrHotPink:      return RAZR_GLOW;
		case Theme::Y2KSilver:        return Y2K_BONDI;
		case Theme::CyberpunkRed:        return CYBER_NEON;
		case Theme::Default:
		default:                      return MP_ACCENT;
	}
}

lv_color_t MakerphoneTheme::statusLedHighlightColor(){
	switch(getCurrent()){
		case Theme::StealthBlack: return STEALTH_BONE;
		// The fallbacks below are never observed - the highlight pixel
		// rides the same opacity as the LED dot, and that opacity
		// returns LV_OPA_TRANSP on every non-Stealth-Black theme.
		// The values still resolve to a sensible per-theme 'brightest
		// chrome / body-text white' so a future caller that probes
		// the colour outside the opacity gate reads something coherent.
		case Theme::Nokia3310:        return N3310_HIGHLIGHT;
		case Theme::GameBoyDMG:       return GBDMG_LCD_LIGHT;
		case Theme::AmberCRT:         return AMBER_CRT_HOT;
		case Theme::SonyEricssonAqua: return AQUA_CHROME;
		case Theme::RazrHotPink:      return RAZR_CHROME;
		case Theme::Y2KSilver:        return Y2K_SHINE;
		case Theme::CyberpunkRed:        return CYBER_HOT;
		case Theme::Default:
		default:                      return MP_TEXT;
	}
}

uint8_t MakerphoneTheme::statusLedIdleOpa(){
	// LV_OPA_70 is the calibrated 'always-on tactical status LED'
	// opacity: bright enough that the dot reads as a deliberate
	// indicator (the eye picks it out as 'lit, armed') rather than
	// a stray pixel, dim enough that it doesn't dominate the tile
	// body. On a real Vertu Constellation Black or BlackBerry Bold
	// 9900 Stealth the status LED rested at roughly 60-75% of full
	// brightness when idle (the LED never fully dimmed during use -
	// that was the whole point of a status indicator) and LV_OPA_70
	// is the closest 1-pixel approximation of that brightness on a
	// 16 bpp panel.
	//
	// Higher than S108's LV_OPA_50 chrome-shine idle and S110's
	// LV_OPA_40 EL-bleed idle because a status LED is fundamentally a
	// 'lit indicator', not a 'reflected highlight' - it reads as
	// brighter than ambient panel light by definition, while the
	// chrome-shine and EL-bleed were always softer reflected/leaked
	// effects. The idle-opacity ranking (40 < 50 < 70) is therefore
	// physically faithful: an EL panel bleed is the dimmest, a
	// reflected glass shine is in the middle, an emitting LED is the
	// brightest.
	return statusLedEnabled() ? LV_OPA_70 : LV_OPA_TRANSP;
}

uint8_t MakerphoneTheme::statusLedSelectedOpa(){
	// LV_OPA_COVER (full intensity) on selection - the focused
	// Stealth Black tile snaps to a 'fully-bright tactical LED' that
	// the eye reads as 'this row is the active selection'. This is a
	// non-pulsing, on/off overlay because the existing halo already
	// pulses on selection; layering a second pulsing element on top
	// would make the focused tile read as jittery rather than 'armed',
	// which contradicts the cool composure of the Stealth Black
	// aesthetic (a real tactical handset's status LED transitioned
	// cleanly between 'armed' and 'active' brightness, never flashing
	// or fading - the LED was a status indicator, not a notification
	// indicator, and panel-level emit/dim was handled by per-row
	// driver logic).
	//
	// The 70%->100% gap is intentionally smaller than S108's 50%->100%
	// (Aqua) and S110's 40%->100% (RAZR) because the Stealth Black
	// LED is already armed at idle - the focus state is a small bump
	// in brightness, not a transition from 'dim' to 'lit'. A wider gap
	// would read as 'LED started flashing' rather than 'LED brightened
	// to confirm focus', which is the wrong cue for a status
	// indicator.
	return statusLedEnabled() ? LV_OPA_COVER : LV_OPA_TRANSP;
}


// ---------------------------------------------------------------------
// S114 - Y2K Silver translucent-Lucite jewel helpers.
//
// The turn-of-the-millennium Y2K consumer-electronics aesthetic (iMac
// G3 Snow / Bondi / Tangerine, iPod 1G click-wheel, Sony Discman
// MZ-E700, Sony VAIO PCG-505, Nokia 8210 'Frost Silver', Sharp J-SH04 -
// the brushed-aluminium-and-translucent-blue gadgets that defined the
// late-1990s to early-2000s era of consumer electronics) shipped a
// single defining accent: a small translucent-Lucite jewel - usually a
// Bondi-blue polycarb insert - tucked into one corner of the gadget's
// polished pearl-silver shell. The iMac G3's translucent handle, the
// iPod 1G's scroll-wheel ring, the Sony Discman's circular Lucite
// power LED, the VAIO PCG-505's translucent-blue badge: every Y2K-era
// device paired its brushed-aluminium body with one carefully-placed
// Lucite accent, and that pairing was the era's single defining
// visual cue. Without that translucent jewel, a pearl-silver tile
// with charcoal-blue icon strokes just reads as 'a flat icon on a
// silver panel' rather than 'a Y2K-era polished-aluminium gadget
// with a Bondi-blue Lucite accent', missing the entire point of the
// era's industrial-design vocabulary.
//
// PhoneIconTile consumes these helpers at idle (gated on
// luciteJewelEnabled()) so every tile under Y2KSilver rests with a
// faint Y2K_BONDI 3 x 3 jewel in its bottom-left corner - the always-
// translucent 'frosted Bondi insert' cue every Y2K-era gadget showed
// when ambient light caught its Lucite accent. Selecting the tile
// then snaps the jewel to LV_OPA_COVER so the focused tile reads as
// 'this row is the active selection, Lucite jewel catching a direct
// light beam' against its softly-frosted neighbours - the same
// focus-feedback cue every iMac G3 / iPod 1G UI used to mark its
// current row, where the translucent accent suddenly saturated to
// its full Bondi-blue intensity rather than its usual frosted-pearl
// idle shade.
//
// Default / Nokia 3310 / Game Boy DMG / Amber CRT / Sony Ericsson
// Aqua / RAZR Hot Pink / Stealth Black return values that produce
// the previous byte-identical behaviour: luciteJewelEnabled() ==
// false, both opacity helpers return LV_OPA_TRANSP (so
// luciteJewelColor() and luciteJewelHighlightColor() values are
// never observed), keeping the existing tile silhouette unchanged
// on every non-Y2K-Silver theme.
//
// Mechanically a corner-anchored 3x3 jewel (with a 1x1 highlight
// pixel), like S112's status LED but anchored to the bottom-LEFT
// rather than the top-right. The four Phase O icon-glyph overlays
// (S108 Aqua top-edge, S110 RAZR bottom-edge, S112 Stealth top-right
// corner, S114 Y2K bottom-left corner) deliberately occupy four
// disjoint geometric axes so a future theme can combine any subset
// of them without overpainting, and so a user flipping between the
// four themes sees the highlight move around the tile perimeter
// rather than re-colouring the same anchor. The bottom-left anchor
// is also physically faithful: the iMac G3's translucent handle,
// the iPod 1G's Apple-logo badge, and the Sony Discman's Lucite
// power LED all sat in the bottom-left of their respective
// products, so anchoring the jewel to the bottom-left of the tile
// captures Y2K-era placement convention directly.
// ---------------------------------------------------------------------

bool MakerphoneTheme::luciteJewelEnabled(){
	return getCurrent() == Theme::Y2KSilver;
}

lv_color_t MakerphoneTheme::luciteJewelColor(){
	switch(getCurrent()){
		case Theme::Y2KSilver: return Y2K_BONDI;
		case Theme::CyberpunkRed: return CYBER_NEON;
		// The fallbacks below are never observed - luciteJewelIdleOpa()
		// and luciteJewelSelectedOpa() both return LV_OPA_TRANSP on
		// every non-Y2K-Silver theme, so the jewel's colour can't reach
		// the framebuffer. The values still resolve to a sensible per-
		// theme 'brightest accent' so a future caller that probes the
		// colour outside the opacity gate (e.g. a debug overlay) reads
		// something coherent rather than an undefined value.
		case Theme::Nokia3310:        return N3310_FRAME;
		case Theme::GameBoyDMG:       return GBDMG_INK;
		case Theme::AmberCRT:         return AMBER_CRT_HOT;
		case Theme::SonyEricssonAqua: return AQUA_GLOW;
		case Theme::RazrHotPink:      return RAZR_GLOW;
		case Theme::StealthBlack:     return STEALTH_LED;
		case Theme::Default:
		default:                      return MP_ACCENT;
	}
}

lv_color_t MakerphoneTheme::luciteJewelHighlightColor(){
	switch(getCurrent()){
		case Theme::Y2KSilver: return Y2K_SHINE;
		case Theme::CyberpunkRed: return CYBER_HOT;
		// The fallbacks below are never observed - the highlight pixel
		// rides the same opacity as the jewel, and that opacity returns
		// LV_OPA_TRANSP on every non-Y2K-Silver theme. The values still
		// resolve to a sensible per-theme 'brightest chrome / body-text
		// white' so a future caller that probes the colour outside the
		// opacity gate reads something coherent.
		case Theme::Nokia3310:        return N3310_HIGHLIGHT;
		case Theme::GameBoyDMG:       return GBDMG_LCD_LIGHT;
		case Theme::AmberCRT:         return AMBER_CRT_HOT;
		case Theme::SonyEricssonAqua: return AQUA_FOAM;
		case Theme::RazrHotPink:      return RAZR_SHINE;
		case Theme::StealthBlack:     return STEALTH_BONE;
		case Theme::Default:
		default:                      return MP_TEXT;
	}
}

uint8_t MakerphoneTheme::luciteJewelIdleOpa(){
	// LV_OPA_30 is the calibrated 'always-translucent Lucite' opacity:
	// bright enough that the jewel reads as a deliberate accent (the
	// eye picks it out as 'frosted Bondi-blue jewel, slightly cloudy')
	// rather than a stray pixel, dim enough that it doesn't compete
	// with the icon strokes themselves. On a real iMac G3 / iPod 1G /
	// Sony Discman the idle Lucite jewel sat at roughly 25-35% of
	// its full saturation when the gadget wasn't catching a direct
	// light beam (the polycarb's natural cloudiness diffused the
	// colour) and LV_OPA_30 is the closest 3x3-jewel approximation
	// of that translucent idle shade on a 16 bpp panel.
	//
	// Lower than S108's LV_OPA_50 chrome-shine idle, S110's LV_OPA_40
	// EL-bleed idle, and S112's LV_OPA_70 status-LED idle because a
	// real Y2K-era Lucite jewel was never 'lit' at idle - it was
	// always frosted-cloudy, with the colour barely tinting the
	// pearl-silver shell underneath. The four idle opacities therefore
	// rank physically: 30 (Lucite cloudy) < 40 (EL bleed) < 50
	// (reflected glass shine) < 70 (emitting status LED). A user
	// flipping between the four themes sees the highlight intensity
	// step up monotonically, reinforcing that they're four genuinely
	// different lighting models rather than four recoloured versions
	// of the same overlay.
	return luciteJewelEnabled() ? LV_OPA_30 : LV_OPA_TRANSP;
}

uint8_t MakerphoneTheme::luciteJewelSelectedOpa(){
	// LV_OPA_COVER (full intensity) on selection - the focused Y2K
	// Silver tile snaps to a 'fully-saturated Bondi-blue Lucite jewel'
	// that the eye reads as 'this row is the active selection, Lucite
	// jewel catching a direct light beam'. This is a non-pulsing,
	// on/off overlay because the existing halo already pulses on
	// selection; layering a second pulsing element on top would make
	// the focused tile read as jittery rather than 'lit', which
	// contradicts the still-photograph polish of the Y2K aesthetic
	// (where the gadget always read as a carefully-lit product shot,
	// never an animated UI element).
	//
	// The 30% -> 100% gap is intentionally the widest of the four
	// Phase O icon-glyph overlays (vs S108's 50%->100%, S110's
	// 40%->100%, S112's 70%->100%) because a real Y2K-era Lucite
	// jewel exhibited the widest dynamic range of any of the four
	// era-defining lighting cues: idle Lucite was almost
	// imperceptibly tinted, but a direct light beam would saturate
	// the jewel to its full Bondi intensity in a single perceptual
	// step. The 70-percentage-point gap captures that 'frosted to
	// fully-saturated' transition without needing an in-between
	// animated state.
	return luciteJewelEnabled() ? LV_OPA_COVER : LV_OPA_TRANSP;
}


// ---------------------------------------------------------------------
// S116 - Cyberpunk Red neon-rim helpers.
//
// The defining visual cue of every cyberpunk-noir UI from Blade Runner
// (1982) onwards is a saturated neon-tube edge that visibly bleeds
// light into the surrounding panel: real neon tubes don't sit on a
// surface like a painted sticker, they push their colour outward into
// the bezel as a soft rim glow that the eye reads as 'lit, alive,
// transmitting'. PhoneIconTile consumes these helpers at idle (gated
// on neonRimEnabled()) so every tile under CyberpunkRed rests with a
// faint CYBER_NEON 1 px strip running down its right edge - the
// always-on neon-tube edge glow cue. Selecting the tile then snaps the
// strip to LV_OPA_COVER so the focused tile reads as 'this row is the
// active selection, neon tube driven at full output' against its
// softly-rimmed neighbours.
//
// Default / Nokia 3310 / Game Boy DMG / Amber CRT / Sony Ericsson Aqua
// / RAZR Hot Pink / Stealth Black / Y2K Silver return values that
// produce the previous byte-identical behaviour: neonRimEnabled() ==
// false, both opacity helpers return LV_OPA_TRANSP (so neonRimColor()
// and neonRimHighlightColor() values are never observed), keeping the
// existing tile silhouette unchanged on every non-Cyberpunk theme.
//
// Mechanically a right-edge vertical 1-px strip, distinct from S108's
// top-edge horizontal strip, S110's bottom-edge horizontal strip,
// S112's top-right 2x2 corner dot, and S114's bottom-left 3x3 corner
// jewel. The five icon-glyph overlay axes (top edge / bottom edge /
// top-right corner / bottom-left corner / right edge vertical) stay
// disjoint so a future theme can combine any subset of them without
// overpainting, and a user flipping between the five themes sees the
// highlight move around the tile perimeter rather than re-colouring
// the same anchor. The right-edge anchor is also physically faithful:
// vertical neon-kanji-style signage is the cyberpunk genre's most
// iconic signage geometry, so anchoring the rim to the right edge of
// the tile captures that placement convention directly.
// ---------------------------------------------------------------------

bool MakerphoneTheme::neonRimEnabled(){
	return getCurrent() == Theme::CyberpunkRed;
}

lv_color_t MakerphoneTheme::neonRimColor(){
	switch(getCurrent()){
		case Theme::CyberpunkRed: return CYBER_NEON;
		// The fallbacks below are never observed - neonRimIdleOpa() and
		// neonRimSelectedOpa() both return LV_OPA_TRANSP on every non-
		// Cyberpunk-Red theme, so the strip's colour can't reach the
		// framebuffer. The values still resolve to a sensible per-theme
		// 'brightest accent' so a future caller that probes the colour
		// outside the opacity gate (e.g. a debug overlay) reads
		// something coherent rather than an undefined value.
		case Theme::Nokia3310:        return N3310_FRAME;
		case Theme::GameBoyDMG:       return GBDMG_INK;
		case Theme::AmberCRT:         return AMBER_CRT_HOT;
		case Theme::SonyEricssonAqua: return AQUA_GLOW;
		case Theme::RazrHotPink:      return RAZR_GLOW;
		case Theme::StealthBlack:     return STEALTH_LED;
		case Theme::Y2KSilver:        return Y2K_BONDI;
		case Theme::Default:
		default:                      return MP_ACCENT;
	}
}

lv_color_t MakerphoneTheme::neonRimHighlightColor(){
	switch(getCurrent()){
		case Theme::CyberpunkRed: return CYBER_TEAL;
		// The fallbacks below are never observed - the highlight pixel
		// rides the same opacity as the rim, and that opacity returns
		// LV_OPA_TRANSP on every non-Cyberpunk-Red theme. The values
		// still resolve to a sensible per-theme 'second-brightest
		// chrome / body-text' so a future caller that probes the colour
		// outside the opacity gate reads something coherent.
		case Theme::Nokia3310:        return N3310_HIGHLIGHT;
		case Theme::GameBoyDMG:       return GBDMG_LCD_LIGHT;
		case Theme::AmberCRT:         return AMBER_CRT_HOT;
		case Theme::SonyEricssonAqua: return AQUA_FOAM;
		case Theme::RazrHotPink:      return RAZR_SHINE;
		case Theme::StealthBlack:     return STEALTH_BONE;
		case Theme::Y2KSilver:        return Y2K_SHINE;
		case Theme::Default:
		default:                      return MP_TEXT;
	}
}

uint8_t MakerphoneTheme::neonRimIdleOpa(){
	// LV_OPA_60 is the calibrated 'always-on neon-tube edge bleed'
	// opacity: bright enough that the rim reads as a deliberate
	// emitting tube (the eye picks it out as 'lit, alive,
	// transmitting') rather than a stray pixel, dim enough that it
	// doesn't dominate the tile body. On a real cyberpunk-noir
	// establishing shot the idle neon-tube rim sits at roughly
	// 50-65% of full intensity (the tube is always emitting but
	// its edge bleed falls off into the surrounding panel) and
	// LV_OPA_60 is the closest 1-pixel-strip approximation of that
	// bleed on a 16 bpp panel.
	//
	// Slots between S108's LV_OPA_50 chrome-shine idle and S112's
	// LV_OPA_70 status-LED idle because a neon tube emits more than
	// a passive glass reflection but less than a focused tactical
	// LED dot - the rim is bright but spread along an entire edge,
	// while the LED is concentrated onto a 2 x 2 corner cell. The
	// five idle opacities now rank physically from coolest to
	// hottest emission: 30 (Y2K Lucite cloudy) < 40 (RAZR EL bleed)
	// < 50 (Aqua reflected glass shine) < 60 (Cyberpunk neon-tube
	// edge bleed) < 70 (Stealth Black emitting status LED). A user
	// flipping between the five themes sees the highlight intensity
	// step up monotonically, reinforcing that they're five genuinely
	// different lighting models rather than five recoloured versions
	// of the same overlay.
	return neonRimEnabled() ? LV_OPA_60 : LV_OPA_TRANSP;
}

uint8_t MakerphoneTheme::neonRimSelectedOpa(){
	// LV_OPA_COVER (full intensity) on selection - the focused
	// Cyberpunk Red tile snaps to a 'fully-driven neon tube' that
	// the eye reads as 'this row is the active selection, neon
	// tube at full output'. This is a non-pulsing, on/off overlay
	// because the existing halo already pulses on selection;
	// layering a second pulsing element on top would make the
	// focused tile read as jittery rather than 'lit at full
	// output', which contradicts the deliberate composition of a
	// cyberpunk-noir establishing shot (where each neon sign reads
	// as a steady source, never a flicker - flicker is reserved
	// for the distant signage in the wallpaper background, not the
	// foreground HUD).
	//
	// The 60% -> 100% gap is intentionally narrower than S114's
	// 30%->100% (Y2K Lucite, 70 pp), S110's 40%->100% (RAZR EL,
	// 60 pp), and S108's 50%->100% (Aqua chrome, 50 pp), and
	// slightly wider than S112's 70%->100% (Stealth LED, 30 pp).
	// The gap narrows monotonically as idle opacity rises - the
	// brighter the idle source, the smaller the perceptual gap to
	// 'fully lit'. Cyberpunk's neon rim already glows visibly at
	// idle, so the focus state is a moderate brightness bump
	// rather than a transition from 'dim' to 'lit', which is the
	// right cue for an emitting source.
	return neonRimEnabled() ? LV_OPA_COVER : LV_OPA_TRANSP;
}
