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

- [ ] **`loadMock()` is dead code** in `MAKERphone-Firmware.ino` — kept
  commented out behind `//loadMock(true);` for development convenience.
  Worth gating behind a `MAKERPHONE_LOAD_MOCK_DATA` build flag in
  `MAKERphoneConfig.h` so a future contributor can flip it without
  editing the .ino.

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
