# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-17 ~15:11
* **HEAD on `main`:** `853c344` (`feat(mp24): S-MP20/4d -- land
  StaticRC.cpp via shim patches`). The docs checkpoint commit on
  top of that is outside the CI path filter, so the green build
  state is preserved.
* **Build status:** GREEN. Run `25994466122` -- build job
  `success`, flash job `success`. Zero fix-forwards this fire --
  the feat commit landed clean on first try (same pattern as
  S-MP20/4b + S-MP20/4c).
* **Device boot status:** HEALTHY. boot.log from artifact
  `7043542391` (42 lines, 0 crash markers) shows the same shape
  as the prior fires:

      STORE: mounted; 2 files, 1757 / 1860161 bytes used (0%)
      POWER: polling GPIO2 at 50 Hz, debounce 3 ticks
      MP24: Entering background-tasks-only loop (LVGL owns the panel).
      MODEM: state -> POWER -> BOOT -> boot probe... (up to 16 s)

  Modem probe still wedged at ~16 s waiting on URCs (S-MP21
  territory: SIM presence + PWR_KEY polarity). No `Guru`,
  `panic`, `abort`, or backtrace anywhere in the log.
* **Flasher status:** ONLINE and reliable.
* **Binary size:** 883296 bytes / 2 MB partition (43% used, 1.16
  MB free). **Identical byte-count** to the prior two fires'
  images (S-MP20/4b + S-MP20/4c), which confirms `--gc-sections`
  discarded `StaticRC.cpp` whole at link time exactly as
  predicted -- the TU compiled cleanly but its symbols are
  unreferenced (no screen instantiates any StaticRC yet, and
  Game.h is not in SRCS), so the linker dropped them. Likewise
  the new shim no-op overloads (`Sprite::drawIcon(File, ...)` +
  `Sprite::pushRotateZoomWithAA`) are gc'd. Established
  "compile-only validation" pattern from S-MP20/2 -> /3 -> /4 ->
  /4b -> /4c.

## What S-MP20/4d actually shipped (in commit `853c344`)

The third leaf of the Rendering subsystem:
`src/Games/GameEngine/Rendering/StaticRC.cpp` (31 lines). It
wraps a File-backed RGB565 icon at fixed PixelDim, with `push()`
branching on rotation: `rot == 0` calls `parent->drawIcon(file,
pos.x, pos.y, dim.x, dim.y)` direct; non-zero rot allocates a
temp Sprite, drawIcon's into it with TFT_TRANSPARENT mask, then
`pushRotateZoomWithAA(x, y, rot, 1, 1, TFT_TRANSPARENT)` on the
temp.

Three shim diffs ship in the same commit so the TU compiles:

  - `circuitos_shim/include/TFT_eSPI.h` -- adds `TFT_TRANSPARENT
    0x0120` to the canonical Bodmer palette block. Same pattern
    as the TFT_BLACK / TFT_RED / ... macros landed in S-MP20/4.
  - `circuitos_shim/include/Display/Sprite.h` -- adds `#include
    <FS.h>` (for the `File` type), a `drawIcon(File, ...)`
    overload, and `pushRotateZoomWithAA(x, y, rot, sx, sy,
    chroma)` -- the 6-arg no-parent variant. The SpriteRC 7-arg
    with-parent variant is NOT added; held back for S-MP20/4e.
  - `circuitos_shim/MP24Sprite.cpp` -- no-op definitions for both
    new methods, matching the 9C-shim drawing-stub policy.

CMakeLists.txt grew by one SRCS entry + a 20-line comment block
explaining the shim diff set and the gc-sections gambit. Net
diff: 4 files, +65 lines, 0 lines removed.

## What the next fire should do

Pick ONE of these. Roughly ordered by risk-adjusted payoff.

1. **S-MP20/4e -- Land `SpriteRC.cpp`** (18 lines). Pulls
   `std::make_shared<Sprite>`, `Sprite::pushRotateZoomWithAA(
   parent, x, y, rot, sx, sy, t)` -- the 7-arg WITH-parent
   variant (this fire only added the 6-arg variant), and
   `setSwapBytes` via TFT_eSprite cast. The
   `setSwapBytes` call already has a TFT_eSPI stub (no-op).
   Expect 1 fix-forward (the 7-arg pushRotateZoomWithAA overload
   if I haven't pre-added it). **Recommended next.**

2. **S-MP20/4f -- Vendor `<Display/GIFAnimatedSprite.h>`** before
   AnimRC.cpp can land. Inspect the existing
   `mp24/components/circuitos_shim/include/Display/` layout
   (just `Display.h`, `Sprite.h`; Color.h is supplied by
   upstream circuitos) and add a `GIFAnimatedSprite.h`
   forwarding to an empty class with the same shape upstream
   has. AnimRC's `gif->push(parent, x, y)` and
   `gif->pushRotate(parent, x, y, rot)` need stubs too.

3. **S-MP20/5 -- Land `Game.cpp` + `GameSystem.cpp`.** With both
   GameEngine systems (Collision + Rendering) now having every
   leaf .cpp compiling clean (5 of 5 leaves landed), Game.cpp's
   `render(this, ...)` and `collision(this)` member-init lines
   should resolve at link time even with --gc-sections. Still
   need `extern Game* startedGame` defined somewhere -- the
   prior plan recommended SleepServiceStub.cpp alongside the
   existing `gameStarted` extern.

4. **S-MP20/6 -- Highscore.cpp** (parallel-safe leaf -- can
   land any time, doesn't touch the engine wiring).

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
  chunks.
* `pyelftools` must be installed each fire:
  `pip install --break-system-packages --quiet pyelftools`.
  (Skip if no crash to decode -- this fire didn't need it.)
* The repo lives at `$HOME/repo/mp_firmware`. Clone with
  `--depth=50` to avoid blowing the /sessions disk quota (~538
  MB free at session start).

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
  * **/4d: StaticRC.cpp landed via shim patches (commit
    `853c344`) <-- THIS FIRE**
  * /4e: SpriteRC.cpp (next fire)
  * /4f: AnimRC.cpp + GIFAnimatedSprite shim (later)
  * /5: Game.cpp + GameSystem.cpp -- both engine subsystems'
    leaves now have full .cpp coverage, so the
    render(this,...) + collision(this) member-init lines should
    link cleanly. Still gated on extern Game* startedGame being
    defined.
  * /6: Highscore.cpp (parallel-safe)
  * /7+: four game implementations, GamesScreen wiring, dialer
* **S-MP21** -- not started (modem hardware bring-up)
* **S-MP22+** -- not started
