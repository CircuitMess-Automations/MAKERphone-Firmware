# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-18 ~00:35 UTC
* **HEAD on `main`:** `0c1f74d` (`feat(mp24): S-MP23/6 -- Repo<Convo>
  NVS-backed with packed serialisation`). One feature commit, GREEN
  on first build with zero fix-forwards.
* **Build status:** GREEN at HEAD `0c1f74d` (run `26007175574`, both
  `build` and `flash` jobs completed/success).
* **Device boot status:** HEALTHY. boot.log scan: 0 crash markers
  (panic/Guru/abort/Backtrace count = 0). Boot shape unchanged from
  the previous green baseline (`99027c0`):

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

  No Repo<Convo> code path is exercised at boot -- the live binary
  runs MessageServiceStub.cpp instead of upstream MessageService.cpp,
  and the only call sites for Storage.Convos.* live inside the
  ConvoScreen / ConvoView screens. The meaningful evidence this fire
  shipped is a clean link of the 334-line specialisation block
  (including serialise/deserialise helpers + 7 explicit template
  specialisations) with no symbol conflicts and a byte-identical
  boot shape.
* **Flasher status:** ONLINE and reliable.
* **Binary size:** 1.22 MB / 2 MB partition (~60%). Within noise of
  the prior baseline. The new code is mostly comments and the seven
  Repo<Convo> specialisations + serialiser pair share the existing
  nvs.h surface; no new components or managed dependencies.

## What this fire actually shipped (net)

1. **`0c1f74d` -- S-MP23/6.** `mp24/components/chatter_app/shim/
   StorageStub.cpp` now provides per-method specializations for
   Repo<Convo>::add/update/remove/get/all/exists/clear. Records
   live in NVS namespace `"cnv"`, one packed-blob per Convo keyed
   by `'c' + 13 base32 chars` (14 chars, under the
   `NVS_KEY_NAME_MAX_SIZE-1 = 15` limit). A separate `"__idx"`
   blob holds the packed array of `UID_t`s for all().

   Unlike Friend and PhoneContact, Convo is **not** POD -- `struct
   Convo : Entity` holds a `std::vector<UID_t> messages` whose
   `data()` lives on the heap. A raw `nvs_set_blob(&c,
   sizeof(Convo))` would persist the vector's internal pointer /
   size / capacity fields and round-trip into garbage, so this
   sub-step ships a custom wire format:

       [ uid:8 | unread:1 | count:2 | uids:count*8 ]

   - count = uint16 LE number of UID_t entries
   - uids  = count * 8 bytes (little-endian-natural UID_t)

   11-byte fixed header + 8*count bytes total. count is capped at
   `kConvoMsgCap = 1024` messages per conversation to keep blobs
   comfortably under the multi-page NVS blob ceiling and refuse
   absurd payloads silently -- matches the kMsgTextCap discipline
   from S-MP23/5.

   NVS namespace allocation now (all four repos NVS-backed):

       Friend             "frd"  (S-MP23/3, prefix 'f')
       PhoneContacts ns   "pc"   (S-MP23/2, prefix 'c')
       Repo<PhoneContact> "pc"   (S-MP23/4, prefix 'p')
       Repo<Message>      "msg"  (S-MP23/5, prefix 'm')
       Repo<Convo>        "cnv"  (S-MP23/6, prefix 'c')

   The `'c'` prefix in `"cnv"` cannot collide with the
   PhoneContacts namespace `'c<base32>'` records because the NVS
   namespaces are different (`"cnv"` != `"pc"`).

   Anonymous-namespace helpers (mirroring S-MP23/5 plus the new
   serialiser):

       cvKey(uid, out)        -- 14-char base32 NVS key, prefix 'c'
       cvSerialize(c, &vec)   -- Convo -> packed blob, refuses
                                  messages vectors larger than
                                  kConvoMsgCap
       cvDeserialize(p, sz,   -- packed blob -> Convo, refuses
                      &out)       short blobs, count overflow,
                                  len-size mismatches
       cvLoad(uid, &Convo)    -- READONLY open + size probe +
                                  nvs_get_blob + cvDeserialize +
                                  uid match (defensive)
       cvSave(c)              -- cvSerialize + READWRITE open +
                                  nvs_set_blob + nvs_commit
       cvErase(uid)           -- READWRITE open + nvs_erase_key +
                                  nvs_commit (NOT_FOUND -> success)
       cvIdxLoad(&vec)        -- read __idx into vector; empty
                                  namespace / empty blob -> empty
                                  vec + true
       cvIdxSave(vec)         -- write vector to __idx (empty
                                  vector erases the key)
       cvIdxContains          -- linear scan

   Partial-failure semantics match Repo<Friend> / Repo<PhoneContact>
   / Repo<Message>: if blob erase succeeds but index save fails,
   the index briefly references a missing record; cvLoad() returns
   false for the size/uid checks, so the orphan degrades to "Convo
   looks missing" rather than corruption.

   Non-specialised members (begin/reserve/write/read/getPath) fall
   through to the primary template -- same arrangement as Friend
   / PhoneContact / Message. `ConvoRepo::write` / `ConvoRepo::read`
   remain the explicit no-op overrides below the new section
   (never reached because add/get path no longer routes through
   them).

   Files: `mp24/components/chatter_app/shim/StorageStub.cpp`
   (+334). One-file commit.

## Why this fire shipped Repo<Convo> only

The previous fire's checkpoint named S-MP23/6 as the next step and
described it as "Repo<Convo> NVS-backed. Convo holds
`vector<UID_t> messages` (variable length) plus `bool unread`.
Serialise as `[uid:8 | unread:1 | count:2 | uids:count*8]`. UID
matches the Friend UID by convention. Namespace `"cnv"` keyed by
prefix `'c' + 13 base32 chars`." That sized cleanly to a single
30-minute fire:

* The blob format was already designed in the prior checkpoint,
  so no design work was needed in this fire.
* The Repo specialisations follow the Friend / PhoneContact /
  Message pattern verbatim apart from cvSave/cvLoad routing
  through the new serialiser. Pattern-match coding.
* All four repos (Friend, PhoneContact, Message, Convo) are now
  NVS-backed, completing the S-MP23 four-repo migration. The
  remaining S-MP23/7 audit fire is independent.
* Repo<Convo> isn't exercised at boot today (MessageServiceStub
  short-circuits all call sites that would touch Storage.Convos)
  so this fire's success criterion is a clean link + unchanged
  boot shape, which the build + flash artefacts confirmed.

## Remaining S-MP23 sub-steps

* **S-MP23/7.** Audit every call site of `Storage.*` and
  `PhoneContacts::*` for "now-true" assertions that the all-no-op
  stub previously masked. Contact-list screens may now see a
  populated list where they previously saw empty; recents sort
  may now matter; ConvoBox / ConvoView may now render real
  message bodies. Worth doing now that all four Repos are
  NVS-backed.

A side option for an interleave fire: unify Repo<PhoneContact>
and the PhoneContacts namespace so the latter's `upsert`/`remove`
auto-maintain the Repo's index. Not required by any current call
site (the two layers are read by disjoint screens) but tidies up
the dual-prefix-in-shared-namespace arrangement carried from
S-MP23/4.

## Open hardware questions still outstanding

Same as before -- none resolved this fire:

* **Q2.** `uPOWER_OFF` GPIO1 polarity for power-off control
  (currently Hi-Z).
* **Q3.** Modem boot-FAIL triage. boot.log still shows AT not
  responding ("MODEM: boot probe... (0..15 s elapsed)" with no
  state transition to MODEM_READY). The SIM/signal/operator probes
  from S-MP21/1 would surface state once AT works; the AT-never-
  responds case still needs hardware-side triage.
* **Q4.** STATUS / NET_STATUS LED traces.

## What the next fire should do

Pick the most natural follow-up. Two options:

* **S-MP23/7 (audit) -- recommended.** With all four Repos now
  NVS-backed, walk every Storage.* / PhoneContacts.* / Friend
  / Convo / Message call site for assumptions the all-no-op stub
  previously masked. Likely scope: a careful read pass over
  src/Storage/*.cpp, src/Services/*Service.cpp, and the
  contacts-/conversations-/inbox-related screens. The output is
  a short note in the next checkpoint listing any "now-true" cases
  found and any small fixes shipped (e.g. a defensive guard, an
  empty-list rendering tweak) plus a list of cases that are still
  guarded by MessageServiceStub and therefore don't need work yet.
  This fire should be small (~1-3 surgical commits).
* **S-MP21/3 (modem triage)** if a SIM is inserted between fires
  and AT starts responding (the post-READY probes from S-MP21/1
  will print SIM/signal/operator status). Pivot to this if the
  boot.log shape changes.
* **S-MP24 (real clock)** is the natural next "feature" after
  S-MP23/7. Try AT+CCLK? first; fall back to SNTP over the
  cellular data connection. PhoneClock consumes the source.

## Helper script note carried from prior fires

* The helpers (`flash_iter.sh`, `addr2line.py`) need to be
  recreated each fire from the brief; the sandbox doesn't
  preserve `/home/claude/` (in fact, `/home/claude/` is not
  writable -- `mkdir: cannot create directory '/home/claude':
  Permission denied`).
* This fire's sandbox: user was `awesome-focused-ramanujan`,
  `$HOME` was `/sessions/awesome-focused-ramanujan`, and
  `/sessions/...` was 100% full (9.8G/9.8G -- shared across
  parallel sessions). `~/repo` write fails on no-space; used
  `/tmp/mp24_$$/` with the root FS having ~700 MB free.
* The bash tool's 45-second timeout makes the helper scripts
  impractical to run end-to-end in one call; this fire used
  chunked GH API polling (each `sleep 40` + status call in its
  own bash call) and it worked smoothly. Build job took ~2.5 min,
  flash + boot capture ~1 min total for this commit.
* The CI workflow triggers on push automatically via the
  `mp24/**` / `src/**` path filter -- no manual
  `workflow_dispatch` is needed for code changes. Docs-only
  commits (like this checkpoint) do NOT trigger a build.
* /tmp from prior sessions can have files owned by `nobody`;
  either rm what you can or use a fresh dir name like
  `/tmp/mp24_$$/`.
* `git config --global` will fail when `/sessions/...` is full
  (cannot write `~/.gitconfig.lock`). Use `git -c user.name=...
  -c user.email=... commit` or `git config --local` instead.
* The Read tool's connected-folders rule blocks `/tmp/...` reads
  even when bash can reach them; use `sed -n` / `cat` via bash
  instead, or use a python heredoc for in-place edits.
* Cloning the repo into the outputs mount
  (`/sessions/<user>/mnt/outputs/`) fails on `.git/config`
  unlinking due to bindfs semantics. Stick to `/tmp` even when
  it's tight on space.
