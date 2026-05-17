# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-17 ~14:14
* **HEAD on `main`:** `4d92863` (`fix(build): S-MP20/3/1 -- hold CollisionSystem.cpp back`)
* **Build status:** GREEN. Run `25993182837` -- build job `success`,
  flash job `success`. Prior run `25993099877` (commit `4ba3170`,
  S-MP20/3 with all 5 Collision/* .cpp files) failed compilation
  on `CollisionSystem.cpp` only; the four leaves before it built
  clean, so `S-MP20/3/1` (commit `4d92863`) kept those four and
  commented `CollisionSystem.cpp` out with a TODO block. One
  fix-forward, then green.
* **Device boot status:** HEALTHY. boot.log from artifact
  `7043137997` shows:

      STORE: mounted; 2 files, 1757 / 1860161 bytes used (0%)
      POWER: polling GPIO2 at 50 Hz, debounce 3 ticks
      MP24: Entering background-tasks-only loop (LVGL owns the panel).
      MODEM: state -> POWER -> BOOT -> boot probe...

  Modem probe still wedged at ~16 s waiting on URCs (S-MP21
  territory: SIM presence + PWR_KEY polarity). No `Guru`,
  `panic`, `abort`, or backtrace markers anywhere in the log.
* **Flasher status:** ONLINE and reliable across both runs this
  fire. The port-detection issue from the previous fire is gone.

## What S-MP20/3 + S-MP20/3/1 actually shipped

S-MP20/3 (`4ba3170`) attempted to add the entire Collision
subsystem (5 TUs) to chatter_app SRCS:

    Games/GameEngine/Collision/CollisionComponent.cpp
    Games/GameEngine/Collision/CircleCC.cpp
    Games/GameEngine/Collision/RectCC.cpp
    Games/GameEngine/Collision/PolygonCC.cpp
    Games/GameEngine/Collision/CollisionSystem.cpp

The first four are pure value types over `glm::vec2` -- their
only non-stdlib includes are the headers in `Collision/` plus
Arduino.h (PolygonCC uses `abs` / math). They compiled clean in
both runs. `CollisionSystem.cpp` failed compilation under our
toolchain with three independent issues:

1. **GLM experimental headers.** `<glm/gtx/matrix_transform_2d.hpp>`
   and `<glm/gtx/vector_angle.hpp>` are flagged "experimental" in
   GLM 1.0.1 and emit `#error` unless `GLM_ENABLE_EXPERIMENTAL`
   is defined first. Our vendored copy doesn't pre-define that.

2. **TFT_GREEN / TFT_RED unknown.** `CollisionSystem::drawDebug`
   uses TFT_eSPI color macros that don't exist on our
   `circuitos_shim/Display/Sprite.h` surface.

3. **Sprite::drawPixel signature mismatch.** Upstream calls
   `canvas->drawPixel(points.front().x, points.front().y)` with
   two `float` args; our Sprite has `drawPixel(int32_t, int32_t,
   uint16_t color)` -- three args, integers.

S-MP20/3/1 (`4d92863`) reverted just `CollisionSystem.cpp` from
the SRCS list and left an inline comment block in
`chatter_app/CMakeLists.txt` documenting all three issues, so the
next fire can pick up the patch without re-diagnosing.

## What the next fire should do

Pick ONE of these. They are roughly ordered by risk-adjusted
payoff.

1. **S-MP20/4 -- Land `CollisionSystem.cpp`.** This is the
   correctly-scoped follow-up. Three small patches needed:

   * In `mp24/components/chatter_app/shim_includes/arduino_glm_compat.h`,
     `#define GLM_ENABLE_EXPERIMENTAL` BEFORE any glm includes.
     (Or add it as a global compile flag on chatter_app via
     `target_compile_definitions`. Either works; the header is
     slightly cleaner because the macro is conceptually paired
     with the other glm-compat stuff.)
   * For TFT_GREEN / TFT_RED: add `#define TFT_GREEN 0x07E0` and
     `#define TFT_RED 0xF800` (or equivalent constants from
     `Display/Color.h` if one exists) to the shim. Both are
     standard 565 RGB constants -- check what Color is defined
     as in `Sprite.h` first.
   * For the drawPixel signature mismatch: either add a 2-arg
     overload to our Sprite shim that forwards to the 3-arg
     version with `TFT_WHITE` (or the foreground color), OR
     patch the .cpp callsite. Adding the overload to the shim
     is preferred because it's a one-line addition and avoids
     touching upstream source. The call is `drawPolygon` -- the
     color is passed as a parameter so the missing third arg
     should be that color, not white. Read CollisionSystem.cpp
     line 364 for the right substitution.

2. **S-MP20/5 -- Land `Game.cpp` + `GameSystem.cpp` together.**
   `GameSystem.cpp` is two-line trivial; `Game.cpp` pulls
   `Chatter.h`, `Loop/LoopManager.h`, `InputChatter.h`, `FSLVGL.h`,
   references `extern bool gameStarted` (already provided by
   `SleepServiceStub.cpp`) and `extern Game* startedGame` (NOT
   yet provided -- check). Its constructor calls
   `render(this, Chatter.getDisplay()->getBaseSprite())` and
   `collision(this)`, which need `RenderSystem` + `CollisionSystem`
   constructors available. Adding only Game.cpp + GameSystem.cpp
   will produce undefined references. So this item is gated on
   first having `CollisionSystem.cpp` landed (S-MP20/4) AND on
   adding `RenderSystem.cpp`, which has its OWN dependency on
   the Rendering/* leaves. Net: this is a 3-4 commit follow-up,
   not a single one.

3. **S-MP20/4b -- Land the Rendering subsystem in parallel.**
   `Rendering/RenderComponent.cpp` is a 4-method getter/setter
   leaf -- safe to add alone. `Rendering/SpriteRC.cpp`,
   `StaticRC.cpp`, `AnimRC.cpp` will likely hit the same kind
   of TFT_eSPI / Sprite shim mismatches as CollisionSystem.cpp
   did, so expect a similar 1-2 fix-forward cycle per file.
   `RenderSystem.cpp` is the system class that ties them
   together. If S-MP20/4 lands cleanly, take the same patches
   forward into the Rendering side.

## What S-MP20 still needs after this fire

* `CollisionSystem.cpp` (deferred -- see S-MP20/4 above)
* `Rendering/*.cpp` (RenderComponent + RenderSystem + Sprite/Static/Anim)
* `Game.cpp` + `GameSystem.cpp`
* `Highscore.cpp` (uses NVS only; should be standalone-safe)
* `ResourceManager.cpp` (pulls `FS/RamFile.h` -- flagged as
  problematic by S-MP18i; may need a shim)
* `TextInput.cpp` (pulls `Input/Input.h`, `Loop/LoopManager.h`)
* `Common/Score.cpp` and `Common/Hearts.cpp` (both make_shared
  `SpriteRC` so they're gated on Rendering landing first)
* Per-game files: `Snake/Snake.cpp`, `Invaders/SpaceInvaders.cpp`
  + `Star.cpp`, `Pong/Bonk.cpp` + `GameState.cpp` + `PauseState.cpp`
  + `TitleState.cpp` + `State.cpp`, `Space/SpaceRocks.cpp` +
  `Player.cpp`
* `Screens/GamesScreen.cpp`, `Screens/PhoneGamesScreen.cpp`,
  `Screens/PhoneDialerScreen.cpp`
* Navigation wiring: `PhoneHomeScreen::setOnLeftSoftKey` (CALL)
  -> push `PhoneDialerScreen`; `PhoneMainMenu::Phone` and
  `::Games` tile handlers.

## Helper script note carried from previous fire

The previous fire's note flagged a `flash_iter.sh` improvement
(check FLASH_CONCL before scanning boot.log). This fire ran the
helper as written in the task brief, and additionally fixed a
multi-artifact bug: when GitHub stores >1 `boot-log` artifact for
a run, the bash command substitution captures multiple ids
separated by newlines, which produces a malformed curl URL. The
fix is to break out of the python loop after the first match:

    BL_ID=$(echo "$ARTIFACTS" | python3 -c "
    import json,sys
    d=json.load(sys.stdin)
    for a in d['artifacts']:
        if a['name']=='boot-log':
            print(a['id']); break")

Same shape for FW_ID. The helper lives outside the repo, so the
next fire needs to recreate it from the brief and re-apply this
fix.

## Open items unchanged from the brief

* Q2: `uPOWER_OFF` GPIO1 polarity (currently Hi-Z)
* Q3: Modem boot-FAIL triage if PWR_KEY polarity fix is insufficient
* Q4: STATUS / NET_STATUS LED traces
