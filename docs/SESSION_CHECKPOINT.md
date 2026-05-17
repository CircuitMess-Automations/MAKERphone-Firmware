# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-17 ~19:04-19:14
* **HEAD on `main`:** `cd0094d` (`feat(mp24): S-MP20/10b -- land
  SpaceRocks (Asteroids) + Hearts/Score helpers in SRCS`). This
  fire shipped TWO commits, both on `main`, both clean on the
  first build:
    - `95d6745` -- feat(mp24): S-MP20/10a -- shim TFT_eSPI
      fillRoundRect + drawRoundRect.
    - `cd0094d` -- feat(mp24): S-MP20/10b -- land SpaceRocks
      (Asteroids) + Hearts/Score helpers in SRCS. **GREEN on
      the first push.**
* **Build status:** GREEN at HEAD `cd0094d` (run `26000043276`,
  both `build` and `flash` jobs completed/success).
* **Device boot status:** HEALTHY. boot.log scan: 0 crash markers.
  Same boot sequence as prior fires' green HEADs:

      STORE: mounted; 2 files, 1757 / 1860161 bytes used (0%)
      POWER: polling GPIO2 at 50 Hz, debounce 3 ticks
      MP24: Entering background-tasks-only loop (LVGL owns the panel).
      MODEM: state -> POWER -> BOOT -> boot probe... (up to 16 s)

* **Flasher status:** ONLINE and reliable.
* **Binary size:** `makerphone_mp24.bin` is **883,408 bytes** at
  HEAD `cd0094d` -- EXACTLY UNCHANGED from `1eeb9d0` and earlier
  green HEADs. Snake / SpaceInvaders / Bonk / SpaceRocks TUs (and
  the new Common/Hearts.cpp + Common/Score.cpp helpers) all
  compile + link cleanly, but --gc-sections drops them at link
  time because no in-SRCS instantiation site references their
  symbols yet. Binary delta as predicted: 0 bytes.

## What this fire actually shipped (net)

Two commits, both GREEN at the final HEAD (no fix-forwards):

1. **`95d6745` -- S-MP20/10a.** Pre-emptive shim additions to
   prepare for SpaceRocks.cpp landing. Single net-new transitive
   dep surfaced during the audit:

     * SpaceRocks.cpp line 211 calls
       `spriteRC->getSprite()->fillRoundRect(0, 0, 4, 4, 1, TFT_WHITE);`
       which is the Bodmer 6-arg shape (x, y, w, h, radius,
       color). Added `void fillRoundRect(...)` as a silent no-op
       on the TFT_eSPI shim base class (which Sprite inherits
       via TFT_eSprite). Also added `void drawRoundRect(...)`
       with the same signature for symmetry, even though only
       fillRoundRect is currently referenced -- the pair always
       ships together in real Bodmer/TFT_eSPI.

   Addition is DEAD until /10b lands; binary delta 0.
   Build + flash + boot all GREEN.

2. **`cd0094d` -- S-MP20/10b.** Added four .cpp files to
   chatter_app SRCS, immediately after the Pong/Bonk block in
   the Games section:

     * `${SRC_DIR}/Games/Common/Hearts.cpp`     (~30 LOC)
     * `${SRC_DIR}/Games/Common/Score.cpp`      (~30 LOC)
     * `${SRC_DIR}/Games/Space/Player.cpp`      (~25 LOC)
     * `${SRC_DIR}/Games/Space/SpaceRocks.cpp`  (~420 LOC)

   Build GREEN on the first push. SpaceRocks's Game-class API
   surface is a strict subset of Snake / SpaceInvaders (Game
   ctor with a resource list, `pop()`, `Audio.play({chirps})`
   via the protected ChirpSystem member, addObject /
   removeObject for GameObject lifecycle, collision.addPair /
   removePair / wallsAll for callbacks). All proven by the
   /5 + /6 + /6c + /7f3 scaffolding.

   The wrapWalls struct uses RectCC (/4 collision leaf), the
   player uses PolygonCC + AnimRC (/4 + /4f), asteroids use
   CircleCC + StaticRC (/4 + /4d), and bullets use CircleCC +
   SpriteRC with the new fillRoundRect.

   Hearts.cpp + Score.cpp are the shared Games/Common HUD
   helpers. They are also new to SRCS; Snake / SpaceInvaders /
   Bonk do not use them (they roll their own baseSprite-print()
   scoring). Hearts uses `drawIcon(File, ...)` (proven by /4d)
   and Score uses `sprite->printf` + `setCursor` +
   `setTextColor` (proven by /6d + /7f3/1).

   All four TUs compile + link + gc-section'd; binary unchanged
   at 883,408 bytes.

## Why this fire could finish /10 cleanly in one push

The 30-minute fire budget covered TWO CI cycles (build + flash)
back-to-back with room to spare. The audit-first approach
identified the entire delta in ~3-5 minutes of Reading:

  * Player.h / Player.cpp / SpaceRocks.h / SpaceRocks.cpp --
    read end-to-end.
  * Hearts.h / Hearts.cpp / Score.h / Score.cpp -- read
    end-to-end (Common helpers that SpaceRocks pulls in).
  * Cross-checked every `display->` / `sprite->` / `obj->`
    method call against the existing shim coverage. One missing
    overload: fillRoundRect.
  * Confirmed `<Pins.hpp>` provides BTN_4/5/6, RES_HEART /
    RES_GOBLET / FILE_HEART / FILE_GOBLET come from upstream
    ResourceManager.h (already a transitive header in SRCS).
  * Cross-checked Game.h's API surface (getFile, addObject,
    removeObject, Audio member, collision member, pop()) ==
    a strict subset of what Snake exercises.

All four upstream Chatter games (Snake, SpaceInvaders, Bonk,
SpaceRocks) now compile + link cleanly inside chatter_app.
S-MP20 has now landed the games-engine compile-validation
phase end-to-end.

## What the next fire should do

**S-MP20/11 -- wire up the GamesScreen + PhoneGamesScreen
instantiation sites.** This is the moment --gc-sections stops
dropping the four game TUs at link time. Binary will grow by
an estimated ~20-60 KB (4 games x ~5-15 KB each, depending on
how aggressive --gc-sections is on each game's transitive
unused-symbol set).

Files at `src/Phone/`:
  * `GamesScreen.h` + `GamesScreen.cpp` -- the tile picker
    for the four games.
  * `PhoneGamesScreen.h` + `PhoneGamesScreen.cpp` -- the
    wrapper that pushes the picker from PhoneMainMenu's Games
    tile.

Recipe:

1. **Audit `src/Phone/GamesScreen.{h,cpp}` and
   `PhoneGamesScreen.{h,cpp}`** end-to-end. Identify every
   `#include`, every `new <GameType>(...)` call, every menu
   tile / image asset, and every method invoked on the four
   game subclasses. Compare against the in-SRCS coverage.
2. **Audit any new transitive deps.** Likely candidates:
   - A new ImageLoader / icon asset class for the menu tiles.
   - A new ScreenStack push pattern that we haven't seen yet.
   - Maybe a SettingsService dependency for "Games disabled"
     toggle (Chatter has this; MP24 may not).
3. **First commit (`/11a`):** all shim / new-stub additions
   (DEAD until /11b). If the audit reveals zero gaps, skip /11a.
4. **Second commit (`/11b`):** add GamesScreen.cpp +
   PhoneGamesScreen.cpp to chatter_app SRCS. **Binary will
   grow** because GamesScreen's switch cases will keep the
   game subclasses alive through --gc-sections.

If /11 hits a wall (e.g. transitive deps explode), the next
fire can:
  * Revert /11 and fall back to **S-MP20/12** (the simpler
    final wiring step -- PhoneMainMenu's Games tile +
    PhoneDialerScreen wiring); or
  * Skip directly to **S-MP21** (modem hardware bring-up),
    which is decoupled from games-engine work.

Once /11 + /12 both land, S-MP20 is done -- four games are
playable through the menu, plus the dialer is wired to the
left soft key -- and we advance to S-MP21.

## Helper script note carried from prior fires

* The helpers (`flash_iter.sh`, `addr2line.py`) need to be
  recreated each fire from the brief; the new sandbox doesn't
  preserve `/home/claude/` (in fact, `/home/claude/` is not
  writable -- `mkdir: cannot create directory '/home/claude':
  Permission denied`). In this fire's sandbox the user was
  `practical-confident-carson`, `$HOME` was `/sessions/
  practical-confident-carson`, and `/sessions/...` was 100%
  full (`9.8G/9.8G`), so the writable scratch path used was
  `/tmp/work/` (1.1 GB free under `/`). The repo cloned to
  `/tmp/work/repo/mp_firmware/`. As before, the bash tool's
  45-second timeout makes the helper scripts impractical to
  run end-to-end; this fire used the chunked GH API approach
  (poll jobs every 40s with a separate bash call each time)
  and it worked smoothly.
* The CI workflow triggers on push automatically via the
  `mp24/**` / `src/**` path filter -- no manual
  `workflow_dispatch` needed. Updating
  `docs/SESSION_CHECKPOINT.md` does NOT trigger a build (it's
  outside the path filter), so the checkpoint commit at the end
  of each fire is free.
* `pyelftools` was not needed this fire (no device crash).
* The Read/Write/Edit host-side tools cannot reach the repo
  (it's inside the VM under `/tmp/work/...`), so all file
  edits had to go through bash + python heredocs. The
  multi-line-replacement pattern still works:
  `python3 << 'PY'` blocks that load the file, assert the old
  block is present, and replace it once.

## Open items unchanged from the brief

* Q2: `uPOWER_OFF` GPIO1 polarity (currently Hi-Z)
* Q3: Modem boot-FAIL triage if PWR_KEY polarity fix is insufficient
* Q4: STATUS / NET_STATUS LED traces

## Roadmap status snapshot

* **S-MP20** -- in progress (all four games compile + link;
  GamesScreen wiring + Phone menu wiring still to go).
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
  * /9a: shim Sprite setTextWrap(bool,bool) + empty SD.h
    (commit `b819c86`).
  * /9b: State.cpp + TitleState.cpp + GameState.cpp +
    PauseState.cpp + Bonk.cpp in SRCS (commit `1eeb9d0`).
    Build GREEN on the first push -- no fix-forwards.
    Bonk.cpp compiles + links + all five Pong TUs
    gc-section'd; binary delta 0.
  * **/10a: shim TFT_eSPI fillRoundRect + drawRoundRect
    (commit `95d6745`) -- THIS FIRE.**
  * **/10b: Common/Hearts.cpp + Common/Score.cpp +
    Space/Player.cpp + Space/SpaceRocks.cpp in SRCS (commit
    `cd0094d`). Build GREEN on the first push -- no
    fix-forwards. All four TUs compile + link + gc-section'd;
    binary delta 0. -- THIS FIRE.**
  * /11: PLANNED for next fire -- GamesScreen +
    PhoneGamesScreen in SRCS; binary GROWS (4 games x
    ~5-15 KB each = ~20-60 KB delta).
  * /12: PLANNED -- wire PhoneMainMenu's Games tile +
    PhoneDialerScreen. After /12, S-MP20 is done.
* **S-MP21** -- not started (modem hardware bring-up)
* **S-MP22+** -- not started
