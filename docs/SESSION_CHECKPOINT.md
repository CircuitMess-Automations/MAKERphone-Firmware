# MAKERphone v2.4 Port — Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-17 ~13:20
* **HEAD on `main`:** `fafeef9` (`feat(mp24): S-MP20/1 -- vendor glm 1.0.1 + Arduino-GLM glm.h shim`)
* **Build status:** GREEN. CI build job succeeded on both the push-triggered
  run (`25991728759`) and the manual re-dispatch (`25991869162`).
* **Device boot status:** UNKNOWN. Both runs above failed on the
  `flash` job with 8 consecutive `esptool` connection retries hitting
  `Failed to connect to ESP32-S3: No serial data received.`
  The flasher Mac's own diagnostic message ends with:

      All 8 flash attempts failed -- device is unrecoverable
      via esptool. Most likely cause: firmware in flash
      is crashing too early for ROM USB-Serial/JTAG to
      respond. Physical recovery needed (SW24 BOOT pin).

  The `boot-log` artifact uploaded by each of those failed runs is a
  stale 40-line capture from a *previous* (successful) run still
  sitting in the flasher's `firmware/` working directory -- it is
  NOT evidence that S-MP20/1's binary booted. The 12:24 UTC runs
  on `bd9a54c` *did* boot cleanly; nothing post-12:24 has been
  confirmed on hardware.

## What S-MP20/1 actually shipped

A pure-headers vendor of `g-truc/glm` 1.0.1 into
`mp24/components/chatter_app/shim_includes/glm/` (428 files, ~3.2 MB)
plus a one-file Arduino-GLM compatibility shim at
`mp24/components/chatter_app/shim_includes/glm.h` that forwards
`<glm.h>` to `<glm/glm.hpp>` + `<glm/ext.hpp>`.

Nothing in `chatter_app`'s SRCS currently includes glm, so this
commit changes ZERO compiled TUs. The output binary is essentially
identical to `bd9a54c`'s -- which is why "build green, device
unflashable" is consistent with the commit being correct: the
flash failure is a hardware / runner-side condition, not a
code regression.

## Why I did not revert

The brief's revert policy (`docs above`, §2.3) triggers on
*runtime* crashes (`flash_iter.sh` exit 2) after up to 3
fix-forwards. The current situation is `esptool` cannot connect
at all -- there is no runtime to crash and no fix any commit can
make. Reverting to `bd9a54c` would not change the device's
responsiveness to `esptool`; the same hardware is now mute to
the runner regardless of which binary the build job produced.

## What the next fire should do

1. Re-run `flash_iter.sh --no-dispatch` first. The device may
   have recovered on its own (USB renumeration, watchdog reset,
   user re-plug). Two cases:
   * Exit 0 with a fresh boot.log (with the `ESP-ROM:esp32s3-...`
     banner -- NOT just the stale `STORE:` lines this fire saw)
     -> proceed with S-MP20/2 below.
   * Still flash=failure -> stop, leave a note. The user needs
     to physically tap SW24 BOOT to drop the chip into download
     mode, or replug USB. Don't pile more commits.

2. **Known `flash_iter.sh` bug to fix when convenient**: the
   script gates only on `BUILD_CONCL`, never on `FLASH_CONCL`.
   That meant the boot-log scan ran on the stale 40-line log
   and almost reported "device healthy" despite the flash job
   conclusively failing. Add a `[[ "$FLASH_CONCL" != "success" ]]
   && exit 1` check between the build-success guard and the
   artifact download. (Helper script lives in `/home/claude/`,
   not the repo, so this fix is on the agent side, not a commit.)

3. **S-MP20/2** (when CI is trustable again): add
   `src/Games/GameEngine/Game.cpp` + `GameObject.cpp` (smallest
   leaf pair) to `chatter_app`'s SRCS to validate the glm
   include path under real compilation. If that builds, append
   `Rendering/*.cpp`, then `Collision/*.cpp`, then the four
   game implementations -- one or two SRCS files per commit so
   build/link errors stay diagnosable.

4. **S-MP20 wiring**: once the engine + four games compile,
   add `GamesScreen.cpp` + `PhoneGamesScreen.cpp` +
   `PhoneDialerScreen.cpp` to SRCS and wire the
   `PhoneHomeScreen::setOnLeftSoftKey` (CALL) ->
   `PhoneDialerScreen` push, and the `PhoneMainMenu` Phone /
   Games tiles.

## Open items unchanged from the brief

* Q2: `uPOWER_OFF` GPIO1 polarity (currently Hi-Z)
* Q3: Modem boot-FAIL triage if PWR_KEY polarity fix is insufficient
* Q4: STATUS / NET_STATUS LED traces
