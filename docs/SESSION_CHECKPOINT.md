# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-18 ~22:11 UTC
* **HEAD on `main`:** `abff9ed` (`feat(mp24): S-MP21/1 -- post-
  READY SIM/signal/operator probes`). One feature commit, GREEN
  on first build with zero fix-forwards.
* **Build status:** GREEN at HEAD `abff9ed` (run
  `26004109536`, both `build` and `flash` jobs completed/
  success).
* **Device boot status:** HEALTHY. boot.log scan: 0 crash
  markers. Same boot sequence as prior fires:

      BATT: curve-fitting cal active
      STORE: mounted; 2 files, 1757 / 1860161 bytes used (0%)
      POWER: polling GPIO2 at 50 Hz, debounce 3 ticks
      MP24: Entering background-tasks-only loop (LVGL owns the panel).
      MODEM: state -> POWER -> BOOT -> boot probe... (up to 16 s)

  The new SIM/signal/operator probes do **not** appear in this
  fire's boot.log because the modem is still stuck in BOOT --
  never reaches READY -- so the post-`AT+CGMM` code path is
  unreached. That's the expected current hardware state (bare
  PCB, no SIM, modem-firmware probe never returns OK). The new
  probes are in place and will fire the moment the modem comes
  alive.
* **Flasher status:** ONLINE and reliable.
* **Binary size:** `mp24-firmware` artifact is 12,120,420 bytes
  at HEAD `abff9ed` (up from 12,119,504 at `29c6eeb` -- a
  +916 byte delta from the ~25 lines of new logic + format
  strings). Still well under the 2 MB app partition.

## What this fire actually shipped (net)

S-MP21 has its first incremental step landed.

1. **`abff9ed` -- S-MP21/1.** Extended `mp24/main/hal/modem.c`
   `modem_sm_task()` to add three best-effort AT probes after
   the existing `AT+CGMM` model query, before `vTaskDelete`:

     * `AT+CPIN?` (3s timeout) -- SIM state: `READY`, `SIM PIN`,
       or `+CME ERROR: 10` (no SIM). Logged as `SIM: <state>`.
       If `modem_at_send` returns non-OK, logs an explicit
       hint that the modem likely returned no-SIM/locked.
     * `AT+CSQ` (2s timeout) -- RSSI/BER, e.g. `+CSQ: 18,99`.
       Logged as `signal: <value>`. Doesn't require
       registration; only the modem firmware to be up.
     * `AT+COPS?` (8s timeout) -- current operator name.
       Logged as `operator: <value>`. The 8 s timeout
       accommodates an in-progress network re-scan; if the
       modem is unregistered it returns `+COPS: 0` quickly
       anyway.

   Each probe is wrapped in an `if (modem_at_send(...) == ESP_OK)`
   block with a `else { ESP_LOGW(...) }` fallback so a single
   ERROR / timeout never aborts the others. `resp` buffer
   bumped 64 -> 96 bytes for longer operator-name responses
   like `+COPS: 0,0,"T-Mobile Hrvatska",7`.

   The change is a strict superset of S-MP20's modem behavior:
   if the modem never wakes up (today's case), the boot.log
   looks identical. The moment AT starts responding, the next
   three lines of boot.log will tell us EXACTLY which of the
   "no SIM / no signal / unregistered" failure modes we're in.

## Why this fire was uneventful

The previous fire (S-MP20/14, HEAD `29c6eeb`) had already
finished the games-engine compile validation. S-MP20 is
structurally DONE. This fire moved to S-MP21 (modem hardware
bring-up) from the roadmap and shipped the smallest useful
diagnostic step: post-READY visibility.

The 30-minute fire budget covered one CI cycle with room to
spare. Audit-first approach: read `modem.h` + `modem.c`
end-to-end (~5 minutes), confirmed PWR_KEY polarity matches
Quectel V1.1 §3.4.1 in software (idle HIGH, 2.5 s LOW pulse,
back to HIGH), confirmed UART pins match `pins.h`, identified
the post-`AT+CGMM` insertion point, wrote the patch (25 lines),
push -> wait for CI -> boot.log scan.

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

