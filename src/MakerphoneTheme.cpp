#include "MakerphoneTheme.h"
#include <Settings.h>

MakerphoneTheme::Theme MakerphoneTheme::themeFromByte(uint8_t raw){
	// Defensive clamp - any persisted byte outside the known enum range
	// falls back to Default so a corrupted NVS page can never render
	// against a non-existent theme. Same pattern PhoneSynthwaveBg uses
	// for wallpaperStyle.
	switch(raw){
		case static_cast<uint8_t>(Theme::Nokia3310): return Theme::Nokia3310;
		case static_cast<uint8_t>(Theme::Default):
		default:                                     return Theme::Default;
	}
}

MakerphoneTheme::Theme MakerphoneTheme::getCurrent(){
	return themeFromByte(Settings.get().themeId);
}

const char* MakerphoneTheme::getName(Theme t){
	switch(t){
		case Theme::Default:   return "SYNTHWAVE";
		case Theme::Nokia3310: return "NOKIA 3310";
		default:               return "DEFAULT";
	}
}
