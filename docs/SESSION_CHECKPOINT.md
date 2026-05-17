# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-18 ~22:32 UTC
* **HEAD on `main`:** `d9968a0` (`feat(mp24): S-MP21/2 --
  rate-limit modem boot probe log + add FAIL triage`). One
  feature commit, GREEN on first build with zero fix-forwards.
* **Build status:** GREEN at HEAD `d9968a0` (run
  `26004546942`, both `build` and `flash` jobs completed/
  success).
* **Device boot status:** HEALTHY. boot.log scan: 0 crash
  markers. NEW cleaner boot sequence in this fire:

      STORE: mounted; 2 files, 1757 / 1860161 bytes used (0%)
      POWER: polling GPIO2 at 50 Hz, debounce 3 ticks
      MP24: Entering background-tasks-only loop (LVGL owns the panel).
      MODEM: state -> POWER
      MODEM: state -> BOOT
      MODEM: boot probe... (0 s elapsed)
      MODEM: boot probe... (5 s elapsed)
      MODEM: boot probe... (10 s elapsed)
      MODEM: boot probe... (15 s elapsed)

  Boot probe log dropped from ~36 lines/20s capture to 4. The
  20 s boot capture window ends before the 30 s MODEM_FAILED
  timeout, so the new FAIL triage hints remain latent but in
  place. The post-READY SIM/signal/operator probes from
  S-MP21/1 are likewise unreached -- same hardware blocker as
  before (no SIM, modem firmware never gets to "OK").
* **Flasher status:** ONLINE and reliable.
* **Binary size:** unchanged within noise -- patch adds ~6
  short ESP_LOG strings.

## What this fire actually shipped (net)

S-MP21 has its second incremental step landed.

1. **`d9968a0` -- S-MP21/2.** Two small improvements to
   `mp24/main/hal/modem.c` `modem_sm_task()`:

   - **Rate-limit the boot-probe log to once per 5 elapsed
     seconds** (plus one line on entry). Previously the probe
     loop fired `ESP_LOGI("boot probe... (N s elapsed)")` on
     every iteration -- once per 500 ms -- yielding ~60 lines
     of essentially identical log noise during the 30 s
     timeout window when the modem doesn't respond. Now a
     healthy boot.log shows at most ~7 boot-probe lines. The
     probe rate itself stays at 500 ms; only the log line is
     throttled. Robust against scheduler delays: uses
     `elapsed_s >= last + 5` instead of `% 5 == 0`.

   - **Add a four-line triage hint on the 30 s timeout** (the
     `MODEM_FAILED` path), pointing at the four hardware
     checks from Q3 in `docs/MP24_OPEN_HW_QUESTIONS.md`:
     VBAT_RF/VBAT_BB rails, PWR_KEY trace, RESET_N idle level,
     UART1 wiring. Makes the FAIL log actionable the moment
     it appears.

   Net change: 23 insertions, 7 deletions, single file. No
   behavior change in the happy path: when the modem responds
   within seconds, the probe loop logs at most once before
   exiting.

   Empirically verified in run `26004546942` -- boot.log
   shrunk from 40 lines (prior fire's snapshot) to 12 lines
   over the same 20 s capture window. Same MODEM state
   transitions, just legible now.

## Why this fire was uneventful

The prior two fires already landed the structurally big pieces:
S-MP20 (games engine + glm + dialer wiring -- DONE) and
S-MP21/1 (post-READY SIM/signal/operator probes). The board is
still in the no-SIM / no-modem-response state, so neither set
of probes has anything to surface. This fire took the obvious
follow-up: tame the noise so the *next* hardware bring-up
session can read the log at a glance.

## Open hardware questions still outstanding

Same as before -- none resolved this fire:

* **Q2.** `uPOWER_OFF` GPIO1 polarity for power-off control
  (currently Hi-Z).
* **Q3.** Modem boot-FAIL triage. The new SIM/signal/operator
  probes are the first half of the answer (visibility into
  WHICH failure mode after AT works). The "AT never responds"
  case still needs hardware-side triage: is the SIM physically
  inserted? Does the PWR_KEY trace reach the modem? Is RESET_N
  actually high? Is VBAT supplying the modem?
* **Q4.** STATUS / NET_STATUS LED traces.

## What the next fire should do

Two viable paths, depending on hardware state:

1. **If the user inserts a SIM into the test device before the
   next fire**, the new probes will reveal SIM/signal/operator
   state in boot.log, unblocking S-MP21 proper:
     * Confirm `AT+CPIN?` returns `READY`.
     * Confirm `AT+CSQ` shows real RSSI (not 99).
     * Use `AT+CMGS` / SMS path to send a test message
       (exercises `mp24/main/hal/sms.c`).
     * Add an `AT+COPS?` follow-up that retries every 5s
       in a background task until registered.

2. **If still no SIM** (most likely -- this is a multi-fire
   blocker on hardware action), pivot to **S-MP23 -- replace
   `StorageStub.cpp` with NVS-backed `Repo<T>`**. This is
   independent of modem bring-up and a small, well-scoped
   change:
     * Locate `StorageStub.cpp` in `mp24/components/chatter_app/
       shim/`.
     * For each `Repo<T>::add/remove/forEach`, key NVS records
       by a per-type prefix (e.g. `cnt_` for contacts) + a
       numeric id.
     * Confirm via a single contact-add + reset-and-reboot
       that the entry survives.

   Or, alternatively, **S-MP26 polish-and-verify** -- walk
   every menu destination once in chat, compile a punch-list
   of which downstream screens visibly render vs. which are
   still LVGL-blank-screen stubs. Cheap, informational, and
   informs what to prioritize in S-MP22-25.

## Helper script note carried from prior fires

* The helpers (`flash_iter.sh`, `addr2line.py`) need to be
  recreated each fire from the brief; the sandbox doesn't
  preserve `/home/claude/` (in fact, `/home/claude/` is not
  writable -- `mkdir: cannot create directory '/home/claude':
  Permission denied`). The brief's `if [[ ! -d /home/claude/
  repo/mp_firmware ]]` check therefore always fires and the
  clone command fails on the missing parent.
* This fire's sandbox: user was `epic-admiring-pasteur`,
  `$HOME` was `/sessions/epic-admiring-pasteur`, and
  `/sessions/...` was 100% full again (9.8G/9.8G -- it
  appears to be a shared filesystem across all parallel
  sessions). Even `mkdir` inside `$HOME` fails on it.
  `/tmp/` (the root-FS tmp) was owned by `nobody` from
  a previous session and not writable for this user.
  The writable scratch path used was `/var/tmp/` (885 MB
  free under `/`). The repo cloned to
  `/var/tmp/claude/repo/mp_firmware/` (16 MB).
* As before, the bash tool's 45-second timeout makes the
  helper scripts impractical to run end-to-end in one call;
  this fire used the chunked GH API approach (poll jobs every
  ~35-40s with a separate bash call each time) and it worked
  smoothly.
* The CI workflow triggers on push automatically via the
  `mp24/**` / `src/**` path filter -- no manual
  `workflow_dispatch` is needed for code changes. Docs-only
  commits (like this checkpoint) do NOT trigger a build.

