# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-18 ~03:30 UTC
* **HEAD on `main`:** `2fc34c9`
  (`revert(mp24): S-MP25/3 + S-MP25/3/1 -- min-free hit heap_wd
  stack ceiling`). The repo functionally returns to the S-MP25/2
  4-number HEAP shape that the prior fire established as green.
* **Build status:** GREEN at HEAD `2fc34c9`
  (run `26011760480`, `build` job completed/success).
* **Device boot status:** **UNKNOWN -- DEVICE LIKELY BRICKED.**
  See "Important: device state" below. The `flash` job for run
  `26011760480` failed all 8 esptool retries with
  `"Failed to connect to ESP32-S3: No serial data received."`
  and the runner concluded:

      All 8 flash attempts failed -- device is unrecoverable
      via esptool. Most likely cause: firmware in flash is
      crashing too early for ROM USB-Serial/JTAG to respond.
      Physical recovery needed (SW24 BOOT pin).

  The `boot-log` artifact attached to this run is **stale** --
  it's the boot.log left on disk from the previous (`474e4e4`)
  flash attempt, re-uploaded by `actions/upload-artifact` because
  the boot capture step was skipped. Anyone reading run
  `26011760480`'s boot.log should ignore it.
* **Flasher status:** Online and responsive, but the **target
  device on `/dev/cu.usbmodem2101` is not responding to the ROM
  USB-Serial/JTAG bridge**. Most likely the
  S-MP25/3/1 firmware (HEAP crash 15.6 s into boot) is replaying
  fast enough that the chip never exposes a usable USB CDC
  endpoint to esptool.

## Important: device state

The S-MP25/3 + S-MP25/3/1 commits (now reverted in `main`) DID
flash onto the device during this fire's earlier attempts. That
firmware is what is currently sitting in flash on the
MAKERphone. It crashes every ~15.6 s with a heap_wd stack
overflow, then resets, then crashes again. This cycle is fast
enough that the ESP32-S3's built-in USB-Serial/JTAG controller
either never enumerates or enumerates briefly and disappears
before esptool's connect handshake completes -- hence the
`No serial data received` error across all 8 retry attempts.

**Recovery path** (requires the user to be physically near the
phone):

1. Hold down the SW24 BOOT button on the MAKERphone.
2. Briefly tap the reset (or power-cycle via USB unplug/replug
   while still holding SW24).
3. Release SW24 once the Mac shows the
   `/dev/cu.usbmodem2101` device re-enumerating in "Download
   Boot Mode."
4. Re-dispatch the `Build MP2.4 Firmware (ESP-IDF)` workflow
   on `main`. The current green HEAD `2fc34c9` will flash and
   bring the device back to S-MP25/2 boot behaviour.

Until step 1-4 happens, no CI flash job can succeed -- the
device is bricked from CI's perspective. The next fire of this
scheduled task should:

* Detect the same `Failed to connect to ESP32-S3` pattern in
  the most recent run.
* If detected and `HEAD == 2fc34c9` still, **do not push any
  new commits.** Pushing won't help -- the device can't be
  flashed. Just re-verify the most recent CI run and log a
  note that the device is still bricked.
* If the most recent run shows a successful flash (i.e. the
  user has physically recovered the device), then proceed
  with the normal S-MP25/3 retry path documented under
  "What the next fire should try."

## What landed this fire

Four commits total, ending in a net no-op (one feature + one
fix-forward + one revert + this docs update). The work is fully
documented in commit history; the practical effect on `main` is
zero lines changed in `mp24/`:

1. `5834b87` -- `feat(mp24): S-MP25/3 -- add total-pool min-free
   to heap watchdog`. Added fifth field `min-free` to the
   periodic HEAP: log line. Built green; the resulting firmware
   crashed on device.
2. `474e4e4` -- `fix(mp24): S-MP25/3/1 -- bump heap_wd task
   stack 2048 -> 3072`. Stack-size fix-forward. Built green;
   device crashed *identically* (same backtrace, same
   iteration count). This is the build that bricked the
   device for subsequent flash attempts.
3. `2fc34c9` -- `revert(mp24): S-MP25/3 + S-MP25/3/1 -- min-free
   hit heap_wd stack ceiling`. Returns `mp24/main/app_main.cpp`
   byte-for-byte to its `63a4451` shape. Build green;
   flash blocked by the bricked device above.
4. This SESSION_CHECKPOINT.md update.

## Why the revert

The crash on attempts 1 and 2 was deterministic and identical:

      ***ERROR*** A stack overflow in task heap_wd has been detected.
      Backtrace: panic_abort -> esp_system_abort ->
                 vApplicationStackOverflowHook -> vTaskSwitchContext
                 -> [corrupted] |<-CORRUPTED

Pattern: four HEAP: lines print successfully, then the fifth
iteration aborts mid-vsnprintf (boot.log catches the line
truncated at `min-free`). Bumping the stack 2048 -> 3072 did
NOT change the crash point. The bumped-stack run's
addr2line-decoded backtrace was identical to the unbumped
one -- which falsified the "needs more bytes" hypothesis after
a single attempt.

Per the brief: "Max 3 fix-forwards per debugging episode. If
still broken, revert. ... Don't pile fix-forwards forever."
Reverted instead of escalating to 4096 / 8192 / further-bump
attempts. The choice cost the device a brick (it's now stuck
on the S-MP25/3/1 firmware until the user recovers it via
SW24) but kept the *repo* green and reproducible.

## What the next fire should try (post device recovery)

The S-MP25 goal of "make `min-free` visible" is still worth
pursuing. After the device is physically recovered and the
current green HEAD `2fc34c9` is successfully re-flashed, the
next attempt should NOT just re-add the original change. The
new attempt should use one of these safer shapes:

**Option A (lowest risk):** Pre-format the HEAP: line into a
local `char buf[160]` via `snprintf`, then a single-arg
`ESP_LOGI(TAG, "%s", buf)`. This collapses the va_list
machinery into one well-bounded `snprintf` call where the
scratch usage is predictable and capped by the buffer size.

**Option B (next-lowest risk):** Split the HEAP: log into
two `ESP_LOGI` calls of ~3 args each:

    ESP_LOGI(TAG, "HEAP: free=%lu B  min-free=%lu B",
             free_total, min_free);
    ESP_LOGI(TAG, "HEAP: free-internal=%lu B  min-internal=%lu B  "
                  "largest-internal=%lu B",
             free_int, min_int, largest_int);

Both lines still start with `HEAP:` so grep-ability is fine.

**Option C (last resort):** Drop the min-free addition from
S-MP25 entirely. The four numbers we already have are
sufficient to detect internal-RAM leaks; PSRAM-pool leaks
would need a separate diagnostic.

Either A or B should be the first attempt. Option B is the
strict superset of "what already worked" (S-MP25/2 used the
same call shape, just with 4 args instead of 3) so it has
the strongest empirical backing.

**Critically: do not re-test heap_wd changes without first
confirming the most recent flash succeeded.** If the device
is still bricked, no firmware change can be validated. Log
the brick state and wait for the next fire after recovery.

## Hardware status

No new hardware questions resolved or opened this fire. Q1
(SPH0645 mic on GPIO41) remains resolved. Q2 (uPOWER_OFF
polarity), Q3 (modem PWR_KEY triage), Q4 (LED traces) remain
open. The modem still doesn't reach `MODEM_READY` so the
S-MP24/3 CCLK refresh task remains dead code on the device,
unchanged from prior fires.

## Idle-state baseline (unchanged from prior checkpoint)

Recorded here for future leak-hunt reference; same numbers
the device was producing at S-MP25/2:

  - free total              = 2 245 980 B (mostly PSRAM)
  - free internal           =   229 051 B
  - minimum internal seen   =   229 051 B (no shrinkage)
  - largest internal block  =   155 648 B
  - idle internal-RAM fragmentation = 73 403 B (32 %)

## Repo state at end of fire

```
2fc34c9 revert(mp24): S-MP25/3 + S-MP25/3/1 -- min-free hit heap_wd stack ceiling
474e4e4 fix(mp24): S-MP25/3/1 -- bump heap_wd task stack 2048 -> 3072
5834b87 feat(mp24): S-MP25/3 -- add total-pool min-free to heap watchdog
e7a1491 docs(mp24): session checkpoint after S-MP25/2 -- HEAP four-number baseline live
63a4451 feat(mp24): S-MP25/2 -- add largest-free-internal to heap watchdog
36b8c51 feat(mp24): S-MP25/1 -- periodic heap watermark logging
```

The effective end-state of `mp24/main/app_main.cpp` (post-
revert) is byte-identical to its `63a4451` shape, but
`5834b87`, `474e4e4`, `2fc34c9` are visible in `git log` for
traceability.

## Subsequent fire log (post-brick observation)

Each subsequent scheduled-task fire that runs while the device
remains bricked appends one line here so future fires (and the
user, when they come back to the bench) can see the timeline.
A line here means "the fire ran, confirmed the device is still
bricked via CI run inspection, and abstained from new commits per
the directive above."

* 2026-05-18 03:34 UTC -- fire ran. Inspected latest workflow
  run `26011760480` (HEAD `2fc34c9`): `build=success`,
  `flash=failure`. Flash-step log contains 8x
  `A fatal error occurred: Failed to connect to ESP32-S3: No
  serial data received.` plus `All 8 flash attempts failed --
  device is unrecoverable`. No newer runs exist (the docs
  commit `a8f553f` is outside the `mp24/**`, `src/**`,
  `libraries/Chatter-Library/**`, `.github/workflows/build-mp24.yml`
  CI path filter so it did not trigger CI). Functional baseline
  on `mp24/` remains `2fc34c9`. Device still bricked. Abstaining
  from new feature/fix commits per checkpoint directive.

* 2026-05-18 03:45 UTC -- fire ran. Re-inspected latest
  workflow run `26011760480` (HEAD `2fc34c9`) directly via
  the GitHub API (no clone -- session VM had a full disk).
  Flash job log unchanged: 8x
  `A fatal error occurred: Failed to connect to ESP32-S3: No
  serial data received.` followed by `All 8 flash attempts
  failed -- device is unrecoverable` and `Physical recovery
  needed (SW24 BOOT pin)`. No newer build-mp24 runs since the
  previous fire (the prior fire's docs commit `75e6415` is also
  outside the CI path filter). The boot-log artifact attached
  to run 26011760480 is a stale 1490 B copy from a prior
  successful flash (uploaded by `if-no-files-found: ignore`)
  -- not evidence of a current boot. Functional baseline on
  `mp24/` remains `2fc34c9`. Device still bricked. Abstaining
  from new feature/fix commits per checkpoint directive.

* 2026-05-18 04:05 UTC -- fire ran. Inspected latest workflow
  run `26011760480` (HEAD `2fc34c9`) via the GitHub API. State
  unchanged from the two prior fires: `build=success`,
  `flash=failure`, flash-step log has the same 8x
  `A fatal error occurred: Failed to connect to ESP32-S3: No
  serial data received.` sequence ending in `All 8 flash
  attempts failed -- device is unrecoverable` and `Physical
  recovery needed (SW24 BOOT pin)`. No newer `build-mp24` runs
  have been triggered since 2026-05-18T03:23Z (the prior fire's
  docs commit `8dc9a3d` is outside the `mp24/**`, `src/**`,
  `libraries/Chatter-Library/**`,
  `.github/workflows/build-mp24.yml` CI path filter, so it did
  not dispatch a new run). The `boot-log` artifact attached to
  run 26011760480 is still the stale 1490 B copy from the last
  pre-brick flash. Functional baseline on `mp24/` remains
  `2fc34c9`. Device still bricked, awaiting physical SW24 BOOT
  recovery at the bench. Abstaining from new feature/fix
  commits per checkpoint directive; appending this docs-only
  log line for timeline continuity.

* 2026-05-18 04:30 UTC -- fire ran. Departed slightly from the
  prior three fires' "passive re-read" pattern: dispatched a
  fresh `workflow_dispatch` on `main` (current tip `46e3361`,
  which is a docs-only commit atop `2fc34c9` so the `mp24/`
  binary is byte-identical to the green `2fc34c9` baseline).
  The dispatch produced run `26013477042` -- `build=success`
  in ~2 min, then `flash=failure` after the same 8x
  `Failed to connect to ESP32-S3: No serial data received.`
  sequence ending in the same `All 8 flash attempts failed --
  device is unrecoverable ... Physical recovery needed (SW24
  BOOT pin)` message. Confirmation that the device is still
  bricked is now *current* (probed 2026-05-18T04:28-04:30Z)
  rather than inferred from the stale `26011760480` run. The
  `boot-log` artifact attached to run 26013477042 has SHA256
  `335a8e4e75e5ea054de65499bdedd7bdaef3c2c4bd274d81889ab5faa5a8e9e8`
  -- byte-identical to the previous run's stale artifact, so
  no new boot evidence was captured (the boot-capture step is
  skipped because the flash step exited 2). Functional
  baseline on `mp24/` remains `2fc34c9`. Device still bricked,
  awaiting physical SW24 BOOT recovery at the bench.
  Abstaining from new feature/fix commits per checkpoint
  directive. Future fires can reuse the workflow_dispatch
  probe-and-confirm pattern when the most recent run is more
  than ~30 min stale; it costs ~2.5 min of CI but provides
  current evidence of brick state rather than re-reporting a
  cached observation.

* 2026-05-18 04:47 UTC -- fire ran. Passive re-read pattern (last
  active probe was 2026-05-18 04:30 - ish, still well under the
  ~30 min staleness threshold called out in the prior entry).
  Re-inspected workflow run `26013477042` (HEAD `46e3361`)
  via the GitHub API: `build=success` (completed
  2026-05-18T04:28:42Z), `flash=failure` (completed
  2026-05-18T04:30:28Z). Flash-step log tail confirms the same
  8x `A fatal error occurred: Failed to connect to ESP32-S3:
  No serial data received.` sequence terminating in
  `All 8 flash attempts failed -- device is unrecoverable`
  and `Physical recovery needed (SW24 BOOT pin)`. The
  `boot-log` artifact attached to the run is the same
  1490 B stub with SHA256 `335a8e4e...` documented in the
  prior entry -- no new boot evidence captured (boot-capture
  step is skipped when flash exits 2). No newer `build-mp24`
  runs since the 04:30 active probe; HEAD `df00d0b` is the
  prior fire's docs commit, which is outside the
  `mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
  `.github/workflows/build-mp24.yml` CI path filter and so
  did not dispatch a fresh run. Functional baseline on
  `mp24/` remains `2fc34c9`. Device still bricked.
  Abstaining from new feature/fix commits per checkpoint
  directive; appending this docs-only entry for timeline
  continuity.

* 2026-05-18 05:10 UTC -- fire ran. Active probe (the previous
  active probe at 2026-05-18 04:30Z had aged out at ~36 min, past
  the ~30 min staleness threshold called out in the 04:30 entry).
  Dispatched a fresh `workflow_dispatch` on `main` (HEAD
  `89ec3a2`, prior fire's docs-only commit atop `2fc34c9` -- so
  the `mp24/` binary inputs are byte-identical to the green
  baseline). The dispatch produced run `26014627881` --
  `build=success` (completed 2026-05-18T05:08:34Z), `flash=failure`
  (completed 2026-05-18T05:10:18Z). Flash-step log tail confirms
  the same 8x `A fatal error occurred: Failed to connect to
  ESP32-S3: No serial data received.` sequence terminating in
  `All 8 flash attempts failed -- device is unrecoverable` and
  `Physical recovery needed (SW24 BOOT pin)`. The `boot-log`
  artifact attached to run 26014627881 has zip SHA256
  `335a8e4e75e5ea054de65499bdedd7bdaef3c2c4bd274d81889ab5faa5a8e9e8`
  -- byte-identical to the 26013477042 (04:30 active probe) and
  26011760480 (03:23 prior baseline) artifacts. The boot.log
  inside the zip (3473 B) is unchanged from prior probes; it
  records the S-MP25/3/1 firmware's `heap_wd` stack-overflow
  crash that originally bricked the device (truncated at the
  fifth `MP24: HEAP: free=2245980 B  min-free` line, then
  `***ERROR*** A stack overflow in task heap_wd has been
  detected.` and corrupted backtrace, then `Rebooting...` into a
  fresh second-stage bootloader that never reaches user code
  again -- the ROM USB-Serial/JTAG no longer responds because
  the app crashes too early). No newer `build-mp24` runs since
  this active probe. Functional baseline on `mp24/` remains
  `2fc34c9`. Device still bricked, awaiting physical SW24 BOOT
  recovery at the bench. Abstaining from new feature/fix
  commits per checkpoint directive; appending this docs-only
  entry for timeline continuity. The boot.log evidence confirms
  the brick root-cause is what `a8f553f` documented (heap_wd
  stack overflow during `min-free` log formatting) -- not a new
  failure mode -- which strengthens the "no firmware change can
  fix this without physical recovery" conclusion.

* 2026-05-18 05:25 UTC -- fire ran. Passive re-read pattern (last
  active probe at 2026-05-18T05:10:18Z is ~15 min stale, well
  under the ~30 min staleness threshold called out in the 04:30
  entry). Re-inspected workflow run `26014627881` (HEAD
  `89ec3a2`) via the GitHub API: `build=success` (completed
  2026-05-18T05:08:27Z), `flash=failure` (completed
  2026-05-18T05:10:13Z). Flash-step log tail confirms the same
  8x `A fatal error occurred: Failed to connect to ESP32-S3:
  No serial data received.` sequence terminating in
  `All 8 flash attempts failed -- device is unrecoverable` and
  `Physical recovery needed (SW24 BOOT pin)`. Downloaded the
  `boot-log` artifact attached to run 26014627881 to confirm
  staleness: zip SHA256
  `335a8e4e75e5ea054de65499bdedd7bdaef3c2c4bd274d81889ab5faa5a8e9e8`
  -- byte-identical to the 05:10 fire's recorded SHA256. The
  3473 B boot.log inside (SHA256
  `83edb0b7ba92ccc4a1f7af3779a7ce96492fb01b454107d17643920c2056f4f8`)
  is also unchanged from prior probes; it still records the
  S-MP25/3/1 firmware's `heap_wd` stack-overflow crash at the
  fifth `MP24: HEAP: free=2245980 B  min-free` line, then
  `Rebooting...` into a fresh second-stage bootloader that
  truncates at "Disabling RNG early entropy source..." and
  never reaches user code (boot.log records exactly the
  second-stage bootloader's partition-table prints and the load
  of the factory app, with no `app_main` markers afterwards).
  No newer `build-mp24` runs since the 05:10 active probe;
  HEAD `5cd2037` is the prior fire's docs-only commit, outside
  the `mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
  `.github/workflows/build-mp24.yml` CI path filter, so no
  fresh run was dispatched. Workspace setup note: this fire's
  session VM had `/sessions` at 100% used and the bindfs-
  mounted `outputs` couldn't host a git clone (config-lock
  unlink fails on bindfs), so the clone went to `/var/tmp` on
  the root partition with `--depth 50` (~16 MB) -- worth
  documenting for future fires hitting the same disk pressure.
  Functional baseline on `mp24/` remains `2fc34c9`. Device
  still bricked, awaiting physical SW24 BOOT recovery at the
  bench. Abstaining from new feature/fix commits per
  checkpoint directive; appending this docs-only entry for
  timeline continuity.

* 2026-05-18 05:46 UTC -- fire ran. Active probe (the previous
  active probe at 2026-05-18T05:10:18Z was ~36 min stale, just
  past the ~30 min staleness threshold from the 04:30 entry).
  Dispatched a fresh `workflow_dispatch` on `main` (HEAD
  `90ad035`, prior fire's docs-only commit atop `2fc34c9` -- so
  the `mp24/` binary inputs are byte-identical to the green
  baseline). The dispatch produced run `26015891956` (run
  number 185) -- `build=success` (completed
  2026-05-18T05:49:xxZ), `flash=failure` (completed
  2026-05-18T05:50:29Z, ~4 min wall time from dispatch).
  Flash-step log tail confirms the same 8x `A fatal error
  occurred: Failed to connect to ESP32-S3: No serial data
  received.` sequence terminating in `All 8 flash attempts
  failed -- device is unrecoverable` and `Physical recovery
  needed (SW24 BOOT pin)`. The `boot-log` artifact attached to
  run 26015891956 has size 1490 B and SHA256 prefix
  `335a8e4e75e5ea054de65499b...` -- byte-identical (by prefix)
  to the boot-log artifacts attached to the prior 4 fires'
  active-probe runs (26011760480 / 26013477042 / 26014627881).
  No new boot evidence captured; the boot-capture step is
  skipped when flash exits 2 and the artifact is the stale
  pre-brick stub preserved by `if-no-files-found: ignore`.
  Side observation: the flash job's runner-emitted log
  timestamps show as `2026-05-17T13:17:xxZ` (one day + 16 hours
  behind wall-clock) -- the flasher Mac appears to have a
  drifting / mis-set clock, but the runner identity is
  unambiguous (run id, job id 76465948798, and run conclusion
  all confirm this is a fresh job; only the per-line log
  timestamps are anomalous). Workspace setup note: this fire's
  session VM had `/sessions` at 100% used (9.8G/9.8G) AND `/`
  at 97% used (only 361 MB free), and the bindfs-mounted
  `outputs` rejected git's config-lock unlinks. Successfully
  used `/dev/shm` (tmpfs, 2 GB) for a `--depth 5` clone (about
  16 MB) -- worth documenting for future fires hitting the
  same dual-disk-pressure failure mode. Functional baseline on
  `mp24/` remains `2fc34c9`. Device still bricked, awaiting
  physical SW24 BOOT recovery at the bench. Abstaining from
  new feature/fix commits per checkpoint directive; appending
  this docs-only entry for timeline continuity.

* 2026-05-18 06:07 UTC -- fire ran. Passive re-read pattern (last
  active probe at 2026-05-18T05:50:28Z is ~17 min stale, well
  under the ~30 min staleness threshold called out in the 04:30
  entry). Re-inspected workflow run `26015891956` (HEAD
  `90ad035`) via the GitHub API: `build=completed/success`
  (2026-05-18T05:46:31Z -> 2026-05-18T05:48:42Z),
  `flash=completed/failure` (2026-05-18T05:48:45Z ->
  2026-05-18T05:50:28Z). Run is the 05:46 active probe; no newer
  `build-mp24` runs since then because the four intervening fires
  (05:25, 05:10 active, 05:46 active, 06:07 this one) and their
  docs commits `5cd2037` and `f12901e` are outside the
  `mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
  `.github/workflows/build-mp24.yml` CI path filter, so no fresh
  dispatch was needed. Downloaded the `boot-log` artifact
  (id 7050413363) attached to the run: zip is 1490 B with
  SHA256 `335a8e4e75e5ea054de65499bdedd7bdaef3c2c4bd274d81889ab5faa5a8e9e8`,
  byte-identical (full SHA, not just prefix) to the boot-log
  artifacts attached to the 26011760480 / 26013477042 /
  26014627881 runs. The inner boot.log is 3473 B with SHA256
  `83edb0b7ba92ccc4a1f7af3779a7ce96492fb01b454107d17643920c2056f4f8`,
  also byte-identical to the prior probes' inner boot.log. The
  log content is unchanged: ESP-IDF v5.5 2nd-stage bootloader
  prints the partition table and loads the factory app at
  0x10000, then truncates at `Disabling RNG early entropy
  source...` with no `app_main` markers afterwards -- consistent
  with the S-MP25/3/1 firmware crashing too early for the
  USB-Serial/JTAG to respond, which has been the brick's
  signature since the original `a8f553f` checkpoint.
  Workspace setup note: this fire's session VM had both
  `/sessions` at 100% used (9.8G/9.8G) and `/` at 97% used
  (~345 MB free after clone), and the bindfs-mounted `outputs`
  rejected git's config-lock unlinks (same dual-disk-pressure
  failure mode the 05:46 fire documented). The `/dev/shm`
  approach the 05:46 fire suggested does NOT survive across
  bash invocations in this VM (each `mcp__workspace__bash` call
  apparently gets a fresh `/dev` namespace and `/dev/shm` is
  empty on call N+1 even after writing to it on call N). What
  worked here: `/tmp/fire_2015/` (a fresh subdir owned by the
  current fire's uid 2015 user, sibling to the stale
  `/tmp/mp_firmware` left behind by an earlier fire's uid; the
  earlier dir is owned by nobody:nogroup and read-only from
  this uid, but `/tmp` itself is world-writable so a fresh
  subdir works). Documented for future fires: prefer
  `/tmp/fire_<uid>/` over `/dev/shm/` when both `/sessions` and
  `/var/tmp` paths are obstructed.  Functional baseline on
  `mp24/` remains `2fc34c9`. Device still bricked, awaiting
  physical SW24 BOOT recovery at the bench. Abstaining from
  new feature/fix commits per checkpoint directive; appending
  this docs-only entry for timeline continuity.

* 2026-05-18 06:30 UTC -- fire ran. Active probe (the previous
  active probe finished at 2026-05-18T05:50:28Z, ~40 min stale,
  past the ~30 min staleness threshold from the 04:30 entry).
  Dispatched a fresh `workflow_dispatch` on `main` (HEAD
  `7d72421`, prior fire's docs-only commit atop `2fc34c9` -- so
  the `mp24/` binary inputs are byte-identical to the green
  baseline). The dispatch produced run `26017210952` (run
  number 186) -- `build=completed/success` (2026-05-18T06:26:01Z
  -> 2026-05-18T06:28:16Z, ~2 min 15 s), `flash=completed/failure`
  (2026-05-18T06:28:19Z -> 2026-05-18T06:30:01Z, ~1 min 42 s,
  total wall time from dispatch to flash-conclusion ~4 min 4 s).
  Flash-step log tail confirms the same 8x `A fatal error
  occurred: Failed to connect to ESP32-S3: No serial data
  received.` sequence terminating in `All 8 flash attempts
  failed -- device is unrecoverable` and `Physical recovery
  needed (SW24 BOOT pin)`, exit code 2. The `boot-log` artifact
  attached to run 26017210952 (id 7050908177, size 1490 B) has
  zip SHA256
  `335a8e4e75e5ea054de65499bdedd7bdaef3c2c4bd274d81889ab5faa5a8e9e8`
  -- byte-identical (full SHA, not just prefix) to the boot-log
  artifacts attached to runs 26011760480 / 26013477042 /
  26014627881 / 26015891956. The inner boot.log is 3473 B with
  SHA256 `83edb0b7ba92ccc4a1f7af3779a7ce96492fb01b454107d17643920c2056f4f8`,
  also byte-identical to the prior probes' inner boot.log. The
  log content is unchanged: the captured S-MP25/3/1 firmware
  boot truncates at the fifth `MP24: HEAP: free=2245980 B
  min-free` line into `***ERROR*** A stack overflow in task
  heap_wd has been detected.`, followed by a corrupted backtrace
  and `Rebooting...` into a second-stage bootloader print
  sequence that loads the factory app at 0x10000 but never
  emits any `app_main` markers -- consistent with the firmware
  in flash crashing too early for the USB-Serial/JTAG to respond,
  the brick signature since `a8f553f`. The boot-capture step in
  the flash workflow is skipped on exit 2; `if-no-files-found:
  ignore` preserves the pre-brick stub from upload-time, so the
  artifact persists across runs but does not represent a fresh
  capture. Workspace setup note: this fire's session VM had
  `/sessions` at 100% used (9.8G/9.8G full -- no headroom) AND
  `/` at 97% used (~321 MB free), and the bindfs-mounted
  `outputs` rejected git's config-lock unlinks (the same
  dual-disk-pressure failure mode the 05:46 and 06:07 fires
  documented). Successful path: `/tmp/cl_<session-uid>/` (a
  fresh subdir owned by the current fire's uid, sibling to the
  numerous stale `/tmp/mp24*`, `/tmp/cmp24`, `/tmp/work_mp24`,
  etc. directories left behind by earlier fires' uids that are
  now owned by nobody:nogroup and read-only from this uid).
  Each scheduled-task fire gets a distinct uid (this one ran
  as 2016) and so a fresh subdir under `/tmp` reliably works;
  17+ stale per-fire dirs are currently visible in `/tmp`,
  cumulatively pinning ~250+ MB of root-disk space. A future
  fire (or an out-of-band sweep) reclaiming these would help,
  but this fire stayed under the 321 MB headroom comfortably
  with `--depth 5 --filter=blob:none` (~16 MB working clone).
  Documented here for cross-fire continuity. Functional baseline
  on `mp24/` remains `2fc34c9`. Device still bricked, awaiting
  physical SW24 BOOT recovery at the bench. Abstaining from new
  feature/fix commits per checkpoint directive; appending this
  docs-only entry for timeline continuity.

* 2026-05-18 06:50 UTC -- fire ran. Passive re-read pattern (last
  active probe at 2026-05-18T06:30:01Z is ~20 min stale, under
  the ~30 min staleness threshold from the 04:30 entry).
  Re-inspected workflow run `26017210952` (HEAD `7d72421`) via
  the GitHub API: `build=completed/success` (2026-05-18T06:26:01Z
  -> 2026-05-18T06:28:16Z, ~2 min 15 s wall), `flash=completed/
  failure` (2026-05-18T06:28:19Z -> 2026-05-18T06:30:01Z, ~1 min
  42 s wall). No newer `build-mp24` runs since the 06:30 active
  probe; HEAD `81527bb` is the prior fire's docs-only commit,
  outside the `mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
  `.github/workflows/build-mp24.yml` CI path filter, so no fresh
  dispatch was needed. Downloaded the `boot-log` artifact
  (id 7050908177) attached to the run: zip is 1490 B with SHA256
  `335a8e4e75e5ea054de65499bdedd7bdaef3c2c4bd274d81889ab5faa5a8e9e8`,
  byte-identical (full SHA, not just prefix) to the boot-log
  artifacts attached to the 26011760480 / 26013477042 /
  26014627881 / 26015891956 / 26017210952 runs. The inner
  boot.log is 3473 B with SHA256
  `83edb0b7ba92ccc4a1f7af3779a7ce96492fb01b454107d17643920c2056f4f8`,
  also byte-identical to the prior probes' inner boot.log. The
  log content is unchanged: the captured S-MP25/3/1 firmware
  boot truncates at the fifth `MP24: HEAP: free=2245980 B
  min-free` line (the same `MP24: HEAP: free=2245980 B  min-free`
  string seen in every prior probe), the next line being
  `***ERROR*** A stack overflow in task heap_wd has been
  detected.`, then a corrupted backtrace and `Rebooting...` into
  a fresh second-stage bootloader print sequence that loads the
  factory app at 0x10000 but never emits any `app_main` markers
  -- consistent with the firmware in flash crashing too early
  for the USB-Serial/JTAG to respond, the brick signature since
  `a8f553f`. The boot-capture step in the flash workflow is
  skipped on exit 2; `if-no-files-found: ignore` preserves the
  pre-brick stub from upload-time, so the artifact persists
  across runs but does not represent a fresh capture. Workspace
  setup note: this fire's session VM had `/sessions` at 100%
  used (9.8G/9.8G full -- no headroom) AND `/` at 88% used
  (~1.2 GB free), and the bindfs-mounted `outputs` rejected
  git's config-lock unlinks (same dual-disk-pressure failure
  mode prior fires documented). Successful path: `/var/tmp/
  zalous-work/` (a fresh subdir owned by the current fire's
  user, sibling to the stale `/var/tmp/claude/` left behind by
  an earlier fire's uid that is now owned by nobody:nogroup and
  read-only from this user). Working clone is `--depth 50`
  (~30 MB), well under the root-disk headroom. Documented for
  cross-fire continuity. Functional baseline on `mp24/` remains
  `2fc34c9`. Device still bricked, awaiting physical SW24 BOOT
  recovery at the bench. Abstaining from new feature/fix commits
  per checkpoint directive; appending this docs-only entry for
  timeline continuity.
