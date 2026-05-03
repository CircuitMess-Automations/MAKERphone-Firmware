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
	// S101 — global theme selector (0 = Default / Synthwave, 1 = Nokia 3310
	// Monochrome). Phase O (S101–S120) bundles each theme as a palette +
	// wallpaper pair (S101, S103, ...) plus a matching icon-glyph + accent
	// pass (S102, S104, ...). The wallpaper layer (PhoneSynthwaveBg)
	// consults this byte before falling back to wallpaperStyle so a user
	// who picks a non-default theme automatically sees the matching
	// wallpaper variant on every subsequent screen push. Out-of-range
	// values clamp to Default at the theme layer; same defensive pattern
	// the existing wallpaperStyle / soundProfile readers use.
	uint8_t themeId = 0;
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
	// S144 - owner name shown on the lock screen. Set via the
	// PhoneOwnerNameScreen reached from the SYSTEM section of
	// PhoneSettingsScreen ("Owner name" row); when non-empty the
	// LockScreen tucks the string between the status bar and the
	// clock face as a small retro greeting and shifts the clock
	// face / preview / unread container down to make room. An
	// empty string (the factory default) keeps the classic
	// clock-anchored-to-the-status-bar layout.
	//
	// Stored as a fixed-width 24-byte buffer (23 visible chars plus
	// the trailing nul) so the SettingsData blob stays plain-old-data
	// and the existing NVS-resize pattern that grew this struct via
	// soundProfile / wallpaperStyle / themeId / keyTicks reads the
	// new field as zero-initialised on a first boot after upgrade.
	char ownerName[24] = "";
	// S146 - custom power-off message painted over the warm-cream
	// phosphor plate while the PhonePowerDown CRT-shrink animation
	// is in its preamble phase. Set via the PhonePowerOffMessageScreen
	// reached from the SYSTEM section of PhoneSettingsScreen
	// ("Power-off msg" row, just above About). When non-empty the
	// PhonePowerDown overlay appends a ~700 ms preamble that holds
	// the plate at full brightness while the message is centred on
	// it in deep-purple pixelbasic16 -- the Sony-Ericsson "Bye!"
	// flourish that the S57 stub baseline never had. An empty
	// string (the factory default) skips the preamble entirely so
	// the existing CRT shrink fires immediately, exactly the way
	// every existing test driver and host expects.
	//
	// Stored as a fixed-width 24-byte buffer (23 visible chars plus
	// the trailing nul) -- same shape as the S144 ownerName slot, so
	// the SettingsData blob stays plain-old-data and the existing
	// NVS-resize pattern reads the new field as zero-initialised on
	// a first boot after the firmware grows.
	char powerOffMessage[24] = "";
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
