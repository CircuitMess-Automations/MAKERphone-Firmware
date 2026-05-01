#ifndef CHATTER_LIBRARY_SETTINGS_H
#define CHATTER_LIBRARY_SETTINGS_H

#include <Arduino.h>

#define SLEEP_STEPS 5
#define SHUTDOWN_STEPS 5

extern const uint32_t SleepSeconds[SLEEP_STEPS];
extern const char* SleepText[SLEEP_STEPS];

extern const uint32_t ShutdownSeconds[SHUTDOWN_STEPS];
extern const char* ShutdownText[SHUTDOWN_STEPS];

// MAKERphone S52 — three-state sound profile selector. The legacy
// `bool sound` field stays in the struct (BuzzerService, PhoneRingtoneEngine,
// SettingsScreen still read it), so the new `soundProfile` adds finer-grained
// intent on top:
//
//   0 = Mute    — sound off, no buzzer at all (sound == false)
//   1 = Vibrate — sound off for the buzzer, kept distinct so a future
//                 hardware revision with a real vibration motor can act on
//                 it; today vibrate behaves identically to mute audibly.
//                 (sound == false)
//   2 = Loud    — sound on, full ringer + key clicks (sound == true)
//
// PhoneSoundScreen (S52) keeps `sound` and `soundProfile` consistent on save,
// so all existing readers of `sound` continue to work unchanged. Persisted
// profile values outside [0..2] are clamped to Loud on read at the screen
// layer to be defensive against NVS-resize wipes that read the new byte as
// uninitialised garbage.
struct SettingsData {
	// MAKERphone prototype defaults: sound off, no auto-sleep, no auto-shutdown.
	// sleepTime / shutdownTime are indices into SleepSeconds[] / ShutdownSeconds[];
	// index 0 corresponds to "OFF" (0 seconds, which SleepService treats as
	// "never auto-enter sleep"). Production Chatter previously defaulted to 30s
	// sleep / 30 min shutdown; for MAKERphone we want a phone that stays on.
	bool sound = false;
	uint8_t sleepTime = 0;
	uint8_t shutdownTime = 0;
	uint8_t screenBrightness = 200;
	bool tested = false;
	uint32_t messagesSent = 0;
	uint32_t messagesReceived = 0;
	// S52 — three-state sound profile (0=Mute, 1=Vibrate, 2=Loud).
	// Default 0 (Mute) aligns with the existing `sound = false` factory default.
	uint8_t soundProfile = 0;
	// S53 — wallpaper style selector (0=Synthwave, 1=Plain, 2=GridOnly, 3=Stars).
	// Read by PhoneSynthwaveBg's constructor on every screen build, so changing
	// the value via PhoneWallpaperScreen takes effect on the next screen push
	// (e.g. popping back to PhoneHomeScreen / PhoneMainMenu). Persisted values
	// outside [0..3] are clamped to Synthwave at the wallpaper layer to be
	// defensive against NVS-resize wipes that read the new byte as garbage.
	uint8_t wallpaperStyle = 0;
	// S68 — haptic-style nav-key ticks toggle. When `true` the BuzzerService
	// emits a very short (~5 ms), very high-pitched click on D-pad / numpad
	// navigation buttons (LEFT, RIGHT, 2, 4, 6, 8, ENTER, BACK) -- a "you
	// pressed a key" tactile-style cue, distinct from the full per-button
	// musical tones that the legacy `sound` flag (Loud profile) drives.
	// The tick fires only when the legacy `sound` flag is `false`, so the
	// two layers never double up: in Loud profile the existing 25 ms key
	// tones cover navigation feedback, and in Mute / Vibrate the optional
	// keyTicks layer can still give the user a subtle confirmation that a
	// keypress was registered. Default `false` matches the conservative
	// MAKERphone factory profile (sound off, no auto-sleep, no auto-shutdown).
	// Toggle is exposed via PhoneHapticsScreen, reachable from the SOUND
	// section of PhoneSettingsScreen as the "Key clicks" row.
	bool keyTicks = false;
};

class SettingsImpl {
public:
	bool begin();

	void store();

	SettingsData& get();

	void reset();

private:
	SettingsData data;
};

extern SettingsImpl Settings;

#endif //CHATTER_LIBRARY_SETTINGS_H
