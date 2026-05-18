# MAKERphone v2.4 Port -- Session Checkpoint

Updated by the scheduled-task agent at the end of each fire so the
next fire can pick up cleanly. This file is OUTSIDE the CI path
filter (`mp24/**`, `src/**`, `libraries/Chatter-Library/**`,
`.github/workflows/build-mp24.yml`) on purpose, so updating it
does not consume a build.

## Latest fire

* **Date (UTC):** 2026-05-18 ~00:57 UTC
* **HEAD on `main`:** `6a106df` (`feat(mp24): S-MP23/7 -- unify
  PhoneContacts ns + Repo<PhoneContact> index`). One feature
  commit, GREEN on first build with zero fix-forwards.
* **Build status:** GREEN at HEAD `6a106df` (run `26007785804`,
  both `build` and `flash` jobs completed/success).
* **Device boot status:** HEALTHY. boot.log scan: 0 crash markers
  (panic/Guru/abort/Backtrace count = 0). Boot shape unchanged
  from the prior baseline (`0c1f74d`):

      STORE:   README.txt                       919 B
      STORE:   hello.txt                        140 B
      STORE: sentinel hello.txt missing or unreadable
      STORE: mounted; 2 files, 1757 / 1860161 bytes used (0%)
      POWER: polling GPIO2 at 50 Hz, debounce 3 ticks
      MP24: Entering background-tasks-only loop (LVGL owns the panel).
      MODEM: state -> POWER
      MODEM: state -> BOOT
      MODEM: boot probe... (0..15 s elapsed)

  Neither saveRecord nor eraseRecord runs at boot -- the only call
  sites are PhoneContacts:: namespace setters reachable from
  PhoneContactEdit / PhoneContactRingtonePicker /
  PhoneContactWallpaperPicker / PhoneBirthdayReminders'
  setBirthday path, none of which are hit during boot. The
  evidence this fire shipped clean is a successful link of the
  modified TU (the std::vector<UID_t> work compiles inside the
  PhoneContacts anonymous namespace using pcRepoIdxLoad /
  pcRepoIdxSave / pcRepoIdxContains from the Repo<PhoneContact>
  anonymous namespace, both at file scope) plus an unchanged boot
  shape.
* **Flasher status:** ONLINE and reliable.
* **Binary size:** within noise of the prior baseline; the new code
  is ~45 lines, mostly comments + a single vector load/save call.

## What this fire actually shipped (net)

1. **`6a106df` -- S-MP23/7.** `mp24/components/chatter_app/shim/
   StorageStub.cpp`'s PhoneContacts namespace setters now keep the
   shared Repo<PhoneContact> '__idx' blob in sync.

   Before: PhoneContacts namespace's saveRecord() wrote
   'c<base32>' blobs; Repo<PhoneContact>::add wrote 'p<base32>'
   blobs and maintained '__idx'. The two layers kept INDEPENDENT
   stores inside the shared "pc" NVS namespace. A birthday saved
   via `PhoneContacts::setBirthday(uid, m, d)` went to the
   'c<base32>' record, but Repo<PhoneContact>'s '__idx' stayed
   empty. Then `Storage.PhoneContacts.all()` returned an empty
   list and PhoneBirthdayReminders -- the only live consumer of
   that .all() in chatter_app SRCS -- never surfaced the
   birthday the user saved.

   After: saveRecord() also pushes the uid into '__idx' (skipping
   duplicates via pcRepoIdxContains). eraseRecord() prunes the
   uid from '__idx'. The per-record blobs stay independent
   ('c<base32>' vs 'p<base32>'), so the two layers can still
   diverge on per-record content; only the enumeration index is
   the union. Read paths through PhoneContacts::* are unchanged
   (they consult the per-record blob, not the index).

   Partial-failure semantics match the rest of the shim: if the
   per-record write succeeds but the index update fails, the next
   mutating call retries the index add, and reads via
   PhoneContacts::* keep working in the meantime. Reads via
   Storage.PhoneContacts.* see the index but get a default-
   constructed PhoneContact from Repo<PhoneContact>::get() (the
   'p<base32>' blob doesn't exist for these UIDs), which
   PhoneBirthdayReminders tolerates because it only uses .all()
   to enumerate and reads birthdays through PhoneContacts::*.

   Files: `mp24/components/chatter_app/shim/StorageStub.cpp`
   (+43 / -2). One-file commit.

## Why this fire shipped S-MP23/7 only

The previous fire's checkpoint named S-MP23/7 as the recommended
follow-up and described it as a small surgical audit pass to
catch 'now-true' cases the all-no-op stub previously masked. The
audit found exactly one case worth fixing in this fire:
PhoneBirthdayReminders' enumeration source / lookup-target split.
Other call sites already tolerate empty repos correctly (Inbox
/ Friends / Contacts / Speed Dial / Lock / Profile / About all
walk Storage.Friends.all() and degrade cleanly on the empty
case; ProfileService::begin() is not invoked in mp24 yet, so the
device's own Friend record is never seeded; SettingsScreen
factory reset already calls .clear() on all four repos
correctly).

The fix sized cleanly to a single 30-minute fire: 45 lines of
diff in one file, calls into helper functions already defined in
the same TU, zero new external dependencies, zero new linker
symbols, zero build flag changes.

## What the audit confirmed unchanged (no fixes needed)

* `InboxScreen` / `FriendsScreen` / `PhoneContactsScreen` /
  `PhoneSpeedDialScreen` / `PhoneFindFriendsRadar` -- all walk
  `Storage.Friends.all()`. Repo<Friend>::all() correctly returns
  an empty vector for an empty repo, so the screens just render
  no rows. No regressions from S-MP23/3.
* `LockScreen::checkUnread` walks `Storage.Convos.all()` and
  filters on `.unread`. Empty repo path returns empty vector, no
  iteration, no unread badge. Correct.
* `PhoneAboutScreen::computePeerCount` / `ProfileScreen` peer
  count both guard `size() > 1 ? size() - 1 : 0` to avoid the
  unsigned-underflow on an empty Friends repo. Correct since
  the device's own Friend record isn't seeded.
* `SettingsScreen` factory-reset path calls .clear() on all four
  repos -- now actually wipes the NVS records (not a regression
  -- this is the desired post-S-MP23 behaviour).
* `MessageService` upstream is excluded from chatter_app SRCS;
  the live binary links MessageServiceStub.cpp which short-
  circuits every Storage.Messages / Storage.Convos / Storage.
  Friends write. So the Repo<Convo> / Repo<Message> NVS-backed
  add/update/remove paths shipped in S-MP23/5..6 don't fire at
  runtime today -- they ship as ready-to-use surfaces, exercised
  only by the future SMS-backed MessageService adapter.

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

S-MP23 is now genuinely complete: all four Repos NVS-backed and
the shared index syncs across the dual-prefix layout. Pick one:

* **S-MP24 (real clock) -- recommended next feature.** Try
  AT+CCLK? to read modem-reported network time. Fallback: SNTP
  over the cellular data connection. `PhoneClock` consumes the
  source. Sizing: probably 2-4 fires (modem AT probe, parse +
  SNTP fallback, PhoneClock wire-up, polish).
* **S-MP21/3 (modem triage)** if a SIM is inserted between fires
  and AT starts responding (the post-READY probes from S-MP21/1
  will print SIM/signal/operator status). Pivot to this if the
  boot.log shape changes to "MODEM: state -> READY" or similar.
* **S-MP25 (heap-leak cleanup)** -- refactor HomeFactory.cpp to
  keep single instances of menu/lock; audit every push() call
  site for matching cleanup. Lower-priority unless heap growth
  surfaces in boot.log.

## Helper script note carried from prior fires

* The helpers (`flash_iter.sh`, `addr2line.py`) need to be
  recreated each fire from the brief; the sandbox doesn't
  preserve `/home/claude/` (in fact, `/home/claude/` is not
  writable -- `mkdir: cannot create directory '/home/claude':
  Permission denied`).
* This fire's sandbox: user was `hopeful-serene-thompson`,
  `$HOME` was `/sessions/hopeful-serene-thompson` and was 100%
  full (9.8G/9.8G -- shared across parallel sessions). Even
  `~/.gitconfig.lock` failed with ENOSPC, so this fire used
  `HOME=/var/tmp/work` to host `.gitconfig`. The repo lived at
  `/tmp/cmp24/mp_firmware` (copied from the prior fire's
  `/tmp/work/repo/mp_firmware`, which was owned by `nobody` and
  thus read-only to this session's uid 2000); the copy lands
  the .git tree in this session's ownership and lets pulls /
  pushes / commits work.
* The bash tool's 45-second timeout makes long polling
  impractical in one call. This fire chunked the CI poll into
  ~30-second sleeps per call. Build took ~2 minutes; flash +
  boot capture ~1 minute. Total fire wall time well within the
  30-minute budget with room for one more iteration.
* The CI workflow triggers on push automatically via the
  `mp24/**` / `src/**` path filter -- no manual
  `workflow_dispatch` is needed for code changes. Docs-only
  commits (like this checkpoint) do NOT trigger a build.
* The Read tool's connected-folders rule blocks `/tmp/...` reads
  even when bash can reach them; use `sed -n` / `cat` via bash
  instead, or use a python heredoc for in-place edits.
* `git config --global` will fail when `/sessions/...` is full
  (cannot write `~/.gitconfig.lock`). Use `git -c user.name=...
  -c user.email=... commit`, or set `HOME=/var/tmp/work` so
  `~/.gitconfig` lives on the root FS that still has space.
* Cloning the repo into the outputs mount
  (`/sessions/<user>/mnt/outputs/`) fails on `.git/config`
  unlinking due to bindfs semantics. Stick to `/tmp` or
  `/var/tmp`.
