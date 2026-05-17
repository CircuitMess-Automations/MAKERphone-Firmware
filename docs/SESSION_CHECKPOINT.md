# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-17 ~17:15
* **HEAD on `main`:** `a1817c4` (`feat(mp24): S-MP20/6b --
  pre-emptive InputChatter + FSLVGL stubs`). A docs checkpoint
  commit on top of this one will be outside the CI path filter,
  so the green build state is preserved.
* **Build status:** GREEN. Run `25997398734` -- build job
  `success`, flash job `success`. Zero fix-forwards this fire --
  both new stub TUs compiled clean on first push. Eleven clean
  landings in a row on the GameEngine subsystem (S-MP20/4b + /4c
  + /4d + /4e + /4f + /5 + /6 + now /6b).
* **Device boot status:** HEALTHY. boot.log from artifact
  `7044422873` (40 lines, 0 crash markers) is byte-for-byte
  identical in shape to the prior fire's boot.log:

      STORE: mounted; 2 files, 1757 / 1860161 bytes used (0%)
      POWER: polling GPIO2 at 50 Hz, debounce 3 ticks
      MP24: Entering background-tasks-only loop (LVGL owns the panel).
      MODEM: state -> POWER -> BOOT -> boot probe... (up to 16 s)

  Modem probe still wedged at ~16 s waiting on URCs (S-MP21
  territory: SIM presence + PWR_KEY polarity). No `Guru`,
  `panic`, `abort`, or backtrace anywhere in the log.
* **Flasher status:** ONLINE and reliable.
* **Binary size:** 883408 bytes / 2 MB partition (43% used, 1.16
  MB free). **Identical to the prior fire's 883408** -- both
  new stub TUs gc-sections'd at link time as predicted. No
  in-SRCS file currently references their symbols (Game.cpp's
  start / stop / loop bodies are themselves gc'd because no
  game subclass references Game's vtable / ctor). Compile-only
  validation pattern from S-MP20/2 -> /3 -> /4 -> /4b -> /4c ->
  /4d -> /4e -> /4f -> /5 -> /6 -> /6b holds. Eleven consecutive
  clean landings.

## What S-MP20/6b actually shipped (in commit `a1817c4`)

Two new shim TUs in `mp24/components/chatter_app/shim/` plus the
SRCS-list patch that lands them:

  - `shim/InputChatterStub.cpp` (~130 LOC): provides storage for
    `InputChatter::keyMap` (empty) and `InputChatter::instance`
    (nullptr), the InputChatter ctor (forwards to InputLVGL
    with LV_INDEV_TYPE_ENCODER, sets instance), the three
    required virtual overrides (`read`, `buttonPressed`,
    `buttonReleased`) all as no-ops, and `getInputInstance()`
    which lazily news up a singleton InputChatter on first call.

  - `shim/FSLVGLStub.cpp` (~90 LOC): provides storage for the
    three static members `cacheLoaded` / `cache` / `specialCache`
    plus `loadCache()` and `unloadCache()` as no-ops. Skips
    `cached[]` (not referenced from any in-SRCS TU) and the
    non-static members (no in-SRCS code constructs an FSLVGL
    instance).

  - `chatter_app/CMakeLists.txt` (+35 lines): adds both files to
    SRCS in a new slot between Highscore.cpp (the /6 leaf) and
    the S-MP18j StorageStub.cpp group, with a comment block
    explaining why we stub rather than landing the upstream .cpp
    files (Pins.hpp coverage + InputLVGL indev ownership for
    InputChatter; RamFile pure-virtual issue for FSLVGL --
    same blocker that gated StorageStub).

Net diff: 3 files, +252 / -0 lines.

## Why this fire chose /6b rather than going straight to /7

Per the previous fire's checkpoint guidance, audit InputChatter +
FSLVGL + Chatter before adding any game subclass:

  - InputChatter.cpp -- NOT in chatter_app SRCS. Source file at
    src/InputChatter.cpp. Adding it directly fails because its
    keyMap initialiser uses BTN_UP/DOWN/A/B and our shim
    Chatter.h does NOT pull <Pins.hpp> (those macros are
    defined in hal/buttons.h, only reachable through the shim
    Pins.hpp which Chatter.h omits). Beyond that, the upstream
    buttonPressed/Released bodies route events into the indev
    InputLVGL owns; on MP2.4 lvgl_glue owns the keypad indev
    so this routing is dead-end anyway.
  - FSLVGL.cpp -- NOT in chatter_app SRCS. Source file at
    src/FSLVGL.cpp. Includes <FS/RamFile.h> which is the same
    blocker that gated the upstream Storage layer at S-MP18i
    (RamFile inherits from fs::FileImpl in arduino-esp32 3.3.8
    but doesn't override all pure virtuals -- std::make_shared
    fails as abstract). Stub is the correct treatment.
  - Chatter (global `Chatter` ChatterImpl instance) -- ALREADY
    provided by circuitos_shim/MP24Chatter.cpp which is in
    chatter_app's REQUIRES chain. No work needed.

Two of three need shim work -> shim work goes first as /6b.

## What the next fire should do

Pick ONE of these. Roughly ordered by risk-adjusted payoff.

1. **S-MP20/7 -- First game implementation (Snake).** This is
   now unblocked -- the externs Game.cpp's start/stop/loop
   bodies reference will resolve cleanly via the /6b stubs.
   Snake is the simplest of the four upstream games.

   What to expect:
     - Snake.cpp pulls Game.h (transitively pulls
       Games/GameEngine/Game.h + ResourceManager.h +
       GameObject.h + Collision/CollisionSystem.h +
       Rendering/RenderSystem.h + Interface/LVScreen.h +
       <Loop/LoopListener.h> + <Audio/ChirpSystem.h>). All of
       these are either in SRCS or transitively pulled by
       existing SRCS files -- spot-check via grep before adding.
     - Snake.cpp pulls Highscore.h (already in SRCS as /6).
     - Snake.cpp may pull other Snake-specific headers
       (Pawn / Field / etc. depending on the upstream layout).
       Audit src/Games/Snake/ to see what files Snake.cpp's
       includes resolve to and which need to land alongside.
     - Once Snake.cpp lands in SRCS, Snake's vtable pulls
       Game's vtable, which pulls Game::loop / Game::~Game.
       Game::loop body references FSLVGL::loadCache (now
       resolved via stub). Game::start / Game::stop are NOT
       virtual so they remain gc'd until something explicitly
       calls them (typically GamesScreen / PhoneGamesScreen).
     - Expected binary delta: nonzero this time -- Game.cpp's
       reachable section grows, plus Snake.cpp itself.

   Budget: probably needs the full 30-min fire with one
   fix-forward.

2. **S-MP20/7b -- Smaller game first (Bonk).** If Snake.cpp
   audit shows too many cascading deps, pick the simplest of
   the four (Bonk and Snake are usually neck-and-neck). The
   point of /7 is to flush out the first round of game-leaf
   surprises; pick whichever has the fewest external deps.

3. **S-MP20/6c -- More compile-only validation leaves.** If
   neither game-subclass options look clean, look for other
   src/Games/GameEngine/ files that haven't landed yet. The
   GameEngine subsystem .cpp coverage was supposed to be
   complete after /6, but a fresh scan of src/Games/GameEngine/
   may surface stragglers.

## Helper script note carried from prior fires

* The helpers (`flash_iter.sh`, `addr2line.py`) need to be
  recreated each fire from the brief; the new sandbox doesn't
  preserve `/home/claude/`. In this sandbox the user's home
  directory has restricted permissions and the repo lives at
  `/sessions/<id>/repo/mp_firmware` instead. The full
  helper-script invocation is impractical inside the 45-second
  bash tool timeout -- poll the GitHub Actions REST API directly
  in shorter chunks (`sleep 35 && curl .../jobs`). This fire
  used the chunked approach: ~3 polls of 35-40s each got us
  past build (5-6 min) and flash (10-30s).
* The CI workflow triggers on push automatically via the
  `mp24/**` path filter -- no manual `workflow_dispatch` needed.
* `pyelftools` was not needed this fire (no crash to decode).
* The repo lives at `/sessions/<id>/repo/mp_firmware`. `git
  clone --depth 50` works fine (~25 MB).

## Open items unchanged from the brief

* Q2: `uPOWER_OFF` GPIO1 polarity (currently Hi-Z)
* Q3: Modem boot-FAIL triage if PWR_KEY polarity fix is insufficient
* Q4: STATUS / NET_STATUS LED traces

## Roadmap status snapshot

* **S-MP20** -- in progress
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
    (commit `9fe4bf1`) -- Rendering subsystem .cpp coverage
    complete
  * /5: Game.cpp + GameSystem.cpp landed via SleepServiceStub
    `Game* startedGame` definition (commit `c4ad2ed`) --
    GameEngine subsystem .cpp coverage complete
  * /6: Highscore.cpp landed via parallel-safe SRCS add
    (commit `9b42710`)
  * **/6b: pre-emptive InputChatter + FSLVGL stubs (commit
    `a1817c4`) <-- THIS FIRE -- stubs the symbols Game.cpp's
    start/stop/loop bodies reference, so /7 can land Snake
    without a link failure**
  * /7+: four game implementations, GamesScreen wiring, dialer
* **S-MP21** -- not started (modem hardware bring-up)
* **S-MP22+** -- not started
