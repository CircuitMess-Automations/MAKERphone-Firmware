# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-17 ~16:42
* **HEAD on `main`:** `c4ad2ed` (`feat(mp24): S-MP20/5 -- land
  Game.cpp + GameSystem.cpp via shim patch`). A docs checkpoint
  commit on top of this one will be outside the CI path filter,
  so the green build state is preserved.
* **Build status:** GREEN. Run `25996590264` -- build job
  `success`, flash job `success`. Zero fix-forwards this fire --
  the feat commit landed clean on first try, making it six clean
  landings in a row on the GameEngine subsystem (S-MP20/4b + /4c
  + /4d + /4e + /4f + /5).
* **Device boot status:** HEALTHY. boot.log from artifact
  `7044185429` (40 lines, 0 crash markers) shows the same shape
  as the prior fires:

      STORE: mounted; 2 files, 1757 / 1860161 bytes used (0%)
      POWER: polling GPIO2 at 50 Hz, debounce 3 ticks
      MP24: Entering background-tasks-only loop (LVGL owns the panel).
      MODEM: state -> POWER -> BOOT -> boot probe... (up to 16 s)

  Modem probe still wedged at ~16 s waiting on URCs (S-MP21
  territory: SIM presence + PWR_KEY polarity). No `Guru`,
  `panic`, `abort`, or backtrace anywhere in the log.
* **Flasher status:** ONLINE and reliable.
* **Binary size:** 883408 bytes / 2 MB partition (43% used, 1.16
  MB free). **Identical to the prior fire's 883408** -- Game.cpp
  + GameSystem.cpp TUs get gc'd at link time as predicted.
  Nothing in the 229-entry SRCS list pulls Game.h directly or
  transitively (audit verified), so --gc-sections drops both
  TUs and all their external references (InputChatter::
  getInputInstance, FSLVGL::loadCache, Chatter, ChirpSystem::
  stop, the friend Game::objects access from GameSystem).
  Compile-only validation pattern from S-MP20/2 -> /3 -> /4 ->
  /4b -> /4c -> /4d -> /4e -> /4f -> /5 holds.

## What S-MP20/5 actually shipped (in commit `c4ad2ed`)

The engine core: `src/Games/GameEngine/Game.cpp` (~120 LOC) and
`src/Games/GameEngine/GameSystem.cpp` (4 LOC). Together they
provide the Game base class (LVScreen-host pattern + LoopListener
ticking + load/start/stop/pop lifecycle + ResourceManager-backed
asset loading + CollisionSystem + RenderSystem ownership) and
the GameSystem base class (ctor + friend-accessed
Game::objects getter).

Two pieces in the commit:

  - `chatter_app/CMakeLists.txt` -- adds Game.cpp + GameSystem.cpp
    to SRCS with a comment block explaining the audit (only six
    upstream files include Game.h: PhoneDialerScreen,
    PhoneGamesScreen, MainMenu, GamesScreen, ShutdownService,
    SleepService -- all six excluded from SRCS) and the gc-
    sections gambit. Slot is between AnimRC.cpp (the /4f leaf)
    and the S-MP18j Storage stub group.

  - `chatter_app/shim/SleepServiceStub.cpp` -- defines
    `Game* startedGame = nullptr;` alongside the existing
    `bool gameStarted = false;`. Forward-declared `class Game;`
    is sufficient; the pointer is never derefed in this TU.
    Belt-and-braces: if gc-sections ever fails to drop Game.cpp,
    this satisfies the `extern Game* startedGame;` reference
    from Game.cpp's start()/stop() bodies.

Net diff: 2 files, +63 lines, -0 lines.

## What the next fire should do

Pick ONE of these. Roughly ordered by risk-adjusted payoff.

1. **S-MP20/6 -- Land `Highscore.cpp`.** Parallel-safe leaf that
   provides per-game high-score persistence. Header is at
   `src/Games/GameEngine/Highscore.h`; the .cpp is small (~80
   LOC). Audit its deps first: it likely pulls Settings.h
   (chatter_library, available) and ResourceManager.h (already
   reachable since Game.h pulls it). If the audit is clean it
   compiles + gc's the same way the /4* leaves did.
   **Recommended next.**

2. **S-MP20/7 -- First game implementation (Snake).** Snake is
   the simplest of the four upstream games. Pulls Game.h
   transitively (via `Snake : public Game`). Once Snake.cpp is
   in SRCS, the linker *will* retain Game.cpp + GameSystem.cpp
   + the Rendering + Collision leaves because Snake's ctor
   chains into Game's ctor which in turn instantiates
   RenderSystem + CollisionSystem. That's a much bigger leap
   than /5 -- expect compile + link surprises (InputChatter::
   getInputInstance not defined; FSLVGL::loadCache not defined;
   etc.). Plan for a 30-min budget of audit + maybe one fix-
   forward.

3. **Audit InputChatter + FSLVGL availability before /7.** Game.
   cpp's start() / stop() call `InputChatter::getInputInstance()`
   and pop() calls `FSLVGL::loadCache()`. Neither InputChatter.cpp
   nor FSLVGL.cpp is in SRCS. If Snake.cpp's link path retains
   Game.cpp, those will be unresolved externals. Decide whether
   to stub them (no-op shim, same pattern as the other Stubs)
   or to land the real .cpp's first.

## Helper script note carried from prior fires

* The helpers (`flash_iter.sh`, `addr2line.py`) need to be
  recreated each fire from the brief; the new sandbox doesn't
  preserve `/home/claude/`. In this sandbox `$HOME` is
  `/sessions/<id>/`. Place them at `$HOME/work/flash_iter.sh` +
  `$HOME/work/addr2line.py` and adjust internal path
  references.
* `flash_iter.sh` polls in 30-second intervals up to 12 minutes,
  which exceeds the 45-second `mcp__workspace__bash` tool
  timeout. Workaround: poll the GitHub Actions REST API directly
  in shorter bash calls (`sleep 35 && curl .../jobs`). The full
  helper-script invocation is impractical -- do it manually in
  chunks. This fire used the chunked approach successfully again
  (build ~2.5 min, flash ~30 s, all polled in 35-40 s chunks).
* The CI workflow triggers on push automatically via the
  `mp24/**` path filter -- no manual `workflow_dispatch` needed.
* `pyelftools` must be installed each fire:
  `pip install --break-system-packages --quiet pyelftools`.
  (Skip if no crash to decode -- this fire didn't need it.)
* The repo lives at `$HOME/work/repo/mp_firmware`. `git clone`
  (no --depth flag works fine on this sandbox; ~50 MB).

## Open items unchanged from the brief

* Q2: `uPOWER_OFF` GPIO1 polarity (currently Hi-Z)
* Q3: Modem boot-FAIL triage if PWR_KEY polarity fix is insufficient
* Q4: STATUS / NET_STATUS LED traces

## Roadmap status snapshot

* **S-MP20** -- in progress
  * /1: glm vendoring (done in earlier fire)
  * /2: GameObject.cpp in SRCS (commit `65572e3`)
  * /2/1: undef Arduino radians/degrees macros (commit `dde74d9`)
  * /3: Collision/* leaves (CollisionComponent, CircleCC, RectCC,
    PolygonCC) in SRCS (commit `4ba3170`)
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
  * **/5: Game.cpp + GameSystem.cpp landed via SleepServiceStub
    `Game* startedGame` definition (commit `c4ad2ed`) <-- THIS
    FIRE -- GameEngine subsystem .cpp coverage complete**
  * /6: Highscore.cpp (parallel-safe)
  * /7+: four game implementations, GamesScreen wiring, dialer
* **S-MP21** -- not started (modem hardware bring-up)
* **S-MP22+** -- not started
