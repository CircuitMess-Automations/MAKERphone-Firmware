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

- [x] **Boot-service init order has a small race window for ringtones.**
  `Phone.begin()` (PhoneCallService) subscribes to the message pipeline
  before `Buzz.begin()` / `Ringtone.begin()` have fired (those run
  after the `IntroScreen` callback completes, ~3 s into boot). A
  `CALL_REQUEST` payload that lands during the splash + intro window
  would raise `PhoneIncomingCall` but with a silent ringtone. Real-world
  rare — a paired peer would have to ring within the first ~3 s of
  power-on — but worth flipping the order in a follow-up so the
  ringtone engine is initialised before the call subscription is active.
  -- fixed in S212. S148 had already promoted `Ringtone.begin()` and
  S161 promoted `Vibrate.begin()` out of the IntroScreen dismiss
  callback into the top-level `boot()` flow, but `Buzz.begin()` and
  `Phone.begin()` were still mis-ordered: `Phone.begin()` ran right
  after `Profiles.begin()` (the call subscription was the third audio
  consumer to register), while `Buzz.begin()` was the slowest -- it
  only fired ~3 s into boot when the IntroScreen dismiss callback
  ran. S212 closes the window by (a) relocating `Phone.begin()` past
  the audio stack so it is the LAST audio-adjacent service to
  subscribe to messages, and (b) promoting `Buzz.begin()` from the
  IntroScreen dismiss callback into the top-level `boot()` flow,
  next to `Ringtone.begin()` / `Vibrate.begin()`. Both intro-callback
  bodies (the SIM-PIN and direct paths) drop their `Buzz.begin()`
  line and gain a comment marker pointing to the new home. The
  `BuzzerService::begin()` body is a thin listener subscription
  (Input + MessageService fan-out, no LoopManager attachment until a
  tone is requested) so promoting it is side-effect free. Result:
  by the first `LoopManager::loop()` tick after `setup()` returns,
  every audio engine is alive, so a `CALL_REQUEST` arriving during
  the splash / SIM-PIN / IntroScreen window now rings audibly and
  any inbound text chimes through Buzz as designed.

- [x] **`PhoneAppStubScreen` reachable from the Settings default-case.**
  Today the stub's appName is "SETTINGS" (post-fix above), but the code
  path itself only fires if a future `PhoneSettingsScreen::Item` enum
  value is added without being wired in `IntroScreen.cpp`. Worth
  converting into a compile-time `static_assert` over the enum size so
  the branch becomes unreachable instead of a soft runtime fallback.
  -- fixed in S211. `PhoneSettingsScreen::Item` now ends in a
  `Count = 20` sentinel, `ItemCount` derives from it, and two
  `static_assert`s pin both the kLayout row count
  (`PhoneSettingsScreen.cpp`) and the IntroScreen dispatch case
  count (`IntroScreen.cpp`) against `Item::Count` -- so adding a
  new enumerator without (a) adding a kLayout row and (b) adding
  a dispatch `case` is now a build error. The pre-S211 soft
  `default:` branch is kept as a belt-and-braces fallback for the
  out-of-range-alias edge case but is unreachable in normal
  operation. The audit also caught the matching `ItemCount = 19`
  vs `kLayout` 20-row drift introduced by S206 -- buildList()
  was writing `rows[19]` one past the `rows[ItemCount]` array
  end (overlapping the `cursor` field on the current struct
  layout), and the cursor wrap clamped at 18 so the `Demo mode`
  ADVANCED row was unreachable from the keypad. Both fall out
  of the new derivation.

- [x] **Sample-row contacts (uid==0) silently no-op CALL / MESSAGE / EDIT.**
  Intentional (S37/S38) but the user gets no visual cue. Add a
  `PhoneNotificationToast` "Sample contact — add a real one" so the
  feedback is explicit. -- fixed in S215. The v2.0 sweep below
  claimed S171 had wired this toast, but a `git log` audit shows
  S171 actually shipped the `PhoneStressReliever` fidget toy and
  never touched the contact-detail no-op path -- so the toast was
  still missing in production. S215 closes the gap by growing
  `PhoneContactDetail` a public `showSampleContactToast()` helper
  backed by a lazily-built `PhoneNotificationToast` member parented
  to the screen's `obj` (LV_EVENT_DELETE cascade auto-frees on
  pop). The three IntroScreen handlers (`contactDetailCall`,
  `contactDetailMessage`, `contactDetailEdit`) call the helper
  before their existing early-return on `uid == 0`, so a press on
  a placeholder row now flashes the soft-key (legacy behaviour)
  *and* slides a "Sample contact / Add a real one" toast in from
  above the status bar for ~2 s.

- [x] **`loadMock()` is dead code** in `MAKERphone-Firmware.ino` — fixed
  in S201. The `MAKERPHONE_LOAD_MOCK_DATA` build flag (default 0) now
  lives in `src/MAKERphoneConfig.h`; the helpers and the `Chatters[]`
  table compile out unless a developer flips the flag via
  `-DMAKERPHONE_LOAD_MOCK_DATA=1` in arduino-cli, removing the need to
  edit the .ino just to seed dev data.

- [x] **`Settings.sound = false` override on every boot.** `setup()`
  forcibly muted the device on boot for the prototype, which meant
  the user-facing sound toggle in `PhoneSoundScreen` (S52) wrote
  Settings but was overridden on the next reboot. -- fixed in
  S216. `SettingsData` grows a `bool soundOverrideMigrated = false`
  byte at the end of the blob (next to `demoSpeed`, so the existing
  NVS-resize pattern reads it as zero on a first boot of v2.1) and
  the `.ino` setup() block is rewritten from the unconditional
  `if(cfg.sound || cfg.sleepTime != 0 || cfg.shutdownTime != 0)`
  clamp to a one-shot `if(!cfg.soundOverrideMigrated)` migration
  that runs the legacy clamp exactly once, flips the flag to
  `true` on the same `Settings.store()` call, and is skipped
  forever after. Net effect: a freshly-flashed device migrates
  byte-identically to the pre-S216 path on first boot, but every
  subsequent boot respects whatever the user has chosen on
  PhoneSoundScreen / PhoneHapticsScreen / PhoneProfileScreen /
  PhoneSleepScreen / PhoneShutdownScreen. Phase J's
  `PhoneProfileScreen` (S159) -- and the legacy sound /
  sleepTime / shutdownTime knobs -- are now authoritative.

- [x] **`InboxScreen` still compiles the legacy `MainMenu`-style row
  layout** alongside the new `PhoneMessageRow` (S31). The new row is
  the path the rest of the firmware uses; delete the old class after
  one more verification pass. -- fixed in S213. Every populated
  inbox path already used `PhoneMessageRow`; only the empty-state
  "Add friend" CTA still routed through the legacy
  `src/Elements/ListItem.h` widget. S213 swaps that for a phone-
  style focusable button painted with the MP_* palette
  (transparent body, 1 px MP_DIM idle frame, 2 px MP_ACCENT
  focused frame matching the `PhoneMenuGrid` cursor halo, warm-
  cream pixelbasic7 "+ ADD FRIEND" label centred inside it) and
  drops the `#include "../Elements/ListItem.h"` from
  `InboxScreen.cpp`. The CTA is parented to `listContainer` so the
  existing `clearList()` teardown sweeps it up on every rebuild
  without any extra wrapper plumbing; ENTER still routes to
  `PairScreen` exactly as before. The `ListItem` class itself is
  left intact for the remaining v1.0 hosts (`FriendsScreen`,
  `GamesScreen`) -- those are tracked separately and a future
  session can migrate them once the matching phone-style
  replacements are in place.

- [x] **No persisted "owner name" / call-history limit.** S55's About
  screen lists the peer count, but `PhoneCallHistory` (S27) keeps an
  in-memory ring buffer that is wiped on every reboot. Phase R session
  S144 (owner name) and a Storage-backed history move are tracked there.
  -- fixed in S217. The owner-name half had already shipped in S144;
  the call-history half lands here. A new `PhoneCallHistoryStorage`
  service (`src/Services/PhoneCallHistoryStorage.h` / `.cpp`,
  modelled on `PhoneComposerStorage`) persists the ring as a single
  NVS blob keyed `log` under namespace `mpcalls`, holding a 4-byte
  header (magic 'M','H', version=1, count) followed by up to
  `PhoneCallHistory::MaxEntries` x 40-byte fixed-stride entries. The
  per-entry stride is pinned by a `static_assert` against
  `MaxNameLen` / `MaxTsLen` so a future tweak to the entry geometry
  trips a build error rather than a silent on-disk format drift.
  `PhoneCallHistory` grows two private flags (`demoOnly`,
  `duringSeed`) and two helpers (`loadFromStorage()`,
  `saveToStorage()`); the constructor reads "load -> fall back to
  the demo set only when the persisted log is empty", `addEntry()`
  persists after every real mutation (skipped while seeding so the
  placeholder set never leaks into NVS), and `clearEntries()` wipes
  the persisted blob alongside the in-memory ring. Visible output
  on a freshly-flashed device is byte-identical (the demo set still
  paints the screen on first push); the only behavioural delta is
  that a real call lands in NVS and survives a power-cycle.

## Cosmetic

- [x] `PhoneSoftKeyBar` flash duration is ~80 ms which feels slightly
  short on the dimmed state set by `PhoneIdleDim` — the flash can be
  invisible if the user presses a softkey within the dim-fade window.
  Lengthen to 120 ms or wake the dim before flashing. -- fixed in S210.
  `PhoneSoftKeyBar::flashSide()` now calls `IdleDim.resetActivity()`
  before painting the press-feedback flash, so the panel is always
  driven to the user's full brightness for the duration of the
  `FlashMs` (180 ms) timer regardless of the prior idle stage. The
  call is a cheap idempotent no-op when already bright, and it
  short-circuits while `Chatter.backlightPowered() == false` so the
  SleepService fade-in/out path is unaffected. The flash visuals
  themselves are unchanged byte-for-byte; only the brightness floor
  is lifted.

- [x] `PhoneClockFace` redraws the seconds dot at 1 Hz, which causes a
  one-pixel cyan ghost on some panels (LovyanGFX rounding). Switch the
  dot to a 2×2 block or rate-limit the redraw to 0.5 Hz. — fixed in
  S207. The pixelbasic16 `":"` glyph is replaced with two 2x2 cyan
  `lv_obj_t` blocks (top + bottom dot) that share the existing
  `colonOn` state and toggle together via `LV_OBJ_FLAG_HIDDEN`. The
  hidden cells are cleanly cleared to the parent's background on the
  off frame, so no 1-pixel ghost can survive across frames. The two
  blocks are anchored to `LV_ALIGN_TOP_MID` with explicit y offsets
  (4 / 10) that approximate the original glyph dot positions, so the
  visual cadence and placement match the pre-S207 rendering.

- [x] `PhoneSynthwaveBg` star-twinkle pseudo-RNG is reseeded on every
  screen mount, so the same constellation reappears. Seed off
  `millis()` so each home-screen entry feels fresh. -- fixed in S208.
  `buildStars()` now seeds a tiny local Numerical-Recipes LCG from
  `millis()` at mount time and walks each star through a small X
  offset in `[-2..+2]` px / Y offset in `[-1..+1]` px (clamped to
  keep the star inside the upper sky band) plus a phase shift in
  `[0, 2*period)` so the twinkle cadence lands at a different point
  of its ping-pong cycle each push. The LCG is local, so the global
  Arduino `random()` stream stays untouched and other widgets /
  games / ringtone phases that depend on it remain deterministic.

- [x] Long T9 entries in `PhoneNotepad` overrun the 1-line caret hint on
  very narrow notes (≤ 8 chars). Truncate the hint to fit. -- fixed in
  S209. `PhoneT9Input::buildPendingStrip()` now reserves a fixed 24 px
  case-label box on the right edge, leaves a 4 px gap, and clamps the
  pending-hint label to the remaining width with `LV_LABEL_LONG_DOT`,
  so a long pending hint string can no longer collide with the case
  pill. The case label itself is now box-bounded with right-aligned
  text and `LV_LABEL_LONG_CLIP`, so a future localised case glyph
  cluster cannot push back into the hint. The text rendered for the
  current ITU-T E.161 keymap (longest hint `[p]qrs7` ~ 30 px) still
  fits comfortably inside the new 125 px hint budget, so the visible
  output on every existing host (PhoneNotepad, ConvoScreen,
  PhoneContactEdit) is byte-identical -- this is a defensive layout
  hardening, not a visible redesign.

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
  fixed in S211. `PhoneSettingsScreen::Item` now ends in a
  `Count = 20` sentinel, `ItemCount` derives from it, and two
  `static_assert`s pin the kLayout row count and the IntroScreen
  dispatch case count against `Item::Count`, so a future Item
  row that lands without being wired in either site is a build
  error rather than a soft runtime fallback. The pre-S211
  `default:` branch survives as a belt-and-braces guard for the
  out-of-range-alias edge case but is unreachable in normal
  operation post-S211. Side benefit: the audit caught the
  `ItemCount = 19` vs `kLayout` 20-row drift S206 had introduced
  -- both bugs fall out of the new enum-derived count.
- **Sample-row contacts (uid==0) silently no-op** — fixed in S215.
  An earlier draft of this entry attributed the fix to S171, but
  S171 actually shipped the `PhoneStressReliever` fidget toy and
  never touched the contact-detail no-op path. S215 wires the
  missing `PhoneNotificationToast("Sample contact / Add a real
  one")` through a new `PhoneContactDetail::showSampleContactToast()`
  helper, called from the three uid==0 guards in `IntroScreen.cpp`.
- **`loadMock()` is dead code** — fixed in S201. The
  `MAKERPHONE_LOAD_MOCK_DATA` build flag (default 0) now lives in
  `src/MAKERphoneConfig.h`; the helpers and the `Chatters[]` table
  compile out unless a developer flips the flag via
  `-DMAKERPHONE_LOAD_MOCK_DATA=1` in arduino-cli.
- **`Settings.sound = false` boot override** — fixed in S216.
  Rather than removing the override block outright (which would have
  surprised any production Chatter still carrying `sound=true` /
  `sleepTime=1` / `shutdownTime=1` from the original non-MAKERphone
  firmware), S216 keeps the migration semantic but gates it on a new
  `SettingsData::soundOverrideMigrated` byte so the clamp runs
  exactly once -- on the first boot of v2.1 firmware on each device
  -- and is skipped forever after. The user-facing sound / sleep /
  shutdown toggles (PhoneSoundScreen / PhoneHapticsScreen /
  PhoneProfileScreen / PhoneSleepScreen / PhoneShutdownScreen) are
  now authoritative for the rest of the device's life.
- **`InboxScreen` legacy `MainMenu`-style row layout** — fixed in
  S213. Every populated inbox path already used `PhoneMessageRow`;
  only the empty-state "Add friend" CTA still routed through the
  legacy `src/Elements/ListItem.h` widget. S213 swaps that one
  remaining caller for a phone-style focusable "+ ADD FRIEND"
  button painted with the MP_* palette (MP_DIM idle border,
  MP_ACCENT focused border, warm-cream pixelbasic7 label) and
  drops the legacy include from `InboxScreen.cpp`. ENTER still
  routes to `PairScreen` exactly as before. `ListItem` itself is
  left intact for the remaining v1.0 hosts (`FriendsScreen`,
  `GamesScreen`); those migrate in a later session.
- **No persisted owner name / call-history limit** — fixed in S217.
  S144 shipped the persisted owner name. S217 closes the call-
  history half: a new `PhoneCallHistoryStorage` service persists
  the ring buffer as a single NVS blob keyed `log` under namespace
  `mpcalls`, modelled on `PhoneComposerStorage`. `PhoneCallHistory`
  loads the persisted log in its constructor and falls back to the
  demo set only when NVS is empty; `addEntry` writes through after
  every real mutation (gated by `duringSeed` / `demoOnly` so the
  placeholder set never leaks into NVS), and `clearEntries` wipes
  the persisted blob alongside the in-memory ring. The 40-byte
  per-entry stride is pinned by a `static_assert` so a future
  tweak to the entry geometry trips a build error rather than a
  silent on-disk format drift.
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

- [x] **`PhoneDemoModeScreen` (S200) carries no persisted slide
  pointer** — fixed in S203. `SettingsData` now grows a
  `demoSlideStart` byte (default 0, clamped to `[0..kSlideCount-1]`
  at the screen layer). The screen seeds `slideIdx` from the byte
  in its constructor via a new public `resolveStartSlide()`
  accessor, and writes the currently visible slide back via a new
  private `persistCurrentSlide()` helper on dismiss (any-key exit).
  Auto-advances inside an open run are intentionally NOT persisted
  so the nightly auto-cycle never burns NVS write budget; only the
  dismiss-time write hits NVS, and only when the value actually
  changed. The byte sits at the end of the blob next to `alarmTone`
  so the existing NVS-resize pattern reads it as zero-initialised
  on a first boot after the firmware grows — which maps to slide 0,
  the byte-identical pre-S203 default.

- [x] **`PhoneDemoModeScreen` slide pace is not user-tunable** -- fixed
  in S206. `PhoneDemoModeScreen` now grows a public `Speed` enum
  (Medium / Slow / Fast), the per-preset constants
  `kSlidePeriodMediumMs` (3000, the byte-identical pre-S206 default),
  `kSlidePeriodSlowMs` (5000), `kSlidePeriodFastMs` (1500), and a
  `resolveSlidePeriodMs()` static helper that reads
  `Settings.demoSpeed` and falls back to Medium for any out-of-range
  value. `lv_timer_create` ticks at the resolved cadence so the
  chosen pace takes effect on the next push of the demo deck. A new
  `PhoneDemoSpeedScreen` picker (Slow / Medium / Fast, modelled on
  the `PhoneLockWidgetScreen` three-row dirty-aware picker) is
  wired into the ADVANCED group of `PhoneSettingsScreen` directly
  above the existing "Demo mode" row so the speed knob clusters
  with the demo entry it configures. The new byte sits at the end
  of the SettingsData blob next to `demoSlideStart` so the existing
  NVS-resize pattern reads it as zero-initialised on a first boot
  after the firmware grows -- which maps to Medium, the byte-
  identical pre-S206 default.

- [x] **Speed-dial editor (S151) does not warn before overwriting an
  assigned slot** — fixed in S202. `PhoneSpeedDialScreen` now grows a
  third `Mode::Confirm` that intercepts a destructive PICK before
  any `Settings.speedDial[d]` mutation. ENTER on a contact that
  would overwrite a *different* existing binding hands off to a
  centered "REPLACE? / CLEAR?" panel; ENTER persists, BACK returns
  to pick mode with the cursor still on the contact the user picked.
  Trivial cases (empty slot, picking the same contact, clearing an
  already-empty slot) still apply directly without a prompt.

- [x] **`PhoneVirtualPet` save data lives in NVS but never garbage-
  collects on a wipe.** A user who factory-resets through the Settings
  → System submenu keeps their pet level/name; arguably the right
  behaviour, but should be documented or explicitly paired with a
  "Reset pet" action. -- fixed in S214. The `Settings → Factory reset`
  prompt-YES handler in `src/Screens/SettingsScreen.cpp` now calls
  `Pet.reset()` immediately after the `Storage.*` clears and before
  `nvs_flash_erase()` -- which (a) rewrites the `mppet/p` blob to its
  default `{age=0, hunger=happy=energy=100, awake}` state through the
  still-valid NVS handle so the wipe has a deterministic baseline to
  re-erase, (b) drops the cached in-memory `PhoneVirtualPetService::st`
  so any tail-end consumer that touches `Pet.*` between the prompt
  callback and `ESP.restart()` sees a fresh pet rather than the
  pre-wipe stats, and (c) makes the intent visible in the code so a
  future reader does not have to infer "the pet is wiped because
  `nvs_flash_erase()` blows away every namespace including `mppet`."
  No new UI surface; the existing `PhoneVirtualPet` screen still
  exposes the per-pet `BTN_4` long-press reset for users who want to
  wipe just the pet without nuking their friends/messages/contacts.

- [x] **`PhoneRadio` (S195) does not gate against the Silent profile**
  -- fixed in S205. `PhoneRadio` now exposes a static
  `isSilenced()` helper (reads `!Settings.get().sound`, the legacy
  bool that `PhoneProfileScreen` (S159) writes to `false` for the
  SILENT and MEETING profiles and `true` for GENERAL / OUTDOOR /
  HEADSET) and `startPlayback()` plus the retune branch of
  `tuneTo()` short-circuit the `PhoneRingtoneEngine::play()` call
  when silenced. The screen still flips to "playing" state
  visually -- so the soft-key reads STOP, L/R tuning still feels
  live, and `PhoneProfileScreen` taking the device back to
  GENERAL re-arms the dial via the existing PLAY soft-key -- but
  the dial never asks the engine to drive the piezo. The pill
  also grows a third state (`MUTED`, muted-purple body with cyan
  border / cyan text) so the user can read at a glance that the
  radio is alive but silenced rather than wondering whether the
  station "Static 108" is just naturally quiet.

- [ ] **`PhoneBeatMaker` (S194) save slots are limited to 4 by the
  current `Settings.beatPatterns[4]` array.** Real users will quickly
  fill all four; bump to 8 in v2.1 once the NVS-resize migration plan
  is in place.

- [x] **`PhoneOperatorBanner` (S147) renders the user-pixelable logo
  every frame** — fixed in S204. The pre-S204 banner spawned up to 80
  per-cell `lv_obj` rectangles inside `rebuildLogo()` and threw them
  away on the next screen push. S204 swaps that for a single
  `lv_canvas` whose 16×5 `LV_IMG_CF_TRUE_COLOR_ALPHA` backing buffer
  (240 bytes, an inline member with no heap churn) is rasterised
  once on edit and reused on every subsequent push. Lit cells are
  stamped via `lv_canvas_draw_rect`, the LVGL 8.x portable API that
  survives both the older `set_px()` and the newer
  `set_px_color`/`set_px_opa` split. Rendered output is byte-
  identical; the LVGL object count for the banner drops from
  "1 host + up to 80 cells" to "1 host + 1 canvas".

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
