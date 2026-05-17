# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-17 ~14:55
* **HEAD on `main`:** `1109bb5` (`feat(mp24): S-MP20/4c -- add
  Games/GameEngine/Rendering/RenderSystem.cpp to SRCS`). The
  docs checkpoint commit on top of that is outside the CI path
  filter, so the green build state is preserved.
* **Build status:** GREEN. Run `25994067792` -- build job
  `success`, flash job `success`. Zero fix-forwards this fire --
  the feat commit landed clean on first try (same pattern as
  S-MP20/4b).
* **Device boot status:** HEALTHY. boot.log from artifact
  `7043422785` (42 lines, 0 crash markers) shows the same shape
  as the prior fire:

      STORE: mounted; 2 files, 1757 / 1860161 bytes used (0%)
      POWER: polling GPIO2 at 50 Hz, debounce 3 ticks
      MP24: Entering background-tasks-only loop (LVGL owns the panel).
      MODEM: state -> POWER -> BOOT -> boot probe... (up to 16 s)

  Modem probe still wedged at ~16 s waiting on URCs (S-MP21
  territory: SIM presence + PWR_KEY polarity). No `Guru`,
  `panic`, `abort`, or backtrace anywhere in the log.
* **Flasher status:** ONLINE and reliable.
* **Binary size:** 883296 bytes / 2 MB partition (43% used, 1.16
  MB free). **Identical byte-count** to the prior fire's image,
  which confirms `--gc-sections` discarded `RenderSystem.cpp`
  whole at link time exactly as predicted -- the TU compiled
  cleanly but its symbols are unreferenced (no screen instantiates
  any RenderSystem yet, and Game.h is not in SRCS), so the
  linker dropped them. This is the established
  "compile-only validation" pattern from S-MP20/2 + S-MP20/3 +
  S-MP20/4 + S-MP20/4b (GameObject, Collision/*,
  CollisionSystem, RenderComponent).

## What S-MP20/4c actually shipped (in commit `1109bb5`)

The second leaf of the Rendering subsystem:
`src/Games/GameEngine/Rendering/RenderSystem.cpp` (34 lines).
This is the canvas-iterator -- `update(deltaMicros)` walks
`getObjects()`, buckets each visible RenderComponent by layer,
sorts the layer set, then per-layer dispatches
`RenderComponent::push(canvas, pos, rot)`.

Its only external includes:

  - `<set>`, `<map>` -- stdlib, already exercised by
    `CollisionSystem.cpp` (S-MP20/4).
  - `"RenderSystem.h"` -> `<Display/Sprite.h>` (shimmed) +
    `GameSystem.h` (header-only) + `GameObject.h` (validated by
    S-MP20/2).

The key risk flagged by the prior checkpoint -- that
RenderSystem.cpp's body calls `GameSystem::getObjects()` whose
definition lives in `GameSystem.cpp` (NOT in SRCS) and could
trip an unresolved-symbol link error if --gc-sections didn't
drop the TU -- DID NOT FIRE. Verified by:

  - CI build job `success` (no unresolved-symbol diagnostics).
  - Binary size unchanged at 883296 bytes (identical to S-MP20/4b
    image, byte-for-byte).
  - Map file shows zero `RenderSystem`-decorated symbols
    anywhere -- the TU was discarded whole.

CMakeLists.txt grew by one SRCS entry + a 22-line comment block
explaining the gc-sections gambit and the documented revert path
if it had failed.

## What the next fire should do

Pick ONE of these. Roughly ordered by risk-adjusted payoff.

1. **S-MP20/4d -- Land `StaticRC.cpp`** (31 lines). Pulls
   `Sprite::drawIcon(File, x, y, w, h)`, `drawIcon` with the
   6-arg `(scale, transparent)` overload,
   `Sprite::pushRotateZoomWithAA(x, y, rot, sx, sy, t)`, and
   `TFT_TRANSPARENT`. Each may or may not be in the shim Sprite.
   Expect 1-3 fix-forwards if the shim is missing entries -- same
   shape as S-MP20/4's drawPixel/TFT_eSPI palette
   pre-emptive additions. **Recommended next.**

2. **S-MP20/4e -- Land `SpriteRC.cpp`** (18 lines). Listed by the
   prior plan as having possible `drawString` / `setTextColor`
   signature mismatches. Smaller surface than StaticRC; could be
   the safer bet of the two if /4d hits too many shim gaps.
   Check shim Sprite first.

3. **S-MP20/4f -- Vendor `<Display/GIFAnimatedSprite.h>`** before
   AnimRC.cpp can land. Inspect the existing
   `mp24/components/circuitos_shim/include/Display/` layout (just
   `Display.h`, `Sprite.h`, `Color.h`) and add a
   `GIFAnimatedSprite.h` following the same pattern.

4. **S-MP20/5 -- Land `Game.cpp` + `GameSystem.cpp`.** Still gated
   on Rendering subsystem being available, since `Game.cpp`'s
   constructor calls `render(this, ...)` and `collision(this)`.
   With RenderSystem.cpp now compiling clean as of S-MP20/4c the
   `render(this, ...)` half is unblocked from the engine side
   (still gated on `Sprite* canvas` source-of-truth -- the
   upstream allocator lives in `Game.cpp` itself, so this is
   just a Game-shape question once Game.cpp lands).
   ALSO needs `extern Game* startedGame` to be defined somewhere
   -- recommend SleepServiceStub.cpp alongside the existing
   `gameStarted` extern.

5. **S-MP20/6 -- Highscore.cpp** (parallel-safe leaf -- can
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
* The repo lives at `$HOME/repo/mp_firmware`.

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
  * **/4c: RenderSystem.cpp in SRCS (commit `1109bb5`) <--
    THIS FIRE**
  * /4d-/4f: Rendering subsystem leaves remaining (next fire)
  * /5: Game.cpp + GameSystem.cpp (Rendering side now unblocked
    from a header-compile standpoint; CollisionSystem still
    landed -- both base ctors will resolve once GameSystem.cpp
    also lands alongside Game.cpp)
  * /6: Highscore.cpp (parallel-safe)
  * /7+: four game implementations, GamesScreen wiring, dialer
* **S-MP21** -- not started (modem hardware bring-up)
* **S-MP22+** -- not started
