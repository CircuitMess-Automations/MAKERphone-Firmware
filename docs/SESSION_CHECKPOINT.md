# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-18 ~23:30 UTC
* **HEAD on `main`:** `34a8146` (`feat(mp24): S-MP23/3 -- Repo<Friend>
  NVS-backed with index blob`). One feature commit, GREEN on first
  build with zero fix-forwards.
* **Build status:** GREEN at HEAD `34a8146` (run `26005892869`,
  both `build` and `flash` jobs completed/success).
* **Device boot status:** HEALTHY. boot.log scan: 0 crash markers
  (panic/guru/abort/Backtrace count = 0). Boot shape unchanged
  from prior green baseline (`eafd088`):

      BATT: curve-fitting cal active
      BATT: first sample: 1216 mV (1.22 V)
      STORE:   README.txt                       919 B
      STORE:   hello.txt                        140 B
      STORE: sentinel hello.txt missing or unreadable
      STORE: mounted; 2 files, 1757 / 1860161 bytes used (0%)
      POWER: polling GPIO2 at 50 Hz, debounce 3 ticks
      MP24: Entering background-tasks-only loop (LVGL owns the panel).
      MODEM: state -> POWER
      MODEM: state -> BOOT
      MODEM: boot probe... (0 s elapsed) ... (15 s elapsed)

  No Repo<Friend> mutations happen during the 20 s boot capture
  window, so the new NVS code path isn't exercised at boot. The
  link is clean and the boot shape is byte-identical to the
  previous green baseline -- which is the meaningful evidence
  this fire was after: that specializing seven member functions
  of Repo<Friend> and adding the index-blob helpers doesn't
  break anything at link time or runtime.
* **Flasher status:** ONLINE and reliable.
* **Binary size:** ~12.15 MB artifact bundle (firmware ~1.21 MB
  app binary; within noise from prior baseline -- the new code
  is tiny, +304 LOC of which roughly half is comments).

## What this fire actually shipped (net)

1. **`34a8146` -- S-MP23/3.** `mp24/components/chatter_app/shim/
   StorageStub.cpp` now provides per-method specializations for
   Repo<Friend>::add/update/remove/get/all/exists/clear. Records
   live in NVS namespace `"frd"`, one blob per Friend keyed by
   14 chars (`'f' + 13 base32 chars` of the 64-bit UID, well under
   `NVS_KEY_NAME_MAX_SIZE-1 = 15`). A separate `"__idx"` blob
   holds the packed array of `UID_t`s needed by all().

   Anonymous-namespace helpers:

       friendKey(uid, out)    -- 14-char base32 NVS key
       friendLoad(uid, out)   -- READONLY open + nvs_get_blob
                                 + uid match (defensive)
       friendSave(f)          -- READWRITE open + nvs_set_blob
                                 + nvs_commit
       friendErase(uid)       -- READWRITE open + nvs_erase_key
                                 + nvs_commit
       friendIdxLoad(vec)     -- read __idx into vector
       friendIdxSave(vec)     -- write vector to __idx (empty
                                 vector erases the key)
       friendIdxContains(...) -- linear scan

   Partial-failure semantics: if blob erase succeeds but index
   save fails, the index briefly points to a missing record;
   friendLoad() returns false in that case so the orphan
   degrades to "Friend looks missing" rather than corruption.

   Friend is POD/trivially-copyable on xtensa-esp-elf:
   `{uint64_t uid; Profile{char[21], uint8, uint16}; uint8[32]}`
   = 64 bytes with no internal padding (uid 8-aligned, Profile
   24, encKey 32). Raw `nvs_set_blob` over `&Friend` is correct.

   Also restored `PhoneContacts::displayNameOf()`'s Friend
   fallback that S-MP23/2 dropped. The upstream "fall back to
   `f.profile.nickname`" path now works because
   `Storage.Friends.get(uid)` returns the real persisted record.

   Added `<algorithm>` include for `std::remove` in the
   index-removal path.

   Files: `mp24/components/chatter_app/shim/StorageStub.cpp`
   (+304 -6). One-file commit.

   Repo<PhoneContact> / Repo<Message> / Repo<Convo> remain the
   all-no-op primary template. PhoneContacts namespace keeps its
   own NVS-backed implementation from S-MP23/2; Message/Convo
   need custom serialisation (non-POD members: `std::string` /
   `std::vector` / `void*`) which is deferred.

## Why this fire shipped Repo<Friend> (not the full S-MP23/3)

The previous fire's checkpoint scoped S-MP23/3 as "Repo<Friend>,
Repo<Message>, Repo<Convo>, Repo<PhoneContact> NVS-backed" -- all
four in one step. That's too big for a 30-minute fire. This fire
took the smallest atomic slice that delivers real value:

* Friend is the only one of the four where Repo<T> is used
  *directly* (the others go through MessageRepo / ConvoRepo
  subclasses with custom serialisation, or through the
  PhoneContacts namespace which already has its own NVS store).
* Friend is the smallest POD of the four -- 64 fixed bytes,
  no nested containers -- so the blob serialisation pattern
  from PhoneContacts (S-MP23/2) ports almost verbatim.
* Restoring `displayNameOf()`'s Friend fallback was the named
  follow-on from S-MP23/2's checkpoint; doing it now closes
  that loop and unblocks contact-list/dialer screens from
  falling back to a paired peer's broadcast nickname when the
  user hasn't entered a custom contact name.

The future sub-steps stay roughly the same:

* **S-MP23/4.** Repo<PhoneContact> NVS-backed. Mostly a
  copy-rename of the Friend code with namespace `"pc"` and a
  consolidation note: PhoneContacts namespace currently writes
  keys `"c<base32>"` in `"pc"` -- to share the same store,
  pick a key prefix `'p'` for Repo<PhoneContact>'s record key
  (or a non-overlapping numeric range) and a `"__idx"` key for
  the index. Both layers see the same blobs.
* **S-MP23/5.** MessageRepo NVS-backed. Custom serialisation:
  Message has `Type type`, `std::string text` for TEXT, or a
  single `uint8_t picIndex` for PIC. Serialise to a packed
  blob: `[uid:8 | convo:8 | flags:1 | type:1 | len:2 | data:N]`
  with N=0 for NONE, N=string.size() for TEXT, N=1 for PIC.
* **S-MP23/6.** ConvoRepo NVS-backed. Convo holds
  `vector<UID_t> messages` (variable length) plus a `bool
  unread`. Serialise as `[uid:8 | unread:1 | count:2 |
  uids:count*8]`. UID matches the Friend UID by convention
  (see Convo.hpp comment), so the convo key can be the same
  base32 scheme.
* **S-MP23/7.** Audit every call site of `Storage.*` and
  `PhoneContacts::*` for "now-true" assertions that the all-
  no-op stub previously masked. Contact-list screens may now
  see a populated list where they previously saw empty;
  recents sort may now matter; LoRaService's `encKeyMap` walk
  is still gated by the LoRa stub, so it stays dormant.

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

Pick the next S-MP23 sub-step from the list above. Most natural
continuation is **S-MP23/4 -- Repo<PhoneContact> NVS-backed**,
which is mostly a copy-rename of this fire's Friend code with
a consolidation against the existing PhoneContacts namespace.

Alt path if a SIM gets inserted between fires: pivot back to
S-MP21/3 (post-READY probes will surface SIM state).

## Helper script note carried from prior fires

* The helpers (`flash_iter.sh`, `addr2line.py`) need to be
  recreated each fire from the brief; the sandbox doesn't
  preserve `/home/claude/` (in fact, `/home/claude/` is not
  writable -- `mkdir: cannot create directory '/home/claude':
  Permission denied`).
* This fire's sandbox: user was `awesome-focused-ritchie`,
  `$HOME` was `/sessions/awesome-focused-ritchie`, and
  `/sessions/...` was 100% full again (9.8G/9.8G -- shared
  across parallel sessions). `~/repo` write fails on no-space;
  used `/tmp/aw_mp24/repo/mp_firmware/` instead, with the
  root FS having ~740 MB free.
* As before, the bash tool's 45-second timeout makes the
  helper scripts impractical to run end-to-end in one call;
  this fire used chunked GH API polling (each `sleep 30-40` +
  status call in its own bash call) and it worked smoothly.
  The build job took ~2 min and flash + boot capture took
  ~1 min total for this commit.
* The CI workflow triggers on push automatically via the
  `mp24/**` / `src/**` path filter -- no manual
  `workflow_dispatch` is needed for code changes. Docs-only
  commits (like this checkpoint) do NOT trigger a build.
* /tmp from prior sessions can have files owned by `nobody`;
  either rm what you can or use a fresh dir name like
  `/tmp/<initials>_mp24/`.
* `git clone --depth 50` keeps the working tree tiny when
  disk pressure is high.
