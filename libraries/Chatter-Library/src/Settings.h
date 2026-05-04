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
	// S147 - operator-logo banner (text + 5x16 user-pixelable bitmap)
	// painted just under the status bar on PhoneHomeScreen so the
	// device wears the classic Sony-Ericsson / Nokia carrier-banner
	// silhouette every time the user wakes to the homescreen.
	//
	// Two slots:
	//
	//   operatorText - up to 15 visible chars (16-byte buffer with
	//   trailing nul) shown left-aligned in pixelbasic7 cream. Empty
	//   string keeps the text half blank so a logo-only banner is
	//   possible. The factory default "MAKERphone" makes the banner
	//   meaningful on first boot.
	//
	//   operatorLogo - five 16-bit rows. Bit 15 of each row is the
	//   leftmost column (pixel x = 0) and bit 0 is the rightmost
	//   (pixel x = 15), so the bitmap reads naturally when written
	//   out as binary literals. Pixels are rendered at 1 device-px
	//   per cell on the homescreen so the on-screen size matches
	//   the literal 16x5 cell budget. The factory default is all
	//   zeros (an empty logo), which the banner widget treats as
	//   "text only" -- no stray dots show up on first boot before
	//   the user has touched the editor.
	//
	// Both slots are edited from PhoneOperatorScreen, reachable via
	// the SYSTEM section of PhoneSettingsScreen ("Operator" row,
	// directly below the S146 "Power-off msg" row). The screen
	// switches between TEXT (T9 entry) and LOGO (16x5 cursor-driven
	// pixel toggling) modes via the BTN_L bumper, so the keypad
	// layout stays familiar even while editing the bitmap. On exit
	// the screen calls Settings.store() so a power-cycle preserves
	// both slots through the existing NVS-resize pattern that grew
	// the struct via soundProfile / wallpaperStyle / themeId /
	// keyTicks / ownerName / powerOffMessage.
	char     operatorText[16] = "MAKERphone";
	uint16_t operatorLogo[5]  = { 0, 0, 0, 0, 0 };
	// S159 - phone profile selector (the classic Sony-Ericsson / Nokia
	// "General / Silent / Meeting / Outdoor / Headset" five-state
	// system), enumerated in order:
	//
	//   0 = General  - ringer on, full volume, default factory profile.
	//   1 = Silent   - no ringer, no key tones, no buzzer at all.
	//   2 = Meeting  - ringer off, vibration-only intent (the buzzer
	//                  stays silent today; a future hardware revision
	//                  with a real vibration motor can light up on
	//                  this profile without touching this enum).
	//   3 = Outdoor  - loudest ringer + vibrate combo (today the
	//                  buzzer shares the same loud tone budget as
	//                  General; the distinction is preserved so S160
	//                  / S161 can attach louder per-profile ringtones
	//                  and a more aggressive vibration pattern here
	//                  without churn).
	//   4 = Headset  - ringer on, vibrate off (audio assumed routed
	//                  through a wired/Bluetooth headset on a future
	//                  hardware revision; today behaves identically
	//                  to General audibly, but is preserved so the
	//                  per-profile ringtone slot it owns can carry a
	//                  quieter melody for headset use).
	//
	// PhoneProfileScreen (S159) reads + writes this field and on save
	// also fans the choice out to the legacy sound boolean and the
	// three-state soundProfile byte so existing readers
	// (BuzzerService, PhoneRingtoneEngine, SettingsScreen,
	// PhoneSoundScreen, PhoneHapticsScreen) keep working with no
	// churn:
	//
	//   General  -> sound=true,  soundProfile=2 (Loud)
	//   Silent   -> sound=false, soundProfile=0 (Mute)
	//   Meeting  -> sound=false, soundProfile=1 (Vibrate)
	//   Outdoor  -> sound=true,  soundProfile=2 (Loud)
	//   Headset  -> sound=true,  soundProfile=2 (Loud)
	//
	// Default 0 (General) makes a freshly-flashed device boot into
	// the most "phone-like" profile -- ringer on, full volume -- which
	// matches how every Sony-Ericsson / Nokia of the era shipped from
	// the factory. Persisted values outside [0..4] are clamped to
	// General at the screen layer to be defensive against NVS-resize
	// wipes that read the new byte as uninitialised garbage. Sits
	// next to soundProfile / themeId / keyTicks so the profile-related
	// settings cluster together in the SettingsData blob.
	uint8_t phoneProfile = 0;
	// S160 - per-profile ringtone selection. One ringtone id per
	// PhoneProfileScreen profile (General / Silent / Meeting / Outdoor /
	// Headset, indexed in the same order the phoneProfile byte
	// enumerates). The id encoding mirrors PhoneContactRingtone:
	//
	//   0..4    - PhoneRingtoneLibrary::Id (Synthwave / Classic / Beep /
	//             Boss / Silent), in the order the library defines them.
	//   100..103 - PhoneComposer save slots 0..3 (the same offset the
	//             per-contact ringtoneId field uses, so the picker can
	//             reuse PhoneContactRingtone::pickerCount() / pickerIdAt()
	//             without forking a parallel encoding).
	//
	// Defaults match the per-profile vibe documented in
	// PhoneProfileScreen.h:
	//
	//   General  -> 1 (Classic)   - factory profile, full ringer.
	//   Silent   -> 4 (Silent)    - looped rest, never asserts the piezo
	//                                even if a future revision unmutes
	//                                mid-ring.
	//   Meeting  -> 4 (Silent)    - vibration-only intent today; the
	//                                ringtone slot stays Silent so a
	//                                misconfigured Meeting profile never
	//                                surprises the user with audio.
	//   Outdoor  -> 3 (Boss)      - loudest stock tone for a noisy
	//                                environment.
	//   Headset  -> 0 (Synthwave) - quieter melody for headset use.
	//
	// PhoneCallService reads the active profile's slot
	// (Settings.profileRingtones[Settings.phoneProfile]) for any peer
	// that does not have a contact override set, so a freshly-flashed
	// device rings with profile-appropriate audio out of the box.
	// Per-contact ringtones (S153) still take precedence: the call
	// service first checks PhoneContacts::exists(peer) and, if a
	// contact entry is present, hands over to its stored ringtoneId.
	// Persisted values outside the documented encoding are clamped to
	// PhoneContactRingtone::DefaultId (Synthwave) at the screen /
	// resolver layer so a stale NVS blob never crashes playback.
	// Sits next to phoneProfile so the profile-related settings stay
	// clustered in the SettingsData blob; the existing NVS-resize
	// pattern (that grew this struct via soundProfile / wallpaperStyle
	// / themeId / keyTicks / ownerName / powerOffMessage / operatorText
	// / operatorLogo / phoneProfile) reads the new five-byte slice as
	// zero-initialised on a first boot after the firmware grows --
	// which would map every profile to the Synthwave default; the
	// resolver layer applies the documented per-profile defaults when
	// the slot reads as 0 to keep first-boot behaviour matching the
	// table above.
	uint8_t profileRingtones[5] = { 1, 4, 4, 3, 0 };
	// S151 - speed-dial slots mapping numeric keypad digits to LoRa
	// peer UIDs. A long-press of BTN_1..BTN_9 on the homescreen
	// (PhoneHomeScreen) looks up the matching slot and fires
	// PhoneCallService::placeCall(uid) when a non-zero UID is stored;
	// when the slot is empty (factory default) the gesture falls
	// through to opening an empty PhoneDialerScreen so the user can
	// dial a number manually instead. Slot 0 is reserved for the
	// existing "hold 0 to quick-dial" gesture (S22) which still
	// opens the dialer empty -- the array is sized 10 so the
	// indexing matches the digit pressed without an off-by-one,
	// even though slot 0 is currently unused. UID_t is uint64_t so
	// the array claims 80 bytes; the SettingsData blob stays plain-
	// old-data and the existing NVS-resize pattern (that grew this
	// struct via soundProfile / wallpaperStyle / themeId / keyTicks
	// / ownerName / powerOffMessage / operatorText / operatorLogo)
	// reads the new field as zero-initialised on a first boot after
	// the firmware grows. Edited from PhoneSpeedDialScreen, reachable
	// from the SYSTEM section of PhoneSettingsScreen ("Speed dial"
	// row, directly below "Operator").
	uint64_t speedDial[10] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
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
