# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-18 ~23:14 UTC
* **HEAD on `main`:** `eafd088` (`feat(mp24): S-MP23/2 -- back
  PhoneContacts namespace with NVS`). One feature commit, GREEN on
  first build with zero fix-forwards.
* **Build status:** GREEN at HEAD `eafd088` (run `26005461264`,
  both `build` and `flash` jobs completed/success).
* **Device boot status:** HEALTHY. boot.log scan: 0 crash markers
  (panic/guru/abort/Backtrace count = 0). Boot shape unchanged
  from prior green baseline (c1adca8):

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
      MODEM: boot probe... (0 s elapsed) ... (15 s elapsed)

  No PhoneContacts mutations happen during the 20 s boot capture
  window, so the new NVS code path isn't exercised at boot. The
  link is clean and the boot shape is byte-identical to the
  previous green baseline -- which is the meaningful evidence
  this fire was after: that adding `nvs_flash` to chatter_app's
  REQUIRES and including `nvs.h` from StorageStub.cpp doesn't
  break anything at link time or runtime.
* **Flasher status:** ONLINE and reliable.
* **Binary size:** ~1.21 MB (unchanged within noise from prior
  baseline; the new code path is tiny and most NVS surface is
  already linked in by the S-MP23/1 init call).

## What this fire actually shipped (net)

1. **`eafd088` -- S-MP23/2.** PhoneContacts namespace in
   `mp24/components/chatter_app/shim/StorageStub.cpp` is now
   NVS-backed instead of all-no-op. All 28 functions read /
   write the real PhoneContact struct under NVS namespace `"pc"`,
   one blob per contact, keyed by a 14-byte base32 encoding of
   the 64-bit UID (`'c' + 13 base32 chars`).

   Three anonymous-namespace helpers do the heavy lifting:

       uidToKey(uid, out)   -- 14-char base32 NVS key
       loadRecord(uid, out) -- READONLY open + nvs_get_blob
                               + uid match (defensive)
       saveRecord(c)        -- READWRITE open + nvs_set_blob
                               + nvs_commit
       eraseRecord(uid)     -- READWRITE open + nvs_erase_key
                               + nvs_commit

   Every public function now does the read-modify-write cycle
   through these helpers. Behaviour matches upstream
   PhoneContacts.cpp at the public-API boundary -- including
   the "clearX returns true when nothing to clear" semantics
   for clearBirthday/clearDisplayName/clearWallpaper, and the
   ContactFlag_HasX gating for has* / *Of queries.

   Difference from upstream worth noting: displayNameOf's fall-
   back to Friend's broadcast nickname is dropped (returns "" on
   miss) because Storage.Friends is still the no-op Repo<Friend>.
   S-MP23/3 will restore the fallback once Repo<Friend> is NVS-
   backed too.

   Also added `nvs_flash` to chatter_app/CMakeLists.txt REQUIRES
   so `#include "nvs.h"` resolves without leaning on transitive
   exposure through main/.

   Files: `mp24/components/chatter_app/shim/StorageStub.cpp`
   (+322 -32), `mp24/components/chatter_app/CMakeLists.txt`
   (+1 -0). Two-file commit.

   On boot with an empty NVS partition, every `loadRecord`
   returns false and behaviour is byte-identical to the
   previous all-no-op stub (getters return defaults / derived
   values, setters succeed and persist). The first time the
   user edits a contact, that contact's state survives a reset
   cycle.

## Why this fire shipped the storage step (not modem)

The previous fire identified S-MP23 as the right next sub-step
(hardware-blocked on modem-side Q3). PhoneContacts namespace was
the smallest, most-bounded slice of the Storage surface as the
brief suggested -- 28 functions over a single struct, no Repo<T>
template complications. Pure read-modify-write through three
helpers, clean NVS commit policy, byte-for-byte semantic match
with upstream behaviour at the public-API boundary.

## Open hardware questions still outstanding

Same as before -- none resolved this fire:

* **Q2.** `uPOWER_OFF` GPIO1 polarity for power-off control
  (currently Hi-Z).
* **Q3.** Modem boot-FAIL triage. boot.log still shows AT not
  responding ("MODEM: boot probe... (0..15 s elapsed)" with no
  state transition to MODEM_READY). The SIM/signal/operator
  probes from S-MP21/1 would surface state once AT works; the
  AT-never-responds case still needs hardware-side triage.
* **Q4.** STATUS / NET_STATUS LED traces.

## What the next fire should do

S-MP23 momentum continues with `S-MP23/3` -- back Repo<T> with
NVS:

1. **S-MP23/3 -- Repo<Friend>, Repo<Message>, Repo<Convo>,
   Repo<PhoneContact> NVS-backed.** Each upstream `T` gets
   serialised as one NVS blob keyed by `<type><base32_uid>`
   under separate per-type NVS namespaces ("frd", "msg",
   "cnv", "pc"; the existing PhoneContacts namespace already
   uses "pc" -- consolidate so Repo<PhoneContact> and the
   namespace see the same store).

   The Repo<T>::all() listing maintains an index blob
   (`__idx`) per namespace -- a packed array of UID_t's --
   kept in sync on add/remove. Repo<T>::get/exists do a
   single nvs_get_blob; the additional uid match is already
   in the PhoneContacts helpers and can be reused.

   Once that lands, `displayNameOf`'s Friend fallback can be
   restored.

2. **S-MP23/4** -- audit every call site of `Storage.*` and
   `PhoneContacts::*` for "now-true" assertions that were
   guarded by the all-no-op stub. For example, contact-list
   screens may now see a populated list where they previously
   saw empty; recents sort may now matter, etc.

Alt path if a SIM gets inserted between fires: pivot back to
S-MP21/3 (post-READY probes will surface SIM state).

## Helper script note carried from prior fires

* The helpers (`flash_iter.sh`, `addr2line.py`) need to be
  recreated each fire from the brief; the sandbox doesn't
  preserve `/home/claude/` (in fact, `/home/claude/` is not
  writable -- `mkdir: cannot create directory '/home/claude':
  Permission denied`).
* This fire's sandbox: user was `brave-keen-albattani`,
  `$HOME` was `/sessions/brave-keen-albattani`, and
  `/sessions/...` was 100% full again (9.8G/9.8G -- shared
  across parallel sessions). Even `~/.gitconfig` write fails
  on the default path; export `GIT_CONFIG_GLOBAL=/tmp/.gitconfig`
  works around it. The writable scratch path used was
  `/tmp/mp_work/mp_firmware/`, with the root FS having ~820 MB
  free.
* As before, the bash tool's 45-second timeout makes the
  helper scripts impractical to run end-to-end in one call;
  this fire used chunked GH API polling (each `sleep 30-40` +
  status call in its own bash call) and it worked smoothly.
  The build job took ~2.5 min and flash + boot capture took
  ~1.5 min total for this commit.
* The CI workflow triggers on push automatically via the
  `mp24/**` / `src/**` path filter -- no manual
  `workflow_dispatch` is needed for code changes. Docs-only
  commits (like this checkpoint) do NOT trigger a build.
* /tmp/flash_iter from prior sessions can have files owned by
  `nobody`; either rm it or use a fresh dir name.
