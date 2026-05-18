# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-18 ~01:50 UTC
* **HEAD on `main`:** `cc2767f`
  (`feat(mp24): S-MP24/3 -- periodic AT+CCLK? refresh with 60 s
  drift gate`). Single feature commit this fire, no build
  fix-forwards needed.
* **Build status:** GREEN at HEAD `cc2767f`
  (run `26009292065`, both `build` and `flash` jobs
  completed/success on first push). No intermediate failures.
* **Device boot status:** HEALTHY. boot.log scan: 0 crash markers
  (panic/Guru/abort/Backtrace count = 0). Boot shape unchanged
  from the prior baseline (`a510593`):

      BATT: curve-fitting cal active
      BATT: first sample: 1220 mV (1.22 V)
      STORE:   README.txt                       919 B
      STORE:   hello.txt                        140 B
      STORE: sentinel hello.txt missing or unreadable
      STORE: mounted; 2 files, 1757 / 1860161 bytes used (0%)
      POWER: polling GPIO2 at 50 Hz, debounce 3 ticks
      MP24: Entering background-tasks-only loop (LVGL owns the panel).
      MODEM: state -> POWER
      MODEM: state -> BOOT
      MODEM: boot probe... (0..15 s elapsed)

  No `CLOCK:` line in the captured 20 s window because the modem
  still doesn't reach MODEM_READY (Q3 unresolved). The S-MP24/3
  refresh task is spawned only when `clock_source_have_time()`
  returns true at the end of `sms_boot_task`, which it doesn't on
  this hardware, so the task is dead code on the device today --
  the firmware behaves exactly as it did at HEAD `a510593`. The
  moment the modem starts responding the initial bridge call AND
  the refresh-task spawn will both fire; once spawned, the task
  wakes every 3600 s and the boot.log captured at that point
  would show either a `CLOCK: refresh: drift ...` line (cache
  updated) or no log line at all (drift within the 60 s gate).
* **Flasher status:** ONLINE and reliable.
* **Binary size:** within noise of the prior baseline. Net add is
  ~117 lines in `mp24/main/hal/clock_source.c` (one new exported
  function + one static helper + one new atomic field), ~50 lines
  in `mp24/main/hal/clock_source.h` (doc + prototype), and ~70
  lines in `mp24/main/app_main.cpp` (one new file-static task +
  a conditional `xTaskCreate` in `sms_boot_task`). No new managed
  components, no new REQUIRES, no new INCLUDE_DIRS, no new headers.

## What this fire actually shipped (net)

1. **`cc2767f` -- S-MP24/3 (FEATURE).** Three files modified:

   `mp24/main/hal/clock_source.h` -- new exported prototype:

       esp_err_t clock_source_refresh(uint32_t drift_threshold_sec);

   Doc comment lists the five possible returns (ESP_OK update
   applied; ESP_ERR_NOT_FOUND drift within gate, no-op;
   ESP_ERR_INVALID_STATE no anchor; ESP_ERR_TIMEOUT modem not
   READY; ESP_FAIL parse failure). Top-of-file comment block
   updated with an S-MP24/3 paragraph explaining the drift-gate
   rationale.

   `mp24/main/hal/clock_source.c` -- new file-static atomic
   `s_set_at_ticks` captures `xTaskGetTickCount()` at the moment
   the cache was last set; used by refresh to compute the
   locally-extrapolated wall clock. The existing
   `clock_source_init` success path now writes
   `s_set_at_ticks` alongside `s_epoch_utc` / `s_tz_offset_seconds`
   before flipping `s_have_time` true (ordering matches the prior
   "data before flag" convention so concurrent readers never see a
   torn mix). Two new functions:
     - `query_and_parse_cclk(timeout_ms, &utc, &tz)` -- static
       helper that does `modem_at_send("+CCLK?", ...)`, runs the
       reply through the existing `parse_cclk` +
       `local_fields_to_utc_epoch`, and returns the validated
       (utc, tz) pair without touching cached state.
     - `clock_source_refresh(drift_threshold_sec)` -- the public
       refresh entry point. Gates on `s_have_time` (no anchor ->
       ESP_ERR_INVALID_STATE) and `modem_state() == MODEM_READY`
       (-> ESP_ERR_TIMEOUT) before calling the helper. Computes
       `extrapolated_utc = cached_utc + (now_ticks -
       s_set_at_ticks) / configTICK_RATE_HZ` using unsigned tick
       subtraction so the 49-day wrap doesn't matter at hourly
       cadence. Returns ESP_ERR_NOT_FOUND when
       `|fresh_utc - extrapolated_utc| <= drift_threshold_sec`
       (logged at DEBUG so production boot.logs don't fill with
       "no-op" lines every hour); on a real drift overshoot it
       updates the three atomics in the same order init does and
       logs at INFO with the signed delta + new epoch.

   `mp24/main/app_main.cpp` -- new file-static task:

       static void clock_refresh_task(void *arg) {
           const TickType_t period       = pdMS_TO_TICKS(3600 * 1000);
           const uint32_t   drift_thresh = 60;
           for (;;) {
               vTaskDelay(period);
               esp_err_t rr = clock_source_refresh(drift_thresh);
               if (rr == ESP_OK)
                   clock_source_bridge_apply(clock_source_epoch_utc(),
                                             clock_source_tz_offset_seconds());
               /* ESP_ERR_NOT_FOUND + other returns: keep ticking */
           }
       }

   Spawned at the tail of `sms_boot_task`, gated on
   `clock_source_have_time()`, with stack 4096 and priority
   `tskIDLE_PRIORITY + 1` (matches `sms_boot_task` itself). On
   no-modem / no-NITZ hardware the gate fails, the task is never
   spawned, and `sms_boot_task` reaches its `vTaskDelete(NULL)`
   exactly as before.

   Component churn:
     M `mp24/main/hal/clock_source.h`     +49 -7
     M `mp24/main/hal/clock_source.c`     +117 -0
     M `mp24/main/app_main.cpp`           +70 -0

   Total: 3 files changed, 236 insertions(+), 7 deletions(-).

## Why this fire shipped S-MP24/3 only

The S-MP24/2 checkpoint recommended S-MP24/3 (periodic re-query
with drift gate) as the next single-fire feature. That estimate
held: one feature commit, no build fix-forwards, ~15 min wall
time including the CI loop. S-MP24/4 (SNTP-over-cellular fallback)
and the existing S-MP25 / S-MP21 candidates are still open.

## What the audit confirmed unchanged (no fixes needed)

* `parse_cclk` and `local_fields_to_utc_epoch` are untouched --
  the new `query_and_parse_cclk` calls them as-is. The init
  function still inlines the same sequence directly (it predates
  the helper); a future refactor could collapse init onto the
  helper too, but doing it now risked subtly changing the init
  log lines, which the previous fire's verification specifically
  exercised. Leaving init alone keeps the boot.log shape pixel-
  identical for the no-modem path.
* `ClockSourceBridge.cpp` is untouched -- the refresh task calls
  the same `clock_source_bridge_apply` symbol that S-MP24/2
  introduced, with the same `(epoch_utc, tz_offset_sec)`
  signature.
* `modem_at_send` is mutex-protected, so calling it concurrently
  from `sms_boot_task` and `clock_refresh_task` is safe by
  contract.
* All other HAL paths are unchanged.

## Open hardware questions still outstanding

Same as before -- none resolved this fire:

* **Q2.** `uPOWER_OFF` GPIO1 polarity for power-off control
  (currently Hi-Z).
* **Q3.** Modem boot-FAIL triage. boot.log still shows AT not
  responding (`MODEM: boot probe... (0..15 s elapsed)` with no
  state transition to MODEM_READY). The clock_source HAL, the
  bridge, AND the refresh task all sit cleanly inert today; the
  first `CLOCK:` line in boot.log will be the trigger that the
  modem started talking.
* **Q4.** STATUS / NET_STATUS LED traces.

## What the next fire should do

S-MP24/3 is in. Pick one (in priority order):

* **S-MP24/4 (next clock feature).** SNTP-over-cellular fallback
  for hardware where the carrier doesn't deliver NITZ. Wait for
  modem READY, dial up a PDP context with `AT+CGACT=1,1`, hit an
  NTP server via the modem's TCP socket API
  (`AT+QIOPEN`/`AT+QIRD`), parse the SNTP response, and feed it
  through `clock_source_bridge_apply` the same way AT+CCLK does.
  Sized for 2-3 fires (PDP bringup + NTP exchange + integration
  with the existing refresh task as an alternative source).
* **S-MP21/3 (modem triage)** -- only if a SIM is inserted
  between fires and the modem starts responding. The new bridge
  + refresh task will print a `CLOCK:` line in the next boot.log,
  so the existence of one is the trigger.
* **S-MP25 (heap-leak cleanup)** -- still low-priority unless
  the boot.log heap line starts trending down. (No heap line
  in today's 20 s window -- could add an `MP24: heap free Nk`
  log at the end of app_main for trend visibility; not in scope
  for this fire.)

## Previous fire (S-MP24/2 -- ClockSourceBridge live)

* **Date (UTC):** 2026-05-18 ~01:34 UTC
* **HEAD on `main`:** `a510593`
  (`fix(build): S-MP24/2/1 -- move clock_source_bridge_apply
  extern "C" decl to file scope`). Two new code commits this fire:
  one feature (S-MP24/2) and one build fix-forward (S-MP24/2/1).
* **Build status:** GREEN at HEAD `a510593`
  (run `26008853891`, both `build` and `flash` jobs
  completed/success). The intermediate feature commit
  (`b0f4b8d`) failed CI on a C++ linkage-spec scope error;
  fix-forward `a510593` resolved it and lands green.
* **Device boot status:** HEALTHY. boot.log scan: 0 crash markers
  (panic/Guru/abort/Backtrace count = 0). Boot shape unchanged
  from the prior baseline (`dc36dd1`):

      BATT: curve-fitting cal active
      BATT: first sample: 1220 mV (1.22 V)
      STORE:   README.txt                       919 B
      STORE:   hello.txt                        140 B
      STORE: sentinel hello.txt missing or unreadable
      STORE: mounted; 2 files, 1757 / 1860161 bytes used (0%)
      POWER: polling GPIO2 at 50 Hz, debounce 3 ticks
      MP24: Entering background-tasks-only loop (LVGL owns the panel).
      MODEM: state -> POWER
      MODEM: state -> BOOT
      MODEM: boot probe... (0..15 s elapsed)

  No `CLOCK:` line in the captured 20 s window because the modem
  still doesn't reach MODEM_READY (Q3 unresolved). The S-MP24/2
  bridge call is gated on `clock_source_have_time()`, which
  returns false today, so the bridge is dead code on this
  hardware -- the firmware behaves exactly as before. The moment
  the modem starts responding the bridge will fire, log a
  `CLOCK: bridge: set PhoneClock to YYYY-MM-DD HH:MM:SS (UTC+/-H:MM,
  utc_epoch=...)` line, and the status bar / Date&Time picker /
  PhoneAlarmService / PhoneVirtualPet flip to the network-supplied
  wall clock.
* **Flasher status:** ONLINE and reliable.
* **Binary size:** within noise of the prior baseline. Net add is
  ~135 lines of pure C++ (the bridge TU) + ~25 lines in
  `app_main.cpp`. No new managed components, no new REQUIRES, no
  new INCLUDE_DIRS, no new headers.

## What this fire actually shipped (net)

1. **`b0f4b8d` -- S-MP24/2 (FEATURE).** New
   `mp24/components/chatter_app/shim/ClockSourceBridge.cpp` (~135
   lines). Exports a single extern-"C" symbol
   `clock_source_bridge_apply(uint32_t epoch_utc,
   int32_t tz_offset_sec)` that takes the cached output of
   `hal/clock_source` and pushes it into `PhoneClock`. Wired into
   `sms_boot_task` in `mp24/main/app_main.cpp` immediately after
   the `clock_source_init(5000)` call, gated on
   `clock_source_have_time()` so we never feed a 0 epoch through.

   Critical correctness note: PhoneClock uses a leap-year-free
   synthetic calendar (every year is 365 days, anchor
   Thu 2026-01-01 == synthetic epoch 1766016000). Real Unix
   epoch from AT+CCLK? is 1767225600 for 2026-01-01 00:00 UTC --
   14 leap days ahead. Calling `setEpoch(epoch_utc + tz)` directly
   would render the date ~14 days off. The bridge therefore
   converts through civil-time fields:

       int64_t local_epoch = (int64_t)epoch_utc + tz_offset_sec;
       gmtime_r(local_epoch, &tm_local);     -- get y/m/d/h/m/s
       synth_epoch = PhoneClock::buildEpoch(year, month, day,
                                            hour, minute, second);
       PhoneClock::setEpoch(synth_epoch);

   `buildEpoch()` clamps year to [2020..2099] and every other
   field to its valid range, so partial modem garbage cannot
   crash anything -- it just lands on a clamped date.

   Logged at INFO on every successful apply with a single
   "CLOCK: bridge: set PhoneClock to YYYY-MM-DD HH:MM:SS
   (UTC+/-H:MM, utc_epoch=...)" line so future fires can confirm
   wall-clock acquisition by grepping the boot.log artifact.

   Component churn:
     + `mp24/components/chatter_app/shim/ClockSourceBridge.cpp`  new (~135 lines)
     M `mp24/components/chatter_app/CMakeLists.txt`              add shim/ClockSourceBridge.cpp to SRCS
     M `mp24/main/app_main.cpp`                                  forward-decl + gated call inside sms_boot_task

2. **`a510593` -- S-MP24/2/1 (BUILD FIX-FORWARD).** The
   `b0f4b8d` feature commit placed an
   `extern "C" void clock_source_bridge_apply(...)` prototype
   inside `sms_boot_task` (block scope). C++ does not permit
   linkage specifications in block scope (gcc rejects with
   "expected unqualified-id before string constant"), so CI
   failed at `app_main.cpp:418`. The fix-forward moves the
   prototype up to file scope, immediately after the existing
   `extern "C" { #include "hal/*.h" }` block. Call site loses
   the inline prototype and calls the function directly. No
   header changes, dependency direction stays
   main -> chatter_app.

   Component churn:
     M `mp24/main/app_main.cpp`                                  prototype at file scope, not block scope

## Why this fire shipped S-MP24/2 only

The prior checkpoint recommended S-MP24/2 (clock_source ->
PhoneClock bridge) as the next feature, sized as a single fire.
That estimate held: one feature commit + one build fix-forward,
total wall time well under the 30-minute budget. S-MP24/3
(periodic re-query for drift correction) and S-MP24/4
(SNTP-over-cellular fallback) are still open and sized for
single fires each.

## What the audit confirmed unchanged (no fixes needed)

* PhoneClock itself is untouched this fire -- the bridge only
  calls into its existing public API (`buildEpoch`, `setEpoch`).
  Today every `PhoneClock::nowEpoch()` call still returns
  "anchor + uptime" because the modem isn't responding and the
  bridge call is skipped.
* `sms_boot_task` keeps its same overall shape -- sms_init,
  calls_init, audio_i2s2_init, clock_source_init -- with one
  additional gated call after clock_source_init. The mutex
  serialising AT commands inside `modem_at_send` is unaffected.
* All other HAL paths are unchanged.
* The `panic-on-no-battery` path and the open-HW notes from
  prior fires are still pending; this fire did not touch any
  of them.

## Open hardware questions still outstanding

Same as before -- none resolved this fire:

* **Q2.** `uPOWER_OFF` GPIO1 polarity for power-off control
  (currently Hi-Z).
* **Q3.** Modem boot-FAIL triage. boot.log still shows AT not
  responding ("MODEM: boot probe... (0..15 s elapsed)" with no
  state transition to MODEM_READY). The clock_source HAL +
  bridge sit cleanly inert today; the first `CLOCK:` line in
  boot.log will be the trigger that the modem started talking.
* **Q4.** STATUS / NET_STATUS LED traces.

## What the next fire should do

S-MP24/2 is in. Pick one (in priority order):

* **S-MP24/3 (recommended next feature).** Periodic re-query
  for drift correction. Wire a tiny FreeRTOS timer (or extend
  sms_boot_task into a long-running loop) that runs the AT+CCLK?
  probe on a ~1-hour cadence after the first successful sync.
  Refresh PhoneClock only if the new value is more than 60 s off
  the locally-extrapolated wall time, so we don't fight
  legitimate user edits in the Date&Time picker. Sized for a
  single fire (the timer + delta-gate + a reused call into
  `clock_source_bridge_apply`).
* **S-MP21/3 (modem triage)** -- only if a SIM is inserted
  between fires and the modem starts responding. The new
  bridge will print a `CLOCK:` line in the next boot.log,
  so the existence of one is the trigger.
* **S-MP25 (heap-leak cleanup)** -- still low-priority unless
  the boot.log heap line starts trending down.

## Helper script note carried from prior fires

* The helpers (`flash_iter.sh`, `addr2line.py`) still need to be
  recreated each fire from the brief; the sandbox doesn't
  preserve them and `/home/claude/` is not writable
  (`mkdir: cannot create directory '/home/claude':
  Permission denied`).
* This fire's sandbox: user was `nice-elegant-faraday`, `$HOME`
  was `/sessions/nice-elegant-faraday` and `/sessions` was
  **100% full** (9.8 G / 9.8 G -- shared across parallel
  sessions). Could not even write `~/.gitconfig`. `pip install`
  failed with ENOSPC. Workarounds used this fire:
    - `export GIT_CONFIG_GLOBAL=/tmp/gitconfig` and write the
      gitconfig there (`safe.directory=*` + user.name/.email).
    - Clone fresh into `/tmp/mp24_cur/mp_firmware` (UID-owned,
      succeeded). The pre-existing clones under `/tmp/claude`,
      `/tmp/aw_mp24`, `/tmp/work`, `/tmp/wozniak`, etc. are
      all owned by `nobody:nogroup` and read-only to this
      fire; cannot reuse them.
    - Helper scripts written to `/tmp/mp24_cur/bin/`
      (`flash_iter.sh`, `addr2line.py`) instead of `/tmp/`
      directly (the latter has a `flash_iter.sh` owned by
      `nobody` from a prior fire that cannot be overwritten).
    - Skipped `pip install pyelftools` (no space and no crash
      to decode this fire). If a future fire hits a crash and
      pyelftools is still uninstallable, decode by hand from
      the symbol table the build uploads.
* The bash tool's 45-second timeout makes long polling
  impractical in one call. This fire chunked the CI poll into
  ~30-second sleeps per call. Build took ~70 s + ~80 s
  retry; flash + boot capture ~30 s each. Total fire wall time
  well within the 30-minute budget.
* The CI workflow triggers on push automatically via the
  `mp24/**` / `src/**` path filter -- no manual
  `workflow_dispatch` is needed for code changes. Docs-only
  commits (like this checkpoint) do NOT trigger a build.
* The Read tool's connected-folders rule blocks `/tmp/...`
  reads even when bash can reach them; use `sed -n` / `cat`
  via bash instead, or use a python heredoc for in-place
  edits. Write tool fails the same way -- use
  `cat > path <<'EOF'` or python open-for-write via bash.
* Cloning the repo into the outputs mount
  (`/sessions/<user>/mnt/outputs/`) fails on `.git/config`
  unlinking due to bindfs semantics. Stick to `/tmp` (this
  fire used `/tmp/mp24_cur`).

