# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-17 ~18:52-19:02
* **HEAD on `main`:** `1eeb9d0` (`feat(mp24): S-MP20/9b -- land
  Bonk (Pong) + four state TUs in SRCS`). This fire shipped TWO
  commits, both on `main`, both clean on the first build:
    - `b819c86` -- feat(mp24): S-MP20/9a -- shim Sprite
      setTextWrap(bool,bool) + empty SD.h.
    - `1eeb9d0` -- feat(mp24): S-MP20/9b -- land Bonk (Pong) +
      four state TUs in SRCS. **GREEN on the first push.**
* **Build status:** GREEN at HEAD `1eeb9d0` (run `25999786525`,
  both `build` and `flash` jobs completed/success).
* **Device boot status:** HEALTHY. boot.log scan: 0 crash markers.
  Same boot sequence as prior fires' green HEADs:

      STORE: mounted; 2 files, 1757 / 1860161 bytes used (0%)
      POWER: polling GPIO2 at 50 Hz, debounce 3 ticks
      MP24: Entering background-tasks-only loop (LVGL owns the panel).
      MODEM: state -> POWER -> BOOT -> boot probe... (up to 16 s)

* **Flasher status:** ONLINE and reliable.
* **Binary size:** `makerphone_mp24.bin` is **883,408 bytes** at
  HEAD `1eeb9d0` -- EXACTLY UNCHANGED from `75174b4` and `a561c51`.
  Snake / SpaceInvaders / Bonk TUs all compile + link but
  --gc-sections drops them at link time because no in-SRCS
  instantiation site references their symbols yet. Binary delta
  as predicted: 0 bytes.

## What this fire actually shipped (net)

Two commits, both GREEN at the final HEAD (no fix-forwards):

1. **`b819c86` -- S-MP20/9a.** Pre-emptive shim additions to
   prepare for Bonk landing. Two surface-level additions, both
   driven by an audit of `src/Games/Pong/{Bonk,State,TitleState,
   GameState,PauseState}.{h,cpp}`:

     * Added two-arg `Sprite::setTextWrap(bool wrap_x, bool wrap_y)`
       (plus a one-arg form for completeness) as no-op overloads in
       `mp24/components/circuitos_shim/include/Display/Sprite.h`.
       Pong/GameState.cpp line 82 calls
       `display->setTextWrap(false, false)` -- the LovyanGFX 2-arg
       shape. Real Bodmer/TFT_eSPI exposes only a 1-arg form;
       LovyanGFX added the per-axis variant. Same dead-method
       pattern as the rest of the LovyanGFX text API stubs landed
       in /6d.

     * Added empty `mp24/components/circuitos_shim/include/SD.h`
       header. Pong/GameState.cpp + Pong/TitleState.cpp have a
       vestigial `#include <SD.h>` carried over from the original
       Chatter codebase -- but neither TU references any SD-
       related symbol. The `SD` global is never touched, no
       `SD.begin()` / `SD.open()` calls exist. The include is
       dead weight; an empty shim header resolves it without
       requiring an upstream patch. The `SD` global is
       intentionally absent so any latent caller will fail loud
       at link time rather than silently mis-routing.

   Both additions are DEAD until /9b lands; binary delta 0.
   Build + flash + boot all GREEN.

2. **`1eeb9d0` -- S-MP20/9b.** Added five Pong/.cpp files to
   chatter_app SRCS, immediately after SpaceInvaders.cpp in the
   Games section:

     * `${SRC_DIR}/Games/Pong/State.cpp`        (~1 line: base dtor)
     * `${SRC_DIR}/Games/Pong/TitleState.cpp`   (~60 LOC)
     * `${SRC_DIR}/Games/Pong/GameState.cpp`    (~175 LOC)
     * `${SRC_DIR}/Games/Pong/PauseState.cpp`   (~50 LOC)
     * `${SRC_DIR}/Games/Pong/Bonk.cpp`         (~70 LOC)

   Build GREEN on the first push. Bonk.cpp's Game subclass API
   surface is a strict subset of Snake's (Game ctor with empty
   resource list, pop(), Audio.play({chirps}) through the
   protected ChirpSystem member), so the Game.cpp + GameSystem.cpp
   + Highscore.cpp + TextInput.cpp scaffolding from /5 + /6 + /6c
   covers the engine path with no further additions. All five
   Pong TUs compile + link + gc-section'd; binary unchanged at
   883,408 bytes.

## Why this fire could finish /9 cleanly in one push

The 30-minute fire budget covered one CI cycle (~6 min total --
3-4 min build, ~30s flash, 20s boot capture, ~1 min artifact
download) with plenty of head room. The audit-first approach
identified the entire delta in ~5 minutes of Reading:

  * Bonk.h / Bonk.cpp / GameState.cpp / TitleState.cpp /
    PauseState.cpp / State.{hpp,cpp} -- read end-to-end.
  * Diffed include sets vs Snake.h / SpaceInvaders.h -- only
    two net-new transitive deps surfaced.
  * Cross-checked every `display->...` method call against the
    existing shim Sprite.h -- one missing overload.
  * Confirmed `<SD.h>` was unused (zero references to the `SD`
    symbol in any Bonk file).

Three of four games are now in SRCS. SpaceRocks is the last
remaining game (S-MP20/10).

## What the next fire should do

**S-MP20/10 -- fourth game implementation lands: SpaceRocks
(Asteroids).**

Files at `src/Games/Space/`:
  * `Player.h` + `Player.cpp` (~25 LOC -- player class)
  * `SpaceRocks.h` (~3.5 KB) + `SpaceRocks.cpp` (~12.6 KB) --
    main Game subclass

Recipe (now fully established by /7f3, /8b, /9):

1. **Audit `src/Games/Space/{Player,SpaceRocks}.{h,cpp}`** vs
   Snake / SpaceInvaders / Bonk. Read every `#include` and
   compare to the shim coverage we already have. Asteroids
   likely uses the same baseSprite drawing surface as Bonk
   (fillRect / drawString / drawBitmap) plus possibly
   `drawCircle` for the player ship or asteroids -- which is
   already on the shim.
2. **Audit any `setFreeFont(&XXX)` for unfamiliar font symbols.**
   If SpaceRocks uses a font beyond `TomThumb` / `fonts::Font0`
   / `fonts::Font2`, add an inline constexpr GFXfont stub.
3. **Audit warnings-as-errors triggers.** Eyeball SpaceRocks.cpp
   for any obvious UB patterns (++x % 2 on the same lvalue, etc.)
   that might re-trip a different warning than the ones we've
   already softened.
4. **First commit (`/10a`):** all shim additions (DEAD until
   /10b). If the audit reveals zero gaps, this commit can be
   skipped entirely.
5. **Second commit (`/10b`):** add Player.cpp + SpaceRocks.cpp
   to chatter_app SRCS. Compile + link validation only; binary
   delta should be 0 because no instantiation site.

If /10 lands cleanly, **S-MP20/11** is the moment --gc-sections
stops dropping the game TUs. /11 adds GamesScreen.cpp +
PhoneGamesScreen.cpp to SRCS, which instantiate each game in
their switch cases, and the binary grows by an estimated
~20-60 KB (4 games x ~5-15 KB each).

**S-MP20/12** wires `PhoneMainMenu`'s Games tile to push the
GamesScreen instance, and `PhoneHomeScreen::setOnLeftSoftKey` to
push a new `PhoneDialerScreen`. After /12, S-MP20 is done and we
move on to S-MP21 (modem hardware bring-up).

### Alternative path if /10 hits trouble

If SpaceRocks surfaces something nasty (e.g. a missing
ResourceManager-backed asset load, an unfamiliar networking
hook, or a runtime dependency on a feature that's still stubbed),
the next fire can:
  * Skip to **S-MP21** -- modem hardware bring-up, which is the
    next session in the roadmap and is decoupled from games-engine
    work; or
  * Revert /10 and document the blocker in this checkpoint for
    later investigation.

## Helper script note carried from prior fires

* The helpers (`flash_iter.sh`, `addr2line.py`) need to be
  recreated each fire from the brief; the new sandbox doesn't
  preserve `/home/claude/` (in fact, `/home/claude/` is not even
  writable -- `mkdir: cannot create directory '/home/claude':
  Permission denied`). In this fire's sandbox the user was
  `busy-ecstatic-cori`, `$HOME` was `/sessions/busy-ecstatic-
  cori`, and the writable scratch path was that home plus
  `/sessions/busy-ecstatic-cori/tmp/`. The repo cloned to
  `/sessions/busy-ecstatic-cori/repo/mp_firmware/`. As before,
  the bash tool's 45-second timeout makes the helper scripts
  impractical to run end-to-end; this fire used the chunked GH
  API approach (poll jobs every 30-40s with a separate bash call
  each time) and it worked well.
* The CI workflow triggers on push automatically via the
  `mp24/**` / `src/**` path filter -- no manual
  `workflow_dispatch` needed. Updating
  `docs/SESSION_CHECKPOINT.md` does NOT trigger a build (it's
  outside the path filter), so the checkpoint commit at the end
  of each fire is free.
* `pyelftools` was not needed this fire (no device crash).
* The Read/Write/Edit host-side tools cannot reach the repo
  (it's inside the VM under `/sessions/.../repo/...`), so all
  file edits had to go through bash + python heredocs. The
  multi-line-replacement pattern from /8a worked again here:
  `python3 << 'PY'` blocks that load the file, assert the old
  block is present, and replace it once.

## Open items unchanged from the brief

* Q2: `uPOWER_OFF` GPIO1 polarity (currently Hi-Z)
* Q3: Modem boot-FAIL triage if PWR_KEY polarity fix is insufficient
* Q4: STATUS / NET_STATUS LED traces

## Roadmap status snapshot

* **S-MP20** -- in progress (Snake + SpaceInvaders + Bonk are in
  SRCS; SpaceRocks + GamesScreen + Phone wiring still to go).
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
    (commit `9fe4bf1`)
  * /5: Game.cpp + GameSystem.cpp landed via SleepServiceStub
    `Game* startedGame` definition (commit `c4ad2ed`)
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
  * /7f1: ContextTransition.h bracket-include patch (commit
    `0109c89`).
  * /7f2: shim Sprite readPixel/printf/drawFastHLine stubs
    (commit `3a02c01`).
  * /7f3: Snake.cpp lands via Element.h patch + SRCS add
    (commit `6a71c4f`). Build initially RED -- five
    `print()` overload errors.
  * /7f3/1: shim TFT_eSPI gains Print-style overloads
    (commit `a561c51`). Build GREEN. Snake.cpp compiles +
    links + gc-section'd; binary delta 0.
  * /8a: shim TFT_eSPI gains TomThumb / GFXfont /
    setFreeFont + 7-arg drawBitmap (commit `b4b92c4`).
  * /8b: SpaceInvaders.cpp + Star.cpp in SRCS (commit
    `4c2f416`). Build initially RED -- one
    -Werror=sequence-point error.
  * /8b/1: soften -Werror=sequence-point (commit `75174b4`).
    Build GREEN. SpaceInvaders.cpp compiles + links +
    gc-section'd; binary delta 0.
  * **/9a: shim Sprite setTextWrap(bool,bool) + empty SD.h
    (commit `b819c86`) -- THIS FIRE.**
  * **/9b: State.cpp + TitleState.cpp + GameState.cpp +
    PauseState.cpp + Bonk.cpp in SRCS (commit `1eeb9d0`).
    Build GREEN on the first push -- no fix-forwards.
    Bonk.cpp compiles + links + all five Pong TUs
    gc-section'd; binary delta 0. -- THIS FIRE.**
  * /10: PLANNED for next fire -- SpaceRocks.cpp landing.
  * /11: PLANNED -- GamesScreen + PhoneGamesScreen in SRCS;
    binary grows (4 games x ~5-15 KB each = ~20-60 KB delta).
  * /12: PLANNED -- wire PhoneMainMenu's Games tile +
    PhoneDialerScreen. After /12, S-MP20 is done.
* **S-MP21** -- not started (modem hardware bring-up)
* **S-MP22+** -- not started
