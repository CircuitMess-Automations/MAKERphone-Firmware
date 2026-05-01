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
- [ ] **S81** — `PhoneTicTacToe` — vs CPU.
- [ ] **S82** — `PhoneMemoryMatch` — pixel-icon pairs, count-to-clear timer.
- [ ] **S83** — `PhoneSokoban` — engine + 5 hand-built levels.
- [ ] **S84** — `PhoneSokoban+` (split) — 10 more levels + level-select grid.
- [ ] **S85** — `PhonePinball` — single-table flippers + bumpers.
- [ ] **S86** — `PhonePinball+` (split) — second table + leaderboard.
- [ ] **S87** — `PhoneHangman` — uses existing T9 input + inline word list.
- [ ] **S88** — `PhoneConnectFour` — vs CPU.
- [ ] **S89** — `PhoneReversi` — Othello vs CPU.
- [ ] **S90** — `PhoneWhackAMole` — dialer-key reaction game.
- [ ] **S91** — `PhoneLunarLander` — fuel/thrust physics.
- [ ] **S92** — `PhoneHelicopter` — endless side-scrolling avoidance.
- [ ] **S93** — `Phone2048`.
- [ ] **S94** — `PhoneSolitaire` — Klondike, dialer-driven column select.
- [ ] **S95** — `PhoneSudoku` — three difficulty packs, generated.
- [ ] **S96** — `PhoneWordle` — daily 5-letter guess.
- [ ] **S97** — `PhoneSimon` — memory + buzzer-tone Simon Says.
- [ ] **S98** — `PhoneSnakesLadders` — vs CPU.
- [ ] **S99** — `PhoneAirHockey` — vs CPU, single-screen.
- [ ] **S100** — `PhoneTowerDefence` — single lane, 8 waves.

## Phase O — Themes & Icon Sets (S101–S120)
Two sessions per theme: **part 1 = palette + wallpaper variant**, **part 2 = matching icon glyphs + soft-key tint + lock-screen accent**. All themes pickable from `Settings → Theme`.

- [ ] **S101** — Theme: Nokia 3310 Monochrome — palette + wallpaper.
- [ ] **S102** — Theme: Nokia 3310 Monochrome — icon glyphs + accents.
- [ ] **S103** — Theme: Game Boy DMG (4-shade green) — palette + wallpaper.
- [ ] **S104** — Theme: Game Boy DMG — icon glyphs + accents.
- [ ] **S105** — Theme: Amber CRT — palette + wallpaper.
- [ ] **S106** — Theme: Amber CRT — icon glyphs + accents.
- [ ] **S107** — Theme: Sony Ericsson Aqua — palette + wallpaper.
- [ ] **S108** — Theme: Sony Ericsson Aqua — icon glyphs + accents.
- [ ] **S109** — Theme: RAZR Hot Pink — palette + wallpaper.
- [ ] **S110** — Theme: RAZR Hot Pink — icon glyphs + accents.
- [ ] **S111** — Theme: Stealth Black — palette + wallpaper.
- [ ] **S112** — Theme: Stealth Black — icon glyphs + accents.
- [ ] **S113** — Theme: Y2K Silver — palette + wallpaper.
- [ ] **S114** — Theme: Y2K Silver — icon glyphs + accents.
- [ ] **S115** — Theme: Cyberpunk Red — palette + wallpaper.
- [ ] **S116** — Theme: Cyberpunk Red — icon glyphs + accents.
- [ ] **S117** — Theme: Christmas / Festive — palette + wallpaper.
- [ ] **S118** — Theme: Christmas / Festive — icon glyphs + accents.
- [ ] **S119** — Theme: Surprise / Daily-Cycle — engine + palette.
- [ ] **S120** — Theme: Surprise / Daily-Cycle — rotation logic + icon variants.

## Phase P — More Apps (S121–S135)
Composer is broken into three sessions because it needs (a) keypad note-entry UI, (b) RTTTL parser, (c) save-slot wiring to `PhoneRingtoneEngine`. Tamagotchi-style virtual pet adds persistent state via the existing `Storage` layer.

- [ ] **S121** — `PhoneComposer` — keypad note-entry UI.
- [ ] **S122** — `PhoneComposer` — RTTTL parser + serializer.
- [ ] **S123** — `PhoneComposer` — save slots + wire to `PhoneRingtoneEngine`.
- [ ] **S124** — `PhoneAlarmClock` — multi-alarm with snooze.
- [ ] **S125** — `PhoneTimers` — multi-timer countdown list (extends S62).
- [ ] **S126** — `PhoneCurrencyConverter` — offline rate table, two-column converter.
- [ ] **S127** — `PhoneUnitConverter` — length / mass / temperature / volume.
- [ ] **S128** — `PhoneWorldClock` — six-zone clock grid.
- [ ] **S129** — `PhoneVirtualPet` — Tamagotchi-style feed/sleep/play, persists hunger/age across reboots.
- [ ] **S130** — `PhoneMagic8Ball` — shake + reveal answer.
- [ ] **S131** — `PhoneDiceRoller` — 1d4 → 2d20.
- [ ] **S132** — `PhoneCoinFlip` — gravity-style flip animation.
- [ ] **S133** — `PhoneFortuneCookie` — daily wisdom from inline string list.
- [ ] **S134** — `PhoneFlashlight` — full-white screen + brightness max.
- [ ] **S135** — `PhoneBirthdayReminders` — list + per-contact birthday field.

## Phase Q — Pocket Organiser (S136–S143)
Sony Ericsson "Organiser" classics — practical apps with offline-only state. Each persists through the existing `Storage` layer so a paused habit streak still counts on reboot.

- [ ] **S136** — `PhoneTodo` — task list with three priority levels + tick-off animation.
- [ ] **S137** — `PhoneHabits` — five-habit daily tracker, streak counter, weekly heatmap.
- [ ] **S138** — `PhonePomodoro` — work/break cycle timer, configurable durations, cycle counter.
- [ ] **S139** — `PhoneMoodLog` — one-tap-per-day mood journal, 30-day strip view.
- [ ] **S140** — `PhoneScratchpad` — instant quick-jot pad (one buffer, separate from S64 Notepad's saved list).
- [ ] **S141** — `PhoneExpenses` — running daily/weekly tally with category tags.
- [ ] **S142** — `PhoneCountdown` — days-until-event widget (multiple events, sortable by closeness).
- [ ] **S143** — `PhoneSteps` — mock pedometer (uptime-derived) with daily goal + streak.

## Phase R — Nostalgia Layer (S144–S163)
Tiny details that make the difference between "phone-shaped toy" and "this is exactly how my Nokia did it".

- [ ] **S144** — Owner name on lock screen (T9 entry in Settings).
- [ ] **S145** — Custom welcome greeting on boot ("Hello, $NAME!").
- [ ] **S146** — Custom power-off message.
- [ ] **S147** — Operator-logo banner — text + 5×16 user-pixelable logo.
- [ ] **S148** — Boot melody (Sony-Ericsson-style four-note chime).
- [ ] **S149** — Upgraded power-off melody (real descending arpeggio, replaces the S57 placeholder).
- [ ] **S150** — Charge-complete chime (one-shot on charger transition).
- [ ] **S151** — Speed dial 1–9 — long-press digit on home dials that contact.
- [ ] **S152** — Birthday confetti animation when system date matches a contact (depends on S135).
- [ ] **S153** — Per-contact custom ringtone (depends on S121–S123 composer output).
- [ ] **S154** — "Press any key" idle hint that fades in after 10 s of stillness.
- [ ] **S155** — Animated battery-charge fill bars on lock + home.
- [ ] **S156** — Envelope-flying SMS-sent animation.
- [ ] **S157** — "Delivered" double-tick chime.
- [ ] **S158** — "Missed call" inverted-color flash on next wake.
- [ ] **S159** — Profile system (General / Silent / Meeting / Outdoor / Headset).
- [ ] **S160** — Per-profile ringtone selection (depends on S159 + S123).
- [ ] **S161** — Vibration patterns per ringtone (buzzer-pulse choreography).
- [ ] **S162** — SIM-card "PIN unlock" boot screen — decorative four-digit PIN entry.
- [ ] **S163** — "Phone yawns" idle animation — eyes blink on home after 5 min idle.

## Phase S — Quirks & Easter Eggs (S164–S178)
The kind of thing customers post about when they discover it.

- [ ] **S164** — `*#06#` on dialer → fake IMEI reveal screen.
- [ ] **S165** — `*#0000#` → firmware-info screen (Sony-Ericsson code).
- [ ] **S166** — Konami code on the d-pad → unlocks rainbow theme.
- [ ] **S167** — Long-press `5` on dialer → flashlight quick-shortcut.
- [ ] **S168** — Tilt simulator (hold L+R together) → shake-to-randomize current screen.
- [ ] **S169** — Random tip-of-the-day banner on home idle.
- [ ] **S170** — ASCII art slideshow viewer — eight hand-pixeled drawings.
- [ ] **S171** — `PhoneStressReliever` — tap a thing repeatedly, watch reactions.
- [ ] **S172** — Daily fortune in dialer (first open per day).
- [ ] **S173** — Type "S-N-A-K-E" on dialer → instant Snake launch.
- [ ] **S174** — Find-Friends radar mini — animated sweep over the friends list.
- [ ] **S175** — T9 vocab pack expansion (more dictionary words).
- [ ] **S176** — Drum kit on dialer keypad — each digit a different drum.
- [ ] **S177** — Boot phrase rotates each day (motivational quotes).
- [ ] **S178** — Day-of-week themed wallpaper accents.

## Phase T — Personalisation Deep Dive (S179–S188)
Beyond themes — let users actually make the phone *theirs*.

- [ ] **S179** — Theme picker — live preview while scrolling.
- [ ] **S180** — Custom contact ringtone (per friend, depends on S123).
- [ ] **S181** — Custom contact wallpaper (per friend).
- [ ] **S182** — Avatar editor — pixel-paint mini editor (32×32, replaces the S38 picker).
- [ ] **S183** — Soft-key tone customisation.
- [ ] **S184** — Lock screen widget picker (clock-only / clock+date / clock+next-event).
- [ ] **S185** — Home screen layout switcher.
- [ ] **S186** — Wallpaper of the day — daily auto-rotate from a curated list.
- [ ] **S187** — Custom RGB picker for accent color (real-time preview).
- [ ] **S188** — Owner emoji / avatar selection (used on lock + status bar).

## Phase U — Audio / Music (S189–S196)
Buzzer is what we have, so we lean into it.

- [ ] **S189** — Music player — playlist support.
- [ ] **S190** — Music player — shuffle / repeat / continuous.
- [ ] **S191** — Equalizer visualiser — bars dance to active melody.
- [ ] **S192** — Richer system-tone library — 15+ chimes for every action.
- [ ] **S193** — Custom buzzer alarm tone (composer-fed).
- [ ] **S194** — `PhoneBeatMaker` — 16-step drum sequencer.
- [ ] **S195** — `PhoneRadio` — fake FM dial with eight stations of pre-canned melody loops.
- [ ] **S196** — Karaoke title-display mode while a melody plays.

## Phase V — Final Polish & v2.0 Release (S197–S200)

- [ ] **S197** — Memory-leak audit — push/pop every screen 1000× in test mode, heap must stay flat.
- [ ] **S198** — Battery-life pass + idle-dim tuning + LVGL-cost measurement.
- [ ] **S199** — Final QA — exercise every flow, fix critical regressions in the same commit.
- [ ] **S200** — v2.0 changelog + KNOWN_ISSUES sweep + auto-cycling demo mode for the marketing video.

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
