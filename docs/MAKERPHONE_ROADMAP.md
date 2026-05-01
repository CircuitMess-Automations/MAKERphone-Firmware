# MAKERphone 2.0 — 70-Session Roadmap

This is the sequenced build plan that the autonomous `makerphone-firmware-improve` agent follows. Each numbered session is sized to ship in **one** automated run — small enough to compile cleanly in a single attempt, meaningful enough to feel like real progress toward the retro feature-phone experience.

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
- [ ] **S61** — `PhoneStopwatch` app (start/stop/lap, mm:ss.cs).
- [ ] **S62** — `PhoneTimer` app (count-down with buzzer ringtone on zero).
- [ ] **S63** — `PhoneCalendar` app (month grid, today highlight, day detail).
- [ ] **S64** — `PhoneNotepad` app (T9 entry + saved notes list).
- [ ] **S65** — Integrate existing Snake / Pong / SpaceInvaders / SpaceRocks behind a phone-style `PhoneGames` grid.

## Phase M — Final Polish & QA (S66–S70)

- [ ] **S66** — Generic page-transition animation helper (slide left/right, fade, used everywhere).
- [ ] **S67** — Soft-key labels become context-sensitive everywhere — every screen wires its own L/R captions.
- [ ] **S68** — Subtle haptic-style buzzer ticks on key navigation (very short, very quiet, toggle in Settings).
- [ ] **S69** — Idle dimming + sleep behavior (auto-dim after 30s, sleep after 2min, wake on any key).
- [ ] **S70** — End-to-end UX QA pass — exercise every screen flow, file a bug list as `KNOWN_ISSUES.md`, fix all critical regressions in the same commit, and write a v1.0 changelog.

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
