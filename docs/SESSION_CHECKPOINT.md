# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-18 ~22:55 UTC
* **HEAD on `main`:** `c1adca8` (`feat(mp24): S-MP23/1 --
  explicit nvs_flash_init() before Settings/Storage`). One
  feature commit, GREEN on first build with zero fix-forwards.
* **Build status:** GREEN at HEAD `c1adca8` (run
  `26004993323`, both `build` and `flash` jobs completed/
  success).
* **Device boot status:** HEALTHY. boot.log scan: 0 crash
  markers (panic/guru/abort/Backtrace count = 0). Boot shape
  unchanged from prior green baseline:

      BATT: curve-fitting cal active
      BATT: first sample: 1218 mV (1.22 V)
      STORE:   README.txt                       919 B
      STORE:   hello.txt                        140 B
      STORE: sentinel hello.txt missing or unreadable
      STORE: mounted; 2 files, 1757 / 1860161 bytes used (0%)
      POWER: polling GPIO2 at 50 Hz, debounce 3 ticks
      MP24: Entering background-tasks-only loop (LVGL owns the panel).
      MODEM: state -> POWER
      MODEM: state -> BOOT
      MODEM: boot probe... (0 s elapsed) ... (15 s elapsed)

  The new "NVS flash initialised" log line happens BEFORE the
  flasher's USB-CDC capture window opens (it's logged at boot
  time ~30-200 ms in, well before the BATT line at 2.3 s), so
  it's not visible in boot.log. Absence of crashes plus a
  normally-progressing boot through STORE / POWER / MODEM is
  strong evidence the NVS init runs cleanly -- a corrupt-NVS
  path would have crashed in nvs_flash_erase / reinit or
  surfaced ESP_ERROR_CHECK aborts.
* **Flasher status:** ONLINE and reliable.
* **Binary size:** +~250 B for the nvs_flash component init,
  well within noise.

## What this fire actually shipped (net)

S-MP23 has its foundation step landed.

1. **`c1adca8` -- S-MP23/1.** Explicit `nvs_flash_init()` in
   `mp24/main/app_main.cpp` right after `initArduino()` and
   before any other HAL bring-up.

   Discovered while preparing for the S-MP23 StorageStub.cpp ->
   NVS-backed Repo<T> migration: `Settings.begin()` (called
   inside `Chatter.begin()`) does `nvs_open("CircuitOS", ...)`
   which silently fails with `ESP_ERR_NVS_NOT_INITIALIZED`
   unless `nvs_flash_init()` has run first. arduino-esp32 v3.x
   does NOT call it from `initArduino()` -- it deferred that
   to Preferences::begin / WiFi.begin consumers we don't use.
   On MP2.4 the Settings call has been a no-op since S-MP14.

   The fix: standard ESP-IDF idiom -- `nvs_flash_init()`, fall
   into `nvs_flash_erase()` + reinit on `NO_FREE_PAGES` /
   `NEW_VERSION_FOUND`, log success/failure. One INFO line on
   the happy path, one WARN+reinit-cycle if the partition is
   ever corrupted.

   Also added `nvs_flash` to `mp24/main/CMakeLists.txt`
   REQUIRES so `#include "nvs_flash.h"` resolves without
   leaning on transitive exposure through circuitos.

   Files: `mp24/main/app_main.cpp` (+33 -0),
   `mp24/main/CMakeLists.txt` (+1 -0). Two-file commit.

   This is the FOUNDATION step for S-MP23 proper. The next
   incremental commits will start replacing StorageStub's
   `Repo<T>::add/get/all/remove/exists` no-ops with NVS-blob
   reads/writes keyed by per-type-prefix + numeric uid (e.g.
   `cnt_<uid>` for PhoneContact, `frd_<uid>` for Friend,
   `msg_<uid>` for Message, `cnv_<uid>` for Convo).

## Why this fire shipped a foundation step instead of a feature

Two reasons:

1. The previous fire's "what next" guidance recommended
   pivoting to S-MP23 (StorageStub -> NVS) on the assumption
   the modem-bring-up Q3 path stays hardware-blocked. It does
   (boot.log still shows no AT response).
2. Auditing the StorageStub.cpp surface area before writing
   any NVS-backed replacements turned up the prerequisite gap:
   the firmware was running with NVS uninitialised the whole
   time. Fixing that first is a one-commit foundation that
   unblocks both the Settings path (which has been a no-op
   since S-MP14) AND the upcoming Repo<T> migration. Shipping
   it standalone keeps the next-fire diffs focused on the
   actual storage-layer work.

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

S-MP23 is now in flight -- continue it with `S-MP23/2`:

1. **S-MP23/2 -- back PhoneContacts namespace with NVS.**
   The PhoneContacts namespace's 28 functions in
   `mp24/components/chatter_app/shim/StorageStub.cpp` are the
   smallest, most-bounded slice of the Storage surface: they
   take primitives (UID_t, uint8_t, const char*, bool) keyed
   by UID. Replace the no-op bodies with NVS reads/writes
   under a `"pc_"` namespace (one nvs_handle, key pattern
   `<field>_<uid>` e.g. `name_42`, `fav_42`). Keep the Repo<T>
   templates as no-ops in this commit -- that's S-MP23/3.

2. **S-MP23/3 -- back Repo<T> with NVS.** Each Friend /
   PhoneContact / Message / Convo serialised as an NVS blob
   keyed by `<type>_<uid>`. The all() listing maintains an
   index blob (`idx_<type>`) containing the UID list.
   Slightly more involved -- defer to its own fire.

Alt path if a SIM gets inserted between fires: pivot back to
S-MP21/3 (post-READY probes will surface SIM state).

## Helper script note carried from prior fires

* The helpers (`flash_iter.sh`, `addr2line.py`) need to be
  recreated each fire from the brief; the sandbox doesn't
  preserve `/home/claude/` (in fact, `/home/claude/` is not
  writable -- `mkdir: cannot create directory '/home/claude':
  Permission denied`).
* This fire's sandbox: user was `magical-inspiring-mayer`,
  `$HOME` was `/sessions/magical-inspiring-mayer`, and
  `/sessions/...` was 100% full again (9.8G/9.8G -- shared
  across parallel sessions). Even `mkdir` inside `$HOME`
  fails. The writable scratch path used was
  `/tmp/mp24-${USER}/repo/mp_firmware/`, with the root FS
  having 859 MB free.
* As before, the bash tool's 45-second timeout makes the
  helper scripts impractical to run end-to-end in one call;
  this fire used the chunked GH API approach (poll jobs every
  ~35-40s with a separate bash call each time) and it worked
  smoothly.
* The CI workflow triggers on push automatically via the
  `mp24/**` / `src/**` path filter -- no manual
  `workflow_dispatch` is needed for code changes. Docs-only
  commits (like this checkpoint) do NOT trigger a build.
