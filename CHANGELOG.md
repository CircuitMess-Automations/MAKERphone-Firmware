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

## [v2.0] — 2026-05-06 — "Sunset Boulevard" (S70–S200)

The full MAKERphone 2.0 firmware. Sessions S70–S199 grew the Sunset
v1.0 surface into a 200-session feature phone — Phase N games,
Phase O themes, Phase P organisers, Phase Q nostalgia toys, Phase R
quirks + personalisation, Phase S diagnostics, Phase T audio + voice,
Phase U audio deep-dive, Phase V final polish. S200 is the v2.0
sign-off: this changelog entry, the KNOWN_ISSUES sweep, the
"MAKERphone v2.0" version-string bump in `PhoneAboutScreen`, and the
new auto-cycling demo mode for the marketing video.

### Added — Phase N — Games (S71–S100)
- 12 phone-style retro games on top of the v1.0 four (Snake, Pong,
  Space Invaders, Space Rocks): Tetris, 2048, Air Hockey, Pinball,
  Frogger, Breakout, Sokoban, Memory, Whack-a-Mole, Solitaire, Bonk,
  Reflex.
- Pixel-art game tiles + 2×2 paged grid in `PhoneGamesScreen` —
  scrolls through every entry without scrollbars.

### Added — Phase O — Themes (S101–S119)
- Global theme picker at `Settings → Theme` with 10 presets
  (Synthwave, Nokia 3310 Mono, Cyberpunk, Vapor, Pixel Forest,
  Coral Reef, Ice Field, Volcano, Cassette Tape, Retro Arcade).
- Per-theme palette + wallpaper pair, applied to every
  `Phone*Screen` on the next push without a reboot.
- Custom RGB accent override at `Settings → Accent` (S187) — three
  channels with a live preview slab.

### Added — Phase P — Organisers (S120–S150)
- Composer + saved-melody slots feeding ringtones, alarms, and the
  music player (S121–S123).
- Owner name (S144) + welcome screen, owner emoji (S188).
- Operator banner editor — 5×16 user-pixelable carrier logo (S147).
- Speed-dial editor and 1..9 long-press home gesture (S151).
- Per-contact ringtone (S153) and per-profile ringtone (S160).

### Added — Phase Q — Nostalgia (S151–S170)
- Idle-hint, tip-of-the-day and "phone yawns" animations on the
  homescreen (S154 / S163 / S169).
- Konami-code Easter egg unlocks the rainbow theme (S166).
- Tilt simulator — hold L+R to "shake" any opt-in screen (S168).
- Decorative SIM PIN unlock screen between boot splash and intro
  (S162).

### Added — Phase R — Personalisation (S171–S188)
- Wallpaper-of-the-day rotator (S186).
- Home-screen layout switcher: Classic / Minimal / Stack (S185).
- Lock-screen widget composer: ClockDate / ClockOnly / ClockEvent
  (S184).
- Soft-key click-tone customisation (S183).
- Power-off message overlaid on the CRT-shrink animation (S146).

### Added — Phase S — Diagnostics + service menu (S189–S198)
- Hidden `PhoneDiagScreen` (BTN_R Easter egg from About) shows the
  rolling LVGL/loop-cost samples and idle-dim stage in real time.
- Memory-leak audit infrastructure — push/pop every screen 1000×
  in test mode, heap stays flat (S197).
- Battery-life pass: `PhoneIdleDim` two-stage dim @ 30 s and 90 s,
  LVGL-cost measurement service (S198).

### Added — Phase T — Audio + voice (S171–S180)
- Virtual pet (S173) — feed, play, watch it level up.
- Beat-maker — 16-step drum sequencer (S194).
- Fake FM dial — eight stations of pre-canned melody loops (S195).

### Added — Phase U — Audio deep-dive (S189–S196)
- Music player playlists (S189), shuffle / repeat / continuous (S190).
- Equalizer visualiser dancing to the active melody (S191).
- 18-chime system-tone library (S192).
- Custom buzzer alarm tone, composer-fed (S193).
- Karaoke title display while a melody plays (S196).

### Added — Phase V — Final polish + v2.0 release (S197–S200)
- S199 final QA exercised every reachable flow and committed the
  critical-regression fixes inline.
- S200 ships:
  - This v2.0 CHANGELOG entry.
  - `PhoneDemoModeScreen` — looped 9-slide marketing slideshow at
    `Settings → ADVANCED → Demo mode`. Synthwave wallpaper, big
    cream pixelbasic16 title, two cream sub-lines, dim purple
    breadcrumb, sunset-orange progress dots. 3 s per slide; any key
    exits.
  - `kFirmwareVersion` bumped from `MAKERphone v0.55` to
    `MAKERphone v2.0` so the About page (S55) and the firmware-info
    screen (S178) read the v2.0 release tag.
  - KNOWN_ISSUES.md sweep — v1.0 polish items still relevant carry
    forward, v2.0 fresh items added, hardware-only items relabelled
    against v2.0 hardware.

### Notes
- v2.0 builds cleanly against `cm:esp32@1.7.5` on the self-hosted
  `bit-flash` Mac runner. Both legacy build flags
  (`MAKERPHONE_USE_HOMESCREEN=0`, `MAKERPHONE_SHOW_SIM_PIN=0`,
  `MAKERPHONE_USE_T9_COMPOSER=0`) still compile.
- `KNOWN_ISSUES.md` reflects the v2.0 audit; nothing critical
  remains on the v2.0 surface, every entry is opt-in polish for
  v2.1.

[v2.0]: https://github.com/CircuitMess-Automations/MAKERphone-Firmware/releases/tag/v2.0
