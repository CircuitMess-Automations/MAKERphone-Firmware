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
		case Theme::Default:
		default:                return MP_BG_DARK;
	}
}

lv_color_t MakerphoneTheme::accent(){
	switch(getCurrent()){
		case Theme::Nokia3310:  return N3310_FRAME;
		case Theme::GameBoyDMG: return GBDMG_INK;
		case Theme::Default:
		default:                return MP_ACCENT;
	}
}

lv_color_t MakerphoneTheme::highlight(){
	switch(getCurrent()){
		case Theme::Nokia3310:  return N3310_PIXEL;
		case Theme::GameBoyDMG: return GBDMG_INK_MID;
		case Theme::Default:
		default:                return MP_HIGHLIGHT;
	}
}

lv_color_t MakerphoneTheme::dim(){
	switch(getCurrent()){
		case Theme::Nokia3310:  return N3310_PIXEL_DIM;
		case Theme::GameBoyDMG: return GBDMG_LCD_MID;
		case Theme::Default:
		default:                return MP_DIM;
	}
}

lv_color_t MakerphoneTheme::text(){
	switch(getCurrent()){
		case Theme::Nokia3310:  return N3310_PIXEL;
		case Theme::GameBoyDMG: return GBDMG_INK;
		case Theme::Default:
		default:                return MP_TEXT;
	}
}

lv_color_t MakerphoneTheme::labelDim(){
	switch(getCurrent()){
		case Theme::Nokia3310:  return N3310_PIXEL_DIM;
		case Theme::GameBoyDMG: return GBDMG_INK_MID;
		case Theme::Default:
		default:                return MP_LABEL_DIM;
	}
}
