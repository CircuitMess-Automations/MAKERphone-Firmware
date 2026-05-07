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

- [x] **S211** — `PhoneSettingsScreen::Item` exhaustiveness
  `static_assert` + `ItemCount` derivation fix -- the v1.0
  polish item in `KNOWN_ISSUES.md` asked for the
  `IntroScreen.cpp` settings dispatch's `default:` branch
  (a soft runtime fallback to `PhoneAppStubScreen("SETTINGS")`
  whenever a future `PhoneSettingsScreen::Item` row was added
  without being wired up) to be converted into a compile-time
  `static_assert` over the enum size so the branch becomes
  unreachable rather than a soft fallback. While drafting the
  guard the audit caught a real drift bug: `S206` had grown the
  `Item` enum to 20 enumerators (last `DemoSpeed = 19`) and the
  `kLayout` table in `PhoneSettingsScreen.cpp` to 20 selectable
  rows, but the hand-rolled `static constexpr uint8_t ItemCount
  = 19;` in `PhoneSettingsScreen.h` had not been bumped. Two
  silent consequences: (a) `buildList()` walked all 20 layout
  rows and wrote `rows[19]` one past the end of `Row
  rows[ItemCount]`, which on the current struct layout overlaps
  the immediately-following `uint8_t cursor` member -- the
  initial cursor position was effectively garbage every push
  until the first key press normalised it; (b) `moveCursorBy()`
  clamped the wrap to `ItemCount = 19`, so the final `Demo
  mode` row in the ADVANCED group was unreachable from the
  keypad even though it was painted. Both bugs are fixed by
  deriving the count from the enum.

  Concretely S211: (i) appends a sentinel `Count = 20` enumerator
  to `PhoneSettingsScreen::Item` (must remain the last
  enumerator, documented as not a real selectable row), (ii)
  rewrites `ItemCount` as `static_cast<uint8_t>(Item::Count)`
  so adding a new enumerator automatically grows the `rows[]`
  array, the cursor wrap, and every `for(...; i < ItemCount;
  ...)` iteration site without a manual bump, (iii) lands a
  `constexpr countSelectableRows()` recursive helper next to
  `kLayout` in `PhoneSettingsScreen.cpp` plus a `static_assert`
  that pins `kLayoutRowCount == PhoneSettingsScreen::ItemCount`
  -- so kLayout drifting out of sync with the enum is now a
  build error, and (iv) lands a second `static_assert(
  static_cast<uint8_t>(PhoneSettingsScreen::Item::Count) == 20,
  ...)` at the top of the settings-tile lambda in
  `IntroScreen.cpp` -- so adding a new Item enumerator without
  also adding a `case` in the dispatch (and bumping the
  literal) is also a build error. The pre-S211 soft `default:`
  branch is kept as a belt-and-braces fallback for the
  out-of-range-alias edge case, but the static_assert above
  guarantees it is unreachable in normal operation. Visible
  output is byte-identical for every existing path; the only
  behavioural delta is that the `Demo mode` row is now
  reachable via the keypad's down-arrow wrap, which is the
  intended behaviour for an ADVANCED row that has shipped
  since S200. Resolves the matching v1.0 polish item in
  `KNOWN_ISSUES.md`.


- [x] **S212** — boot-service init order ringtone race fix --
  the v1.0 KNOWN_ISSUES item flagged that `Phone.begin()` (the
  `PhoneCallService` -> `MessageService` listener subscription that
  raises `PhoneIncomingCall` on a `CALL_REQUEST` magic body) ran
  early in `boot()` while the audio stack was still half-initialised.
  S148 had promoted `Ringtone.begin()` and S161 promoted
  `Vibrate.begin()` out of the IntroScreen dismiss callback to the
  top-level `boot()` flow, but `Buzz.begin()` was still tucked inside
  the IntroScreen dismiss callback (~3 s into boot, right at the end
  of the CircuitMess intro animation) and `Phone.begin()` itself was
  positioned between `Profiles.begin()` and `Sleep.begin()` -- so the
  call-request listener subscribed BEFORE every audio engine. A
  `CALL_REQUEST` that arrived during the boot splash / decorative
  SIM-PIN / IntroScreen window (the first ~3 s of `LoopManager`
  ticking after `setup()` returns) would push `PhoneIncomingCall`
  on top of the splash and find `Ringtone` / `Vibrate` initialised
  but `Buzz` still silent -- meaning the per-key tone heard during
  the answer / reject confirmation, plus any concurrent inbound
  text chime fan-out through `BuzzerService::msgReceived`, would
  drop on the floor. S212 reorders `boot()` so the call subscription
  is the LAST audio-adjacent service to subscribe: (a) the early
  `Phone.begin();` block (S28 comment + the call) is removed from
  its position right after `Profiles.begin()`; (b) `Buzz.begin();`
  is promoted from inside both IntroScreen dismiss callbacks (the
  `MAKERPHONE_SHOW_SIM_PIN` SIM-PIN path and the direct splash
  path) into the top-level `boot()` flow, immediately after
  `Vibrate.begin();`; (c) `Phone.begin();` is re-inserted right
  after `Buzz.begin();` so by the first `LoopManager::loop()` tick
  after `setup()` returns every audio engine -- Ringtone (S148),
  Vibrate (S161), Buzz (S212) -- is alive before the
  `CALL_REQUEST` listener can fire. Both intro-callback bodies
  drop their `Buzz.begin()` line and gain a `// S212:` comment
  marker pointing to the new home so a future reader does not
  resurrect the duplicate. `BuzzerService::begin()` is a thin
  listener subscription (Input + MessageService fan-out, no
  `LoopManager` attachment until a tone is requested) so
  promoting it is side-effect free; the contract documented in
  `PhoneCallService.h` (subscription must come after
  `Messages.begin()`) still holds because we strictly delayed
  the call -- not advanced it. Visible output is byte-identical
  on every existing path with `Settings.sound = false` (the
  prototype mute override still suppresses key tones until the
  user opts in via the Sound screen); the only behavioural
  delta is that with `Settings.sound = true`, keypresses on the
  splash / SIM-PIN / IntroScreen now produce the same per-key
  musical tone they produce on every other screen, and an
  inbound text or `CALL_REQUEST` arriving during the boot
  window now chimes / rings audibly as designed. Resolves the
  matching v1.0 polish item in `KNOWN_ISSUES.md`.

- [x] **S213** — `InboxScreen` empty-state legacy `ListItem` cleanup --
  the v1.0 polish item in `KNOWN_ISSUES.md` flagged that the inbox
  screen still compiled the `MainMenu`-style row layout
  (`src/Elements/ListItem.h`, white-on-white pixelbasic7 border, the
  legacy theme that pre-dated the MAKERphone reskin) alongside the
  new `PhoneMessageRow` (S31) -- specifically the empty-state
  "Add friend" CTA that fires when `Storage.Friends.all()` returns
  only the device's own efuse MAC. Every populated path already
  used `PhoneMessageRow`; only the no-contacts boot path leaked
  the legacy widget. S213 swaps the legacy `ListItem` for a
  phone-style focusable button painted with the MP_* palette --
  transparent body, 1 px MP_DIM idle frame, 2 px MP_ACCENT focused
  frame (matching the `PhoneMenuGrid` cursor halo), centred warm-
  cream pixelbasic7 "+ ADD FRIEND" label -- and drops the
  `#include "../Elements/ListItem.h"` from `InboxScreen.cpp`. The
  CTA's lv_obj_t and label are parented to `listContainer`, so the
  existing `clearList()` teardown (`lv_obj_clean(listContainer)`)
  sweeps them up on every rebuild with no extra wrapper plumbing.
  Behaviour is byte-identical: the button is still added to
  `inputGroup` with `lv_group_add_obj`, ENTER (`LV_EVENT_PRESSED`)
  still routes to `PairScreen`, and the `MP_LABEL_DIM` "You don't
  have any friends yet. / Press ENTER to pair." hint label below
  is unchanged. The `ListItem` widget itself is left intact for
  the remaining hosts (`FriendsScreen`, `GamesScreen`) that the
  v1.0 firmware still ships -- those screens are tracked
  separately and a future session can migrate them once the
  matching phone-style replacements are in place. Resolves the
  matching v1.0 polish item in `KNOWN_ISSUES.md`.

- [x] **S214** -- Factory-reset `Pet.reset()` pairing --
  the v2.1 polish item in `KNOWN_ISSUES.md` flagged that the
  Settings -> Factory reset path in `src/Screens/SettingsScreen.cpp`
  cleared `Storage.Friends`, `Storage.Convos`, `Storage.Messages`
  and `Storage.PhoneContacts`, then called `nvs_flash_erase()` and
  `Settings.reset()` before rebooting -- but the `PhoneVirtualPet`
  service that S129 had introduced was never explicitly torn down.
  In practice the `nvs_flash_erase()` call wipes every NVS
  namespace, the `mppet/p` blob included, so the next boot did read
  the pet as a fresh default; but (a) the in-memory
  `PhoneVirtualPetService::st` cache (hunger/happy/energy/age) was
  not touched, so any tail-end consumer that asked `Pet.*` between
  the prompt callback and `ESP.restart()` saw the pre-wipe stats
  for ~80 ms; and (b) the cached `s_handle` inside
  `PhoneVirtualPet.cpp` would have been left dangling against an
  erased partition if a subsequent write had landed before the
  restart. S214 closes both gaps by inserting a single `Pet.reset();`
  call into the existing `EV_PROMPT_YES` lambda, immediately after
  the `Storage.*` clears and immediately before `nvs_flash_erase()`.
  `Pet.reset()` (from `src/Services/PhoneVirtualPet.cpp`) rewrites
  the on-NVS blob to `{age=0, hunger=happy=energy=100, awake}` via
  the still-valid handle and zeros `sleepHungerTick`, so the wipe
  has a deterministic baseline to re-erase, and the in-memory
  `st` is reset alongside. The matching
  `#include "../Services/PhoneVirtualPet.h"` is added next to the
  existing `SleepService.h` include so the global `Pet` symbol is
  in scope. No new UI surface; the existing `PhoneVirtualPet`
  screen still exposes the per-pet `BTN_4` long-press reset (the
  wipe-just-the-pet path documented in
  `src/Screens/PhoneVirtualPet.h`) for users who want to start the
  Tamagotchi over without nuking their friends / messages /
  contacts. Resolves the matching v2.1 polish item in
  `KNOWN_ISSUES.md`.

- [x] **S215** -- Sample-contact toast on uid==0 CALL / MESSAGE / EDIT --
  the v1.0 polish item in `KNOWN_ISSUES.md` flagged that
  `PhoneContactsScreen` (S36) seeds a small fallback list of
  placeholder rows (uid==0) so a freshly-flashed device still
  reads as a real phone-book before any peers are paired, and
  that pressing CALL / MESSAGE / EDIT on those rows
  intentionally no-ops per the S37 / S38 contract -- but with
  no visual cue beyond the soft-key press flash, a user
  hammering the placeholder row was easy to leave wondering
  whether the firmware had hung. The v2.0 sweep at the bottom
  of `KNOWN_ISSUES.md` claimed S171 had wired the missing
  `PhoneNotificationToast`, but a `git log` audit shows S171
  actually shipped the `PhoneStressReliever` fidget toy and
  never touched the contact-detail no-op path -- so the toast
  was still missing in production. S215 closes the gap by
  growing `PhoneContactDetail` (`src/Screens/PhoneContactDetail.h`
  / `.cpp`) a public `showSampleContactToast()` helper backed
  by a lazily-built `PhoneNotificationToast` member parented
  to the screen's own `obj`, so the LV_EVENT_DELETE cascade in
  `LVObject` auto-frees the toast when the screen tears down
  (no destructor work needed -- same pattern
  `PhoneHomeScreen::birthdayToast` already uses for the S199
  birthday confetti toast). The toast slides in from above the
  status bar, holds for 2 s with the variant-Generic cyan badge,
  the warm-cream pixelbasic7 title "Sample contact" and the
  muted-cream preview "Add a real one", then slides back out
  with the existing 220 ms easing -- byte-identical animation
  cadence to every other Phone* toast caller. The three
  static handlers in `src/Elements/IntroScreen.cpp`
  (`contactDetailCall`, `contactDetailMessage`,
  `contactDetailEdit`) replace their bare `if(uid == 0) return;`
  guards with `if(uid == 0){ self->showSampleContactToast();
  return; }`, so the soft-key still flashes (per the legacy
  S37/S38 contract) but the user now also sees a "Sample
  contact / Add a real one" toast slide in from above the
  status bar. Reusing one toast instance across the three
  callers means a vigorous mash on the placeholder row spawns
  no stacked toasts -- the second `show()` cancels the first
  and replaces its content in place, the documented
  `PhoneNotificationToast::show()` overlap behaviour. Resolves
  the matching v1.0 polish item in `KNOWN_ISSUES.md`.

- [x] **S216** -- Boot-time `Settings.sound` clamp converted to a
  one-shot migration -- the v1.0 polish item in `KNOWN_ISSUES.md`
  flagged that `MAKERphone-Firmware.ino` ran an unconditional
  "`if(cfg.sound || cfg.sleepTime != 0 || cfg.shutdownTime != 0)
  -> clamp + Settings.store()`" block on every boot to migrate
  production Chatters that had carried over `sound=true` /
  `sleepTime=1` / `shutdownTime=1` from the original (non-MAKER-
  phone) firmware. The block worked for the migration but had a
  long-tail bug: it also fired on every subsequent boot, so a
  user who had legitimately enabled sound through PhoneSoundScreen
  / PhoneHapticsScreen / PhoneProfileScreen (or non-OFF sleep /
  shutdown via PhoneSleepScreen / PhoneShutdownScreen) had their
  preference silently wiped on the next power-cycle. The v2.0
  sweep at the bottom of `KNOWN_ISSUES.md` carried the item
  forward to v2.1 with the explicit "remove the override block
  in v2.1 once we are confident every shipped device has
  migrated" guidance. S216 closes the gap with a defensive one-
  shot migration: a new `bool soundOverrideMigrated = false`
  byte is appended to `SettingsData` (next to `demoSpeed`, so
  the existing NVS-resize pattern that grew the struct via
  `soundProfile` / `wallpaperStyle` / `themeId` / `keyTicks` /
  `ownerName` / `powerOffMessage` / `operatorText` /
  `operatorLogo` / `phoneProfile` / `profileRingtones` /
  `speedDial` / `rainbowUnlocked` / `softKeyTone` /
  `lockWidgetMode` / `homeLayoutMode` / `wallpaperOfDay` /
  `customAccentEnabled` / `customAccentR` / `G` / `B` /
  `ownerEmoji` / `musicPlayMode` / `alarmTone` /
  `demoSlideStart` / `demoSpeed` reads the new field as zero-
  initialised on a first boot after the firmware grows). The
  `.ino` setup() block is rewritten from
  `if(cfg.sound || cfg.sleepTime != 0 || cfg.shutdownTime != 0)`
  to `if(!cfg.soundOverrideMigrated)`, runs the legacy clamp
  exactly once, flips the flag to `true` on the same
  `Settings.store()` call, and then is skipped forever after.
  Net effect: a freshly-flashed device migrates byte-identically
  to the pre-S216 path on first boot, but every subsequent boot
  respects whatever the user has chosen on PhoneSoundScreen /
  PhoneHapticsScreen / PhoneProfileScreen / PhoneSleepScreen /
  PhoneShutdownScreen. Removing the block outright would have
  surprised any production Chatter that had not yet seen the
  v2.1 firmware, so the gated form preserves the migration
  intent without breaking the user-facing toggles. Resolves
  the matching v1.0 polish item in `KNOWN_ISSUES.md`.

- [x] **S217** -- `PhoneCallHistory` NVS-backed persistence --
  the v1.0 polish item in `KNOWN_ISSUES.md` flagged that S55's
  About screen lists the peer count, but `PhoneCallHistory` (S27)
  kept its log only as an `std::vector<Entry>` member of the
  screen object -- so the moment the screen popped, the log
  evaporated, and a power-cycle wiped it without a trace. S217
  closes the gap with a tiny new service `PhoneCallHistoryStorage`
  (`src/Services/PhoneCallHistoryStorage.h` / `.cpp`) modelled on
  `PhoneComposerStorage` (S123): a single NVS blob keyed `log`
  under namespace `mpcalls`, holding a 4-byte header (magic 'M',
  'H', version=1, count) followed by up to `MaxEntries` x 40-byte
  fixed-stride entries packing `type`, the 25-byte name buffer,
  the 9-byte timestamp buffer, the 4-byte little-endian
  `durationSeconds` and the 1-byte `avatarSeed`. The 40-byte
  per-entry stride is pinned by a `static_assert` against
  `PhoneCallHistory::MaxNameLen` and `MaxTsLen`, so a future
  tweak to the entry geometry trips a build error rather than
  a silent on-disk format drift. Worst-case blob size is
  `4 + 32*40 = 1284` bytes which fits comfortably alongside the
  existing `Settings`, `Highscore`, `mpcomp` and `mppet`
  namespaces.

  Concretely S217: (i) ships `PhoneCallHistoryStorage` with the
  static `begin() / hasLog() / loadAll() / saveAll() / clearLog()`
  surface and a single shared NVS handle opened lazily on first
  touch (matching the Highscore / PhoneComposerStorage pattern);
  (ii) grows `PhoneCallHistory` two private flags
  (`demoOnly`, `duringSeed`) and two private helpers
  (`loadFromStorage()`, `saveToStorage()`) so the screen's
  constructor reads "try `loadFromStorage()`, fall back to
  `seedSampleEntries()` only when the persisted log is empty";
  the demo set still populates the visible list on a freshly-
  flashed device but is never written to NVS, and the first
  real `addEntry()` after the demo set lands wipes the
  placeholder list (`demoOnly = true` -> `false`) before the
  new entry is appended; (iii) wires `addEntry()` to call
  `saveToStorage()` after each mutation (gated by `duringSeed`
  / `demoOnly` so the seed run never persists), and
  `clearEntries()` to call `PhoneCallHistoryStorage::clearLog()`
  so a deliberate wipe propagates to NVS. Stack budget on the
  load path is the worst-case blob size (~1.3 KiB scratch
  buffer in `loadFromStorage()`), well inside the ESP32 main-
  task headroom.

  Visible output is byte-identical on a freshly-flashed device:
  the screen still seeds with the same eight demo entries on
  first push (because `loadAll()` returns 0 against an empty
  `mpcalls/log` blob and the constructor falls back to
  `seedSampleEntries()`). The only behavioural delta is that
  once a future S28-style host pushes a real entry through
  `addEntry()` the log now survives a power-cycle: the next
  boot reads the persisted ring back into `entries` via
  `loadFromStorage()` and skips the demo set entirely. Resolves
  the matching v1.0 polish item in `KNOWN_ISSUES.md`.

- [x] **S218** -- `PhoneBeatMaker` NVS-backed pattern + BPM
  persistence -- the v2.0 sweep in `KNOWN_ISSUES.md` flagged that
  the BeatMaker (S194) is currently a transient toy: the
  `pattern[NumTracks][NumSteps]` grid and the `bpm` byte live
  only on the screen object, so popping the screen (or
  power-cycling the device) wipes the user's groove without a
  trace. S218 closes that gap with the same lightweight NVS
  pattern that already backs `PhoneComposerStorage` (S123) and
  `PhoneCallHistoryStorage` (S217): a tiny new service
  `PhoneBeatMakerStorage` (`src/Services/PhoneBeatMakerStorage.h`
  / `.cpp`) holding a single 16-byte blob keyed `pat` under
  namespace `mpbeat`, with a 4-byte header (magic 'M','B',
  version=1, reserved), a 1-byte `bpm`, three reserved bytes
  for forward compatibility, and an 8-byte track-major packed
  pattern bitfield (4 tracks x 16 steps = 64 bits). The byte
  stride and the grid geometry are pinned by `static_assert`s
  against `PhoneBeatMaker::NumTracks` / `NumSteps` / the derived
  `kPatternBytes` so a future tweak to the grid trips a build
  error rather than a silent on-disk format drift.

  Concretely S218: (i) ships `PhoneBeatMakerStorage` with the
  static `begin() / hasSaved() / loadInto() / save() / clear()`
  surface and a single shared NVS handle opened lazily on first
  touch (matching the Highscore / PhoneComposerStorage /
  PhoneCallHistoryStorage pattern); (ii) grows `PhoneBeatMaker`
  one private bool flag (`dirty`, default false) and two private
  helpers (`loadFromStorage()`, `saveToStorage()`); (iii) rewires
  the constructor body from an unconditional `seedDefaultPattern()`
  to "try `loadFromStorage()`, fall back to `seedDefaultPattern()`
  only when the persisted blob is empty" -- the byte-identical
  pre-S218 boom-tss-bap-tss seed and the DefaultBpm still light
  the grid on a freshly-flashed device because `loadInto()`
  returns false against an empty `mpbeat/pat` blob; (iv) wires
  `toggleCell()`, `clearPattern()` and the value-actually-changed
  branch of `nudgeBpm()` to flip `dirty = true` so a visit that
  only played the demo back never burns NVS write budget; (v)
  has `onStop()` call `saveToStorage()` (and reset `dirty`)
  immediately after the existing transport / envelope teardown,
  so the user's edits land in NVS exactly when the screen pops
  to BACK -- a single write per pop, not one per keypress, which
  is what the NVS wear-leveller is happiest with.

  Visible output is byte-identical on a freshly-flashed device:
  the grid still paints with the canonical four-on-the-floor
  groove on first push, and the BPM still reads 120. The only
  behavioural delta is that once the user toggles a cell, nudges
  the tempo or hits BTN_5 to clear, those edits now survive a
  power-cycle: the next push of `*#808#` from the dialer reads
  the persisted blob back into `pattern` + `bpm` via
  `loadFromStorage()` and skips the seed groove entirely. The
  on-disk envelope is the legacy clamp range (60..240 BPM) so a
  corrupt blob can never park an out-of-range value the screen
  would then have to defend against. Resolves the v2.0-fresh
  polish item in `KNOWN_ISSUES.md` (the `Settings.beatPatterns[4]`
  description there assumed the screen already persisted to
  Settings; the actual fix is a free-standing storage service in
  the same family as S217, with a single auto-saved slot rather
  than a four-slot picker UI).

- [x] **S219** -- `PhoneComposer` SILENT-profile preview gate -- the
  v2.1 sweep noted that the composer's BTN_9 preview path
  (`PhoneComposer::togglePreview()`) calls
  `PhoneComposerPlayback::play()` (which in turn calls
  `Ringtone.play()`) without first consulting the active phone
  profile. The `PhoneRingtoneEngine` does mute itself per-loop when
  `Settings.get().sound == false` (`PhoneRingtoneEngine.cpp` lines
  60-83), but the engine still subscribes as a `LoopManager`
  listener and the screen still flips its hint to "STP" -- so under
  SILENT / MEETING the user sees a stopwatch start with no audio
  and no visible explanation, exactly the failure mode S205 fixed
  for `PhoneRadio` (the FM dial). S219 closes the gap with the
  same minimal pattern.

  Concretely S219: (i) lifts a static
  `PhoneComposer::isSilenced()` helper into
  `src/Screens/PhoneComposer.{h,cpp}` that reads
  `!Settings.get().sound` (the legacy bool that
  `PhoneProfileScreen` (S159) writes to `false` for SILENT and
  MEETING and `true` for GENERAL / OUTDOOR / HEADSET, so the
  helper covers every "should the composer drive the piezo right
  now" case without dragging the five-state enum into this
  screen); (ii) inserts an early-return into
  `togglePreview()` immediately after the empty-buffer guard so
  the silent path runs `PhoneComposerPlayback::stop()` (defensive,
  in case a stale engine playhead is still ticking from a profile
  flip mid-preview), flashes the LEFT softkey (matching the
  existing empty-buffer / play-rejected feedback so the gesture
  still feels acknowledged), and hands off to `refreshHints()`
  without ever asking the engine to drive the piezo; (iii)
  rewires `refreshHints()` to surface a "MUT" token in the bottom
  hint line in place of the usual "PLY" when `isSilenced()` is
  true and no preview is in flight, so the no-op flash from
  `togglePreview()` is self-explanatory. The token width stays
  at 3 glyphs so the existing `S%u%s 9=%s 0=SV A=LD` hint string
  still fits inside the 148 px row width with margin to spare.

  Visible output is byte-identical for non-silenced profiles --
  the hint reads `PLY` -> `STP` -> `PLY` exactly as before, and
  every other gesture (SAVE / LOAD / slot-cycle / clear / stamp
  edit) is untouched. The only behavioural delta lands when the
  user has flipped to SILENT or MEETING via `PhoneProfileScreen`:
  the hint reads `MUT`, BTN_9 produces the expected soft-key
  flash without any piezo interaction, and a flip back to
  GENERAL / OUTDOOR / HEADSET re-arms the preview on the next
  press without a screen rebuild. The new include is
  `<Settings.h>`; no header surface changes beyond the public
  static helper. Resolves the v2.1-fresh polish item now logged
  in `KNOWN_ISSUES.md`.

- [x] **S220** -- `PhoneMusicPlayer` SILENT-profile gate -- the v2.1
  sweep logged in `KNOWN_ISSUES.md` flagged that the music player
  (S42 / S43 / S189-S191) was the last in-app screen with an
  un-gated `Ringtone.play()` call after S205 closed `PhoneRadio`
  and S219 closed `PhoneComposer`. `PhoneMusicPlayer::play()`
  drove the engine unconditionally on every PLAY press, and the
  end-of-track auto-advance hook in `onTick()` polled
  `Ringtone.isPlaying()` -- which, since S39's `PhoneRingtoneEngine`
  self-mutes per-loop when `Settings.get().sound == false`, still
  attached the loop listener to `LoopManager` for the millisecond
  between play() and the engine's first mute tick (audible click
  on some Chatter units) and then -- under the SILENT / MEETING
  profile -- left the screen visually playing while the user heard
  nothing and saw no explanation. S220 closes the gap with the
  same minimal pattern S205 / S219 use.

  Concretely S220: (i) lifts a static
  `PhoneMusicPlayer::isSilenced()` helper into
  `src/Screens/PhoneMusicPlayer.{h,cpp}` that reads
  `!Settings.get().sound` (the legacy bool that
  `PhoneProfileScreen` (S159) writes to `false` for SILENT and
  MEETING and `true` for GENERAL / OUTDOOR / HEADSET, so the
  helper covers every "should the music player drive the piezo
  right now" case without dragging the five-state enum into this
  screen); (ii) inserts an early-return into `play()` that runs a
  defensive `Ringtone.stop()` (in case a stale engine playhead is
  still ticking from a profile flip mid-track), sets a new
  private `silentPlayback = true` flag, flips `playing = true`
  and primes `playStartMs` / `pausedAtMs` / `trackTotalMs`
  exactly as the audible path does so the progress bar still
  ticks across the wall-clock duration of the track; (iii) holds
  the equalizer (S191) at `setActive(false)` in the silenced path
  so the dancing bars never imply audio is happening when it is
  not; (iv) rewires `onTick()` to bypass the
  `!Ringtone.isPlaying()` end-of-track check when `silentPlayback`
  is set and instead detect end-of-track from
  `currentElapsedMs() >= trackTotalMs` -- so a silent session
  hands off to the same `advanceForEndOfTrack()` mode-gate
  (Continuous / RepeatAll / RepeatOne / Shuffle) at the same
  point in the timeline an audible session would; (v) repurposes
  the S190 `MODE: ...` mode-caption strip into a `MUTED -- SOUND
  OFF` badge while `silentPlayback && playing`, painted in the
  existing MP_HIGHLIGHT cyan so the badge reads as a deliberate
  state rather than a stuck label; (vi) clears the flag on every
  pause / setTracks / Continuous-final-track / onStop / dtor /
  BTN_BACK exit so a profile flip from SILENT -> GENERAL between
  tracks is picked up on the next `play()` without a stale flag
  dragging the next session into silent-mode behaviour.

  Visible output is byte-identical for non-silenced profiles --
  the existing PLAY / PAUSE icon, the progress bar, the
  equalizer dancing, the soft-key labels, and the `MODE: ...`
  caption are all unchanged. The only behavioural delta lands
  when the user has flipped to SILENT or MEETING via
  `PhoneProfileScreen`: the player still flips to "playing"
  visually (play icon shows pause-bars, soft-key reads PAUSE,
  progress bar advances, BTN_LEFT / BTN_RIGHT skip tracks, the
  S190 mode toggle still cycles), the badge reads `MUTED --
  SOUND OFF`, the equalizer stays still, and the engine never
  drives the piezo until the user changes profile back. The
  legacy include `<Settings.h>` is already in the .cpp; no new
  surface area beyond the public `static bool isSilenced()` and
  the private `bool silentPlayback` member. Resolves the
  v2.1-fresh polish item now logged in `KNOWN_ISSUES.md`.

- [x] **S221** -- `PhoneAlarmTonePicker` SILENT-profile preview
  gate -- the v2.1 sweep continues across every in-app screen with
  an un-gated `Ringtone.play()` call. After S205 closed `PhoneRadio`,
  S219 closed `PhoneComposer` and S220 closed `PhoneMusicPlayer`,
  `PhoneAlarmTonePicker::startPreview()` was the last picker / sound
  surface that drove the engine unconditionally on BTN_ENTER. The
  engine self-mutes per-loop via `Settings.get().sound`, but the
  loop listener still attaches to LoopManager for the millisecond
  between `play()` and the engine's first mute tick (audible click
  on some Chatter units), and under SILENT / MEETING the screen
  left the user staring at a "previewing" highlight with no audio
  and no visible explanation -- exactly the failure mode S205 fixed
  for the FM dial and S220 fixed for the music player.

  S221 closes the gap with the same minimal pattern: (i) lifts a
  static `PhoneAlarmTonePicker::isSilenced()` helper into
  `src/Screens/PhoneAlarmTonePicker.{h,cpp}` that reads
  `!Settings.get().sound`; (ii) inserts an early-return into
  `startPreview()` that runs a defensive `Ringtone.stop()` (in case
  a stale engine playhead is still ticking from a profile flip
  mid-preview), still flips `previewing = true` so a second
  BTN_ENTER tap stops cleanly through the regular `stopPreview()`
  path, still flashes the soft-key for gesture acknowledgement,
  and skips the `Ringtone.play()` call entirely; (iii) repurposes
  the existing `ALARM TONE` caption strip into a `MUTED -- SOUND
  OFF` badge while a silenced preview is "live", painted in the
  same MP_HIGHLIGHT cyan the regular caption uses so the badge
  reads as a deliberate state change rather than a stuck label;
  (iv) reverts the badge on `stopPreview()` and on every
  non-silenced `startPreview()` path so a profile flip from
  SILENT -> GENERAL between previews is picked up without a stale
  caption dragging the next preview into silent-mode appearance.
  `confirmPick()` and `invokeBack()` already route through
  `stopPreview()` so the badge never survives screen-pop, and the
  caption width stays inside the 160 px screen with margin to
  spare (pixelbasic7 at 18 chars ~= 90 px).

  Visible output is byte-identical for non-silenced profiles --
  the existing cursor highlight, saved-dot markers, soft-key
  flash, and "ALARM TONE" caption are all unchanged. The only
  behavioural delta lands when the user has flipped to SILENT or
  MEETING via `PhoneProfileScreen`: BTN_ENTER still flips the row
  to "previewing", BTN_ENTER again still stops it, the soft-key
  still flashes, but the caption reads `MUTED -- SOUND OFF` and
  the engine never drives the piezo until the user changes
  profile back. The new include is `<Settings.h>`; no header
  surface changes beyond the public `static bool isSilenced()` and
  the private `void setMutedCaption(bool)` helper. Resolves the
  v2.1-fresh polish item now logged in `KNOWN_ISSUES.md`.

- [x] **S222** -- `PhoneContactRingtonePicker` SILENT-profile
  preview gate -- the v2.1 sweep finishes its picker pass. After
  S205 closed `PhoneRadio`, S219 closed `PhoneComposer`, S220
  closed `PhoneMusicPlayer` and S221 closed `PhoneAlarmTonePicker`,
  `PhoneContactRingtonePicker::startPreview()` was the remaining
  per-contact picker that drove the engine unconditionally on
  BTN_ENTER. The S221 wording said `PhoneAlarmTonePicker` was the
  last picker / sound surface; that overlooked this screen and
  `PhoneProfileRingtoneScreen` (the latter is the candidate for the
  next pass). The engine self-mutes per-loop via
  `Settings.get().sound`, but the loop listener still attaches to
  LoopManager for the millisecond between `play()` and the engine's
  first mute tick (audible click on some Chatter units), and under
  SILENT / MEETING the screen left the user staring at a
  "previewing" highlight with no audio and no visible explanation
  -- exactly the failure mode S205 fixed for the FM dial and S221
  fixed for the alarm-tone picker.

  S222 closes the gap with the same minimal pattern: (i) lifts a
  static `PhoneContactRingtonePicker::isSilenced()` helper into
  `src/Screens/PhoneContactRingtonePicker.{h,cpp}` that reads
  `!Settings.get().sound`; (ii) inserts an early-return into
  `startPreview()` that runs a defensive `Ringtone.stop()` (in case
  a stale engine playhead is still ticking from a profile flip
  mid-preview), still flips `previewing = true` so a second
  BTN_ENTER tap stops cleanly through the regular `stopPreview()`
  path, still flashes the soft-key for gesture acknowledgement,
  and skips the `Ringtone.play()` call entirely; (iii) repurposes
  the existing `RINGTONE` caption strip into a `MUTED -- SOUND
  OFF` badge while a silenced preview is "live", painted in the
  same MP_HIGHLIGHT cyan the regular caption already uses so the
  badge reads as a deliberate state change rather than a stuck
  label; (iv) reverts the badge on `stopPreview()` and on every
  non-silenced `startPreview()` path so a profile flip from
  SILENT -> GENERAL between previews is picked up without a stale
  caption dragging the next preview into silent-mode appearance.
  `confirmPick()` and `invokeBack()` already route through
  `stopPreview()` so the badge never survives screen-pop, and the
  caption width stays inside the 160 px screen with margin to
  spare (pixelbasic7 at 18 chars ~= 90 px).

  Visible output is byte-identical for non-silenced profiles --
  the existing cursor highlight, saved-dot markers, soft-key
  flash, and `RINGTONE` caption are all unchanged. The only
  behavioural delta lands when the user has flipped to SILENT or
  MEETING via `PhoneProfileScreen`: BTN_ENTER still flips the row
  to "previewing", BTN_ENTER again still stops it, the soft-key
  still flashes, but the caption reads `MUTED -- SOUND OFF` and
  the engine never drives the piezo until the user changes
  profile back. The new include is `<Settings.h>`; no header
  surface changes beyond the public `static bool isSilenced()` and
  the private `void setMutedCaption(bool)` helper. Resolves the
  v2.1-fresh polish item now logged in `KNOWN_ISSUES.md`.

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
