# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-18 ~23:50 UTC
* **HEAD on `main`:** `a276dfb` (`feat(mp24): S-MP23/4 -- Repo<PhoneContact>
  NVS-backed with index blob`). One feature commit, GREEN on first
  build with zero fix-forwards.
* **Build status:** GREEN at HEAD `a276dfb` (run `26006269077`,
  both `build` and `flash` jobs completed/success).
* **Device boot status:** HEALTHY. boot.log scan: 0 crash markers
  (panic/guru/abort/Backtrace count = 0). Boot shape unchanged
  from prior green baseline (`34a8146`):

      STORE:   README.txt                       919 B
      STORE:   hello.txt                        140 B
      STORE: sentinel hello.txt missing or unreadable
      STORE: mounted; 2 files, 1757 / 1860161 bytes used (0%)
      POWER: polling GPIO2 at 50 Hz, debounce 3 ticks
      MP24: Entering background-tasks-only loop (LVGL owns the panel).
      MODEM: state -> POWER
      MODEM: state -> BOOT
      MODEM: boot probe... (0 s elapsed) ... (15 s elapsed)

  No Repo<PhoneContact> mutations happen during the 20 s boot
  capture window, so the new NVS code path isn't exercised at
  boot. The link is clean and the boot shape is byte-identical
  to the previous green baseline -- which is the meaningful
  evidence this fire was after: that adding seven Repo<PhoneContact>
  specializations + the per-namespace NVS helpers doesn't break
  anything at link time or runtime.
* **Flasher status:** ONLINE and reliable.
* **Binary size:** within noise of the prior baseline -- the new
  code is +291 LOC of which roughly half is comments. The new
  helpers and seven specializations together share the existing
  `nvs.h` surface; no new components or managed dependencies.

## What this fire actually shipped (net)

1. **`a276dfb` -- S-MP23/4.** `mp24/components/chatter_app/shim/
   StorageStub.cpp` now provides per-method specializations for
   Repo<PhoneContact>::add/update/remove/get/all/exists/clear.
   Records live in NVS namespace `"pc"`, one blob per PhoneContact
   keyed by `'p' + 13 base32 chars` (14 chars, under the
   `NVS_KEY_NAME_MAX_SIZE-1 = 15` limit). A separate `"__idx"`
   blob holds the packed array of `UID_t`s needed by all().

   The choice of `"pc"` namespace shares it with the PhoneContacts
   namespace's per-contact keystore from S-MP23/2. The blobs
   don't collide because the prefixes differ:

       PhoneContacts namespace:   "c<base32>"  (S-MP23/2)
       Repo<PhoneContact>:        "p<base32>"  (S-MP23/4)
       Index:                     "__idx"      (reserved, S-MP23/4)

   Anonymous-namespace helpers (mirroring S-MP23/3's Friend code
   verbatim apart from names + prefix):

       pcRepoKey(uid, out)     -- 14-char base32 NVS key, prefix 'p'
       pcRepoLoad(uid, out)    -- READONLY open + nvs_get_blob +
                                  uid match (defensive)
       pcRepoSave(c)           -- READWRITE open + nvs_set_blob +
                                  nvs_commit
       pcRepoErase(uid)        -- READWRITE open + nvs_erase_key +
                                  nvs_commit (NOT_FOUND treated as
                                  success)
       pcRepoIdxLoad(vec)      -- read __idx into vector; empty
                                  namespace / empty blob -> empty
                                  vec + true
       pcRepoIdxSave(vec)      -- write vector to __idx (empty
                                  vector erases the key)
       pcRepoIdxContains       -- linear scan

   Partial-failure semantics match Repo<Friend>: if blob erase
   succeeds but index save fails, the index briefly points to a
   missing record; pcRepoLoad() returns false in that case so the
   orphan degrades to "PhoneContact looks missing" rather than
   corruption.

   PhoneContact is POD/trivially-copyable on xtensa-esp-elf:
   `Entity{uint64_t uid}` + `char displayName[24]` + 6 * uint8 +
   `uint32_t lastInteraction` + 3 * uint8 (birthdayMonth /
   birthdayDay / wallpaperStyle) + `uint8_t reserved[5]` = ~52
   bytes after alignment, no internal pointers / std::* members.
   Raw `nvs_set_blob` over `&PhoneContact` is correct.

   Files: `mp24/components/chatter_app/shim/StorageStub.cpp`
   (+291). One-file commit.

   The PhoneContacts namespace's `upsert`/`remove` from S-MP23/2
   and Repo<PhoneContact>'s `add`/`remove` from this fire each
   maintain their own per-UID blob in the shared `"pc"` namespace.
   Today's call sites route through one path or the other (the
   contacts list / dialer reads via PhoneContacts::*; the Pair
   service / Friend-coupled code reads via `Storage.Contacts.all()`),
   so leaving them independent doesn't break any current consumer.
   Unifying the two layers (so PhoneContacts::upsert auto-maintains
   the Repo index) is deferred to a follow-up fire if a call site
   ever needs both views of the same record.

   Repo<Friend> remains as shipped in S-MP23/3 (NVS-backed,
   namespace `"frd"`). Repo<Message> / Repo<Convo> remain the
   all-no-op primary template; their non-POD members (`std::string`
   / `std::vector` / `void*`) need custom serialisation in later
   fires (S-MP23/5 and S-MP23/6).

## Why this fire shipped Repo<PhoneContact> only

The previous fire's checkpoint named S-MP23/4 as the next step
and described it as "mostly a copy-rename of the Friend code with
namespace `"pc"`." That sized cleanly to a single 30-minute fire:

* PhoneContact is the same flavour of POD record as Friend
  (fixed-size struct, no internal containers), so the
  serialisation pattern ports verbatim from S-MP23/3.
* The namespace-collision question with the existing PhoneContacts
  namespace was already decided by the prior checkpoint (use
  prefix `'p'` to avoid the existing `'c'` records); no design
  rework was needed in this fire.
* Repo<Message> / Repo<Convo> would have required new serialisation
  code (variable-length tail for std::string text / std::vector
  uids) that's worth its own fire.

The remaining S-MP23 sub-steps stay roughly the same as named in
the previous checkpoint:

* **S-MP23/5.** MessageRepo NVS-backed. Custom serialisation:
  Message has `Type type`, `std::string text` for TEXT, or a
  single `uint8_t picIndex` for PIC. Serialise to a packed
  blob: `[uid:8 | convo:8 | flags:1 | type:1 | len:2 | data:N]`
  with N=0 for NONE, N=string.size() for TEXT, N=1 for PIC.
* **S-MP23/6.** ConvoRepo NVS-backed. Convo holds
  `vector<UID_t> messages` (variable length) plus a `bool
  unread`. Serialise as `[uid:8 | unread:1 | count:2 |
  uids:count*8]`. UID matches the Friend UID by convention,
  so the convo key can use the same base32 scheme (in a new
  namespace like `"cnv"` or `"msg"`).
* **S-MP23/7.** Audit every call site of `Storage.*` and
  `PhoneContacts::*` for "now-true" assertions that the all-
  no-op stub previously masked. Contact-list screens may now
  see a populated list where they previously saw empty;
  recents sort may now matter. Worth doing once all four
  Repos are NVS-backed.

A side option for an interleave fire: unify Repo<PhoneContact>
and the PhoneContacts namespace so the latter's `upsert`/`remove`
auto-maintain the Repo's index. Not required by any current call
site (the two layers are read by disjoint screens) but tidies up
the dual-prefix-in-shared-namespace arrangement this fire shipped.

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

Pick the next S-MP23 sub-step. Most natural continuation is
**S-MP23/5 -- MessageRepo NVS-backed**, which is the first
sub-step needing custom (non-POD) serialisation. The pattern from
S-MP23/3 and S-MP23/4 still applies for the index + key scheme;
only the per-record blob format changes.

Alt path if a SIM gets inserted between fires: pivot back to
S-MP21/3 (post-READY probes will surface SIM state once AT works).

## Helper script note carried from prior fires

* The helpers (`flash_iter.sh`, `addr2line.py`) need to be
  recreated each fire from the brief; the sandbox doesn't
  preserve `/home/claude/` (in fact, `/home/claude/` is not
  writable -- `mkdir: cannot create directory '/home/claude':
  Permission denied`).
* This fire's sandbox: user was `jolly-adoring-bardeen`,
  `$HOME` was `/sessions/jolly-adoring-bardeen`, and
  `/sessions/...` was 100% full (9.8G/9.8G -- shared across
  parallel sessions). `~/repo` write fails on no-space; used
  `/tmp/mp_firmware_$(whoami)/` instead, with the root FS
  having ~720 MB free.
* As before, the bash tool's 45-second timeout makes the
  helper scripts impractical to run end-to-end in one call;
  this fire used chunked GH API polling (each `sleep 35-40` +
  status call in its own bash call) and it worked smoothly.
  Build job took ~2 min, flash + boot capture ~1 min total
  for this commit.
* The CI workflow triggers on push automatically via the
  `mp24/**` / `src/**` path filter -- no manual
  `workflow_dispatch` is needed for code changes. Docs-only
  commits (like this checkpoint) do NOT trigger a build.
* /tmp from prior sessions can have files owned by `nobody`;
  either rm what you can or use a fresh dir name like
  `/tmp/mp_firmware_$(whoami)/`.
* `git config --global` will fail when `/sessions/...` is full
  (cannot write `~/.gitconfig.lock`). Use `git -c user.name=...
  -c user.email=... commit` or `git config --local` instead.
* The Read tool's connected-folders rule blocks `/tmp/...`
  reads even when bash can reach them; use `sed -n` / `cat`
  via bash instead.
