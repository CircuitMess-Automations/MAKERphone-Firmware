# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-17 ~15:32
* **HEAD on `main`:** `19b8e1f` (`feat(mp24): S-MP20/4e -- land
  SpriteRC.cpp via three shim patches`). A docs checkpoint
  commit on top of this one will be outside the CI path filter,
  so the green build state is preserved.
* **Build status:** GREEN. Run `25994949457` -- build job
  `success`, flash job `success`. Zero fix-forwards this fire --
  the feat commit landed clean on first try (same pattern as
  S-MP20/4b + S-MP20/4c + S-MP20/4d).
* **Device boot status:** HEALTHY. boot.log from artifact
  `7043691390` (42 lines, 0 crash markers) shows the same shape
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
  MB free). +112 bytes vs the prior fire's 883296 -- explained
  by the new `Sprite : public TFT_eSprite` inheritance (vtable
  layout extends to include the inherited TFT_eSprite vtable
  entries) plus the two new no-op method symbols
  (`pushRotateZoomWithAA` 7-arg overload + the TFT_eSprite-base
  ctor calls in two Sprite ctors). The SpriteRC.cpp TU itself
  was still gc'd at link time as predicted: nothing in SRCS
  instantiates a SpriteRC -- only Game.h's chain does, and
  Game.h is not in SRCS. Compile-only validation pattern from
  S-MP20/2 -> /3 -> /4 -> /4b -> /4c -> /4d holds.

## What S-MP20/4e actually shipped (in commit `19b8e1f`)

The fourth leaf of the Rendering subsystem:
`src/Games/GameEngine/Rendering/SpriteRC.cpp` (18 lines). Owns
a `std::shared_ptr<Sprite>` built via `std::make_shared<Sprite>(
(Sprite*)nullptr, dim.x, dim.y)`; `push()` branches on `rot == 0`
to either a direct `sprite->push(parent, x, y)` or a
`sprite->pushRotateZoomWithAA(parent, mid_x, mid_y, rot, 1, 1,
TFT_TRANSPARENT)` self-blit around the sprite mid-point.

Three shim diffs ship in the same commit so the TU compiles:

  - `circuitos_shim/include/Display/Sprite.h`
      * `class Sprite : public TFT_eSprite` -- restores the
        inheritance the upstream Sprite class uses, so the
        `static_cast<TFT_eSprite*>(sprite.get())
        ->setSwapBytes(false)` line in the SpriteRC ctor
        compiles. `setSwapBytes` is inherited from TFT_eSPI and
        remains a no-op stub.
      * `int16_t width()` / `int16_t height()` accessors --
        upstream SpriteRC calls `.width()` / `.height()` (not
        the CircuitOS `getWidth()` / `getHeight()` pair) to
        compute the rotation mid-point. Both names now coexist.
      * 7-arg `pushRotateZoomWithAA(Sprite*, int16_t, int16_t,
        float, float, float, uint16_t)` overload -- the
        WITH-parent variant used by SpriteRC's non-zero-rot
        branch. The 6-arg variant landed in S-MP20/4d.

  - `circuitos_shim/MP24Sprite.cpp`
      * Two non-delegating Sprite ctors (`TFT_eSPI*, ...`) and
        (`Sprite*, ...`) now forward the parent argument to the
        TFT_eSprite base via the member-init list.
      * No-op definition for the new 7-arg pushRotateZoomWithAA.

  - `chatter_app/CMakeLists.txt`
      * Adds SpriteRC.cpp to SRCS, with a comment block
        explaining the shim diff set and the gc-sections gambit.

Net diff: 3 files, +67 lines, -3 lines.

## What the next fire should do

Pick ONE of these. Roughly ordered by risk-adjusted payoff.

1. **S-MP20/4f -- Vendor `<Display/GIFAnimatedSprite.h>` +
   land `AnimRC.cpp`** (5th and last Rendering leaf). AnimRC
   holds a `std::unique_ptr<GIFAnimatedSprite>` and its `push()`
   calls `gif->push(parent, x, y)` and `gif->pushRotate(parent,
   x, y, rot)`. We need to:
   - Add a new shim header `circuitos_shim/include/Display/
     GIFAnimatedSprite.h` that forward-declares a
     `GIFAnimatedSprite` class with `push(Sprite*, int16_t,
     int16_t)` + `pushRotate(Sprite*, int16_t, int16_t, float)`
     + a ctor that accepts a `File`.
   - Add no-op definitions in a new shim TU (or piggy-back on
     MP24Sprite.cpp).
   - Inspect upstream AnimRC.cpp/h to confirm the call
     signatures + ctor shape before sketching the shim.
   **Recommended next.** Same gc-sections gambit -- nothing in
   SRCS instantiates AnimRC.

2. **S-MP20/5 -- Land `Game.cpp` + `GameSystem.cpp`** (gated on
   /4f). With all five Rendering leaves having .cpp coverage,
   Game.cpp's `render(this, ...)` and `collision(this)` member-
   init lines should resolve cleanly at link time even with
   --gc-sections. Still need `extern Game* startedGame` defined
   somewhere -- the prior plan recommended SleepServiceStub.cpp
   alongside the existing `gameStarted` extern.

3. **S-MP20/6 -- Highscore.cpp** (parallel-safe leaf -- can land
   any time, doesn't touch the engine wiring).

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
  chunks. This fire used the chunked approach successfully.
* `pyelftools` must be installed each fire:
  `pip install --break-system-packages --quiet pyelftools`.
  (Skip if no crash to decode -- this fire didn't need it.)
* The repo lives at `$HOME/repo/mp_firmware`. Plain `git clone`
  with no `--depth` was fine on the ~538 MB /sessions disk in
  this fire; the .git ended up at ~16 MB.

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
  * **/4e: SpriteRC.cpp landed via three shim patches (commit
    `19b8e1f`) <-- THIS FIRE**
  * /4f: AnimRC.cpp + GIFAnimatedSprite shim (next fire)
  * /5: Game.cpp + GameSystem.cpp -- gated on /4f finishing the
    Rendering leaf set. Still also gated on extern Game*
    startedGame being defined somewhere.
  * /6: Highscore.cpp (parallel-safe)
  * /7+: four game implementations, GamesScreen wiring, dialer
* **S-MP21** -- not started (modem hardware bring-up)
* **S-MP22+** -- not started
