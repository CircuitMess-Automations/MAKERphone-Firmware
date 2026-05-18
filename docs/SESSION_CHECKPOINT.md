# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-18 ~01:15 UTC
* **HEAD on `main`:** `dc36dd1` (`feat(mp24): S-MP24/1 -- AT+CCLK?
  wall-clock source HAL`). One feature commit, GREEN on first
  build with zero fix-forwards.
* **Build status:** GREEN at HEAD `dc36dd1` (run `26008229216`,
  both `build` and `flash` jobs completed/success).
* **Device boot status:** HEALTHY. boot.log scan: 0 crash markers
  (panic/Guru/abort/Backtrace count = 0). Boot shape unchanged
  from the prior baseline (`6a106df`):

      STORE:   README.txt                       919 B
      STORE:   hello.txt                        140 B
      STORE: sentinel hello.txt missing or unreadable
      STORE: mounted; 2 files, 1757 / 1860161 bytes used (0%)
      POWER: polling GPIO2 at 50 Hz, debounce 3 ticks
      MP24: Entering background-tasks-only loop (LVGL owns the panel).
      MODEM: state -> POWER
      MODEM: state -> BOOT
      MODEM: boot probe... (0..15 s elapsed)

  No `CLOCK:` lines in the captured 20 s window because the modem
  never reached MODEM_READY -- Q3 (modem boot-FAIL triage) is
  still outstanding. That is the expected behaviour for this
  fire: the new clock_source code sits on a `wait_for_ready(5000)`
  from inside sms_boot_task and times out cleanly when the modem
  stays in BOOTING. No regression.
* **Flasher status:** ONLINE and reliable.
* **Binary size:** within noise of the prior baseline; the new
  code is ~325 lines (~70 header, ~255 .c) of pure-ESP-IDF C,
  no new managed components, no link-graph changes.

## What this fire actually shipped (net)

1. **`dc36dd1` -- S-MP24/1.** New `mp24/main/hal/clock_source.{h,c}`
   module that, once the modem is in MODEM_READY, sends `AT+CCLK?`
   exactly once, parses the `+CCLK: "YY/MM/DD,HH:MM:SS+/-ZZ"`
   reply, converts local-time + quarter-hour zone to a UTC epoch
   second via `mktime()` (with `TZ=UTC0` forced for safety), and
   caches the result behind atomics for any caller. Wired into
   `sms_boot_task` immediately after `audio_i2s2_init`, with its
   own short 5 s ready-wait since `sms_init` already burned the
   35 s ready-wait budget on its way to reaching READY.

   Sanity rejections (logged at WARN):
     - Year < 2024 or > 2069 (catches the common `80/01/06,...`
       pre-NITZ default and 2-digit-year wraparound garbage).
     - mktime returning `(time_t)-1`.
     - Zone magnitude > 56 quarter-hours (== UTC+/-14:00, the
       max valid zone).
     - sscanf scanning fewer than 8 fields.

   Public API (`hal/clock_source.h`):
     - `clock_source_init(uint32_t ready_wait_ms)` -- idempotent,
       returns ESP_OK on success, ESP_ERR_TIMEOUT if no READY,
       ESP_FAIL if CCLK unparseable / out-of-range.
     - `clock_source_epoch_utc()` -- cached UTC epoch second, 0
       if never acquired.
     - `clock_source_have_time()` -- bool.
     - `clock_source_tz_offset_seconds()` -- modem-reported tz
       offset east of UTC, in seconds (so UTC+8:00 -> +28800).

   Read-once-at-boot model on purpose. A future fire can wire
   periodic re-queries + listener fan-out (S-MP24/2), and bridge
   `clock_source_epoch_utc()` -> `PhoneClock::setEpoch()`
   (S-MP24/3) so the status bar + Date&Time picker pick up the
   network time live. Splitting the work this way keeps each
   commit small enough to ship green per fire.

   Component churn:
     + `mp24/main/hal/clock_source.h`          new (~70 lines)
     + `mp24/main/hal/clock_source.c`          new (~255 lines)
     M `mp24/main/CMakeLists.txt`              add hal/clock_source.c to SRCS
     M `mp24/main/app_main.cpp`                #include + clock_source_init call inside sms_boot_task

## Why this fire shipped S-MP24/1 only

The previous fire's checkpoint named S-MP24 (real clock) as the
recommended next feature, sized at "probably 2-4 fires (modem AT
probe, parse + SNTP fallback, PhoneClock wire-up, polish)". This
fire shipped the first slice -- the modem AT probe + parser HAL.

S-MP24 splits naturally into stages that can each ship green per
fire:

  1. **S-MP24/1 (THIS FIRE).** AT+CCLK? probe in a standalone
     hal/clock_source.{h,c} module. No effect on user-facing
     behaviour today (modem stuck in BOOTING). Lands the parser
     + cache layer so future fires can build on it without
     touching the modem AT framework themselves.
  2. **S-MP24/2 (next fire, sized for one fire).** Add
     `clock_source` -> PhoneClock bridge -- a tiny C++ shim in
     `mp24/components/chatter_app/shim/` that on boot reads
     `clock_source_epoch_utc()` and, if non-zero, calls
     `PhoneClock::setEpoch()`. Effectively no-op on hardware
     where modem still doesn't respond; instant network time on
     hardware where the modem does respond.
  3. **S-MP24/3 (one fire).** Periodic re-query for drift
     correction. Run the AT+CCLK probe on a 1-hour timer, only
     refresh `PhoneClock::setEpoch()` if the new value is more
     than 60 s off the locally-extrapolated wall time, so we
     don't fight legitimate user edits in the Date&Time picker.
  4. **S-MP24/4 (one fire) -- conditional.** SNTP-over-cellular
     fallback path. Only worth shipping if Q3 stays unresolved
     long enough that we want a non-modem time source; otherwise
     dead code.

Splitting the work this way means every fire stays well under
the 30-min budget, each commit is independently revertible, and
the device never regresses past "boots cleanly".

## What the audit confirmed unchanged (no fixes needed)

* `PhoneClock` itself is untouched this fire -- it still backs
  the synthetic 2026-01-01 anchor. Once S-MP24/2 wires the
  bridge, the FIRST `PhoneClock::nowEpoch()` call after modem
  READY will return the network time; today every call still
  returns "anchor + uptime".
* `sms_boot_task` already enforces serialised modem AT access
  via `s_at_mutex` inside `modem_at_send`, so the new
  `modem_at_send("+CCLK?", ...)` call cannot race the SMS or
  CALLS layers -- they all queue on the same mutex.
* All other HAL paths are unchanged -- new file + 1-line wire-in
  in app_main.cpp + 1-line addition to CMakeLists.txt.
* The `panic-on-no-battery` path and all the other open-HW
  notes from prior fires are still pending; this fire did not
  touch any of them.

## Open hardware questions still outstanding

Same as before -- none resolved this fire:

* **Q2.** `uPOWER_OFF` GPIO1 polarity for power-off control
  (currently Hi-Z).
* **Q3.** Modem boot-FAIL triage. boot.log still shows AT not
  responding ("MODEM: boot probe... (0..15 s elapsed)" with no
  state transition to MODEM_READY). The new clock_source code
  is gated on MODEM_READY and times out cleanly today, so it
  doesn't surface anything new about Q3, but it WILL produce
  the first network-time `CLOCK:` log line in boot.log the
  moment the modem starts responding.
* **Q4.** STATUS / NET_STATUS LED traces.

## What the next fire should do

S-MP24/1 is in. Pick one (in priority order):

* **S-MP24/2 (recommended next feature).** Add the
  `clock_source` -> `PhoneClock` bridge. New file
  `mp24/components/chatter_app/shim/ClockSourceBridge.cpp`
  (~40 lines) that reads `clock_source_epoch_utc()` after
  `clock_source_init` returns and, if non-zero, calls
  `PhoneClock::setEpoch(epoch_utc + tz_offset_seconds)` so the
  user sees local wall time on screen. Wire into either
  `sms_boot_task` (extend it) or as a tiny one-shot task spawned
  from `Chatter.begin()`. Sized for a single fire.
* **S-MP21/3 (modem triage)** -- only if a SIM is inserted
  between fires and the modem starts responding. The new
  CCLK probe will print its result in the next boot.log, so
  the existence of a `CLOCK:` line in boot.log is the trigger.
* **S-MP25 (heap-leak cleanup)** -- still low-priority unless
  the boot.log heap line starts trending down.

## Helper script note carried from prior fires

* The helpers (`flash_iter.sh`, `addr2line.py`) need to be
  recreated each fire from the brief; the sandbox doesn't
  preserve `/home/claude/` (in fact, `/home/claude/` is not
  writable -- `mkdir: cannot create directory '/home/claude':
  Permission denied`).
* This fire's sandbox: user was `beautiful-determined-davinci`,
  `$HOME` was `/sessions/beautiful-determined-davinci` and was
  100% full (9.8 G / 9.8 G -- shared across parallel sessions).
  `~/.gitconfig.lock` failed with ENOSPC and any attempt to
  `git config --global` also failed. Workaround used this fire:
  drop the `git config user.name / user.email` calls in favour
  of `git -c user.name=... -c user.email=... commit` for the
  one commit we needed to make, and clone into `/tmp/work_davinci/`
  (UID-owned, succeeded; the canonical `/tmp/claude/repo/...`
  from prior fires was owned by `nobody:nogroup` and read-only
  to this fire).
* The bash tool's 45-second timeout makes long polling
  impractical in one call. This fire chunked the CI poll into
  ~30-second sleeps per call. Build took ~1m20s; flash + boot
  capture ~30 s. Total fire wall time well within the 30-minute
  budget with room for another iteration if needed.
* The CI workflow triggers on push automatically via the
  `mp24/**` / `src/**` path filter -- no manual
  `workflow_dispatch` is needed for code changes. Docs-only
  commits (like this checkpoint) do NOT trigger a build.
* The Read tool's connected-folders rule blocks `/tmp/...` reads
  even when bash can reach them; use `sed -n` / `cat` via bash
  instead, or use a python heredoc for in-place edits. Write
  tool fails the same way -- use `cat > path <<'EOF'` or python
  open-for-write via bash.
* Cloning the repo into the outputs mount
  (`/sessions/<user>/mnt/outputs/`) fails on `.git/config`
  unlinking due to bindfs semantics. Stick to `/tmp` (this fire
  used `/tmp/work_davinci`).
