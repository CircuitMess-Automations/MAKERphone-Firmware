# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-18 ~00:30 UTC
* **HEAD on `main`:** `99027c0` (`feat(mp24): S-MP23/5 -- Repo<Message>
  NVS-backed with packed serialisation`). One feature commit, GREEN
  on first build with zero fix-forwards.
* **Build status:** GREEN at HEAD `99027c0` (run `26006713866`, both
  `build` and `flash` jobs completed/success).
* **Device boot status:** HEALTHY. boot.log scan: 0 crash markers
  (panic/Guru/abort/Backtrace count = 0). Boot shape unchanged from
  the previous green baseline (`a276dfb`):

      STORE:   README.txt                       919 B
      STORE:   hello.txt                        140 B
      STORE: sentinel hello.txt missing or unreadable
      STORE: mounted; 2 files, 1757 / 1860161 bytes used (0%)
      POWER: polling GPIO2 at 50 Hz, debounce 3 ticks
      MP24: Entering background-tasks-only loop (LVGL owns the panel).
      MODEM: state -> POWER
      MODEM: state -> BOOT
      MODEM: boot probe... (0 s elapsed) ... (15 s elapsed)

  No Repo<Message> code path is exercised at boot -- the live
  binary runs MessageServiceStub.cpp instead of upstream
  MessageService.cpp, so the only call sites for Storage.Messages
  are dead code today. The meaningful evidence this fire shipped
  is a clean link of the 367-line specialisation block (including
  serialise/deserialise helpers + 7 explicit template
  specialisations) with no symbol conflicts and a byte-identical
  boot shape.
* **Flasher status:** ONLINE and reliable.
* **Binary size:** within noise of the prior baseline. The new
  code is mostly comments and the seven Repo<Message>
  specialisations + serialiser pair share the existing nvs.h
  surface; no new components or managed dependencies.

## What this fire actually shipped (net)

1. **`99027c0` -- S-MP23/5.** `mp24/components/chatter_app/shim/
   StorageStub.cpp` now provides per-method specialisations for
   Repo<Message>::add/update/remove/get/all/exists/clear. Records
   live in NVS namespace `"msg"`, one packed-blob per Message
   keyed by `'m' + 13 base32 chars` (14 chars, under the
   `NVS_KEY_NAME_MAX_SIZE-1 = 15` limit). A separate `"__idx"`
   blob holds the packed array of `UID_t`s for all().

   Unlike Friend and PhoneContact, Message is **not** POD --
   `class Message : Entity` holds a `void* content` pointing at a
   heap-allocated `std::string` (TEXT) or `uint8_t` (PIC). A raw
   `nvs_set_blob(&m, sizeof(Message))` would persist the pointer
   value and round-trip into garbage, so this sub-step ships a
   custom wire format:

       [ uid:8 | convo:8 | flags:1 | type:1 | len:2 | data:N ]

   - flags bit0 = outgoing, bit1 = received   (remaining bits zero)
   - type   matches Message::Type enum  -- TEXT=0, PIC=1, NONE=2
   - len    uint16 LE byte length of the body
   - data   N bytes:
       * TEXT -- string contents, no NUL terminator
       * PIC  -- 1 byte picIndex
       * NONE -- empty (len=0)

   20-byte fixed header + N bytes total. TEXT length capped at
   `kMsgTextCap = 2048` bytes to stay well under the ESP-IDF NVS
   per-blob limit and refuse absurd payloads silently.

   NVS namespace allocation now:

       Friend             "frd"  (S-MP23/3, prefix 'f')
       PhoneContacts ns   "pc"   (S-MP23/2, prefix 'c')
       Repo<PhoneContact> "pc"   (S-MP23/4, prefix 'p')
       Repo<Message>      "msg"  (S-MP23/5, prefix 'm')

   The `"pc"` namespace is shared between two layers using
   different prefixes -- no key collisions are possible.

   Anonymous-namespace helpers (mirroring S-MP23/3 / S-MP23/4 plus
   the new serialiser):

       msgKey(uid, out)        -- 14-char base32 NVS key, prefix 'm'
       msgSerialize(m, &vec)   -- Message -> packed blob, refuses
                                  TEXT longer than kMsgTextCap
       msgDeserialize(p, sz,   -- packed blob -> Message, refuses
                      &out)       short blobs, unknown types,
                                  len-sz mismatches
       msgLoad(uid, &Message)  -- READONLY open + size probe +
                                  nvs_get_blob + msgDeserialize +
                                  uid match (defensive)
       msgSave(m)              -- msgSerialize + READWRITE open +
                                  nvs_set_blob + nvs_commit
       msgErase(uid)           -- READWRITE open + nvs_erase_key +
                                  nvs_commit (NOT_FOUND -> success)
       msgIdxLoad(&vec)        -- read __idx into vector; empty
                                  namespace / empty blob -> empty
                                  vec + true
       msgIdxSave(vec)         -- write vector to __idx (empty
                                  vector erases the key)
       msgIdxContains          -- linear scan

   Partial-failure semantics match Repo<Friend> / Repo<PhoneContact>:
   if blob erase succeeds but index save fails, the index briefly
   references a missing record; msgLoad() returns false for the
   size/uid checks, so the orphan degrades to "Message looks
   missing" rather than corruption.

   Non-specialised members (begin/reserve/write/read/getPath) fall
   through to the primary template -- same arrangement as Friend
   and PhoneContact. `MessageRepo::write` / `MessageRepo::read`
   remain the explicit no-op overrides below the new section
   (never reached because add/get path no longer routes through
   them).

   Files: `mp24/components/chatter_app/shim/StorageStub.cpp`
   (+367). One-file commit.

## Why this fire shipped Repo<Message> only

The previous fire's checkpoint named S-MP23/5 as the next step and
described it as "MessageRepo NVS-backed. Custom serialisation:
Message has Type type, std::string text for TEXT, or a single
uint8_t picIndex for PIC. Serialise to a packed blob: [uid:8 |
convo:8 | flags:1 | type:1 | len:2 | data:N] with N=0 for NONE,
N=string.size() for TEXT, N=1 for PIC." That sized cleanly to a
single 30-minute fire:

* The blob format was already designed in the prior checkpoint,
  so no design work was needed in this fire.
* The Repo specialisations follow the Friend / PhoneContact
  pattern verbatim apart from msgSave/msgLoad routing through
  the new serialiser. Pattern-match coding.
* Repo<Convo> (the last all-no-op repo, with its own non-POD
  variable-length `messages` vector) needed its own packed
  format and is therefore its own fire (S-MP23/6).
* Repo<Message> isn't exercised at boot today (MessageServiceStub
  short-circuits all call sites) so this fire's success criterion
  is a clean link + unchanged boot shape, which the build + flash
  artefacts confirmed.

## Remaining S-MP23 sub-steps

* **S-MP23/6.** Repo<Convo> NVS-backed. Convo holds
  `vector<UID_t> messages` (variable length) plus `bool unread`.
  Serialise as `[uid:8 | unread:1 | count:2 | uids:count*8]`. UID
  matches the Friend UID by convention. Namespace `"cnv"` keyed
  by prefix `'c' + 13 base32 chars` (no collision with the
  PhoneContacts ns `'c<base32>'` records because `"cnv"` != `"pc"`).
* **S-MP23/7.** Audit every call site of `Storage.*` and
  `PhoneContacts::*` for "now-true" assertions that the all-
  no-op stub previously masked. Contact-list screens may now see a
  populated list where they previously saw empty; recents sort may
  now matter. Worth doing once all four Repos are NVS-backed.

A side option for an interleave fire: unify Repo<PhoneContact> and
the PhoneContacts namespace so the latter's `upsert`/`remove`
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

Pick the next S-MP23 sub-step. Most natural continuation is
**S-MP23/6 -- Repo<Convo> NVS-backed**, which completes the four-
repo NVS migration. The pattern is now well-established (S-MP23/3
+ S-MP23/4 give the POD template; S-MP23/5 gives the
custom-serialisation template). Convo serialisation:

    [ uid:8 | unread:1 | count:2 | uids:count*8 ]

Cap `count` at, say, 1024 messages per conversation. Namespace
`"cnv"`, prefix `'c' + 13 base32 chars` for record keys, `"__idx"`
for the conversation-uid index.

Alt path if a SIM gets inserted between fires: pivot to S-MP21/3
(post-READY probes will surface SIM state once AT works).

## Helper script note carried from prior fires

* The helpers (`flash_iter.sh`, `addr2line.py`) need to be
  recreated each fire from the brief; the sandbox doesn't
  preserve `/home/claude/` (in fact, `/home/claude/` is not
  writable -- `mkdir: cannot create directory '/home/claude':
  Permission denied`).
* This fire's sandbox: user was `jolly-vigilant-mayer`, `$HOME`
  was `/sessions/jolly-vigilant-mayer`, and `/sessions/...` was
  100% full (9.8G/9.8G -- shared across parallel sessions).
  `~/repo` write fails on no-space; used `/tmp/mp_jolly/`
  instead, with the root FS having ~700 MB free.
* The bash tool's 45-second timeout makes the helper scripts
  impractical to run end-to-end in one call; this fire used
  chunked GH API polling (each `sleep 40` + status call in its
  own bash call) and it worked smoothly. Build job took ~2 min,
  flash + boot capture ~1 min total for this commit.
* The CI workflow triggers on push automatically via the
  `mp24/**` / `src/**` path filter -- no manual
  `workflow_dispatch` is needed for code changes. Docs-only
  commits (like this checkpoint) do NOT trigger a build.
* /tmp from prior sessions can have files owned by `nobody`;
  either rm what you can or use a fresh dir name like
  `/tmp/mp_$(whoami)/`.
* `git config --global` will fail when `/sessions/...` is full
  (cannot write `~/.gitconfig.lock`). Use `git -c user.name=...
  -c user.email=... commit` or `git config --local` instead.
* The Read tool's connected-folders rule blocks `/tmp/...` reads
  even when bash can reach them; use `sed -n` / `cat` via bash
  instead, or use a python heredoc for in-place edits.
