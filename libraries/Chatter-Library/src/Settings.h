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
	// S166 - Konami-code Easter egg unlock flag for the Rainbow theme.
	// The PhoneKonamiCode global service (src/Services/PhoneKonamiCode)
	// listens to every button press and watches for the canonical
	// 10-press sequence: LEFT LEFT RIGHT RIGHT LEFT RIGHT LEFT RIGHT
	// BACK ENTER (the literal Konami code mapped onto the Chatter
	// hardware, where BTN_UP/BTN_DOWN are aliased to BTN_LEFT/BTN_RIGHT
	// per Pins.hpp, BTN_B == BTN_BACK, and BTN_A == BTN_ENTER, so the
	// physical sequence is identical to the original NES UU DD LR LR B A
	// pattern). On match the service flips this flag to true, sets
	// themeId to MakerphoneTheme::Theme::Rainbow (11), persists via
	// Settings.store(), and plays a brief one-shot ascending chime.
	// Once unlocked the flag stays true across reboots so the user
	// can swap back to Rainbow from PhoneThemeScreen without having
	// to re-enter the code each session.
	//
	// Stored next to speedDial[] so the SettingsData blob stays plain
	// old data and the existing NVS-resize pattern (that grew this
	// struct via soundProfile / wallpaperStyle / themeId / keyTicks /
	// ownerName / powerOffMessage / operatorText / operatorLogo /
	// phoneProfile / profileRingtones / speedDial) reads the new byte
	// as zero-initialised on a first boot after the firmware grows --
	// which maps to false here, the correct factory default.
	bool rainbowUnlocked = false;
	// S183 - soft-key click-tone customisation. Selects which of a small
	// hand-tuned tone library the BuzzerService plays when the user
	// presses BTN_LEFT or BTN_RIGHT (the two Sony-Ericsson-style soft-key
	// hardware buttons that PhoneSoftKeyBar drives). Encoding matches the
	// PhoneSoftKeyToneLib::Id enum (src/Services/PhoneSoftKeyTone.h):
	//
	//   0 = Classic - NOTE_B4 / 25 ms (the legacy default; byte-identical
	//                 to the soft-key feedback every prior firmware shipped
	//                 so a freshly-flashed device sounds exactly the same
	//                 out of the box).
	//   1 = Click   - NOTE_C6 / 8 ms (snappy high-pitched click).
	//   2 = Bloop   - NOTE_A3 / 30 ms (low buzzy blip).
	//   3 = Chirp   - NOTE_E5 / 18 ms (mid-range chirp).
	//   4 = Silent  - tone suppressed even in Loud profile (lets users
	//                 keep ringer + key tones while opting the soft-keys
	//                 out of the audible feedback layer).
	//
	// The tick layer driven by keyTicks (S68) is unaffected by this byte:
	// in Mute / Vibrate the optional 4 ms / NOTE_F6 navigation click
	// still fires regardless of softKeyTone, so the haptic-feel experience
	// stays consistent across both axes. The screen layer
	// (PhoneSoftKeyToneScreen) clamps out-of-range values to Classic on
	// read so a NVS-resize wipe that lands the new byte at 0xFF degrades
	// gracefully to the documented factory default.
	uint8_t softKeyTone = 0;
	// S184 - lock-screen widget composition picker. Lets the user
	// pick which secondary line(s) the LockScreen renders below the
	// big clock face, so the same hardware can present three distinct
	// "lock-screen vibes" without rebuilding any widget tree:
	//
	//   0 = ClockDate  - the factory default. Clock face renders
	//                    the weekday + day-of-month + month+year
	//                    rows exactly the way every prior firmware
	//                    shipped, so a freshly-flashed device looks
	//                    byte-identical out of the box.
	//   1 = ClockOnly  - hides the weekday + month rows entirely so
	//                    the lock screen reads as a minimalist
	//                    HH:MM-only watch face.
	//   2 = ClockEvent - hides the weekday + month rows and instead
	//                    paints the next armed PhoneAlarmService
	//                    alarm ("NEXT ALARM 07:00") in their place,
	//                    so the lock screen previews the user's
	//                    closest pending event the way a Sony-
	//                    Ericsson agenda widget would. When no
	//                    alarm is armed the line falls back to
	//                    "NO ALARMS SET" in the dim caption colour
	//                    rather than going blank, so the layout
	//                    stays balanced.
	//
	// Edited from PhoneLockWidgetScreen, reachable from the DISPLAY
	// section of PhoneSettingsScreen ("Lock widget" row). Persisted
	// values outside [0..2] clamp to ClockDate at the LockScreen
	// layer, the same defensive pattern soundProfile / wallpaperStyle
	// / themeId / keyTicks already use against NVS-resize wipes that
	// read the new byte as uninitialised garbage. Sits next to
	// softKeyTone so the personalisation-related settings stay
	// clustered in the SettingsData blob; the existing NVS-resize
	// pattern reads the new field as zero-initialised on a first
	// boot after the firmware grows, which maps to ClockDate -- the
	// correct factory default.
	uint8_t lockWidgetMode = 0;
	// S185 - home-screen layout switcher. Lets the user pick which top-
	// level composition the PhoneHomeScreen renders on the synthwave
	// wallpaper, so the same hardware can present three distinct
	// homescreen "vibes" without rebuilding any widget tree:
	//
	//   0 = Classic  - the factory default. Status bar + operator
	//                  banner (when populated) + big clock face +
	//                  rotating tip / idle hints + soft-key bar
	//                  exactly the way every prior firmware shipped,
	//                  so a freshly-flashed device looks byte-
	//                  identical out of the box.
	//   1 = Minimal  - operator banner is hidden and the rotating
	//                  tip banner / "PRESS ANY KEY" idle hint are
	//                  gated off, leaving just the status bar, the
	//                  clock face and the soft-key bar floating on
	//                  the synthwave wallpaper. The result reads as
	//                  a quiet "watch face" mode for users who do
	//                  not want any text on their homescreen apart
	//                  from the time.
	//   2 = Stack    - operator banner is hidden, the rotating tip
	//                  banner / idle hint are gated off, and a
	//                  static "HOLD 0:DIAL  HOLD #:LOCK" shortcut
	//                  hint is painted in the empty wallpaper band
	//                  between the clock face and the soft-key bar.
	//                  Surfaces the long-press gestures muscle-memory
	//                  expects from a Sony-Ericsson feature phone so
	//                  a new user can discover them without reaching
	//                  for the manual.
	//
	// Edited from PhoneHomeLayoutScreen, reachable from the DISPLAY
	// section of PhoneSettingsScreen ("Home layout" row, directly
	// below "Lock widget"). Persisted values outside [0..2] clamp to
	// Classic at the home-screen layer, the same defensive pattern
	// soundProfile / wallpaperStyle / themeId / keyTicks / lockWidgetMode
	// already use against NVS-resize wipes that read the new byte as
	// uninitialised garbage. Sits next to lockWidgetMode so the two
	// "what does my phone look like when idle" knobs cluster together
	// in the SettingsData blob; the existing NVS-resize pattern reads
	// the new field as zero-initialised on a first boot after the
	// firmware grows, which maps to Classic -- the correct factory
	// default.
	uint8_t homeLayoutMode = 0;
	// S186 - "Wallpaper of the day" auto-rotation toggle. When non-zero
	// the PhoneSynthwaveBg::resolveStyleFromSettings() resolver bypasses
	// the user's persisted wallpaperStyle byte and instead returns one
	// of the four core Synthwave variants (Synthwave / Plain / GridOnly
	// / Stars) picked from the day-of-epoch counter, so the homescreen,
	// lock screen, settings family and every other screen that drops a
	// `new PhoneSynthwaveBg(obj)` automatically rotates through the
	// curated list once per day. The wallpaperStyle byte stays
	// persisted underneath so flipping the auto-rotate toggle back off
	// (PhoneWallpaperScreen's "DAILY ROTATE" pager entry) instantly
	// restores the user's previously-chosen Synthwave variant. Theme
	// overrides (Nokia 3310 / DMG / Amber CRT / Aqua / RAZR / Stealth
	// Black / Y2K Silver / Cyberpunk Red / Christmas / Surprise) still
	// take precedence: when a non-default theme is selected the daily
	// rotation is suppressed and the theme's matching wallpaper is
	// drawn instead, the same way wallpaperStyle is suppressed today.
	//
	// Encoding:
	//   0 = OFF (factory default; resolver falls back to wallpaperStyle).
	//   1 = ON  (resolver returns wallpaperOfDayStyle()).
	//
	// Persisted values outside [0..1] clamp to OFF at the wallpaper
	// layer to be defensive against NVS-resize wipes that read the
	// new byte as uninitialised garbage. Sits at the end of the blob
	// so the existing NVS-resize pattern (that grew this struct via
	// soundProfile / wallpaperStyle / themeId / keyTicks / ownerName /
	// powerOffMessage / operatorText / operatorLogo / phoneProfile /
	// profileRingtones / speedDial / rainbowUnlocked / softKeyTone /
	// lockWidgetMode / homeLayoutMode) reads the new field as zero-
	// initialised on a first boot after the firmware grows -- which
	// maps to OFF, the correct factory default.
	uint8_t wallpaperOfDay = 0;
	// S187 - custom RGB accent override. When customAccentEnabled is
	// non-zero the MakerphoneTheme::accent() resolver bypasses the
	// per-theme accent map and instead returns lv_color_make(R, G, B)
	// from the three byte slots below, so the user can dial in any
	// 24-bit accent and have every Phone* widget that already calls
	// MakerphoneTheme::accent() (PhoneIconTile halo, PhoneSoftKeyBar
	// label tint, PhoneChatBubble Sent fill, PhoneTipBanner rule,
	// PhoneIdleHint underline, PhoneLockHint shimmer ...) pick the
	// new colour up on the next screen build. Edited from the new
	// PhoneAccentScreen, reachable from the DISPLAY section of
	// PhoneSettingsScreen ("Accent" row, directly below "Theme") so
	// the personalisation-related DISPLAY rows cluster together.
	//
	// Encoding:
	//   customAccentEnabled = 0 (factory default; resolver falls
	//                            back to the per-theme accent map
	//                            exactly the way every prior firmware
	//                            shipped, so first-boot is byte-
	//                            identical).
	//   customAccentEnabled = 1 (resolver returns lv_color_make(
	//                            customAccentR, customAccentG,
	//                            customAccentB) regardless of the
	//                            currently selected theme).
	//
	// Defaults for the three RGB bytes are seeded with MP_ACCENT
	// (255, 140, 30 -- the canonical synthwave sunset orange) so a
	// freshly-flashed device that toggles the override on without
	// touching the sliders sees no visible difference, the same
	// "initial state == saved state" pattern PhoneBrightnessScreen
	// uses to keep first-touch UX predictable. Persisted values
	// outside [0..1] for customAccentEnabled clamp to 0 at the
	// resolver layer to be defensive against NVS-resize wipes that
	// read the new byte as uninitialised garbage; the three RGB
	// bytes are uint8_t so any 0..255 value is legal by
	// construction.
	//
	// Sits at the end of the blob next to wallpaperOfDay so the
	// existing NVS-resize pattern (that grew this struct via
	// soundProfile / wallpaperStyle / themeId / keyTicks / ownerName
	// / powerOffMessage / operatorText / operatorLogo / phoneProfile
	// / profileRingtones / speedDial / rainbowUnlocked / softKeyTone
	// / lockWidgetMode / homeLayoutMode / wallpaperOfDay) reads the
	// new four-byte slice as zero-initialised on a first boot after
	// the firmware grows -- which maps to override-OFF (the correct
	// factory default) with the RGB bytes meaningless until enabled.
	uint8_t customAccentEnabled = 0;
	uint8_t customAccentR = 255;
	uint8_t customAccentG = 140;
	uint8_t customAccentB = 30;
	// S188 - owner emoji / avatar selection. A small curated index
	// into the PhoneOwnerEmoji catalogue (None / Heart / Star / Smile /
	// Music / Crown / Skull / Bolt / Cat / Coffee / Pizza / Dice /
	// Rocket). When non-zero the LockScreen pins a small 9x9 pixel-
	// art glyph just under the status bar (sharing the strip with any
	// Settings.ownerName text) so the device wears a personal "this
	// is who I am" icon every time the user wakes it -- the kind of
	// Sony-Ericsson personalisation knob a feature-phone user expects
	// to find right next to the owner-name greeting. Edited from the
	// new PhoneOwnerEmojiScreen, reachable from the SYSTEM section of
	// PhoneSettingsScreen ("Owner emoji" row, directly below "Owner
	// name") so the two phone-identity rows cluster together inside
	// the existing SYSTEM group. Persisted values outside the
	// catalogue range clamp to 0 (None) at the resolver layer to be
	// defensive against NVS-resize wipes that read the new byte as
	// uninitialised garbage. Sits at the end of the blob so the
	// existing NVS-resize pattern (that grew this struct via
	// soundProfile / wallpaperStyle / themeId / keyTicks / ownerName
	// / powerOffMessage / operatorText / operatorLogo / phoneProfile
	// / profileRingtones / speedDial / rainbowUnlocked / softKeyTone
	// / lockWidgetMode / homeLayoutMode / wallpaperOfDay /
	// customAccentEnabled / customAccentR / G / B) reads the new
	// byte as zero-initialised on a first boot after the firmware
	// grows -- which maps to None, the correct factory default.
	uint8_t ownerEmoji = 0;
	// S190 - music player playback mode. The PhoneMusicPlayer uses this
	// byte to decide what happens at end-of-track (and on user-driven
	// next/prev) so the four classic feature-phone modes are reachable
	// without a sub-menu:
	//
	//   0 = Continuous - the legacy "albums playing through" behaviour.
	//                    Auto-advance to the next track on end-of-track,
	//                    stop after the last track in the playlist.
	//                    Factory default; a freshly-flashed device that
	//                    has never touched the new mode key behaves
	//                    byte-identically to every prior firmware.
	//   1 = RepeatAll  - same advance logic as Continuous, but on
	//                    end-of-playlist the player wraps to track 0
	//                    and keeps going. The traditional "loop the
	//                    whole playlist" Sony-Ericsson default.
	//   2 = RepeatOne  - end-of-track replays the same track. Manual
	//                    next/prev still advances by one (so the user
	//                    can step out of the loop without changing
	//                    modes); only the auto-advance hook treats
	//                    RepeatOne as "stay here".
	//   3 = Shuffle    - end-of-track and the manual next button pick
	//                    a random track index different from the
	//                    current one (when the playlist has >= 2
	//                    tracks). prev still walks the natural order
	//                    so the user can step back to the previously-
	//                    played track without rerolling.
	//
	// Persisted via PhoneMusicPlayer's setPlayMode() so toggling the
	// mode on the screen survives a reboot. Persisted values outside
	// [0..3] clamp to Continuous at the screen layer to be defensive
	// against NVS-resize wipes that read the new byte as uninitialised
	// garbage. Sits at the end of the blob next to ownerEmoji so the
	// existing NVS-resize pattern reads the new byte as zero-initialised
	// on a first boot after the firmware grows -- which maps to
	// Continuous, the correct factory default.
	uint8_t musicPlayMode = 0;
	// S193 - PhoneAlarmService ringtone selection. The byte indexes into
	// the PhoneAlarmTone catalogue, which extends PhoneContactRingtone
	// with one extra "Factory" slot at id 0:
	//
	//   0          - Factory alarm (the legacy four-note arpeggio that
	//                shipped with S124; defined in PhoneAlarmTone.cpp).
	//   1..5       - PhoneRingtoneLibrary tones, shifted by one so
	//                Synthwave is id 1, Classic id 2, ... Silent id 5.
	//                The shift keeps id 0 reserved for Factory so a
	//                first boot after an NVS resize reads as Factory
	//                (the byte-identical pre-S193 behaviour) instead
	//                of mapping zero straight to Synthwave.
	//   100..103   - PhoneComposer save slots 0..3 (encoding shared
	//                with PhoneContactRingtone so a composition saved
	//                from PhoneComposer is reachable both as a contact
	//                ringer and as the alarm tone without a duplicate
	//                NVS slot).
	//   anything else - falls back to Factory at the resolver layer so
	//                a stale NVS blob never crashes triggerFire().
	//
	// Edited from PhoneAlarmTonePicker, reachable from the SOUND
	// section of PhoneSettingsScreen ("Alarm tone" row, directly
	// below "Softkey tone" so the alarm-related personalisation row
	// clusters with the rest of the SOUND group). Persisted values
	// outside the documented encoding clamp to Factory at the
	// resolver layer so a freshly-flashed device rings with the
	// legacy four-note arpeggio out of the box. Sits at the end of
	// the blob next to musicPlayMode so the existing NVS-resize
	// pattern reads the new byte as zero-initialised on a first
	// boot after the firmware grows -- which maps to Factory, the
	// correct factory default.
	uint8_t alarmTone = 0;
	// S203 - persisted slide pointer for PhoneDemoModeScreen (S200).
	// Today the demo deck always opens at slide 0 because the screen
	// is short-lived and the marketing camera takes a continuous shot
	// of the full nine-slide loop. A release engineer who wants to
	// start the take mid-deck (e.g. to re-shoot just the "audio
	// studio" slide without re-rolling through the first three) had
	// to either count the auto-advances on camera or recompile the
	// firmware. demoSlideStart fixes that: the screen reads this byte
	// in its constructor and seeds slideIdx from it (clamped to
	// PhoneDemoModeScreen::kSlideCount-1) so the first slide painted
	// is whichever one the previous run exited on. On every press of
	// any key the screen writes the *currently visible* slide back
	// here and calls Settings.store() so a power-cycle preserves the
	// pointer through the existing NVS-resize pattern. Auto-advances
	// inside an open run are NOT persisted -- the byte only updates
	// when the user actively dismisses the screen, so the nightly
	// auto-cycle never burns NVS write budget. Persisted values
	// outside [0..kSlideCount-1] clamp to 0 at the screen layer to
	// be defensive against NVS-resize wipes that read the new byte
	// as uninitialised garbage. Sits at the end of the blob next to
	// alarmTone so the existing NVS-resize pattern (that grew this
	// struct via soundProfile / wallpaperStyle / themeId / keyTicks
	// / ownerName / powerOffMessage / operatorText / operatorLogo /
	// phoneProfile / profileRingtones / speedDial / rainbowUnlocked
	// / softKeyTone / lockWidgetMode / homeLayoutMode /
	// wallpaperOfDay / customAccentEnabled / customAccentR / G / B /
	// ownerEmoji / musicPlayMode / alarmTone) reads the new byte as
	// zero-initialised on a first boot after the firmware grows --
	// which maps to slide 0, the byte-identical pre-S203 default.
	uint8_t demoSlideStart = 0;
	// S206 - user-tunable slide pace for PhoneDemoModeScreen (S200).
	// The v2.0 demo deck shipped with a hard-coded 3 s slide period
	// (kSlidePeriodMs in PhoneDemoModeScreen.cpp), which is fine for
	// the marketing-video unattended shoot but feels rushed when a
	// release engineer wants to read each slide in detail at a desk
	// or, conversely, drag-y when someone is just sanity-checking the
	// loop in passing. demoSpeed exposes that period as a three-state
	// preset the user can flip from PhoneDemoSpeedScreen, reachable
	// from the ADVANCED section of PhoneSettingsScreen ("Demo speed"
	// row, directly above the existing "Demo mode" entry):
	//
	//   0 = Medium - 3000 ms / slide. Factory default; a freshly-
	//                flashed device that has never touched the new
	//                row behaves byte-identically to every prior
	//                firmware on the demo deck.
	//   1 = Slow   - 5000 ms / slide. Comfortable read-each-slide
	//                pace for a desk demo.
	//   2 = Fast   - 1500 ms / slide. Rapid sanity-check pace for
	//                an at-a-glance loop.
	//
	// PhoneDemoModeScreen reads this byte through a new
	// resolveSlidePeriodMs() static helper in its constructor and
	// onStart() so the chosen period takes effect on the next push
	// of the screen. Persisted values outside [0..2] clamp to Medium
	// at the screen layer to be defensive against NVS-resize wipes
	// that read the new byte as uninitialised garbage. Sits at the
	// end of the blob next to demoSlideStart so the existing NVS-
	// resize pattern (that grew this struct via soundProfile /
	// wallpaperStyle / themeId / keyTicks / ownerName /
	// powerOffMessage / operatorText / operatorLogo / phoneProfile /
	// profileRingtones / speedDial / rainbowUnlocked / softKeyTone /
	// lockWidgetMode / homeLayoutMode / wallpaperOfDay /
	// customAccentEnabled / customAccentR / G / B / ownerEmoji /
	// musicPlayMode / alarmTone / demoSlideStart) reads the new
	// byte as zero-initialised on a first boot after the firmware
	// grows -- which maps to Medium, the byte-identical pre-S206
	// 3 s default.
	uint8_t demoSpeed = 0;
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
