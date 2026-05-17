# MAKERphone v2.4 Port — Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-17 ~13:55
* **HEAD on `main`:** `dde74d9` (`fix(build): S-MP20/2/1 -- undef Arduino's radians/degrees macros before glm`)
* **Build status:** GREEN. Run `25992715543` build job `success`. The
  prior run `25992610037` (commit `65572e3`, just the SRCS addition
  without the macro fix) was the one and only fix-forward needed.
* **Device boot status:** STILL UNKNOWN, but the failure mode
  IMPROVED. The `flasher` runner (`AlberttekiMacBook-Pro`) was
  *offline* at the start of this fire and *online* by the end
  -- it picked up the flash job for `dde74d9` and failed at the
  earlier "Detect MAKERphone serial port" step with:

      No MAKERphone serial device found at /dev/cu.usb*.
      Plug the board in via USB-C, then re-run this workflow.

  This is a clearer, more actionable signal than the previous
  fafeef9 runs (which got past port detection and then died in
  esptool retry storms). The board appears unplugged or
  un-enumerating; the runner itself is healthy.

## What S-MP20/2 + S-MP20/2/1 actually shipped

S-MP20/2 (`65572e3`) added a single TU to chatter_app SRCS:

    "${SRC_DIR}/Games/GameEngine/GameObject.cpp"

This is the smallest leaf .cpp in the games engine. Its only
non-stdlib includes are GameObject.h + RenderComponent.h +
PixelDim.hpp + CollisionComponent.h. PixelDim.hpp pulls
`<glm.h>` (the Arduino-GLM compat shim), which forwards to
`<glm/glm.hpp>` + `<glm/ext.hpp>`. The body only move-
constructs shared_ptrs and assigns `glm::vec2` / `float` --
no constructor calls into RenderSystem or CollisionSystem,
so the link closure stays small.

The build of `65572e3` failed because Arduino.h (pre-included
into every chatter_app TU via `-include Arduino.h`) defines
`radians`, `degrees`, `sq`, `constrain`, `round`, `bit` as
preprocessor macros. glm declares same-named free functions in
the `glm::` namespace; the preprocessor textually rewrites
glm's declarations and the parser dies with

    error: macro 'radians' passed 3 arguments, but takes just 1
    error: macro 'degrees' passed 3 arguments, but takes just 1

S-MP20/2/1 (`dde74d9`) fixes this by adding a tiny compat header

    mp24/components/chatter_app/shim_includes/arduino_glm_compat.h

that `#undef`s the six colliding macros, and force-includes it
via `target_compile_options(... "SHELL:-include ${dir}/.../arduino_glm_compat.h")`
AFTER `-include Arduino.h`. Subsequent `<glm/...>` parses see
the macros already gone.

Intentionally PRESERVED macros: `abs` (used in 3 upstream call
sites: JigHWTest.cpp:187, Pong/GameState.cpp x2), `min`, `max`
(heavily used as Arduino macros; glm never declares them at
namespace scope so no collision).

## What the next fire should do

1. Re-run `flash_iter.sh --no-dispatch` first to read the most
   recent run. Possible states:
   * `dde74d9` flash now succeeded -> proceed with S-MP20/3
     (add the next GameEngine .cpp pair: `Game.cpp` +
     `GameSystem.cpp`). Game.cpp pulls in `render(this, Sprite*)`
     + `collision(this)` constructor calls into RenderSystem +
     CollisionSystem; those .cpp files are NOT yet in SRCS, so
     adding `Game.cpp` alone produces undefined references at
     link. Either add `Game.cpp` + `GameSystem.cpp` + the
     RenderSystem.cpp + CollisionSystem.cpp .cpp implementations
     in the same commit, OR start with the smaller leaf pair
     `Rendering/RenderComponent.cpp` + `Collision/CollisionComponent.cpp`
     (look these up first -- their bodies may also be empty
     enough that they link with no new symbols).
   * `dde74d9` flash still failing on port detection -> user
     needs to physically replug the MAKERphone USB-C cable.
     Leave repo as-is; do not pile commits.
2. Once a real boot.log lands for `dde74d9`, sanity-check that
   the binary still boots cleanly (it should -- GameObject.cpp
   only adds dead symbols, none of which are referenced by
   anything in the current link closure).
3. Continue down the S-MP20 punch list once the engine .cpp's
   compile:
   * Snake / SpaceInvaders / Bonk / SpaceRocks game impls
   * GamesScreen.cpp, PhoneGamesScreen.cpp, PhoneDialerScreen.cpp
   * Wire PhoneHomeScreen::setOnLeftSoftKey (CALL) -> push
     PhoneDialerScreen
   * Wire PhoneMainMenu::Phone and ::Games tiles

## Helper script update applied this fire (not committed)

`/home/claude/flash_iter.sh` previously gated only on
`BUILD_CONCL` and skipped checking `FLASH_CONCL`. That meant a
flash failure would still proceed to the boot.log scan and could
falsely report "device healthy" off a stale log. This fire added

    [[ "$FLASH_CONCL" != "success" ]] && {
        echo "Flash failed (device unresponsive or runner issue)";
        exit 1;
    }

between the build-success guard and the artifact-scan step.
The helper lives outside the repo, so this is an agent-side fix
that persists across fires only if the helper file is recreated
from this checkpoint's notes.

## Open items unchanged from the brief

* Q2: `uPOWER_OFF` GPIO1 polarity (currently Hi-Z)
* Q3: Modem boot-FAIL triage if PWR_KEY polarity fix is insufficient
* Q4: STATUS / NET_STATUS LED traces
