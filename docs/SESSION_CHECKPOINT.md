# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-17 ~16:52
* **HEAD on `main`:** `9b42710` (`feat(mp24): S-MP20/6 -- land
  Highscore.cpp via parallel-safe SRCS add`). A docs checkpoint
  commit on top of this one will be outside the CI path filter,
  so the green build state is preserved.
* **Build status:** GREEN. Run `25996843156` -- build job
  `success`, flash job `success`. Zero fix-forwards this fire --
  the feat commit landed clean on first try, making it seven
  clean landings in a row on the GameEngine subsystem (S-MP20/4b
  + /4c + /4d + /4e + /4f + /5 + /6).
* **Device boot status:** HEALTHY. boot.log from artifact
  `7044260487` (40 lines, 0 crash markers) shows the same shape
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
  MB free). **Identical to the prior fire's 883408** -- Highscore.
  cpp TU gets gc'd at link time as predicted. No file in the
  230-entry SRCS list pulls Highscore.h (audit verified: only
  Snake.h/.cpp + SpaceInvaders.h/.cpp + Highscore.cpp itself
  include it, and Snake/Invaders are not in SRCS). Compile-only
  validation pattern from S-MP20/2 -> /3 -> /4 -> /4b -> /4c ->
  /4d -> /4e -> /4f -> /5 -> /6 holds. Ten consecutive clean
  landings.

## What S-MP20/6 actually shipped (in commit `9b42710`)

The per-game high-score persistence leaf: `src/Games/GameEngine/
Highscore.cpp` (~95 LOC). Provides the `Highscore` class with
NVS-backed save/load/add/clear/count/get semantics. One NVS
namespace per game name; single "HS" blob containing a count +
fixed-size Score[5] array.

One piece in the commit:

  - `chatter_app/CMakeLists.txt` -- adds Highscore.cpp to SRCS
    immediately after Game.cpp + GameSystem.cpp, with a comment
    block explaining the audit (only Snake.h/.cpp + SpaceInvaders.
    h/.cpp + the file itself include Highscore.h; the three
    in-SRCS storage TUs that mention "Highscore"
    (PhoneCallHistory/BeatMaker/ComposerStorage) only reference
    it in doc comments documenting the NVS-handle pattern, not
    via #include) and the gc-sections gambit. Slot is between
    GameSystem.cpp (the /5 leaf) and the S-MP18j Storage stub
    group, keeping all GameEngine .cpp's grouped.

Dependencies: Arduino.h, FS.h, nvs.h, esp_log.h. All reachable
via existing in-SRCS files (PhoneComposerStorage.cpp pulls nvs.h
+ esp_log.h; FS.h is pulled by Repo.h / FSLVGL.h / ResourceManager.
h via arduino-esp32 libraries/FS).

Net diff: 1 file, +40 lines, -0 lines (one SRCS entry + 38 lines
of comment).

## What the next fire should do

Pick ONE of these. Roughly ordered by risk-adjusted payoff.

1. **S-MP20/7 -- First game implementation (Snake).** Snake is
   the simplest of the four upstream games. Pulls Game.h
   transitively (via `Snake : public Game`). Once Snake.cpp is
   in SRCS, the linker *will* retain Game.cpp + GameSystem.cpp
   + the Rendering + Collision leaves + Highscore.cpp because
   Snake's ctor chains into Game's ctor which in turn
   instantiates RenderSystem + CollisionSystem and (via Snake)
   a Highscore field. This is a much bigger leap than /4-/6 --
   expect compile + link surprises (InputChatter::
   getInputInstance not defined; FSLVGL::loadCache not defined;
   the Game::start / Game::stop / Game::pop bodies reaching
   Chatter / Audio / ChirpSystem). Plan for a 30-min budget of
   audit + maybe one fix-forward.

   **Recommended next** -- but only after item 2 below.

2. **Audit InputChatter + FSLVGL + Chatter availability before
   /7.** Game.cpp's start()/stop() call `InputChatter::
   getInputInstance()`, pop() calls `FSLVGL::loadCache()`,
   ctor uses `Chatter.getDisplay()->getBaseSprite()`. Audit:
   * Is InputChatter.cpp in SRCS? If not -- need to stub or
     land it. Search `mp24/components/chatter_app/CMakeLists.txt`
     for `InputChatter` and `Chatter\.cpp`.
   * Is FSLVGL.cpp in SRCS?
   * Is Chatter.cpp in SRCS or is the global `Chatter` provided
     by a stub? (The brief mentions MP24Chatter.cpp in
     circuitos_shim provides `Chatter.begin()` -- check that
     also defines the `Chatter` global.)
   Decision tree:
   * If all three are in SRCS or already stubbed -> proceed to
     /7 next fire.
   * If any is missing -> land or stub it as S-MP20/6b before /7.

3. **S-MP20/7b -- Smallest game first (Bonk).** If Snake feels
   risky, audit the four games (Snake, SpaceInvaders, Bonk,
   SpaceRocks) and pick the one with the fewest external
   references. Bonk and Snake are usually the simplest.

## Helper script note carried from prior fires

* The helpers (`flash_iter.sh`, `addr2line.py`) need to be
  recreated each fire from the brief; the new sandbox doesn't
  preserve `/home/claude/`. In this sandbox `$HOME` is
  `/sessions/<id>/`. Place them at `$HOME/flash_iter.sh` +
  `$HOME/addr2line.py` (this fire's locations) and adjust
  internal path references (`REPO_LOCAL=$HOME/repo/mp_firmware`).
* `flash_iter.sh` polls in 30-second intervals up to 12 minutes,
  which exceeds the 45-second `mcp__workspace__bash` tool
  timeout. Workaround: poll the GitHub Actions REST API directly
  in shorter bash calls (`sleep 35 && curl .../jobs`). The full
  helper-script invocation is impractical -- do it manually in
  chunks. This fire used the chunked approach (sleep 40 +
  curl-jobs) and it worked cleanly: build was complete by the
  second 40-second sleep, flash by the third.
* The CI workflow triggers on push automatically via the
  `mp24/**` path filter -- no manual `workflow_dispatch` needed.
* `pyelftools` must be installed each fire:
  `pip install --break-system-packages --quiet pyelftools`.
  (This fire didn't need it -- no crash to decode -- but the
  install was done at task start anyway for consistency.)
* The repo lives at `$HOME/repo/mp_firmware`. `git clone --depth
  50` works fine (~25 MB).

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
  * /4e: SpriteRC.cpp landed via three shim patches (commit
    `19b8e1f`)
  * /4f: AnimRC.cpp landed via new GIFAnimatedSprite shim
    (commit `9fe4bf1`) -- Rendering subsystem .cpp coverage
    complete
  * /5: Game.cpp + GameSystem.cpp landed via SleepServiceStub
    `Game* startedGame` definition (commit `c4ad2ed`) --
    GameEngine subsystem .cpp coverage complete
  * **/6: Highscore.cpp landed via parallel-safe SRCS add
    (commit `9b42710`) <-- THIS FIRE -- per-game high-score
    persistence leaf in SRCS**
  * /7+: four game implementations, GamesScreen wiring, dialer
* **S-MP21** -- not started (modem hardware bring-up)
* **S-MP22+** -- not started
