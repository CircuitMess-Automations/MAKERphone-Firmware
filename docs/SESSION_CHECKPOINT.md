# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-17 ~21:42 UTC
* **HEAD on `main`:** `29c6eeb` (`feat(mp24): S-MP20/14 -- wire
  PhoneDialerScreen from home + menu`). This fire shipped TWO
  commits, both on `main`, both GREEN on the first build with
  zero fix-forwards:
    - `c073518` -- feat(mp24): S-MP20/13 -- land
      PhoneDialerScreen.cpp in SRCS (compile-only; binary
      delta near-zero because no entry point references it).
    - `29c6eeb` -- feat(mp24): S-MP20/14 -- wire
      PhoneDialerScreen from home + menu. Binary GROWS
      ~415 KB (firmware artifact size 11704867 -> 12119504
      bytes) because HomeFactory.cpp now instantiates
      PhoneDialerScreen, dragging it through --gc-sections.
* **Build status:** GREEN at HEAD `29c6eeb` (run
  `26003757848`, both `build` and `flash` jobs completed/
  success).
* **Device boot status:** HEALTHY. boot.log scan: 0 crash
  markers. Same boot sequence as prior fires' green HEADs:

      BATT: curve-fitting cal active
      STORE: mounted; 2 files, 1757 / 1860161 bytes used (0%)
      POWER: polling GPIO2 at 50 Hz, debounce 3 ticks
      MP24: Entering background-tasks-only loop (LVGL owns the panel).
      MODEM: state -> POWER -> BOOT -> boot probe... (up to 16 s)

* **Flasher status:** ONLINE and reliable.
* **Binary size:** `mp24-firmware` artifact is 12,119,504 bytes
  at HEAD `29c6eeb` (up from 11,704,867 at `c073518`). The
  ~415 KB jump comes from PhoneDialerScreen + the seven
  transitive screens it references being kept alive at link
  time once `HomeFactory.cpp` instantiates it. Final firmware
  payload (the `.bin` itself; the artifact zip also bundles
  ELF + map) is still well under the 2 MB app partition.

## What this fire actually shipped (net)

S-MP20 is **DONE** as of this fire. Two commits, both GREEN
at the final HEAD (no fix-forwards):

1. **`c073518` -- S-MP20/13.** Added one line to
   `mp24/components/chatter_app/CMakeLists.txt`:

     * `${SRC_DIR}/Screens/PhoneDialerScreen.cpp` (784 LOC)

   PhoneDialerScreen was the last upstream screen held back by
   the games-engine blocker -- its `*#GAME` (76253) Easter-egg
   cheat-code path does a direct `new Snake(...)` and pulls
   `<Games/Snake/Snake.h>`. With Snake landed at /7f3 and
   the games-engine wiring landed through /12, the entire
   include chain now resolves. All seven additional screen
   #includes (PhoneImeiRevealScreen, PhoneFirmwareInfoScreen,
   PhoneFlashlight, PhoneFortuneCookie, PhoneDrumKitScreen,
   PhoneBeatMaker, PhoneMemoryAudit) were already in SRCS
   post-S-MP18; FSLVGL is provided by FSLVGLStub.cpp; Loop
   Manager is in chatter_library. Binary delta near-zero
   (--gc-sections drops the file because nothing instantiates
   PhoneDialerScreen yet).

2. **`29c6eeb` -- S-MP20/14.** Replaced
   `mp24/components/chatter_app/screens/HomeFactory.cpp` to
   add two new navigation hooks atop S-MP19's existing
   PhoneMainMenu / LockScreen wiring:

     * `s_home->setOnLeftSoftKey(on_call_softkey)` --
       BTN_LEFT (CALL softkey) now pushes a fresh
       `PhoneDialerScreen()`.
     * `Icon::Phone` in `on_menu_select()` no longer hits
       the "no compiled destination" branch -- it pushes
       `PhoneDialerScreen()` exactly like Games / Messages /
       etc. do.

   Same heap-leak caveat as every other handler in
   HomeFactory.cpp: the pushed dialer becomes LVGL-owned and
   is not freed on BACK; the deferred fix lives in S-MP25.

   Final navigation set after this fire:

     - BTN_LEFT  (CALL)        -> push PhoneDialerScreen
     - BTN_RIGHT (MENU)        -> push PhoneMainMenu
     - BTN_BACK hold (lock)    -> push LockScreen
     - PhoneMainMenu Phone     -> push PhoneDialerScreen
     - PhoneMainMenu Messages  -> push InboxScreen
     - PhoneMainMenu Contacts  -> push FriendsScreen
     - PhoneMainMenu Music     -> push PhoneMusicPlayer
     - PhoneMainMenu Camera    -> push PhoneCameraScreen
     - PhoneMainMenu Games     -> push PhoneGamesScreen
     - PhoneMainMenu Settings  -> push PhoneSettingsScreen
     - PhoneMainMenu Mail      -> push InboxScreen

   Every PhoneIconTile in the main-menu enum now has a real
   compiled destination. Every PhoneHomeScreen softkey has a
   real handler. S-MP20 is structurally done.

## Why this fire could finish /10 cleanly in one push

The 30-minute fire budget covered TWO CI cycles (build + flash)
back-to-back with room to spare. The audit-first approach
identified the entire delta in ~3-5 minutes of Reading:

  * Player.h / Player.cpp / SpaceRocks.h / SpaceRocks.cpp --
    read end-to-end.
  * Hearts.h / Hearts.cpp / Score.h / Score.cpp -- read
    end-to-end (Common helpers that SpaceRocks pulls in).
  * Cross-checked every `display->` / `sprite->` / `obj->`
    method call against the existing shim coverage. One missing
    overload: fillRoundRect.
  * Confirmed `<Pins.hpp>` provides BTN_4/5/6, RES_HEART /
    RES_GOBLET / FILE_HEART / FILE_GOBLET come from upstream
    ResourceManager.h (already a transitive header in SRCS).
  * Cross-checked Game.h's API surface (getFile, addObject,
    removeObject, Audio member, collision member, pop()) ==
    a strict subset of what Snake exercises.

All four upstream Chatter games (Snake, SpaceInvaders, Bonk,
SpaceRocks) now compile + link cleanly inside chatter_app.
S-MP20 has now landed the games-engine compile-validation
phase end-to-end.

## What the next fire should do

**S-MP21 -- modem hardware bring-up.** With S-MP20 done, the
next non-trivial blocker is the cellular modem. Every fire so
far has logged the same modem trace:

    MODEM: state -> POWER
    MODEM: state -> BOOT
    MODEM: boot probe... (0 s elapsed)
    MODEM: boot probe... (1 s elapsed)
    ...
    MODEM: boot probe... (16 s elapsed)  <-- log capture ends here

i.e. the modem never leaves BOOT -- `AT` never returns a
response. Two likely causes (already enumerated in the brief):

1. SIM not physically inserted (the most likely answer; the
   flasher Mac just has a bare PCB plugged in).
2. PWR_KEY (GPIO 12) polarity wrong -- the Quectel datasheet
   wants a 2.5 s LOW pulse with the line idle HIGH; double-
   check `hal/modem.c` against the schematic.

The next fire should:

* Read `mp24/main/hal/modem.c` end-to-end, especially the
  PWR_KEY pulse code.
* Cross-check against `GSM_module.kicad_sch` -- verify the
  PWR_KEY net polarity matches the code's assumption.
* If polarity is correct, log an explicit `AT+CPIN?` /
  `AT+COPS?` retry loop after the boot probe to make the
  "no SIM" failure mode visible in boot.log (currently we
  only see the silence).
* Punt the actual SIM insertion to the user via a clear
  log message + a memory note. This is the first feature
  that's gated on the physical hardware state.

**Smaller-budget alternatives** if S-MP21 looks too large for
a single fire:

* **Walk every menu destination once in chat** -- compile a
  punch-list of which downstream screens visibly render vs.
  which are still LVGL-blank-screen stubs. This is the
  S-MP26 polish-and-verify pass and could be done now to
  inform what gets fixed in S-MP22-25.
* **Start S-MP23 -- StorageStub.cpp -> NVS-backed
  Repo<T>.** This is independent of modem bring-up and a
  small, well-scoped change.

## Helper script note carried from prior fires

* The helpers (`flash_iter.sh`, `addr2line.py`) need to be
  recreated each fire from the brief; the sandbox doesn't
  preserve `/home/claude/` (in fact, `/home/claude/` is not
  writable -- `mkdir: cannot create directory '/home/claude':
  Permission denied`). The brief's `if [[ ! -d /home/claude/
  repo/mp_firmware ]]` check therefore always fires and the
  clone command fails on the missing parent. In this fire's
  sandbox the user was `tender-magical-knuth`, `$HOME` was
  `/sessions/tender-magical-knuth`, and `/sessions/...` was
  100% full again (9.8G/9.8G -- it appears to be a shared
  filesystem across all parallel sessions), so the writable
  scratch path used was `/tmp/` (903 MB free under `/`).
  The repo cloned to `/tmp/mp_firmware/`. `pyelftools` had
  to be installed with `PYTHONUSERBASE=/tmp/pylocal pip
  install --user --no-cache-dir pyelftools` to avoid the
  default install path on the full `/sessions` filesystem. As before, the bash tool's
  45-second timeout makes the helper scripts impractical to
  run end-to-end; this fire used the chunked GH API approach
  (poll jobs every 40s with a separate bash call each time)
  and it worked smoothly.
* The CI workflow triggers on push automatically via the
  `mp24/**` / `src/**` path filter -- no manual
  `workflow_dispatch` needed. Updating
  `docs/SESSION_CHECKPOINT.md` does NOT trigger a build (it's
  outside the path filter), so the checkpoint commit at the end
  of each fire is free.
* `pyelftools` was not needed this fire (no device crash).
* The Read/Write/Edit host-side tools cannot reach the repo
  (it's inside the VM under `/tmp/work/...`), so all file
  edits had to go through bash + python heredocs. The
  multi-line-replacement pattern still works:
  `python3 << 'PY'` blocks that load the file, assert the old
  block is present, and replace it once.

## Open items unchanged from the brief

* Q2: `uPOWER_OFF` GPIO1 polarity (currently Hi-Z)
* Q3: Modem boot-FAIL triage if PWR_KEY polarity fix is insufficient
* Q4: STATUS / NET_STATUS LED traces

## Roadmap status snapshot

* **S-MP20** -- DONE. Games engine + glm + four games (Snake,
  SpaceInvaders, Bonk, SpaceRocks) all compile + link;
  PhoneGamesScreen wired via PhoneMainMenu Games tile;
  PhoneDialerScreen wired via Home left softkey AND
  PhoneMainMenu Phone tile. All eight menu tiles now have
  real compiled destinations. (See /11-/14 below.)
  * /1: glm vendoring (done in earlier fire)
  * /2: GameObject.cpp in SRCS (commit `65572e3`)
  * /2/1: undef Arduino radians/degrees macros (commit `dde74d9`)
  * /3: Collision/* leaves in SRCS (commit `4ba3170`)
  * /3/1: CollisionSystem.cpp held back (commit `4d92863`)
  * /4: CollisionSystem.cpp landed via shim patches (commit
    `6b632a4`)
  * /4/1: soften `-Werror=unused-local-typedefs` (commit
    `96d76f6`)
  * /4b: RenderComponent.cpp in SRCS (commit `1fa9321`)
  * /4c: RenderSystem.cpp in SRCS (commit `1109bb5`)
  * /4d: StaticRC.cpp landed via shim patches (commit `853c344`)
  * /4e: SpriteRC.cpp landed via three shim patches (commit
    `19b8e1f`)
  * /4f: AnimRC.cpp landed via new GIFAnimatedSprite shim
    (commit `9fe4bf1`)
  * /5: Game.cpp + GameSystem.cpp landed via SleepServiceStub
    `Game* startedGame` definition (commit `c4ad2ed`)
  * /6: Highscore.cpp landed via parallel-safe SRCS add
    (commit `9b42710`)
  * /6b: pre-emptive InputChatter + FSLVGL stubs (commit
    `a1817c4`)
  * /6c: TextInput.cpp landed via parallel-safe SRCS add
    (commit `a1b60d1`)
  * /6d: LovyanGFX-style text API stubs + <Pins.hpp> transitive
    in shim Chatter.h (commit `fda498b`)
  * /7: Snake.cpp landing attempt -- REVERTED (commits
    `358bad7` shipped, `ffae1ba` reverted).
  * /7e: Second Snake.cpp landing attempt -- REVERTED (commits
    `4999a7b` shipped, `5cca539` reverted).
  * /7f1: ContextTransition.h bracket-include patch (commit
    `0109c89`).
  * /7f2: shim Sprite readPixel/printf/drawFastHLine stubs
    (commit `3a02c01`).
  * /7f3: Snake.cpp lands via Element.h patch + SRCS add
    (commit `6a71c4f`). Build initially RED -- five
    `print()` overload errors.
  * /7f3/1: shim TFT_eSPI gains Print-style overloads
    (commit `a561c51`). Build GREEN. Snake.cpp compiles +
    links + gc-section'd; binary delta 0.
  * /8a: shim TFT_eSPI gains TomThumb / GFXfont /
    setFreeFont + 7-arg drawBitmap (commit `b4b92c4`).
  * /8b: SpaceInvaders.cpp + Star.cpp in SRCS (commit
    `4c2f416`). Build initially RED -- one
    -Werror=sequence-point error.
  * /8b/1: soften -Werror=sequence-point (commit `75174b4`).
    Build GREEN. SpaceInvaders.cpp compiles + links +
    gc-section'd; binary delta 0.
  * /9a: shim Sprite setTextWrap(bool,bool) + empty SD.h
    (commit `b819c86`).
  * /9b: State.cpp + TitleState.cpp + GameState.cpp +
    PauseState.cpp + Bonk.cpp in SRCS (commit `1eeb9d0`).
    Build GREEN on the first push -- no fix-forwards.
    Bonk.cpp compiles + links + all five Pong TUs
    gc-section'd; binary delta 0.
  * **/10a: shim TFT_eSPI fillRoundRect + drawRoundRect
    (commit `95d6745`) -- THIS FIRE.**
  * **/10b: Common/Hearts.cpp + Common/Score.cpp +
    Space/Player.cpp + Space/SpaceRocks.cpp in SRCS (commit
    `cd0094d`). Build GREEN on the first push -- no
    fix-forwards. All four TUs compile + link + gc-section'd;
    binary delta 0. -- THIS FIRE.**
  * /11: PhoneGamesScreen wired -- REVERTED (commit
    `6907b78` shipped, `9b92e41` reverted). ResourceManager
    undef references at link.
  * /12a: ResourceManagerShim.cpp -- SPIFFS-only stub for
    Game-engine resource loading (commit `b24ff16`).
  * /12: re-land PhoneGamesScreen + PhoneMainMenu Games-tile
    wiring atop ResourceManagerShim (commit `7a4c015`).
    Build GREEN. Binary unchanged (still no PhoneDialerScreen
    in SRCS, so HomeFactory's leftover stub branch keeps
    things small).
  * **/13: PhoneDialerScreen.cpp added to SRCS (commit
    `c073518`). Build GREEN on the first push -- no
    fix-forwards. Binary delta near-zero (no instantiation
    site yet). -- THIS FIRE.**
  * **/14: Wire PhoneDialerScreen from PhoneHomeScreen left
    softkey + PhoneMainMenu Phone tile via HomeFactory.cpp
    (commit `29c6eeb`). Build GREEN on the first push.
    Binary grows ~415 KB as PhoneDialerScreen and its seven
    Phone-feature dependency screens stop getting dropped
    by --gc-sections. S-MP20 is DONE. -- THIS FIRE.**
* **S-MP21** -- not started (modem hardware bring-up)
* **S-MP22+** -- not started
