# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-17 ~17:27
* **HEAD on `main`:** `a1b60d1` (`feat(mp24): S-MP20/6c -- land
  TextInput.cpp via parallel-safe SRCS add`). A docs checkpoint
  commit on top of this one will be outside the CI path filter,
  so the green build state is preserved.
* **Build status:** GREEN. Run `25997637537` -- build job
  `success`, flash job `success`. Zero fix-forwards this fire --
  TextInput.cpp compiled clean on first push. **Twelve** clean
  landings in a row on the GameEngine subsystem (S-MP20/4b +
  /4c + /4d + /4e + /4f + /5 + /6 + /6b + now /6c).
* **Device boot status:** HEALTHY. boot.log from artifact
  `7044493542` (40 lines, 0 crash markers) is byte-for-byte
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
  MB free). **Identical to the prior fire's 883408** -- TextInput
  TU gc-sections'd at link time as predicted. No in-SRCS file
  currently references TextInput's symbols (Game subclasses are
  not yet in SRCS). Compile-only validation pattern from S-MP20/2
  -> /3 -> /4 -> /4b -> /4c -> /4d -> /4e -> /4f -> /5 -> /6 ->
  /6b -> /6c holds. Twelve consecutive clean landings.

## What S-MP20/6c actually shipped (in commit `a1b60d1`)

Single-line SRCS addition + comment block in
`mp24/components/chatter_app/CMakeLists.txt`:

  - Added `"${SRC_DIR}/Games/GameEngine/TextInput.cpp"` to SRCS
    in a new slot between Highscore.cpp (the /6 leaf) and the
    InputChatter+FSLVGL stub block (/6b).
  - 38-line comment block explaining why TextInput.cpp can
    compile cleanly without any shim work:
      * <Pins.hpp> resolves to the MP2.4 shim Pins.hpp via the
        REQUIRES circuitos_shim chain. The shim Pins.hpp pulls
        hal/buttons.h which defines BTN_0..BTN_9 as enum values
        and BTN_L/BTN_LEFT/BTN_RIGHT/BTN_ENTER/BTN_BACK/BTN_R as
        Decision-E alias macros to BTN_FACE_A / BTN_JOY_LEFT /
        BTN_JOY_RIGHT / BTN_JOY_CLICK / BTN_FACE_C / BTN_FACE_B
        respectively. All keyMap initialiser tokens resolve.
      * Input::getInstance / addListener / removeListener
        resolve via circuitos_shim's MP24Input.
        LoopManager::add/removeListener resolves via upstream
        circuitos.
      * No FS, no FSLVGL, no glm, no Chatter. Only STL +
        Input/Loop headers.

Net diff: 1 file, +38 / -0 lines.

## Why this fire chose /6c rather than going straight to /7

Audit of Snake.cpp's transitive deps found that Snake's
`onStop()` body calls `input->stop(); delete input;` on a
`TextInput* input` member. Without TextInput.cpp in SRCS, the
link would fail with undefined references to TextInput::stop()
and the implicit dtor as soon as Snake.cpp lands.

Three options to fix:
  1. Land TextInput.cpp + Snake.cpp together in /7.
  2. Land TextInput.cpp first as /6c (compile-only validation
     leaf, gc-sections'd, zero binary delta) -- then /7 ships
     Snake.cpp cleanly.
  3. Stub TextInput in shim/.

Option 2 wins on the established pattern: small, well-scoped,
proven gc-section behaviour, preserves the streak of clean
landings, and removes a class of risk from the upcoming /7
landing without taking on the risk of /7 itself in this fire.

## What the next fire should do

Pick ONE of these. Roughly ordered by risk-adjusted payoff.

1. **S-MP20/7 -- First game implementation (Snake).** This is
   now genuinely unblocked: Game.cpp/GameSystem.cpp are in
   SRCS (/5), InputChatter + FSLVGL stubs are in SRCS (/6b),
   TextInput.cpp is in SRCS (/6c), Highscore.cpp is in SRCS
   (/6). Snake.cpp pulls all four cleanly.

   What to expect:
     - Snake.cpp's transitive includes (Game.h ->
       LVScreen.h / LoopListener.h / Highscore.h / ChirpSystem
       / Display) are all resolvable.
     - Snake.h declares `Sprite* baseSprite;`, `Input* buttons;`,
       `Display* display;` -- types from circuitos/UI/Sprite.h,
       circuitos/Input/Input.h, circuitos/Display/Display.h.
       Spot-check via `grep` for any header that Snake.h pulls
       transitively that isn't already covered.
     - Snake.cpp may pull <Chatter.h> directly. The shim
       circuitos_shim/include/Chatter.h is what resolves. As
       discovered in /6b: that shim does NOT include
       <Pins.hpp>, so any BTN_* token used DIRECTLY in
       Snake.cpp (without #include <Pins.hpp>) will be
       undefined. Audit Snake.cpp for naked BTN_* before
       landing.
     - Once Snake.cpp lands, Game's vtable gets pulled,
       Game::loop body executes references to
       InputChatter::getInputInstance + FSLVGL::loadCache
       (resolved via /6b stubs), TextInput::stop + ~TextInput
       (resolved via /6c).
     - Expected binary delta: nonzero. Snake.cpp itself plus
       reachable parts of Game.cpp's body get linked in. First
       real bytes added to the image since /5.

   Budget: full 30-min fire, expect one fix-forward for any
   naked BTN_* in Snake.cpp.

2. **S-MP20/6d -- Audit pass over the four game .cpp files.**
   If /7 looks too risky, do a static audit: for each of
   Snake.cpp / SpaceInvaders.cpp / Bonk.cpp / SpaceRocks.cpp,
   list every direct include, every BTN_* / KEY_* token used,
   and every external symbol referenced. Pick the cleanest
   one to be /7's target.

3. **S-MP20/7b -- Bonk first instead of Snake.** Bonk is
   usually the simplest of the four; if Snake's audit shows
   surprises, swap to Bonk.

## Helper script note carried from prior fires

* The helpers (`flash_iter.sh`, `addr2line.py`) need to be
  recreated each fire from the brief; the new sandbox doesn't
  preserve `/home/claude/`. In this sandbox the user's home
  directory has restricted permissions and the repo lives at
  `/tmp/claude/repo/mp_firmware` instead. The full helper-script
  invocation is impractical inside the 45-second bash tool
  timeout -- poll the GitHub Actions REST API directly in
  shorter chunks (`sleep 35 && curl .../jobs`). This fire
  used the chunked approach: ~4 polls of 40s each got us
  past build (5-6 min) and flash (10-30s).
* The CI workflow triggers on push automatically via the
  `mp24/**` path filter -- no manual `workflow_dispatch` needed.
* `pyelftools` was not needed this fire (no crash to decode).
* The repo lives at `/tmp/claude/repo/mp_firmware`. `git
  clone --depth 100` works fine (~25 MB).

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
  * /6b: pre-emptive InputChatter + FSLVGL stubs (commit
    `a1817c4`)
  * **/6c: TextInput.cpp landed via parallel-safe SRCS add
    (commit `a1b60d1`) <-- THIS FIRE -- ensures TextInput
    symbols are resolvable when Snake's onStop() body refers
    to them in the upcoming /7 landing**
  * /7+: four game implementations, GamesScreen wiring, dialer
* **S-MP21** -- not started (modem hardware bring-up)
* **S-MP22+** -- not started
