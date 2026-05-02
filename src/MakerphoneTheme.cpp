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
