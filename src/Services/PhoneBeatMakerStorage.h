#ifndef MAKERPHONE_PHONEBEATMAKERSTORAGE_H
#define MAKERPHONE_PHONEBEATMAKERSTORAGE_H

#include <Arduino.h>
#include "../Screens/PhoneBeatMaker.h"

/**
 * S218 -- PhoneBeatMakerStorage
 *
 * Tiny NVS-backed persistence layer for the `PhoneBeatMaker` (S194)
 * 4-track / 16-step pattern grid + BPM. The v2.0 sweep noted (in
 * KNOWN_ISSUES.md) that the BeatMaker is currently a transient toy --
 * the pattern array and the BPM live only on the screen object, so
 * popping the screen (or power-cycling the device) wipes the user's
 * groove without a trace. S218 closes that gap with the same
 * lightweight NVS pattern that already backs the composer save slots
 * (`PhoneComposerStorage`, S123) and the call-history ring
 * (`PhoneCallHistoryStorage`, S217): a single small blob keyed `pat`
 * under namespace `mpbeat`.
 *
 * Why NVS and not SPIFFS:
 *   - Same reasoning as the composer / call-history layers: SPIFFS
 *     pays for a filesystem inode tree we don't need, NVS is already
 *     opened at boot for `Settings`, `Highscore`, the Composer slots,
 *     `mppet`, and `mpcalls`.
 *   - The full payload is 16 bytes -- a 4-byte header, a single BPM
 *     byte, three reserved bytes for forward compatibility, and an
 *     8-byte packed pattern bitfield (4 tracks x 16 steps = 64 bits).
 *
 * Blob layout (16 bytes, version 1):
 *   [0]      magic 'M'                                    (1 byte)
 *   [1]      magic 'B'  (MakerphoneBeat)                  (1 byte)
 *   [2]      version (currently 1)                        (1 byte)
 *   [3]      reserved (must be 0)                         (1 byte)
 *   [4]      bpm (clamped to PhoneBeatMaker::MinBpm..MaxBpm) (1 byte)
 *   [5..7]   reserved (must be 0)                         (3 bytes)
 *   [8..15]  pattern bits, track-major:                   (8 bytes)
 *              [8]  track 0 steps 0..7 (LSB = step 0)
 *              [9]  track 0 steps 8..15
 *              [10] track 1 steps 0..7
 *              [11] track 1 steps 8..15
 *              [12] track 2 steps 0..7
 *              [13] track 2 steps 8..15
 *              [14] track 3 steps 0..7
 *              [15] track 3 steps 8..15
 *
 * The byte stride and the track / step counts are pinned by
 * `static_assert`s in the .cpp so a future tweak to NumTracks /
 * NumSteps trips a build error rather than a silent on-disk format
 * drift.
 *
 * Default-vs-saved semantics:
 *   - `PhoneBeatMaker` calls `loadInto()` from its constructor; if
 *     the persisted blob is empty (or malformed) the screen falls
 *     back to its existing `seedDefaultPattern()` boom-tss-bap-tss
 *     groove and the byte-identical default BPM. The seed is NOT
 *     persisted -- it stays an in-memory placeholder until the user
 *     edits something.
 *   - On screen pop (`onStop`), the screen calls `save()` only when
 *     a mutation has happened during the run (a `dirty` flag), so a
 *     visit that just plays the demo back never writes to NVS.
 *
 * Begin behaviour:
 *   - `begin()` is idempotent. Lazy-initialise pattern matches
 *     `PhoneComposerStorage` / `PhoneCallHistoryStorage`: call it
 *     from setup() if you can, but `loadInto` / `save` / `clear`
 *     will also call it on first touch so the screen never has to
 *     plumb the lifecycle.
 *
 * Threading:
 *   - The MAKERphone runs single-threaded against LoopManager, so
 *     there is no locking. NVS itself serialises writes.
 *
 * No global instance:
 *   - Static methods only. The NVS handle is opened on first use and
 *     reused for the device's lifetime, which mirrors the way
 *     `Highscore`, `PhoneComposerStorage`, and `PhoneCallHistoryStorage`
 *     keep their handles alive.
 */
class PhoneBeatMakerStorage {
public:
	/** Open the NVS handle. Idempotent. Safe to call before any other
	 *  method, but every other entry point will call it lazily so a
	 *  caller can also simply omit it. */
	static void begin();

	/** True if a saved pattern blob exists in NVS. False on a fresh
	 *  device, after `clear()`, on NVS open failure, or on a malformed
	 *  / wrong-version blob. */
	static bool hasSaved();

	/** Load the persisted pattern + BPM into the supplied
	 *  PhoneBeatMaker arrays.
	 *
	 *  - `outPattern` must be a `bool[NumTracks][NumSteps]` (the same
	 *    geometry the screen uses internally).
	 *  - `outBpm` is set to the persisted BPM, clamped to the
	 *    PhoneBeatMaker::MinBpm..MaxBpm range.
	 *  - Returns true on success. On failure (missing blob, bad
	 *    magic, wrong version, NVS error) the caller's buffers are
	 *    left untouched -- the screen is expected to fall back to
	 *    its existing `seedDefaultPattern()` path. */
	static bool loadInto(bool outPattern[PhoneBeatMaker::NumTracks][PhoneBeatMaker::NumSteps],
	                     uint8_t* outBpm);

	/** Persist the supplied pattern + BPM. Returns true on success.
	 *  The BPM is clamped to PhoneBeatMaker::MinBpm..MaxBpm before it
	 *  is written so a corrupt caller can never park an out-of-range
	 *  value in NVS. */
	static bool save(const bool pattern[PhoneBeatMaker::NumTracks][PhoneBeatMaker::NumSteps],
	                 uint8_t bpm);

	/** Erase the persisted pattern. Returns true if NVS confirmed the
	 *  delete (or the blob was already absent). */
	static bool clear();
};

#endif // MAKERPHONE_PHONEBEATMAKERSTORAGE_H
