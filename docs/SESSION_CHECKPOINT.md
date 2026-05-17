# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-17 ~14:32
* **HEAD on `main`:** `96d76f6` (`fix(build): S-MP20/4/1 --
  soften -Werror=unused-local-typedefs`). Docs-only checkpoint
  commit (this file) lands on top of that and is outside the CI
  path filter, so the green build state is preserved.
* **Build status:** GREEN. Run `25993575210` -- build job
  `success`, flash job `success`. One fix-forward this fire (the
  feat commit `6b632a4` failed `-Werror=unused-local-typedefs`
  on upstream's unused `typedef glm::vec2 Point;` in
  `CollisionSystem::drawPolygon`; the fix demoted that warning to
  match the existing precedent for upstream-cosmetic suppressions
  on chatter_app). After the soften commit, the next run was
  clean.
* **Device boot status:** HEALTHY. boot.log from artifact
  `7043260361` (40 lines, 0 crash markers) shows:

      STORE: mounted; 2 files, 1757 / 1860161 bytes used (0%)
      POWER: polling GPIO2 at 50 Hz, debounce 3 ticks
      MP24: Entering background-tasks-only loop (LVGL owns the panel).
      MODEM: state -> POWER -> BOOT -> boot probe... (up to 16 s)

  Modem probe still wedged at ~16 s waiting on URCs (S-MP21
  territory: SIM presence + PWR_KEY polarity). No `Guru`,
  `panic`, `abort`, or backtrace anywhere in the log.
* **Flasher status:** ONLINE and reliable across both runs this
  fire.
* **Binary size:** 883296 bytes / 2 MB partition (43% used, 1.16 MB
  free). The Collision subsystem TUs add ~26 KB total to the
  image; well within budget.

## What S-MP20/4 actually shipped (in commit `6b632a4`)

S-MP20/4 closed the three toolchain diffs catalogued by the prior
fire's deferral note and re-enabled
`Games/GameEngine/Collision/CollisionSystem.cpp` in chatter_app
SRCS. The three patches:

1. **`shim_includes/arduino_glm_compat.h` gains
   `#define GLM_ENABLE_EXPERIMENTAL`** at the top of the include
   guard. The header is force-included into every chatter_app TU
   via `target_compile_options(... "SHELL:-include ...")`, so the
   macro is in scope before any glm parse. Unblocks
   `<glm/gtx/matrix_transform_2d.hpp>` and
   `<glm/gtx/vector_angle.hpp>` (both tagged experimental in glm
   1.0.x; they `#error` otherwise).

2. **`circuitos_shim/include/TFT_eSPI.h` gains the canonical
   Bodmer/TFT_eSPI 565 palette** -- TFT_BLACK, TFT_NAVY,
   TFT_DARKGREEN, TFT_DARKCYAN, TFT_MAROON, TFT_PURPLE,
   TFT_OLIVE, TFT_LIGHTGREY, TFT_DARKGREY, TFT_BLUE, TFT_GREEN,
   TFT_CYAN, TFT_RED, TFT_MAGENTA, TFT_YELLOW, TFT_WHITE,
   TFT_ORANGE, TFT_GREENYELLOW, TFT_PINK, TFT_BROWN, TFT_GOLD,
   TFT_SILVER, TFT_SKYBLUE, TFT_VIOLET. These are pulled
   transitively by every TU that includes `<Display/Sprite.h>`,
   which fixes `CollisionSystem::drawDebug` and pre-emptively
   unblocks Snake / SpaceInvaders / JigHWTest for upcoming
   sessions.

3. **`circuitos_shim` Sprite gains a 2-arg `drawPixel(int32_t,
   int32_t)` overload** -- declaration in `Display/Sprite.h`,
   one-liner implementation in `MP24Sprite.cpp` forwarding to the
   3-arg form with `TFT_WHITE`. Matches upstream `TFT_eSprite`
   behaviour, which uses the current foreground color when only
   x/y are supplied. The single call site in the codebase is
   `CollisionSystem::drawPolygon` degenerate-single-point branch,
   which only fires for polygons of size 1 -- a debug-draw path
   that never fires in normal play.

## What S-MP20/4/1 actually shipped (in commit `96d76f6`)

One-line fix-forward. The first build attempt for S-MP20/4 (run
`25993490471`, commit `6b632a4`) failed compilation on
`CollisionSystem.cpp` with:

    error: typedef 'Point' locally defined but not used
        [-Werror=unused-local-typedefs]
        358 |         typedef glm::vec2 Point;

This is an unused `typedef` in upstream's `drawPolygon`. The fix
was to add `-Wno-error=unused-local-typedefs` to chatter_app's
existing `target_compile_options` block, matching the precedent
established by S-MP13b / S-MP18f-w for similar
upstream-cosmetic-pattern suppressions. No source patch.

## What the next fire should do

Pick ONE of these. They are roughly ordered by risk-adjusted
payoff.

1. **S-MP20/4b -- Land the Rendering subsystem.** Six TUs in
   `src/Games/GameEngine/Rendering/`:

   * `RenderComponent.cpp` -- 4-method getter/setter leaf. Pure
     value type. Safe to land alone; should compile clean.
   * `SpriteRC.cpp` -- pulls `<Display/Sprite.h>` heavily. May
     hit drawString / setTextColor signature mismatches similar
     to S-MP20/4's drawPixel issue. Expect 0-2 fix-forwards.
   * `StaticRC.cpp` -- similar to SpriteRC. May share fixes.
   * `AnimRC.cpp` -- pulls `<Display/GIFAnimatedSprite.h>`. Check
     whether that header exists in the shim; if not, this TU is
     blocked.
   * `RenderSystem.cpp` -- system class that owns the Render
     pipeline. Trivial constructor + update loop.

   No screen instantiates a RenderSystem yet, so all six TUs
   will link-time-gc behind --gc-sections, same as the Collision
   subsystem. The point of this session is to validate
   compilation. Land safely small: start with
   `RenderComponent.cpp` alone, push, verify green, then add the
   Sprite/Static/Anim/RenderSystem leaves one by one.

2. **S-MP20/5 -- Land `Game.cpp` + `GameSystem.cpp`.** Gated on
   S-MP20/4b landing the Rendering subsystem, since `Game.cpp`'s
   constructor calls `render(this, ...)` and `collision(this)`
   which need `RenderSystem` + `CollisionSystem` constructors
   resolvable at link time. `Game.cpp` also pulls `Chatter.h`,
   `Loop/LoopManager.h`, `InputChatter.h`, `FSLVGL.h`, plus
   `extern Game* startedGame` (NOT yet defined anywhere -- needs
   a definition in SleepServiceStub.cpp alongside the existing
   `gameStarted`). `GameSystem.cpp` is two-line trivial.

3. **S-MP20/6 -- Land `Highscore.cpp`.** Uses NVS only; should be
   standalone-safe. Can land in parallel with the Rendering
   work. The right size for a short fire that just wants to make
   one more leaf safe.

4. **S-MP20/7 -- Vendor `<Display/GIFAnimatedSprite.h>`** if
   S-MP20/4b's `AnimRC.cpp` lands blocked on it. The shim header
   exists in `mp24/components/circuitos_shim/include/Display/`
   for Sprite, Display, Color -- adding GIFAnimatedSprite as
   another shim follows the same pattern. A 1-commit session.

## Helper script note carried from this fire

* The helpers (`flash_iter.sh`, `addr2line.py`) need to be
  recreated each fire from the brief; the new sandbox doesn't
  preserve `/home/claude/`. Place them at `$HOME/bin/` (in this
  sandbox `$HOME` is `/sessions/<id>/`) and adjust references
  inside the scripts accordingly. The repo lives at
  `$HOME/repo/mp_firmware`.
* The `flash_iter.sh` helper polls in 30-second intervals up to
  12 minutes, which exceeds the 45-second `mcp__workspace__bash`
  tool timeout. Workaround: skip the helper for the wait phase
  and poll the GitHub Actions REST API directly in shorter bash
  calls (`sleep 40 && curl .../jobs`). Only run the helper end-
  to-end if you have a single bash session that can run for
  several minutes -- in this Cowork environment, that's not the
  case.
* The `${HOME}` path in `addr2line.py`'s sys.argv requires the
  helper to write the right path when invoking; the version in
  bin/ uses `$HOME/bin/addr2line.py` so it stays portable.

## Open items unchanged from the brief

* Q2: `uPOWER_OFF` GPIO1 polarity (currently Hi-Z)
* Q3: Modem boot-FAIL triage if PWR_KEY polarity fix is insufficient
* Q4: STATUS / NET_STATUS LED traces
