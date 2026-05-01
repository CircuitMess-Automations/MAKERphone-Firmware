# Changelog

All notable changes to MAKERphone 2.0 firmware. The format is loosely
based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
the version line tracks the `MAKERPHONE_ROADMAP.md` phase that just
shipped (so v1.0 == "Phase A through Phase M complete").

## [v1.0] — 2026-05-01 — "Sunset" (S70)

The first feature-complete MAKERphone 2.0 build. Sessions S01–S69
delivered every Phase A–M widget, screen, and service; S70 is the
end-to-end UX QA pass that signs off on the v1.0 surface.

### Added — Phase A — Foundation Atoms (S01–S12)
- `PhoneStatusBar`, `PhoneSoftKeyBar`, `PhoneClockFace` element trio
  for the homescreen chrome.
- `PhoneSynthwaveBg` wallpaper with stars, sun pulse and scrolling
  perspective grid (S04–S06).
- `PhoneIconTile` + `PhoneMenuGrid` for icon menus.
- `PhoneDialerKey` + `PhoneDialerPad` for the numpad composer.
- `PhonePixelAvatar` (32×32 deterministic retro avatar from a seed —
  no SPIFFS asset cost).
- `PhoneChatBubble` (sent / received variants with optional tail).

### Added — Phase B — Theme + indicators + toasts (S13–S16)
- Restyled `ChatterTheme.cpp` with the MAKERphone palette
  (deep-purple bg, sunset orange accent, cyan highlights, warm
  cream text).
- `PhoneSignalIcon` 4-bar animated indicator (LoRa link strength).
- `PhoneBatteryIcon` retro pixel battery glyph.
- `PhoneNotificationToast` — slide-down auto-dismiss toast.

### Added — Phase C — Homescreen + main menu (S17–S22)
- `PhoneHomeScreen` (synthwave + clock + softkeys), wired as the
  post-`LockScreen` default behind `MAKERPHONE_USE_HOMESCREEN`.
- `PhoneMainMenu` icon grid (Phone / Messages / Contacts / Music /
  Camera / Games / Settings) replacing the legacy vertical carousel.
- Drill-style horizontal slide for home ↔ menu (S21).
- Long-press shortcuts: hold `0` → quick-dial, hold `Back` → lock.

### Added — Phase D — Calls (S23–S28)
- `PhoneDialerScreen` digit composer + CALL softkey.
- `PhoneIncomingCall` (caller name + avatar + Answer / Reject).
- `PhoneActiveCall` (call timer + mute toggle + Hang-up).
- `PhoneCallEnded` 1.5 s "Call ended — Xm Ys" overlay.
- `PhoneCallHistory` recent-calls list (incoming / outgoing /
  missed).
- `PhoneCallService` end-to-end wiring: a paired peer's
  `CALL_REQUEST` LoRa payload now raises `PhoneIncomingCall`.

### Added — Phase E — Messaging restyle (S29–S34)
- `ConvoScreen` re-themed with `PhoneChatBubble` + per-row pixel
  avatars.
- Phone-style `InboxScreen` rows (avatar + name + 1-line preview +
  time).
- `PhoneT9Input` multi-tap text entry with letter-cycle timer +
  cursor caret, wired into `ConvoScreen` as the default composer.
- Sent-message status indicators (clock / single tick / double
  tick).

### Added — Phase F — Contacts (S35–S38)
- `PhoneContact` model on top of the legacy `Friends` repo.
- `PhoneContactsScreen` alphabetical list with A–Z scroll-strip.
- `PhoneContactDetail` (avatar + nickname + uid + Call / Message
  / Edit).
- `PhoneContactEdit` (T9 nickname + 8-avatar picker).

### Added — Phase G — Audio (S39–S43)
- `PhoneRingtoneEngine` non-blocking melody framework over the
  existing `BuzzerService`.
- Five default ringtones (Synthwave / Classic / Beep / Boss /
  Silent) wired into `PhoneIncomingCall`.
- `PhoneMusicPlayer` UI (track / progress / play / pause / skip).
- 10-tune music library: Neon Drive, Pixel Sunrise, Cyber Dawn,
  Crystal Cave, Hyperloop, Starfall, Retro Quest, Moonlit Drift,
  Arcade Hero, Sunset Blvd.

### Added — Phase H — Camera (S44–S46)
- `PhoneCameraScreen` retro viewfinder (corner brackets, dotted
  edge ticks, crosshair, REC dot, mode label).
- L/R mode switcher (Photo / Effect / Selfie).
- `PhoneGalleryScreen` 4-thumbnail placeholder grid.

### Added — Phase I — Lock-screen polish (S47–S49)
- Phone-style `LockScreen` redesign (big clock + wallpaper +
  "slide to unlock" hint).
- Slide-to-unlock animation (Left → Right input chord).
- Latest unread SMS / missed-call summary preview on the lock
  screen.

### Added — Phase J — Settings (S50–S55)
- Phone-style `PhoneSettingsScreen` with grouped sections.
- Brightness slider (S51).
- Sound + vibration profile picker (S52).
- Wallpaper picker — Synthwave / Plain / GridOnly / Stars (S53).
- Date & Time editor backed by a new `PhoneClock` wall-clock
  service (S54).
- About page — device id, firmware version, free heap, uptime,
  paired peer count (S55).

### Added — Phase K — Boot / power / battery (S56–S59)
- MAKERphone-branded `PhoneBootSplash` (3 s sunset wordmark,
  any-key skip).
- CRT-shrink power-down animation + tone (S57).
- Battery-low modal (≤ 15 %) with ringtone chirp.
- Charging overlay drawn above the softkey bar on lock + home.

### Added — Phase L — Utility apps (S60–S65)
- `PhoneCalculator` (4-fn, dialer-pad-style buttons).
- `PhoneStopwatch` (start / stop / lap, mm:ss.cs).
- `PhoneTimer` (countdown with buzzer ringtone on zero).
- `PhoneCalendar` (month grid, today highlight, day detail).
- `PhoneNotepad` (T9 entry + saved notes list).
- `PhoneGamesScreen` 2×2 grid wrapping the existing Snake / Pong /
  SpaceInvaders / SpaceRocks behind a phone-style launcher.

### Added — Phase M — Final polish (S66–S69)
- `PhoneTransitions` page-transition helper (Drill / Modal / Fade /
  Instant) standardising every push / pop animation.
- Context-sensitive softkey labels (every screen wires its own
  L / R captions).
- Subtle haptic-style buzzer ticks on key navigation, opt-in via
  Settings.
- `PhoneIdleDim` service auto-dims the backlight after 30 s and
  restores on the next button press, sitting between active use and
  the existing `SleepService` deep-sleep.

### Fixed — S70
- `IntroScreen` settings default-case fallthrough now reads
  "SETTINGS" instead of "SETTING" — matches every other app-stub
  caption and the `PhoneMainMenu` focused-app caption.

### Notes
- `KNOWN_ISSUES.md` lists the open polish + cosmetic + hardware-only
  items the QA pass uncovered. Nothing critical; everything is fair
  game for a future session.
- v1.0 builds cleanly against `cm:esp32@1.7.5` on the self-hosted
  `bit-flash` Mac runner. The legacy boot path (`MAKERPHONE_USE_HOMESCREEN=0`)
  still compiles for fall-back.

[v1.0]: https://github.com/CircuitMess-Automations/MAKERphone-Firmware/releases/tag/v1.0
