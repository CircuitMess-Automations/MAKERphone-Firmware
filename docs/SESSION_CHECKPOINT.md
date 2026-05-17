# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-17 ~17:53
* **HEAD on `main`:** `ffae1ba` (`revert(mp24): S-MP20/7 -- back
  out Snake.cpp landing (Sprite redefinition)`). Last
  forward-progress commit is `fda498b` (`feat(mp24): S-MP20/6d --
  LovyanGFX-style text API stubs + Pins.hpp transitive include`).
  A docs checkpoint commit on top of this one will be outside the
  CI path filter, so the green build state is preserved.
* **Build status:** GREEN at HEAD `ffae1ba`. Run `25998256465` --
  build job `success`, flash job `success`. /6d was also green
  on its own (run `25997992243`). /7 attempt at `358bad7` failed
  to build and was reverted (run `25998099252`).
* **Device boot status:** HEALTHY. boot.log from artifact
  `7044683009` (40 lines, 0 crash markers). Byte-identical in
  shape to prior fires' boot.log:

      STORE: mounted; 2 files, 1757 / 1860161 bytes used (0%)
      POWER: polling GPIO2 at 50 Hz, debounce 3 ticks
      MP24: Entering background-tasks-only loop (LVGL owns the panel).
      MODEM: state -> POWER -> BOOT -> boot probe... (up to 16 s)

* **Flasher status:** ONLINE and reliable.
* **Binary size:** 883408 bytes / 2 MB partition (43% used, 1.16
  MB free). **Identical to the prior fire's 883408** -- /6d's
  three header additions are pure header expansion, zero binary
  delta as predicted (no in-SRCS file currently references the
  new `textdatum_t` enum, `fonts::IFont` selectors, or
  setTextDatum/setFont overloads, so `--gc-sections` drops them).

## What this fire actually shipped (net)

Two commits added forward progress, one reverted /7:

1. **`fda498b` -- S-MP20/6d** (preserved on top of the revert)
   Three header additions to circuitos_shim:
     - `include/TFT_eSPI.h`: new `enum class textdatum_t : uint8_t`
       (top_*/middle_*/bottom_*/baseline_*) + `namespace fonts`
       with `struct IFont`, `inline constexpr IFont Font0{}` /
       `Font2{}`. C++17 inline constexpr -> single addressable
       definition across TUs.
     - `include/Display/Sprite.h`: new inline overloads
       `void setTextDatum(textdatum_t)` and
       `void setFont(const fonts::IFont*)` -- both silent no-op,
       coexist with existing `setTextDatum(uint8_t)` /
       `setTextFont(uint8_t)` (pure overload additions).
     - `include/Chatter.h`: new `#include <Pins.hpp>` so any TU
       that pulls `<Chatter.h>` gets the Decision-E BTN_LEFT /
       BTN_A / BTN_UP / BTN_1..9 macro aliases transitively.

2. **`358bad7` -- S-MP20/7 (REVERTED in `ffae1ba`)**
   Single SRCS add: `${SRC_DIR}/Games/Snake/Snake.cpp`. Failed
   to build. See diagnosis below.

3. **`ffae1ba` -- revert(mp24): S-MP20/7** -- backs out 358bad7.
   Net effect on disk: identical to `fda498b`.

## Why S-MP20/7 failed -- DIAGNOSIS for the next fire

Build run `25998099252` produced these errors against Snake.cpp:

      mp24/.../circuitos_shim/include/Display/Sprite.h:27:
        error: redefinition of 'class Sprite'
      mp24/.../circuitos_shim/include/Display/Display.h:27:
        error: redefinition of 'class Display'
      src/Games/Snake/Snake.cpp:77:
        'class Sprite' has no member named 'setTextDatum'
      src/Games/Snake/Snake.cpp:81:
        'class Sprite' has no member named 'setFont'
      src/Games/Snake/Snake.cpp:83:
        'class Sprite' has no member named 'drawString'
      src/Games/Snake/Snake.cpp:83:
        'class Sprite' has no member named 'width'
      src/Games/Snake/Snake.cpp:208:
        'class Sprite' has no member named 'drawRect'
      src/Games/Snake/Snake.cpp:209:
        'class Sprite' has no member named 'drawPixel'
      src/Games/Snake/Snake.cpp:216:
        'class Sprite' has no member named 'fillRect'
      (plus many more 'no matching function' for setTextColor(int))

Root cause (chain traced from the build log):

1. Snake.h includes `<UI/Image.h>`. UI/Image.h exists ONLY at
   `mp24/components/circuitos/src/UI/Image.h` (upstream). The
   bracket include resolves through INCLUDE_DIRS to upstream
   (circuitos_shim has no UI/Image.h).

2. Upstream UI/Image.h pulls "SpriteElement.h" (relative quoted
   include) -> upstream SpriteElement.h. Which pulls
   "ElementContainer.h" (relative) -> upstream. Which pulls
   "Element.h" (relative) -> upstream. Which does:

       #include "../Display/Sprite.h"   <-- RELATIVE QUOTED

   Relative quoted includes bypass INCLUDE_DIRS and resolve
   against the source file's directory. So this resolves to
   `mp24/components/circuitos/src/Display/Sprite.h` -- the
   UPSTREAM Sprite class definition gets pulled into Snake's TU.

3. Snake.cpp ALSO includes `<Chatter.h>` (the shim, after the
   bracket lookup hits circuitos_shim first via REQUIRES). Shim
   Chatter.h pulls `<Display/Display.h>` -> SHIM Display.h.
   SHIM Display.h only forward-declares Sprite, so by itself
   it doesn't trigger a Sprite redefinition.

4. BUT Snake's full transitive include set also pulls
   `<Display/Sprite.h>` via bracket include (e.g. via
   Game.h -> Rendering/RenderSystem.h, or any other in-SRCS
   header that goes through SpriteRC.h-style chains). Bracket
   `<Display/Sprite.h>` resolves through INCLUDE_DIRS to the
   SHIM Sprite.h. So both shim Sprite.h AND upstream Sprite.h
   get included in Snake's TU.

5. Header guards do not deduplicate them:
     - Shim Sprite.h uses `#pragma once` (path-based dedup).
     - Upstream Sprite.h uses `#ifndef SWTEST_SPRITE_H` macro.
   Different keys, so the C preprocessor sees them as distinct
   files -> two `class Sprite : public TFT_eSprite { ... }`
   definitions enter the TU -> redefinition error.

6. Same root cause for Display.h (`#pragma once` vs
   `SWTEST_DISPLAY_H`).

7. The "no member named setTextDatum / setFont / ..." errors
   are downstream consequences. After the redefinition errors,
   the compiler keeps going and resolves `baseSprite->...`
   against the UPSTREAM Sprite class (whichever was seen
   first) -- which has none of the methods our shim provides
   (drawString, drawRect, drawPixel, fillRect, width(), etc.
   live ONLY on the shim Sprite class).

Why the existing in-SRCS GameEngine TUs (Game.cpp,
GameSystem.cpp, Highscore.cpp, TextInput.cpp,
RenderComponent.cpp, RenderSystem.cpp, StaticRC.cpp,
SpriteRC.cpp, AnimRC.cpp) don't hit this:
  - None of them transitively pull `<UI/Image.h>`. They go
    through bracket `<Display/Sprite.h>` only, which routes
    to the SHIM. The upstream relative-include chain via
    `"../Display/Sprite.h"` never fires for these TUs because
    nothing pulls upstream UI/Element.h.

## What the next fire should do

Pick ONE of these. Roughly ordered by risk-adjusted payoff.

1. **S-MP20/7 retry via header-guard alignment.** Add an
   `#define SWTEST_SPRITE_H 1` (and `SWTEST_DISPLAY_H`,
   `SWTEST_COLOR_H` if needed) to the TOP of the corresponding
   shim header, BEFORE the `class Sprite` definition. Combined
   with ensuring the SHIM is included FIRST in Snake's TU, this
   makes the upstream copy's `#ifndef SWTEST_SPRITE_H` skip on
   second include (shim wins).

   To force shim-first inclusion for Snake.cpp ONLY (not for
   the 232 other TUs in chatter_app):

       set_source_files_properties(
           "${SRC_DIR}/Games/Snake/Snake.cpp"
           PROPERTIES COMPILE_FLAGS
           "-include Display/Sprite.h -include Display/Display.h"
       )

   This forces the shim copies to be the first thing the
   compiler sees for Snake.cpp's TU. The `<Display/Sprite.h>`
   bracket resolves through INCLUDE_DIRS to circuitos_shim
   (since it's FIRST in REQUIRES).

   Budget: full 30-min fire, one or two fix-forwards expected.

2. **S-MP20/7 retry via Element.h patch.** Edit
   `mp24/components/circuitos/src/UI/Element.h`:

       #include "../Display/Sprite.h"   ->   #include <Display/Sprite.h>

   Bracket include routes through INCLUDE_DIRS -> SHIM Sprite.h.
   This eliminates the dual-Sprite condition entirely. Touches
   upstream circuitos source (which is rare but not forbidden).
   Net effect across the codebase: any TU that previously pulled
   upstream Sprite.h via Element.h's relative include now gets
   SHIM Sprite.h instead. Cross-check: list every in-SRCS .cpp
   that transitively pulls Element.h; verify their compile is
   unaffected by the swap.

   Budget: full 30-min fire, careful audit step before push.

3. **S-MP20/7b -- pick Bonk instead of Snake.** Audit Bonk.h /
   Bonk.cpp transitive includes; if they don't pull UI/Image.h
   (they probably don't -- Bonk uses Sprite via Game.h's
   forward decl, doesn't need Image), Bonk lands cleanly under
   the existing /6d shims. Bonk has sub-state files
   (GameState.cpp, TitleState.cpp, PauseState.cpp) but those
   would land in /7c, /7d, /7e separately.

4. **S-MP20/8 -- skip games for now, do PhoneDialerScreen
   first.** PhoneDialerScreen.cpp is in the brief's S-MP20
   roadmap (".../Phone/PhoneDialerScreen.cpp to SRCS"). It's
   probably less entangled with the dual-Sprite problem.
   Defer the four game .cpp files until a later fire.

## Helper script note carried from prior fires

* The helpers (`flash_iter.sh`, `addr2line.py`) need to be
  recreated each fire from the brief; the new sandbox doesn't
  preserve `/home/claude/`. In this sandbox the user's home
  directory has restricted permissions and the repo lives at
  `/tmp/repo/mp_firmware`. The full helper-script invocation is
  impractical inside the 45-second bash tool timeout -- poll
  the GitHub Actions REST API directly in shorter chunks
  (`sleep 35-40 && curl .../jobs`). This fire used the chunked
  approach successfully across three CI runs.
* The CI workflow triggers on push automatically via the
  `mp24/**` / `src/**` path filter -- no manual
  `workflow_dispatch` needed.
* `pyelftools` was not needed this fire (no crash to decode).
* The repo lives at `/tmp/repo/mp_firmware`. `git clone --depth
  100` works fine (~25 MB).

## Open items unchanged from the brief

* Q2: `uPOWER_OFF` GPIO1 polarity (currently Hi-Z)
* Q3: Modem boot-FAIL triage if PWR_KEY polarity fix is insufficient
* Q4: STATUS / NET_STATUS LED traces

## Roadmap status snapshot

* **S-MP20** -- in progress (Snake.cpp blocked on dual-Sprite
  redefinition; /6d header shims in place + green)
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
  * **/6d: LovyanGFX-style text API stubs + <Pins.hpp>
    transitive in shim Chatter.h (commit `fda498b`) <-- THIS
    FIRE -- new `textdatum_t` enum, `fonts::IFont` selectors,
    setTextDatum(textdatum_t) / setFont(const IFont*) overloads
    on Sprite, and shim Chatter.h now pulls <Pins.hpp> so any
    .cpp that goes through it gets BTN_* macros transitively.
    Zero binary delta. Verified green: run 25997992243 and
    25998256465.**
  * **/7: Snake.cpp landing attempt -- REVERTED (commits
    `358bad7` shipped, `ffae1ba` reverted). Dual-Sprite class
    redefinition issue caused by Snake.h pulling UI/Image.h
    which transitively pulls upstream Sprite.h via relative
    quoted include. See diagnosis section above for the path
    forward.**
* **S-MP21** -- not started (modem hardware bring-up)
* **S-MP22+** -- not started
