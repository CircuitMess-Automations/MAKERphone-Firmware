# MAKERphone 2.0 — 200-Session Roadmap

This is the sequenced build plan that the autonomous `makerphone-firmware-improve` agent follows. Sessions S01–S70 ship the v1.0 retro feature-phone shell; sessions S71–S200 expand the device into a full retro-arcade + organiser + theme-engine experience for v2.0. Each numbered session is sized to ship in **one** automated run — small enough to compile cleanly in a single attempt, meaningful enough to feel like real progress toward the retro feature-phone experience.

The agent picks **the lowest-numbered un-done session** each run, implements it, commits with a `feat(makerphone): SXX – <summary>` message (where `XX` is the session number), and ticks the entry off in this file as part of the same commit.

**Hardware target**: CircuitMess Chatter (ESP32, 160×128 TFT, 16-button shift-register keypad, LoRa, piezo buzzer, battery).
**Constraints**: Arduino + LVGL 8.x, code-only widgets preferred (zero SPIFFS asset cost), all new screens extend `LVScreen`, palette stays consistent with the `MP_*` defines used by every Phone\* widget.

Status legend: `[x]` done, `[ ]` to do, `[~]` in progress (partial / split across runs).

---

## Phase A — Foundation Atoms (S01–S12)
Reusable LVGL widgets, no screens yet. Each widget is fully styled and self-contained.

- [x] **S01** — `PhoneStatusBar` element (top bar: signal, battery, clock).
- [x] **S02** — `PhoneSoftKeyBar` element (bottom bar: left/right context labels + center hint).
- [x] **S03** — `PhoneClockFace` element (large pixelbasic16 time + date + weekday).
- [x] **S04** — `PhoneSynthwaveBg` wallpaper (gradient sky, mountain silhouette, perspective grid).
- [x] **S05** — Twinkle stars overlay on synthwave wallpaper.
- [x] **S06** — Sun pulse + scrolling perspective grid animations.
- [x] **S07** — `PhoneIconTile` element (36×36 pixel-art menu tile with halo pulse).
- [x] **S08** — `PhoneMenuGrid` element (flow-wrap grid + cursor + wrap navigation).
- [x] **S09** — `PhoneDialerKey` element (36×20 numpad key with letters caption + press flash).
- [x] **S10** — `PhoneDialerPad` composer (3×4 grid: 1–9, *, 0, # with cursor + onKeyPress).
- [x] **S11** — `PhonePixelAvatar` element (32×32 retro avatar from a seed/index — no SPIFFS).
- [x] **S12** — `PhoneChatBubble` element (sent / received variants, optional tail + timestamp).

## Phase B — Theme, Indicators, Toasts (S13–S16)
Cross-cutting visual layer that every screen will inherit.

- [x] **S13** — Restyle `ChatterTheme.cpp` with the MAKERphone palette (deep purple bg, sunset orange accent, cyan highlights, warm cream text). Audit existing screens for regressions.
- [x] **S14** — `PhoneSignalIcon` animated 4-bar signal indicator (LoRa link-strength as proxy).
- [x] **S15** — `PhoneBatteryIcon` retro pixel battery (replace generic `BatteryElement` glyph).
- [x] **S16** — `PhoneNotificationToast` — slide-down, auto-dismiss toast (used by call/SMS arrivals later).

## Phase C — Homescreen & Main Menu Wiring (S17–S22)
Compose the existing atoms into the first MAKERphone-feeling screens.

- [x] **S17** — `PhoneHomeScreen` skeleton: synthwave wallpaper + status bar + clock + soft-keys ("CALL" / "MENU"). Boots into existing flow without replacing anything yet.
- [x] **S18** — Wire `PhoneHomeScreen` as the post-`LockScreen` default (gated behind a build flag so we can fall back). Soft-key Right ("MENU") still routes to the legacy `MainMenu`.
- [x] **S19** — `PhoneMainMenu` screen built on `PhoneMenuGrid`: 7 icons (Phone, Messages, Contacts, Music, Camera, Games, Settings).
- [x] **S20** — Wire `PhoneMainMenu` as the new menu (replaces the vertical carousel). Each icon launches its current Chatter equivalent or a `not-yet-built` placeholder.
- [x] **S21** — Homescreen ↔ Main-menu transition (ease-in slide + soft-key feedback).
- [x] **S22** — Long-press "0" on homescreen → quick-dial; long-press "Back" → lock. Same on main-menu.

## Phase D — Phone / Calls (S23–S28)
The flagship "feels-like-a-phone" feature, even though it's a LoRa-backed simulation.

- [x] **S23** — `PhoneDialerScreen` using `PhoneDialerPad` with a digit-buffer label + "CALL" softkey.
- [x] **S24** — `PhoneIncomingCall` screen: caller name, pixel avatar, green Answer / red Hang-up.
- [x] **S25** — `PhoneActiveCall` screen: call timer, caller info, mute button, hang-up.
- [x] **S26** — `PhoneCallEnded` screen: 1.5s "Call ended — Xm Ys" overlay, returns to homescreen.
- [x] **S27** — `PhoneCallHistory` screen: list of recent calls with type icons (incoming/outgoing/missed).
- [x] **S28** — Tie call screens into existing `PairScreen` / `LoRaService`: a paired peer "calling" you triggers `PhoneIncomingCall`.

## Phase E — Messaging Restyle (S29–S34)

- [x] **S29** — Restyle `ConvoScreen` with `PhoneChatBubble` (right-aligned sent, left-aligned received).
- [x] **S30** — Pixel avatars (`PhonePixelAvatar`) next to received bubbles in `ConvoScreen`.
- [x] **S31** — Restyle `InboxScreen` as phone-style message list (avatar + name + 1-line preview + time).
- [x] **S32** — `PhoneT9Input` element — multi-tap text entry with letter-cycle timer + cursor caret.
- [x] **S33** — Wire `PhoneT9Input` into `ConvoScreen` as the default text composer (legacy keyboard kept as fallback).
- [x] **S34** — Message status indicators (clock=pending, single-tick=sent, double-tick=delivered) on sent bubbles.

## Phase F — Contacts / Phonebook (S35–S38)

- [x] **S35** — `PhoneContact` data model + persistence (atop the existing `Friends` model where possible).
- [x] **S36** — `PhoneContactsScreen` — alphabetical list, A–Z scroll-strip on the right.
- [x] **S37** — `PhoneContactDetail` screen — avatar, name, "number" (LoRa peer id), call/message buttons.
- [x] **S38** — `PhoneContactEdit` screen — name field (T9), avatar picker (8 generated PixelAvatars).

## Phase G — Audio, Music, Buzzer (S39–S43)

- [x] **S39** — `PhoneRingtoneEngine` — non-blocking melody framework over the existing `BuzzerService`.
- [x] **S40** — Five default ringtone melodies (Synthwave, Classic, Beep, Boss, Silent).
- [x] **S41** — Ringtone playback wired into `PhoneIncomingCall`.
- [x] **S42** — `PhoneMusicPlayer` screen UI (track name, progress bar, play/pause/skip buttons).
- [x] **S43** — Music-player melody library (10 tunes) playable from the player; uses `PhoneRingtoneEngine`.

## Phase H — Camera (S44–S46)

- [x] **S44** — `PhoneCameraScreen` — retro viewfinder (dotted frame, crosshair, mode label, shutter sound).
- [x] **S45** — Camera mode switcher (Photo / Effect / Selfie placeholder) with L/R bumper navigation.
- [x] **S46** — `PhoneGalleryScreen` placeholder — 4-thumbnail grid stub for future captures.

## Phase I — Lock Screen Polish (S47–S49)

- [x] **S47** — Phone-style lock-screen redesign with big clock + wallpaper + "slide to unlock" hint.
- [x] **S48** — Slide-to-unlock animation (Left → Right input chord, hint shimmer).
- [x] **S49** — Lock-screen notifications preview (latest unread SMS / missed call summary).

## Phase J — Settings (S50–S55)

- [x] **S50** — Phone-style `PhoneSettingsScreen` — grouped sections with chevrons.
- [x] **S51** — Brightness slider (controls the existing `setBrightness` API).
- [x] **S52** — Sound + vibration toggles (mute / vibrate / loud).
- [x] **S53** — Wallpaper picker (Synthwave / Plain / GridOnly / Stars).
- [x] **S54** — Date/time settings screen.
- [x] **S55** — About screen — device id, firmware version, free heap, uptime, peer count.

## Phase K — Boot, Power, Battery (S56–S59)

- [x] **S56** — Boot splash with MAKERphone wordmark + sunset (3s, skippable on any key).
- [x] **S57** — Power-down animation (CRT shrink + tone).
- [x] **S58** — Battery-low modal (≤15%) with ringtone chirp.
- [x] **S59** — Charging animation overlay on lock screen + homescreen.

## Phase L — Utility Apps (S60–S65)

- [x] **S60** — `PhoneCalculator` app (basic 4-fn, dialer-pad-style buttons).
- [x] **S61** — `PhoneStopwatch` app (start/stop/lap, mm:ss.cs).
- [x] **S62** — `PhoneTimer` app (count-down with buzzer ringtone on zero).
- [x] **S63** — `PhoneCalendar` app (month grid, today highlight, day detail).
- [x] **S64** — `PhoneNotepad` app (T9 entry + saved notes list).
- [x] **S65** — Integrate existing Snake / Pong / SpaceInvaders / SpaceRocks behind a phone-style `PhoneGames` grid.

## Phase M — Final Polish & QA (S66–S70)

- [x] **S66** — Generic page-transition animation helper (slide left/right, fade, used everywhere).
- [x] **S67** — Soft-key labels become context-sensitive everywhere — every screen wires its own L/R captions.
- [x] **S68** — Subtle haptic-style buzzer ticks on key navigation (very short, very quiet, toggle in Settings).
- [x] **S69** — Idle dimming + sleep behavior (auto-dim after 30s, sleep after 2min, wake on any key).
- [x] **S70** — End-to-end UX QA pass — exercise every screen flow, file a bug list as `KNOWN_ISSUES.md`, fix all critical regressions in the same commit, and write a v1.0 changelog.

## Phase N — More Games (S71–S100)
A proper retro-arcade carousel for `PhoneGames`. Each entry is one self-contained game extending `LVScreen`, hooked into the existing games grid. Code-only, palette-faithful.

- [x] **S71** — `PhoneTetris` — base falling tetrominoes, line clear with row flash.
- [x] **S72** — `PhoneTetris+` (split) — level progression, ghost piece, T-spin scoring.
- [x] **S73** — `PhoneBounce` — gravity ball, simple side-scrolling level.
- [x] **S74** — `PhoneBounce II` (split) — three more levels + collectible rings.
- [x] **S75** — `PhoneBrickBreaker` — paddle, ball, bricks, power-up bricks.
- [x] **S76** — `PhoneBantumi` — Mancala vs CPU, the Nokia classic.
- [x] **S77** — `PhoneBubbleSmile` — match-3 colored bubbles.
- [x] **S78** — `PhoneBubbleSmile+` (split) — combo cascades + 5 power-ups.
- [x] **S79** — `PhoneMinesweeper` — keypad-driven, three difficulties.
- [x] **S80** — `PhoneSlidingPuzzle` — randomized 15-tile puzzle.
- [x] **S81** — `PhoneTicTacToe` — vs CPU.
- [x] **S82** — `PhoneMemoryMatch` — pixel-icon pairs, count-to-clear timer.
- [x] **S83** — `PhoneSokoban` — engine + 5 hand-built levels.
- [x] **S84** — `PhoneSokoban+` (split) — 10 more levels + level-select grid.
- [x] **S85** — `PhonePinball` — single-table flippers + bumpers.
- [x] **S86** — `PhonePinball+` (split) — second table + leaderboard.
- [x] **S87** — `PhoneHangman` — uses existing T9 input + inline word list.
- [x] **S88** — `PhoneConnectFour` — vs CPU.
- [x] **S89** — `PhoneReversi` — Othello vs CPU.
- [x] **S90** — `PhoneWhackAMole` — dialer-key reaction game.
- [x] **S91** — `PhoneLunarLander` — fuel/thrust physics.
- [x] **S92** — `PhoneHelicopter` — endless side-scrolling avoidance.
- [x] **S93** — `Phone2048`.
- [x] **S94** — `PhoneSolitaire` — Klondike, dialer-driven column select.
- [x] **S95** — `PhoneSudoku` — three difficulty packs, generated.
- [x] **S96** — `PhoneWordle` — daily 5-letter guess.
- [x] **S97** — `PhoneSimon` — memory + buzzer-tone Simon Says.
- [x] **S98** — `PhoneSnakesLadders` — vs CPU.
- [x] **S99** — `PhoneAirHockey` — vs CPU, single-screen.
- [x] **S100** — `PhoneTowerDefence` — single lane, 8 waves.

## Phase O — Themes & Icon Sets (S101–S120)
Two sessions per theme: **part 1 = palette + wallpaper variant**, **part 2 = matching icon glyphs + soft-key tint + lock-screen accent**. All themes pickable from `Settings → Theme`.

- [x] **S101** — Theme: Nokia 3310 Monochrome — palette + wallpaper.
- [x] **S102** — Theme: Nokia 3310 Monochrome — icon glyphs + accents.
- [x] **S103** — Theme: Game Boy DMG (4-shade green) — palette + wallpaper.
- [x] **S104** — Theme: Game Boy DMG — icon glyphs + accents.
- [x] **S105** — Theme: Amber CRT — palette + wallpaper.
- [x] **S106** — Theme: Amber CRT — icon glyphs + accents.
- [x] **S107** — Theme: Sony Ericsson Aqua — palette + wallpaper.
- [x] **S108** — Theme: Sony Ericsson Aqua — icon glyphs + accents.
- [x] **S109** — Theme: RAZR Hot Pink — palette + wallpaper.
- [x] **S110** — Theme: RAZR Hot Pink — icon glyphs + accents.
- [x] **S111** — Theme: Stealth Black — palette + wallpaper.
- [x] **S112** — Theme: Stealth Black — icon glyphs + accents.
- [x] **S113** — Theme: Y2K Silver — palette + wallpaper.
- [x] **S114** — Theme: Y2K Silver — icon glyphs + accents.
- [x] **S115** — Theme: Cyberpunk Red — palette + wallpaper.
- [x] **S116** — Theme: Cyberpunk Red — icon glyphs + accents.
- [x] **S117** — Theme: Christmas / Festive — palette + wallpaper.
- [x] **S118** — Theme: Christmas / Festive — icon glyphs + accents.
- [x] **S119** — Theme: Surprise / Daily-Cycle — engine + palette.
- [x] **S120** — Theme: Surprise / Daily-Cycle — rotation logic + icon variants.

## Phase P — More Apps (S121–S135)
Composer is broken into three sessions because it needs (a) keypad note-entry UI, (b) RTTTL parser, (c) save-slot wiring to `PhoneRingtoneEngine`. Tamagotchi-style virtual pet adds persistent state via the existing `Storage` layer.

- [x] **S121** — `PhoneComposer` — keypad note-entry UI.
- [x] **S122** — `PhoneComposer` — RTTTL parser + serializer.
- [x] **S123** — `PhoneComposer` — save slots + wire to `PhoneRingtoneEngine`.
- [x] **S124** — `PhoneAlarmClock` — multi-alarm with snooze.
- [x] **S125** — `PhoneTimers` — multi-timer countdown list (extends S62).
- [x] **S126** — `PhoneCurrencyConverter` — offline rate table, two-column converter.
- [x] **S127** — `PhoneUnitConverter` — length / mass / temperature / volume.
- [x] **S128** — `PhoneWorldClock` — six-zone clock grid.
- [x] **S129** — `PhoneVirtualPet` — Tamagotchi-style feed/sleep/play, persists hunger/age across reboots.
- [x] **S130** — `PhoneMagic8Ball` — shake + reveal answer.
- [x] **S131** — `PhoneDiceRoller` — 1d4 → 2d20.
- [x] **S132** — `PhoneCoinFlip` — gravity-style flip animation.
- [x] **S133** — `PhoneFortuneCookie` — daily wisdom from inline string list.
- [x] **S134** — `PhoneFlashlight` — full-white screen + brightness max.
- [x] **S135** — `PhoneBirthdayReminders` — list + per-contact birthday field.

## Phase Q — Pocket Organiser (S136–S143)
Sony Ericsson "Organiser" classics — practical apps with offline-only state. Each persists through the existing `Storage` layer so a paused habit streak still counts on reboot.

- [x] **S136** — `PhoneTodo` — task list with three priority levels + tick-off animation.
- [x] **S137** — `PhoneHabits` — five-habit daily tracker, streak counter, weekly heatmap.
- [x] **S138** — `PhonePomodoro` — work/break cycle timer, configurable durations, cycle counter.
- [x] **S139** — `PhoneMoodLog` — one-tap-per-day mood journal, 30-day strip view.
- [x] **S140** — `PhoneScratchpad` — instant quick-jot pad (one buffer, separate from S64 Notepad's saved list).
- [x] **S141** — `PhoneExpenses` — running daily/weekly tally with category tags.
- [x] **S142** — `PhoneCountdown` — days-until-event widget (multiple events, sortable by closeness).
- [x] **S143** — `PhoneSteps` — mock pedometer (uptime-derived) with daily goal + streak.

## Phase R — Nostalgia Layer (S144–S163)
Tiny details that make the difference between "phone-shaped toy" and "this is exactly how my Nokia did it".

- [x] **S144** — Owner name on lock screen (T9 entry in Settings).
- [x] **S145** — Custom welcome greeting on boot ("Hello, $NAME!").
- [x] **S146** — Custom power-off message.
- [x] **S147** — Operator-logo banner — text + 5×16 user-pixelable logo.
- [x] **S148** — Boot melody (Sony-Ericsson-style four-note chime).
- [x] **S149** — Upgraded power-off melody (real descending arpeggio, replaces the S57 placeholder).
- [x] **S150** — Charge-complete chime (one-shot on charger transition).
- [x] **S151** — Speed dial 1–9 — long-press digit on home dials that contact.
- [x] **S152** — Birthday confetti animation when system date matches a contact (depends on S135).
- [x] **S153** — Per-contact custom ringtone (depends on S121–S123 composer output).
- [x] **S154** — "Press any key" idle hint that fades in after 10 s of stillness.
- [x] **S155** — Animated battery-charge fill bars on lock + home.
- [x] **S156** — Envelope-flying SMS-sent animation.
- [x] **S157** — "Delivered" double-tick chime.
- [x] **S158** — "Missed call" inverted-color flash on next wake.
- [x] **S159** — Profile system (General / Silent / Meeting / Outdoor / Headset).
- [x] **S160** — Per-profile ringtone selection (depends on S159 + S123).
- [x] **S161** — Vibration patterns per ringtone (buzzer-pulse choreography).
- [x] **S162** — SIM-card "PIN unlock" boot screen — decorative four-digit PIN entry.
- [x] **S163** — "Phone yawns" idle animation — eyes blink on home after 5 min idle.

## Phase S — Quirks & Easter Eggs (S164–S178)
The kind of thing customers post about when they discover it.

- [x] **S164** — `*#06#` on dialer → fake IMEI reveal screen.
- [x] **S165** — `*#0000#` → firmware-info screen (Sony-Ericsson code).
- [x] **S166** — Konami code on the d-pad → unlocks rainbow theme.
- [x] **S167** — Long-press `5` on dialer → flashlight quick-shortcut.
- [x] **S168** — Tilt simulator (hold L+R together) → shake-to-randomize current screen.
- [x] **S169** — Random tip-of-the-day banner on home idle.
- [x] **S170** — ASCII art slideshow viewer — eight hand-pixeled drawings.
- [x] **S171** — `PhoneStressReliever` — tap a thing repeatedly, watch reactions.
- [x] **S172** — Daily fortune in dialer (first open per day).
- [x] **S173** — Type "S-N-A-K-E" on dialer → instant Snake launch.
- [x] **S174** — Find-Friends radar mini — animated sweep over the friends list.
- [x] **S175** — T9 vocab pack expansion (more dictionary words).
- [x] **S176** — Drum kit on dialer keypad — each digit a different drum.
- [x] **S177** — Boot phrase rotates each day (motivational quotes).
- [x] **S178** — Day-of-week themed wallpaper accents.

## Phase T — Personalisation Deep Dive (S179–S188)
Beyond themes — let users actually make the phone *theirs*.

- [x] **S179** — Theme picker — live preview while scrolling.
- [x] ~~**S180** — Custom contact ringtone (per friend, depends on S123).~~ — already shipped as S153 (PhoneContactRingtonePicker + ringtoneOf wiring); duplicate.
- [x] **S181** — Custom contact wallpaper (per friend).
- [x] **S182** — Avatar editor — pixel-paint mini editor (32×32, replaces the S38 picker).
- [x] **S183** — Soft-key tone customisation.
- [x] **S184** — Lock screen widget picker (clock-only / clock+date / clock+next-event).
- [x] **S185** — Home screen layout switcher.
- [x] **S186** — Wallpaper of the day — daily auto-rotate from a curated list.
- [x] **S187** — Custom RGB picker for accent color (real-time preview).
- [x] **S188** — Owner emoji / avatar selection (used on lock + status bar).

## Phase U — Audio / Music (S189–S196)
Buzzer is what we have, so we lean into it.

- [x] **S189** — Music player — playlist support.
- [x] **S190** — Music player — shuffle / repeat / continuous.
- [x] **S191** — Equalizer visualiser — bars dance to active melody.
- [x] **S192** — Richer system-tone library — 15+ chimes for every action.
- [x] **S193** — Custom buzzer alarm tone (composer-fed).
- [x] **S194** — `PhoneBeatMaker` — 16-step drum sequencer.
- [x] **S195** — `PhoneRadio` — fake FM dial with eight stations of pre-canned melody loops.
- [x] **S196** — Karaoke title-display mode while a melody plays.

## Phase V — Final Polish & v2.0 Release (S197–S200)

- [x] **S197** — Memory-leak audit — push/pop every screen 1000× in test mode, heap must stay flat.
- [x] **S198** — Battery-life pass + idle-dim tuning + LVGL-cost measurement.
- [x] **S199** — Final QA — exercise every flow, fix critical regressions in the same commit.
- [x] **S200** — v2.0 changelog + KNOWN_ISSUES sweep + auto-cycling demo mode for the marketing video.

## Phase W — v2.1 Hotfix Series (S201+)
The 200-session v2.0 roadmap is closed. Phase W picks up the v2.1
candidate-pool items called out at the bottom of `KNOWN_ISSUES.md` —
short, self-contained polish / cleanup tasks that didn't make the
v2.0 cut. Sessions are appended one-by-one as the autonomous agent
claims and ships them, so the next un-done line is always the
lowest-numbered `[ ]`.

- [x] **S201** — `MAKERPHONE_LOAD_MOCK_DATA` build flag — gates the
  dev-only `loadMock()` / `printData()` helpers + `Chatters[]` test
  peer table behind a 0/1 macro in `src/MAKERphoneConfig.h`. Default
  0 (production parity); flip via `-DMAKERPHONE_LOAD_MOCK_DATA=1` in
  arduino-cli for local dev. Replaces the commented-out
  `//loadMock(true);` call site in the .ino. Resolves the matching
  v2.1 polish item in `KNOWN_ISSUES.md`.

- [x] **S202** — `PhoneSpeedDialScreen` overwrite-guard prompt — adds
  a third `Mode::Confirm` to the speed-dial editor (S151) that
  intercepts a destructive PICK before any `Settings.speedDial[d]`
  mutation. When the focused slot already binds to a *different*
  contact, ENTER hands off to a centered "REPLACE? / CLEAR?" panel
  showing `SLOT N`, the current binding, an arrow, and the new pick;
  ENTER persists, BACK returns to pick mode with the cursor still on
  the contact the user picked (no rebuild, no flicker). Trivial
  cases (slot empty, picking the same contact, clearing an already-
  empty slot) still apply directly. Soft-keys flip to
  REPLACE / CLEAR + CANCEL in confirm mode. Resolves the matching
  v2.1 polish item in `KNOWN_ISSUES.md`.

- [x] **S203** — `PhoneDemoModeScreen` persisted slide pointer — adds
  a `Settings.demoSlideStart` byte (default 0, clamped to
  `[0..kSlideCount-1]`) so the v2.0 demo deck (S200) resumes on the
  slide the previous run dismissed on instead of always restarting
  from slide 0. The screen seeds `slideIdx` from the persisted byte
  via a new public `resolveStartSlide()` accessor in its constructor,
  and writes the currently visible slide back via a new private
  `persistCurrentSlide()` helper on dismiss (any-key exit). Auto-
  advances inside an open run are intentionally NOT persisted so
  the nightly auto-cycle never burns NVS write budget; only the
  dismiss-time write hits NVS, and only when the value actually
  changed. Sits at the end of the `SettingsData` blob so the
  existing NVS-resize pattern reads the new byte as zero-initialised
  on a first boot after the firmware grows -- which maps to slide 0,
  the byte-identical pre-S203 default. Resolves the matching v2.1
  polish item in `KNOWN_ISSUES.md`.

- [x] **S204** — `PhoneOperatorBanner` lv_canvas logo cache — replaces
  the up-to-80 per-cell `lv_obj` rectangles the v2.0 banner spawned
  inside `rebuildLogo()` with a single `lv_canvas` whose 16x5
  `LV_IMG_CF_TRUE_COLOR_ALPHA` backing buffer (240 bytes) is
  rasterised once on edit and reused on every subsequent screen
  push. The buffer is an inline member, so its lifetime exactly
  matches the banner instance with no malloc/free churn; lit cells
  are stamped via `lv_canvas_draw_rect` (the LVGL 8.x portable API
  that survives both the older `set_px()` and the newer
  `set_px_color`/`set_px_opa` split). Rendered output is
  byte-identical to the pre-S204 banner -- same MP_ACCENT cells,
  same 1 px stride, same right-anchored placement -- but the LVGL
  object count for the banner drops from "1 host + up to 80 cells"
  to "1 host + 1 canvas". Resolves the matching v2.1 polish item
  in `KNOWN_ISSUES.md`.

- [x] **S205** — `PhoneRadio` SILENT-profile gate — adds a static
  `PhoneRadio::isSilenced()` helper (reads `!Settings.get().sound`,
  the legacy bool that `PhoneProfileScreen` (S159) writes to `false`
  for the SILENT and MEETING profiles and `true` for GENERAL /
  OUTDOOR / HEADSET) and rewires `startPlayback()` and the
  retune branch of `tuneTo()` to short-circuit the
  `PhoneRingtoneEngine::play()` call when silenced. The screen
  still flips to its "playing" state visually so the soft-key
  reads `STOP` and L/R tuning still feels live, but the engine is
  never asked to drive the piezo, so the dial cannot leak even
  the micro-interval of audible noise that could slip in between
  `Ringtone.play()` and the engine's first per-loop
  `Settings.get().sound` mute tick. `refreshStatus()` grows a
  third pill state -- `MUTED` (muted-purple body, cyan border,
  cyan text) sitting between the existing `ON AIR` (sunset
  orange) and `TUNED` (dim purple) so the user can read at a
  glance why the radio is silent. Resolves the matching v2.1
  polish item in `KNOWN_ISSUES.md`.

- [x] **S206** — `PhoneDemoModeScreen` user-tunable slide pace --
  adds a `Settings.demoSpeed` byte (default 0 = Medium / 3000 ms,
  1 = Slow / 5000 ms, 2 = Fast / 1500 ms) so the v2.0 demo deck's
  slide period (S200) is no longer hard-coded. `PhoneDemoModeScreen`
  grows a public `Speed` enum, the per-preset constants
  `kSlidePeriodSlowMs / MediumMs / FastMs`, a `speedFromByte()`
  clamp, and a `resolveSlidePeriodMs()` static helper that reads
  the new byte and falls back to Medium for any out-of-range
  value; `lv_timer_create` then ticks at the resolved cadence so
  the chosen pace takes effect on the next push. A new
  `PhoneDemoSpeedScreen` picker (Slow / Medium / Fast, modelled on
  `PhoneLockWidgetScreen`'s three-row dirty-aware picker) is wired
  into the ADVANCED group of `PhoneSettingsScreen` directly above
  the existing "Demo mode" row so the speed knob clusters with the
  demo entry it configures. Persisted values outside [0..2] clamp
  to Medium at the screen layer to be defensive against NVS-resize
  wipes that read the new byte as uninitialised garbage; the byte
  sits at the end of the SettingsData blob next to demoSlideStart
  so the existing NVS-resize pattern reads it as zero-initialised
  on a first boot after the firmware grows -- which maps to
  Medium, the byte-identical pre-S206 3 s default. Resolves the
  matching v2.1 polish item in `KNOWN_ISSUES.md`.

- [x] **S207** — `PhoneClockFace` 2x2 colon blink anti-ghost — replaces
  the pixelbasic16 `":"` glyph used as the live blinking colon between
  HH and MM with two `lv_obj_t` 2x2 cyan blocks (top + bottom dot).
  The single-pixel-wide colon dots in the font otherwise leave a
  stale 1-pixel cyan ghost on the 1 Hz blink-off frame on panels
  where LovyanGFX rounds odd anti-alias spans differently across
  the row pair (matching the v2.1 polish item in `KNOWN_ISSUES.md`).
  The two blocks share the existing `colonOn` state and toggle
  together via `LV_OBJ_FLAG_HIDDEN` so the cells are cleanly cleared
  to the parent's background on the off frame -- no ghost can
  survive across frames. Both blocks are anchored to `LV_ALIGN_TOP_MID`
  with explicit y offsets (4 / 10) that approximate where the
  original glyph dots rendered within the 14 px line height, so the
  visual cadence and placement match the pre-S207 rendering. The
  field swap (`colonLabel` -> `colonDotTop` / `colonDotBot`) is
  internal to `PhoneClockFace`; no external callers reference the
  old field. Resolves the matching v2.1 polish item in
  `KNOWN_ISSUES.md`.

- [x] **S208** — `PhoneSynthwaveBg` star-field millis()-seeded jitter --
  the v2.0 wallpaper's seven-star `buildStars()` lookup table is
  hand-picked, so before S208 every mount of the home-screen
  produced the byte-identical constellation (same x/y, same
  ping-pong delay) and the sky never felt fresh. `buildStars()`
  now seeds a tiny local Numerical-Recipes LCG from `millis()` at
  mount time and walks each star through (a) a small X offset in
  [-2..+2] px and Y offset in [-1..+1] px, clamped to keep the
  star inside the upper sky band (`x in [0, BgWidth-size]`,
  `y in [0, 30-size]`) so a +2 px drift on the right-edge stars
  (124, 144) cannot fall off the 160 px wallpaper, and (b) a
  phase shift in `[0, 2*period)` so the twinkle cadence lands at
  a different point of its cycle each push. The LCG is local, so
  the global Arduino `random()` stream stays untouched and other
  widgets / games / ringtone phases that depend on it remain
  deterministic. The hand-picked positions, sizes and per-star
  peaks are unchanged -- only the per-mount drift + phase shift
  are new. Resolves the matching v2.1 polish item in
  `KNOWN_ISSUES.md`.

- [x] **S209** — `PhoneT9Input` pending-strip width clamp -- the
  feature-phone multi-tap hint strip below the entry textbox houses
  two free-floating labels (a left-anchored `pendingLabel` showing
  the active key's letter ring with the live letter bracketed, e.g.
  `[p]qrs7` for key 7, and a right-anchored `caseLabel` showing the
  case mode `abc` / `Abc` / `ABC`). Before S209 neither label had a
  width budget, so a long pending hint string -- longer than the
  current ITU-T E.161 keymap produces, but reachable by a
  future localisation, an extended-glyph keymap, or simply a future
  host that narrows the strip -- could collide with the case pill,
  the v2.1 "Long T9 entries overrun the 1-line caret hint on very
  narrow notes" polish item in `KNOWN_ISSUES.md`. `buildPendingStrip()`
  now reserves a fixed 24 px box on the right for the case label
  (right-aligned text, `LV_LABEL_LONG_CLIP` so a future wider case
  glyph cluster cannot push back into the hint either), keeps a 4 px
  gap between the two labels, and clamps `pendingLabel` to the
  remaining width with `LV_LABEL_LONG_DOT` so a hint that does not
  fit truncates with an ellipsis instead of crashing into the case
  pill. The geometry constants land as locals (kPendingLeftX,
  kCaseRightGap, kCaseReserveW, kInterLabelGap, kPendingMaxW) so
  they read like every other Phone* widget's geometry block. Visible
  output on every existing host (PhoneNotepad, ConvoScreen,
  PhoneContactEdit) is byte-identical because the longest current
  pending hint `[p]qrs7` (~30 px) still fits comfortably inside the
  new ~125 px hint budget. Resolves the matching v2.1 polish item
  in `KNOWN_ISSUES.md`.

- [x] **S210** — `PhoneSoftKeyBar` press-flash dim-wake -- the v2.1
  polish item flagged that the soft-key bar's press-feedback flash
  (`flashLeft()` / `flashRight()`, S21, ~180 ms cyan<->sunset-orange
  swap on the label/arrow) can land entirely inside `PhoneIdleDim`'s
  Stage::Dim (30 % of user brightness after 30 s) or Stage::DeepDim
  (12 % after 90 s) window, where the orange-on-purple invert is too
  faint for the user to register. The natural per-key path
  (`anyKeyPressed` listener) does wake `IdleDim` before the screen
  routes its softkey handler, but the order between LVGL listeners
  is not guaranteed across boards, and `flashLeft()` /
  `flashRight()` are also called programmatically from screens
  (e.g. confirmation modals, transient toasts, and any future
  caller that invokes the flash without a hardware key event). To
  make the flash self-sufficient, `PhoneSoftKeyBar::flashSide()`
  now begins with an explicit `IdleDim.resetActivity()` call that
  drives the panel back to the user's full brightness before the
  visual invert lands. The call is intentionally idempotent: when
  already in `Stage::Bright` it short-circuits via the
  `currentStage != Stage::Bright` guard inside
  `PhoneIdleDim::resetActivity()`, and when the backlight is
  electrically off (mid-`SleepService` fade-out / light-sleep) it
  early-returns so the carefully balanced
  deinit/`fadeIn` pair is untouched. Visuals are byte-identical
  on every existing host -- only the brightness floor is lifted.
  The new include is `src/Services/PhoneIdleDim.h`; no header
  surface changes. Resolves the matching v2.1 polish item in
  `KNOWN_ISSUES.md`.

---

## How the agent reads this file

On every run the agent:
1. Reads this file end-to-end.
2. Picks the **lowest-numbered `[ ]`** session (or the only `[~]` if there is one).
3. Implements that single session, keeping the change small enough to compile in one shot and respecting the constraints in the project SKILL.
4. In the same commit, flips the `[ ]` (or `[~]`) for that session to `[x]`. The commit message body always lists the session number, so `git log` is a session-by-session record.
5. Pushes to `origin/main` and monitors the CI run, fixing forward (max 3 attempts) before reverting.

If a session needs to be split (compiled cleanly but only delivered half the scope), the agent leaves it as `[~]` with a parenthetical note describing what's still outstanding, and the next run finishes it.

If a session no longer makes sense (e.g. blocked by missing dependencies or rendered moot by an earlier session's design), the agent strikes through the line, adds a one-line rationale, and proceeds to the next available `[ ]`.

The roadmap is itself editable — when reality diverges from the plan, the agent is allowed to insert a new session at the next free integer or rewrite descriptions, **provided the change is committed in the same run as the work it justifies**.
