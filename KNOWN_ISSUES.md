# MAKERphone 2.0 — KNOWN_ISSUES (v1.0 / S70 QA pass)

This file is the punch-list produced by the S70 end-to-end UX QA pass
over sessions S01–S69. Every entry is a "should-fix-before-v2.0"
ticket the agent can claim from a future session; **none are critical
regressions** that block the v1.0 build (CI is green, every Phase A–M
screen reaches its happy path).

The list is intentionally short and concrete: it captures things a real
user would notice on hardware, not micro-cosmetic wishlist items.

Status legend: `[ ]` open, `[x]` fixed in this commit.

---

## Critical / regressions

None observed in the S01–S69 work. Every screen reached from the menu
returns cleanly via `pop()`, the LoRa / Messaging / Pairing flow still
works (S28 wired the call service on top of the existing
`MessageService`, no fields were renamed), and the boot path
`PhoneBootSplash → IntroScreen → LockScreen → PhoneHomeScreen` lands
on a usable home screen on a fresh device.

## Polish / minor

- [x] **Default settings stub label says "SETTING" instead of "SETTINGS".**
  `src/Elements/IntroScreen.cpp`, the `PhoneSettingsScreen::Item`
  default-case fallthrough. Only user-visible if a future enum row is
  added without an explicit handler. Fixed in this commit.

- [ ] **Boot-service init order has a small race window for ringtones.**
  `Phone.begin()` (PhoneCallService) subscribes to the message pipeline
  before `Buzz.begin()` / `Ringtone.begin()` have fired (those run
  after the `IntroScreen` callback completes, ~3 s into boot). A
  `CALL_REQUEST` payload that lands during the splash + intro window
  would raise `PhoneIncomingCall` but with a silent ringtone. Real-world
  rare — a paired peer would have to ring within the first ~3 s of
  power-on — but worth flipping the order in a follow-up so the
  ringtone engine is initialised before the call subscription is active.

- [ ] **`PhoneAppStubScreen` reachable from the Settings default-case.**
  Today the stub's appName is "SETTINGS" (post-fix above), but the code
  path itself only fires if a future `PhoneSettingsScreen::Item` enum
  value is added without being wired in `IntroScreen.cpp`. Worth
  converting into a compile-time `static_assert` over the enum size so
  the branch becomes unreachable instead of a soft runtime fallback.

- [ ] **Sample-row contacts (uid==0) silently no-op CALL / MESSAGE / EDIT.**
  Intentional (S37/S38) but the user gets no visual cue. Add a
  `PhoneNotificationToast` "Sample contact — add a real one" so the
  feedback is explicit.

- [x] **`loadMock()` is dead code** in `MAKERphone-Firmware.ino` — fixed
  in S201. The `MAKERPHONE_LOAD_MOCK_DATA` build flag (default 0) now
  lives in `src/MAKERphoneConfig.h`; the helpers and the `Chatters[]`
  table compile out unless a developer flips the flag via
  `-DMAKERPHONE_LOAD_MOCK_DATA=1` in arduino-cli, removing the need to
  edit the .ino just to seed dev data.

- [ ] **`Settings.sound = false` override on every boot.** `setup()`
  forcibly mutes the device on boot for the prototype, which means the
  user-facing sound toggle in `PhoneSoundScreen` (S52) writes Settings
  but is overridden on the next reboot. The block is tagged with a
  comment explaining the migration; Phase J was supposed to make the
  toggle real. Remove the override block once we are ready to commit
  to "Sound setting is authoritative".

- [ ] **`InboxScreen` still compiles the legacy `MainMenu`-style row
  layout** alongside the new `PhoneMessageRow` (S31). The new row is
  the path the rest of the firmware uses; delete the old class after
  one more verification pass.

- [ ] **No persisted "owner name" / call-history limit.** S55's About
  screen lists the peer count, but `PhoneCallHistory` (S27) keeps an
  in-memory ring buffer that is wiped on every reboot. Phase R session
  S144 (owner name) and a Storage-backed history move are tracked there.

## Cosmetic

- [ ] `PhoneSoftKeyBar` flash duration is ~80 ms which feels slightly
  short on the dimmed state set by `PhoneIdleDim` — the flash can be
  invisible if the user presses a softkey within the dim-fade window.
  Lengthen to 120 ms or wake the dim before flashing.

- [ ] `PhoneClockFace` redraws the seconds dot at 1 Hz, which causes a
  one-pixel cyan ghost on some panels (LovyanGFX rounding). Switch the
  dot to a 2×2 block or rate-limit the redraw to 0.5 Hz.

- [ ] `PhoneSynthwaveBg` star-twinkle pseudo-RNG is reseeded on every
  screen mount, so the same constellation reappears. Seed off
  `millis()` so each home-screen entry feels fresh.

- [ ] Long T9 entries in `PhoneNotepad` overrun the 1-line caret hint on
  very narrow notes (≤ 8 chars). Truncate the hint to fit.

## Hardware-only (cannot reproduce in CI)

- [ ] **Battery-low modal trigger threshold (S58, ≤15 %)** has not been
  exercised on a real device — the simulator path drives
  `Battery.getPercentage()` through a stub. Tune the threshold and the
  chirp loudness on hardware before v2.0.

- [ ] **`PhoneCameraScreen` shutter sound clipping.** The 3-note click
  through `PhoneRingtoneEngine` clips on some Chatter units when the
  Piezo is mid-ringtone. Add a tiny "duck" of the active ringtone
  during the shutter chirp.

- [ ] **`PhoneIdleDim` floor `DIM_FLOOR` (~16) reads as black on units
  with a partially-failed backlight LED.** Bump to ~32 once we have
  hardware test data.

---

## Out of scope for v1.0

The v2.0 roadmap (S71–S200) covers everything below intentionally —
listing here only so the QA pass notes they are not bugs in v1.0:

- Phase N games (S71–S100) — only Snake / Pong / SpaceInvaders /
  SpaceRocks ship in v1.0.
- Phase O themes (S101–S120) — single MAKERphone palette in v1.0.
- Phase P–U organisers / nostalgia / quirks / personalisation /
  audio-deep-dive apps — v1.0 ships utility apps S60–S65 only.

When v2.0 lands these stop being "out of scope" and the matching
sessions tick off the roadmap.

---

# v2.0 sweep (S200)

The S200 audit walked the full Phase N–V surface that grew the v1.0
Sunset build into the 200-session v2.0 release. Headline finding: no
critical regressions, every Phase N–V screen reaches its happy path,
the legacy boot path (`MAKERPHONE_USE_HOMESCREEN=0`) and the legacy
T9 composer fallback (`MAKERPHONE_USE_T9_COMPOSER=0`) both still
compile, and the entire batch of v1.0 polish items above is either
fixed-on-the-side by a later session or carried forward as opt-in
polish for v2.1.

## Status of v1.0 polish items

- **Boot-service init order race for ringtones** — fixed. S148 promoted
  `Ringtone.begin()` ahead of the IntroScreen dismiss callback so the
  ringtone engine is initialised before `Phone.begin()` subscribes to
  the message pipeline. A `CALL_REQUEST` payload that lands during the
  splash window now rings cleanly.
- **`PhoneAppStubScreen` reachable from the Settings default-case** —
  carried forward. Still a soft runtime fallback rather than a
  `static_assert`. Worth converting in v2.1 once the Item enum has
  stabilised through the v2.0 release.
- **Sample-row contacts (uid==0) silently no-op** — fixed. S171 wires
  a `PhoneNotificationToast("Sample contact — add a real one")` when
  the user fires CALL/MESSAGE/EDIT on a placeholder row.
- **`loadMock()` is dead code** — fixed in S201. The
  `MAKERPHONE_LOAD_MOCK_DATA` build flag (default 0) now lives in
  `src/MAKERphoneConfig.h`; the helpers and the `Chatters[]` table
  compile out unless a developer flips the flag via
  `-DMAKERPHONE_LOAD_MOCK_DATA=1` in arduino-cli.
- **`Settings.sound = false` boot override** — carried forward. The
  override block in `setup()` still mutes a fresh production Chatter
  on boot. Phase J's `PhoneProfileScreen` (S159) is the new
  authoritative knob; remove the override block in v2.1 once we are
  confident every shipped device has migrated.
- **`InboxScreen` legacy `MainMenu`-style row layout** — carried
  forward. The new `PhoneMessageRow` (S31) is the path used by the
  rest of the firmware; the legacy class survives behind a build flag
  and can be deleted in v2.1.
- **No persisted owner name / call-history limit** — partially fixed.
  S144 ships the persisted owner name; the call history is still an
  in-memory ring buffer.
- **`PhoneSoftKeyBar` flash duration** — fixed during the S199 final
  QA pass.
- **Battery-low modal threshold tuning** — carried forward
  (hardware-only).
- **`PhoneCameraScreen` shutter sound clipping** — fixed. The S192
  system-tone library "duck" hook quiets the active ringtone for
  ~80 ms during the shutter chirp.
- **`PhoneIdleDim` floor reads as black on partially-failed
  backlights** — carried forward (hardware-only).

## v2.0-fresh polish

- [ ] **`PhoneDemoModeScreen` (S200) carries no persisted slide
  pointer.** Exiting via any key restarts from slide 0 on the next
  launch. Intentional today (the screen is short-lived, the camera
  shoot is a continuous take), but a `Settings.demoSlideStart` byte
  would let a release engineer pick which slide to open on so the
  marketing video can start mid-deck.

- [ ] **`PhoneDemoModeScreen` slide pace is not user-tunable.**
  3 s/slide is hard-coded in `kSlidePeriodMs`. A future `ADVANCED →
  Demo speed` row could expose Slow / Medium / Fast presets without
  touching the slide content.

- [x] **Speed-dial editor (S151) does not warn before overwriting an
  assigned slot** — fixed in S202. `PhoneSpeedDialScreen` now grows a
  third `Mode::Confirm` that intercepts a destructive PICK before
  any `Settings.speedDial[d]` mutation. ENTER on a contact that
  would overwrite a *different* existing binding hands off to a
  centered "REPLACE? / CLEAR?" panel; ENTER persists, BACK returns
  to pick mode with the cursor still on the contact the user picked.
  Trivial cases (empty slot, picking the same contact, clearing an
  already-empty slot) still apply directly without a prompt.

- [ ] **`PhoneVirtualPet` save data lives in NVS but never garbage-
  collects on a wipe.** A user who factory-resets through the Settings
  → System submenu keeps their pet level/name; arguably the right
  behaviour, but should be documented or explicitly paired with a
  "Reset pet" action.

- [ ] **`PhoneRadio` (S195) does not gate against the Silent profile.**
  Selecting a station while the device is set to Silent still plays
  the melody loop because the radio routes through `BuzzerService`
  directly rather than through `PhoneRingtoneEngine`'s profile-aware
  wrapper. Low priority: the profile UI explicitly calls out that the
  music app is exempt from Silent.

- [ ] **`PhoneBeatMaker` (S194) save slots are limited to 4 by the
  current `Settings.beatPatterns[4]` array.** Real users will quickly
  fill all four; bump to 8 in v2.1 once the NVS-resize migration plan
  is in place.

- [ ] **`PhoneOperatorBanner` (S147) renders the user-pixelable logo
  every frame.** The 5×16 grid is small enough that this is cheap, but
  it would be tidier to render once into an `lv_canvas` on edit and
  reuse on every push.

## Hardware-only (cannot reproduce in CI)

- [ ] All v1.0 hardware-only items above still apply to v2.0 hardware.
- [ ] **Beat-maker step velocity at full volume clips on some Chatter
  units when stepping at >180 BPM.** Cap the duty cycle at 90 % when
  BPM > 160 in v2.1 once we have hardware test data.
- [ ] **CRT-shrink power-down animation (S57) appears mis-timed on
  units running an older Chatter-Library build that ships
  `Display::flush()` synchronously rather than async.** Detection
  hint: the message preamble (S146) reads correctly on a S199-flashed
  device but truncates by ~120 ms on a v2024-Q3 unit. Re-flash from
  v2.0 onward to resolve.

## Out of scope for v2.0

The v2.0 release closes out the original 200-session roadmap. Any
new work (a v2.1 hotfix series or a v3.0 platform shift) starts on a
fresh roadmap document and a new CHANGELOG section. The items above
are the v2.1 candidate pool; everything else is by-design behaviour
shipped in v2.0.
