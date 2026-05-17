# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-17 ~18:30
* **HEAD on `main`:** `a561c51` (`fix(mp24): S-MP20/7f3/1 -- add
  Print-style overloads to shim TFT_eSPI`). This fire shipped
  FOUR commits, all on `main`:
    - `0109c89` -- fix(mp24): S-MP20/7f1 -- ContextTransition.h
      bracket-include patch.
    - `3a02c01` -- fix(mp24): S-MP20/7f2 -- shim Sprite gains
      readPixel/printf/drawFastHLine no-op stubs.
    - `6a71c4f` -- feat(mp24): S-MP20/7f3 -- Element.h bracket
      patch + Snake.cpp in chatter_app SRCS. (Failed build; one
      fix-forward applied.)
    - `a561c51` -- fix(mp24): S-MP20/7f3/1 -- shim TFT_eSPI gains
      Print-style overloads (uint16_t / char / int / long /
      String). Snake.cpp now COMPILES + LINKS. **GREEN.**
* **Build status:** GREEN at HEAD `a561c51` (run `25999021934`,
  both `build` and `flash` jobs completed/success).
* **Device boot status:** HEALTHY. boot.log scan: 0 crash
  markers. Same boot sequence as the prior fire's green HEAD:

      STORE: mounted; 2 files, 1757 / 1860161 bytes used (0%)
      POWER: polling GPIO2 at 50 Hz, debounce 3 ticks
      MP24: Entering background-tasks-only loop (LVGL owns the panel).
      MODEM: state -> POWER -> BOOT -> boot probe... (up to 16 s)

* **Flasher status:** ONLINE and reliable.
* **Binary size:** `mp24-firmware` artifact is 8,430,834 bytes
  (zip; includes .elf + bin + map files). The actual
  `makerphone_mp24.bin` inside is ~883 KB / 2 MB app partition
  (~43% used, ~1.16 MB free) -- essentially unchanged from the
  prior fire's 883,408 bytes. Snake.cpp's TU compiles in but
  --gc-sections drops it at link time (no in-SRCS instantiation
  site references Snake's symbols), so binary delta is ~0 bytes
  as predicted.

## What this fire actually shipped (net)

Four commits, all GREEN at the final HEAD:

1. **`0109c89` -- S-MP20/7f1.** Patched
   `mp24/components/circuitos/src/Support/ContextTransition.h:5`
   from `"../Display/Display.h"` to `<Display/Display.h>`. Routes
   the Snake-transitive include chain through the SHIM Display.h
   instead of upstream. Build + flash green.

2. **`3a02c01` -- S-MP20/7f2.** Added three TFT_eSprite-style
   methods (`readPixel`, `printf`, `drawFastHLine`) to the shim
   Sprite class. Both `Sprite.h` header decls + `MP24Sprite.cpp`
   no-op bodies. <stdarg.h> added for printf's va_list. Build
   + flash green.

3. **`6a71c4f` -- S-MP20/7f3.** Patched
   `mp24/components/circuitos/src/UI/Element.h:6` to bracket-
   include `<Display/Sprite.h>` (re-applying the failed /7e
   patch, now safe). Added `"${SRC_DIR}/Games/Snake/Snake.cpp"`
   to chatter_app SRCS, in the Games section after TextInput.cpp.
   **Build FAILED** with five errors at Snake.cpp lines 613, 637,
   737, 739, 741 -- all `print(...)` calls with non-`const char*`
   arguments.

4. **`a561c51` -- S-MP20/7f3/1 (fix-forward).** Diagnosis: the
   /7e post-mortem's prediction that setTextColor errors would
   disappear once dual-Sprite was fixed was WRONG. The actual
   root cause was that shim `TFT_eSPI` does not derive from
   Arduino's `Print` class, so it lacked `print(char)`,
   `print(uint16_t)` (via `print(unsigned int)`), etc.
   Fix: added inline no-op overloads for the full Print family
   (char / unsigned char / int / unsigned int / long /
   unsigned long / double / String) plus matching `println`
   variants. Build + flash GREEN. **Snake.cpp's TU is now in
   the binary.** (Dropped at link time by --gc-sections because
   nothing instantiates Snake yet; binary size unchanged.)

## Why this fire could finish the /7f sequence

The 30-minute fire budget allowed pipelining: I pushed /7f1
first, then prepared /7f2 locally while CI ran, pushed /7f2
once /7f1's build went green (parallel CI on self-hosted
runner), prepared /7f3 locally while /7f2 ran, and pushed /7f3
once /7f2's build went green. When /7f3 failed I had ~15 min
left in the fire to triage + fix-forward; the fix was a simple
overload-set extension and went green on first try. Five CI
cycles total in ~25 minutes. The diagnosis-first-then-fix
approach from the prior fire's post-mortem made the
fix-forward fast (no flailing on what could be wrong).

## What the next fire should do

**S-MP20/8 -- second game implementation lands: SpaceInvaders.**

The /7f sequence proved out the pattern:
  1. Routing fix in upstream circuitos to bracket-include
     Display.h via Support/ContextTransition.h (DONE -- /7f1).
  2. Routing fix in upstream circuitos to bracket-include
     Sprite.h via UI/Element.h (DONE -- /7f3).
  3. Shim TFT_eSPI exposing the full Print overload family
     (DONE -- /7f3/1).
  4. Shim Sprite exposing readPixel/printf/drawFastHLine
     (DONE -- /7f2).

For SpaceInvaders (the second game in the roadmap order from
the brief):
  * Audit `src/Games/Invaders/SpaceInvaders.{h,cpp}` for any
    additional TFT_eSprite / Sprite methods we haven't stubbed.
    Likely candidates: `drawFastVLine`, `setFreeFont`,
    `fillTriangle`, `drawTriangle`, `drawRoundRect` (typical
    Bodmer/TFT_eSPI helpers). If any are called and missing,
    add no-op stubs to the shim Sprite / shim TFT_eSPI in one
    commit.
  * Add `"${SRC_DIR}/Games/Invaders/SpaceInvaders.cpp"` to
    chatter_app SRCS in a second commit (right after Snake.cpp,
    same Games section). Expect compile + link green; binary
    delta ~0 because still no instantiation site.

If SpaceInvaders surfaces a font-table dependency (the upstream
firmware ships PROGMEM font binaries that the shim TFT_eSPI's
`setFont(const fonts::IFont *)` accepts as no-op, but the data
itself must link), the missing symbols will be `fonts::Font2`
or similar. The shim currently has the type declared in
`TFT_eSPI.h` but no instance defined; if undefined-reference
fails at link, add the static instance definitions to
`circuitos_shim_stub.c` (or a new `MP24Fonts.cpp`) as empty
`fonts::IFont fonts::Font2 = {};` bodies.

If SpaceInvaders cleanly lands, **S-MP20/9** is Bonk (Pong),
then **S-MP20/10** is SpaceRocks. Each should take 1-2 commits.

After all four games are in SRCS, **S-MP20/11** is wiring
GamesScreen.cpp + PhoneGamesScreen.cpp into the in-SRCS set
(both files exist in src/ but are not in SRCS yet -- adding
them is the moment the link drops the --gc-sections protection
and the binary grows). This is the milestone where games
finally show up at runtime.

**S-MP20/12** wires `PhoneMainMenu`'s Games tile to push the
GamesScreen instance, and `PhoneHomeScreen::setOnLeftSoftKey`
to push a new `PhoneDialerScreen`. After /12, S-MP20 is done
and we move on to S-MP21 (modem hardware bring-up).

### Alternative path

If SpaceInvaders surfaces something that needs a deeper shim
change (e.g. requires real `Highscore` persistence rather than
the stub), the next fire can skip to:
  * **S-MP20/9** -- try Bonk first (simpler upstream game).
  * **S-MP21** -- jump ahead to modem hardware bring-up.

## Helper script note carried from prior fires

* The helpers (`flash_iter.sh`, `addr2line.py`) need to be
  recreated each fire from the brief; the new sandbox doesn't
  preserve `/home/claude/` (in fact, `/home/claude/` is not
  even writable -- `mkdir: cannot create directory '/home/
  claude': Permission denied`). In this fire's sandbox the
  user was `busy-keen-volta`, `$HOME` was `/sessions/busy-
  keen-volta`, and the only writable scratch path was
  `/sessions/busy-keen-volta/tmp/`. The repo cloned to
  `/sessions/busy-keen-volta/tmp/repo/mp_firmware/`. Note
  that the bash tool's 45-second timeout makes the helper
  scripts impractical to run end-to-end; this fire used the
  chunked GH API approach (poll jobs every 30-40s with a
  separate bash call each time) and it worked well.
* The CI workflow triggers on push automatically via the
  `mp24/**` / `src/**` path filter -- no manual
  `workflow_dispatch` needed. Updating
  `docs/SESSION_CHECKPOINT.md` does NOT trigger a build (it's
  outside the path filter), so the checkpoint commit at the
  end of each fire is free.
* `pyelftools` was not needed this fire (no device crash).
* `git config --global --add safe.directory <path>` is needed
  if any leftover repo from a prior fire shows up owned by a
  different user (the `nobody:nogroup` namespace from a prior
  sandbox's `/home/claude/` does NOT carry over to this
  sandbox's filesystem, so a fresh clone is the simplest
  start each fire).

## Open items unchanged from the brief

* Q2: `uPOWER_OFF` GPIO1 polarity (currently Hi-Z)
* Q3: Modem boot-FAIL triage if PWR_KEY polarity fix is insufficient
* Q4: STATUS / NET_STATUS LED traces

## Roadmap status snapshot

* **S-MP20** -- in progress (Snake.cpp landed THIS FIRE; three
  more games + GamesScreen + Phone wiring still to go).
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
    `358bad7` shipped, `ffae1ba` reverted).
  * /7e: Second Snake.cpp landing attempt -- REVERTED (commits
    `4999a7b` shipped, `5cca539` reverted).
  * **/7f1: ContextTransition.h bracket-include patch (commit
    `0109c89`) -- THIS FIRE.**
  * **/7f2: shim Sprite readPixel/printf/drawFastHLine stubs
    (commit `3a02c01`) -- THIS FIRE.**
  * **/7f3: Snake.cpp lands via Element.h patch + SRCS add
    (commit `6a71c4f`). Build initially RED -- five
    `print()` overload errors. -- THIS FIRE.**
  * **/7f3/1: shim TFT_eSPI gains Print-style overloads
    (commit `a561c51`). Build GREEN. Snake.cpp's TU compiles
    + links + is gc-sectioned away (binary delta 0). -- THIS
    FIRE.**
  * /8: PLANNED for next fire -- SpaceInvaders.cpp landing.
  * /9: PLANNED -- Bonk.cpp landing.
  * /10: PLANNED -- SpaceRocks.cpp landing.
  * /11: PLANNED -- GamesScreen + PhoneGamesScreen in SRCS;
    binary grows (4 games × ~5-15 KB each = ~20-60 KB delta).
  * /12: PLANNED -- wire PhoneMainMenu's Games tile +
    PhoneDialerScreen. After /12, S-MP20 is done.
* **S-MP21** -- not started (modem hardware bring-up)
* **S-MP22+** -- not started
