# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-17 ~15:50
* **HEAD on `main`:** `9fe4bf1` (`feat(mp24): S-MP20/4f -- land
  AnimRC.cpp via new GIFAnimatedSprite shim`). A docs checkpoint
  commit on top of this one will be outside the CI path filter,
  so the green build state is preserved.
* **Build status:** GREEN. Run `25995428897` -- build job
  `success`, flash job `success`. Zero fix-forwards this fire --
  the feat commit landed clean on first try (same pattern as
  S-MP20/4b + /4c + /4d + /4e -- 5-in-a-row clean landings on
  the Rendering subsystem).
* **Device boot status:** HEALTHY. boot.log from artifact
  `7043838875` (40 lines, 0 crash markers) shows the same shape
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
  MB free). **Identical to the prior fire's 883408** -- AnimRC.cpp
  TU + the seven new no-op GIFAnimatedSprite symbols get gc'd at
  link time, exactly as predicted. Nothing in SRCS instantiates
  AnimRC, so the linker drops the TU and everything reachable
  only from it. Compile-only validation pattern from S-MP20/2 ->
  /3 -> /4 -> /4b -> /4c -> /4d -> /4e -> /4f holds.

## What S-MP20/4f actually shipped (in commit `9fe4bf1`)

The fifth and last leaf of the Rendering subsystem:
`src/Games/GameEngine/Rendering/AnimRC.cpp` (48 lines). Owns a
`std::unique_ptr<GIFAnimatedSprite>` built via
`std::make_unique<GIFAnimatedSprite>(nullptr, file)`. push()
branches on `rot == 0` between `gif->push(parent, pos.x, pos.y)`
and `gif->pushRotate(parent, pos.x, pos.y, rot)`. Also exercises
start/stop/reset/setLoopMode/setLoopDoneCallback on the gif.

Three pieces in the commit, all required for AnimRC.cpp to link:

  - **NEW** `circuitos_shim/include/Display/GIFAnimatedSprite.h`
    -- 80 lines. Forward declares a `GIFAnimatedSprite` class
    with the ctor `(Sprite*, const fs::File&)` plus six methods
    (`start/stop/reset/push/pushRotate/setLoopMode/
    setLoopDoneCallback`). Pulled in via angle-brackets from
    chatter_app, where `circuitos_shim` is FIRST in REQUIRES so
    its include path preempts the upstream
    `circuitos/src/Display/GIFAnimatedSprite.h` (whose impl
    can't link because the circuitos Display/ subtree is
    excluded from the build). Transitively brings `<Util/GIF.h>`
    in for the `GIF::LoopMode { Auto, Single, Infinite }` enum.

  - `circuitos_shim/MP24Sprite.cpp` -- piggy-backs seven no-op
    definitions onto the existing Sprite TU. One ctor + six
    methods, all trivial: ctor is empty, dtor is empty,
    start/stop/reset/setLoopMode/setLoopDoneCallback are
    empty, push/pushRotate accept-args-and-do-nothing.

  - `chatter_app/CMakeLists.txt` -- adds AnimRC.cpp to SRCS with
    a comment block explaining the dep chain + the gc-sections
    gambit. Slot is between SpriteRC.cpp (the /4e leaf) and the
    S-MP18j Storage stub group.

Net diff: 3 files, +191 lines, -0 lines.

## What the next fire should do

Pick ONE of these. Roughly ordered by risk-adjusted payoff.

1. **S-MP20/5 -- Land `Game.cpp` + `GameSystem.cpp`.** Rendering
   subsystem .cpp coverage is now complete
   (RenderComponent + RenderSystem + StaticRC + SpriteRC +
   AnimRC). Game.cpp's member-init `render(this, ...)` and
   `collision(this)` lines should resolve cleanly at link time
   even with --gc-sections. Still gated on `extern Game*
   startedGame` being defined somewhere -- the prior plan
   recommended putting it in `SleepServiceStub.cpp` alongside
   the existing `gameStarted` extern.
   **Recommended next.** Audit Game.h + GameSystem.h to find
   any further externs / unresolved dependencies before the
   commit; do the audit in a single fire chunk if uncertain
   rather than risk a fix-forward chain.

2. **S-MP20/6 -- Highscore.cpp** (parallel-safe leaf -- can land
   any time, doesn't touch the engine wiring). Worth taking
   first if S-MP20/5 audit surfaces an unknown.

3. **S-MP20/7 -- First game implementation (Snake, simplest of
   the four).** Pulls in Game.cpp + GameSystem.cpp transitively,
   so it's effectively gated on /5 landing. Defer until /5 is
   green.

## Helper script note carried from prior fires

* The helpers (`flash_iter.sh`, `addr2line.py`) need to be
  recreated each fire from the brief; the new sandbox doesn't
  preserve `/home/claude/`. In this sandbox `$HOME` is
  `/sessions/<id>/`. Place them at `$HOME/flash_iter.sh` +
  `$HOME/addr2line.py` and adjust internal path references.
* `flash_iter.sh` polls in 30-second intervals up to 12 minutes,
  which exceeds the 45-second `mcp__workspace__bash` tool
  timeout. Workaround: poll the GitHub Actions REST API directly
  in shorter bash calls (`sleep 35 && curl .../jobs`). The full
  helper-script invocation is impractical -- do it manually in
  chunks. This fire used the chunked approach successfully again
  (build ~4 min, flash ~30 s, all polled in 35-40 s chunks).
* `pyelftools` must be installed each fire:
  `pip install --break-system-packages --quiet pyelftools`.
  (Skip if no crash to decode -- this fire didn't need it.)
* The repo lives at `$HOME/repo/mp_firmware`. `git clone --depth
  50` is sufficient to fit comfortably on the ~1.3 GB /sessions
  disk; the .git ended up at ~5 MB shallow.

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
  * **/4f: AnimRC.cpp landed via new GIFAnimatedSprite shim
    (commit `9fe4bf1`) <-- THIS FIRE -- Rendering subsystem
    .cpp coverage complete**
  * /5: Game.cpp + GameSystem.cpp -- now unblocked from the
    Rendering-deps angle. Still gated on `extern Game*
    startedGame` being defined somewhere (recommended host:
    SleepServiceStub.cpp alongside the existing `gameStarted`
    extern).
  * /6: Highscore.cpp (parallel-safe)
  * /7+: four game implementations, GamesScreen wiring, dialer
* **S-MP21** -- not started (modem hardware bring-up)
* **S-MP22+** -- not started
