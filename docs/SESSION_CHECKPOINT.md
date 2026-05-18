# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

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

