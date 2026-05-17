# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-17 ~18:08
* **HEAD on `main`:** `5cca539` (`revert(mp24): S-MP20/7e -- back
  out Snake.cpp landing`). Last forward-progress commit on disk
  is still `fda498b` (S-MP20/6d) -- this fire attempted /7e on top
  of that, the attempt failed, and the revert restored the
  pre-/7e tree. Net effect on disk: identical to commit
  `1695f5a` (the prior fire's checkpoint HEAD).
* **Build status:** GREEN expected at HEAD `5cca539` (revert
  restores byte-identical contents to known-green pre-/7e state).
  /7e attempt at `4999a7b` failed to build (run `25998582162`,
  build failure on `src/Games/Snake/Snake.cpp.obj`).
* **Device boot status:** HEALTHY. Same boot log shape as prior
  fires:

      STORE: mounted; 2 files, 1757 / 1860161 bytes used (0%)
      POWER: polling GPIO2 at 50 Hz, debounce 3 ticks
      MP24: Entering background-tasks-only loop (LVGL owns the panel).
      MODEM: state -> POWER -> BOOT -> boot probe... (up to 16 s)

* **Flasher status:** ONLINE and reliable.
* **Binary size:** 883408 bytes / 2 MB partition (43% used, 1.16
  MB free) -- unchanged from the prior fire (revert restores
  identical disk contents).

## What this fire actually shipped (net)

Two commits, the second reverts the first:

1. **`4999a7b` -- S-MP20/7e (REVERTED in `5cca539`)** -- attempted
   to re-land Snake.cpp by patching one line in
   `mp24/components/circuitos/src/UI/Element.h`:

       -#include "../Display/Sprite.h"
       +#include <Display/Sprite.h>

   Premise: bracket include routes through INCLUDE_DIRS, where
   circuitos_shim is FIRST in chatter_app's REQUIRES, so the
   SHIM Sprite class would win for any TU that transitively
   pulls upstream UI/Element.h.

   FAILED at build: the patch addresses only ONE of the chains
   pulling upstream Sprite.h. Snake.cpp also pulls upstream
   Sprite via `<Support/Context.h>`:
       Snake.h:10 <Support/Context.h>
         Context.h:5 "ContextTransition.h"
           ContextTransition.h:5 "../Display/Display.h"     <-- upstream
             Display.h:10 "Sprite.h"                        <-- upstream Sprite (FIRST)
       Snake.h:11 <UI/Image.h>
         ... eventually Element.h:6 <Display/Sprite.h>      <-- now SHIM
                                                                (DUPLICATE)

   Build run `25998582162` reported "redefinition of 'class
   Sprite'" + "redefinition of 'class Display'" + downstream
   "no member named readPixel / printf / drawFastHLine".

2. **`5cca539` -- revert(mp24): S-MP20/7e** -- backs out 4999a7b.
   Net effect on disk: identical to `1695f5a`.

## Why S-MP20/7e failed -- FULL DIAGNOSIS for the next fire

Two distinct ROOT causes need fixing, in order:

### Root cause A: dual `class Sprite` and dual `class Display`

The patch on `UI/Element.h` only handled the Snake.h:11
`<UI/Image.h>` chain. The OTHER chain via Snake.h:10
`<Support/Context.h>` still pulls upstream `Display.h` via
relative `"../Display/Display.h"` in `Support/ContextTransition.h`,
and upstream `Display.h` line 10 then pulls upstream `Sprite.h`
via the relative same-dir `"Sprite.h"`.

To fix this, the next fire needs ALSO this patch:

    mp24/components/circuitos/src/Support/ContextTransition.h:5
      -#include "../Display/Display.h"
      +#include <Display/Display.h>

That routes Display.h's pull to the SHIM `Display.h` (which uses
`#pragma once` and does NOT pull upstream `Sprite.h` via a
relative include). Upstream `Display.h` would never enter the
TU, so its `#include "Sprite.h"` (line 10) never fires either.

Risk analysis: Support/ContextTransition.h is upstream-circuitos
code. Audit of all in-SRCS .cpp files in chatter_app:

* `Support/Context.h` is included by `src/Games/Snake/Snake.h`,
  `src/Games/Pong/Bonk.h`, and `src/Games/Invaders/SpaceInvaders.h`
  -- all three game headers. None of these games are in SRCS
  today, so the patch is a no-op for the currently-compiling
  set.
* `Support/ContextTransition.h` itself is NOT directly included
  by anything in `src/`. It's pulled only transitively via
  `Context.h:5`. (`grep -rln Support/ContextTransition\.h src/`
  returns zero hits.)

So patching ContextTransition.h is risk-free for current SRCS.

### Root cause B: shim Sprite is missing methods Snake.cpp uses

Even AFTER both Sprite + Display dual-class definitions are
eliminated, Snake.cpp still fails because the shim Sprite is
missing five methods Snake calls:

* `uint16_t Sprite::readPixel(int32_t x, int32_t y)` -- 5 call
  sites (Snake collision-detect logic). Reads a pixel from the
  framebuffer; on the 9C shim this can be a stub returning 0
  (collision detection will misbehave silently, fine for compile
  + link validation).
* `int Sprite::printf(const char* fmt, ...)` -- 3 call sites
  (Snake status overlay). Need a printf-style stub; safe to
  forward-declare without implementation (no-op variadic) or
  redirect to `print()` via vsnprintf into a stack buffer.
* `Sprite::drawFastHLine(int32_t x, int32_t y, int32_t w,
   uint16_t color)` -- 1 call site (Snake border). Stub by
  forwarding to existing `drawLine(x, y, x+w, y, color)` or
  no-op.

The `setTextColor(uint16_t)` confusion at Snake.cpp lines 613 /
637 / 737 / 739 / 741 is reported as
`invalid conversion from 'uint16_t' to 'const char*'`. Likely
side-effect of the class confusion (compiler picked upstream
Sprite, which has a different overload set). Should disappear
once root cause A is fixed; if not, the shim already has both
1-arg and 2-arg `setTextColor` overloads, so a fresh CI run
will clarify.

## What the next fire should do

**S-MP20/7f -- second Snake.cpp landing attempt.**

Three commits, in this order:

1. **`fix(mp24): S-MP20/7f1 -- patch ContextTransition.h to bracket include`**
   * `mp24/components/circuitos/src/Support/ContextTransition.h:5`:
     `"../Display/Display.h"` -> `<Display/Display.h>`
   * Push. Expect green (no in-SRCS change, just a header path
     adjustment that no current SRCS member walks).

2. **`fix(mp24): S-MP20/7f2 -- add readPixel/printf/drawFastHLine stubs to shim Sprite`**
   * In `mp24/components/circuitos_shim/include/Display/Sprite.h`,
     add declarations:
       ```
       uint16_t readPixel(int32_t x, int32_t y);
       int      printf(const char* fmt, ...);
       void     drawFastHLine(int32_t x, int32_t y,
                              int32_t w, uint16_t color);
       ```
   * In `mp24/components/circuitos_shim/src/MP24Sprite.cpp` (or
     wherever the existing Sprite method stubs live -- check
     `grep -l 'Sprite::drawPixel' mp24/components/circuitos_shim/`),
     add the no-op bodies. `readPixel` returns `0`. `printf`
     returns `0`. `drawFastHLine` either no-ops or forwards to
     `drawLine`.
   * Push. Expect green (binary delta: a few hundred bytes for
     the new methods, but they're unreferenced by any in-SRCS
     TU so `--gc-sections` drops them).

3. **`feat(mp24): S-MP20/7f3 -- land Snake.cpp via combined fixes`**
   * Re-apply the `Element.h` bracket-include patch (from `4999a7b`).
   * Re-add the Snake.cpp block to `chatter_app/CMakeLists.txt`
     (from `4999a7b`).
   * Push. Expect Snake.cpp's TU to compile (because the dual-
     Sprite/Display redefinitions are gone AND the missing
     method stubs now exist). Binary delta: 0 -- nothing
     instantiates Snake yet, so `--gc-sections` drops it.

If any of 7f1 / 7f2 / 7f3 fails, follow the standard
fix-forward-or-revert rule. 7f1 in particular is super low-risk
and is the foundation; if it fails, something is very wrong with
my analysis.

If Snake.cpp's setTextColor errors persist after the dual-class
fix, the next-next fire adds an explicit 1-arg overload that
takes precedence (the shim already has both 1-arg and 2-arg
forms, but the upstream-Sprite-bound compiler may have been
picking a different overload than we expect).

### Alternative path if 7f1 or 7f2 fails

If the ContextTransition.h patch breaks something we didn't
predict, the next fire can fall back to:

* **S-MP20/7g** -- skip games for now; do GamesScreen wiring
  + PhoneDialerScreen WITHOUT pulling Snake.h transitively.
  Audit each phone-screen .cpp for whether its header pulls
  any of `<UI/Image.h>`, `<Support/Context.h>`,
  `<Games/Snake/Snake.h>`, etc.
* **S-MP21** -- jump ahead to modem hardware bring-up; this
  doesn't depend on games at all.

## Why this fire could not finish the work

The 30-minute fire budget allows ~3-4 CI cycles at 5-8 min each.
This fire used one CI cycle on the failed /7e attempt and one
on the revert. That left no budget for the larger /7f sequence,
which is 3 commits / 3 CI cycles = ~20+ min. Better to ship the
diagnosis and let the next fire start clean with a clear plan.

## Helper script note carried from prior fires

* The helpers (`flash_iter.sh`, `addr2line.py`) need to be
  recreated each fire from the brief; the new sandbox doesn't
  preserve `/home/claude/`. In this sandbox the working
  directory is `/tmp/work_mp24/mp_firmware`, the user's
  `/tmp/claude/` path from a previous fire is owned by
  `nobody:nogroup` (different namespace) and is not writable.
  The full helper-script invocation is impractical inside the
  45-second bash tool timeout -- poll the GitHub Actions REST
  API directly in shorter chunks (`sleep 35-40 && curl
  .../jobs`). This fire used the chunked approach successfully.
* The CI workflow triggers on push automatically via the
  `mp24/**` / `src/**` path filter -- no manual
  `workflow_dispatch` needed.
* `pyelftools` was not needed this fire (no device crash).
* `git config --global --add safe.directory <path>` is needed
  if any leftover `/tmp/claude/repo/mp_firmware` from a prior
  fire shows up but is owned by a different user.

## Open items unchanged from the brief

* Q2: `uPOWER_OFF` GPIO1 polarity (currently Hi-Z)
* Q3: Modem boot-FAIL triage if PWR_KEY polarity fix is insufficient
* Q4: STATUS / NET_STATUS LED traces

## Roadmap status snapshot

* **S-MP20** -- in progress (Snake.cpp blocked on dual-Sprite +
  missing-method chain; /6d header shims in place + green;
  /7 and /7e both failed and reverted; /7f sequence planned)
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
  * /6c: TextInput.cpp landed via parallel-safe SRCS add
    (commit `a1b60d1`)
  * /6d: LovyanGFX-style text API stubs + <Pins.hpp> transitive
    in shim Chatter.h (commit `fda498b`)
  * /7: Snake.cpp landing attempt -- REVERTED (commits
    `358bad7` shipped, `ffae1ba` reverted). Diagnosis: dual
    Sprite class because UI/Image.h chain pulls upstream Sprite.
  * **/7e: Second Snake.cpp landing attempt with UI/Element.h
    bracket-include patch -- REVERTED (commits `4999a7b`
    shipped, `5cca539` reverted). Diagnosis: ContextTransition.h
    chain ALSO pulls upstream Display + Sprite, AND shim Sprite
    is missing readPixel/printf/drawFastHLine. THIS FIRE.**
  * /7f: PLANNED for next fire -- ContextTransition.h patch +
    shim Sprite method stubs + Snake.cpp landing, in three
    commits with one CI cycle each.
* **S-MP21** -- not started (modem hardware bring-up)
* **S-MP22+** -- not started
