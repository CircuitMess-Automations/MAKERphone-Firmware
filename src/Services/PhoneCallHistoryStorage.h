#ifndef MAKERPHONE_PHONECALLHISTORYSTORAGE_H
#define MAKERPHONE_PHONECALLHISTORYSTORAGE_H

#include <Arduino.h>
#include "../Screens/PhoneCallHistory.h"

/**
 * S217 -- PhoneCallHistoryStorage
 *
 * Tiny NVS-backed persistence layer for the call-history ring buffer
 * `PhoneCallHistory` (S27) keeps in memory. The v1.0 polish item in
 * `KNOWN_ISSUES.md` flagged that S55's About screen lists the peer
 * count, but S27's call-history screen kept its log only as an
 * `std::vector<Entry>` member -- so the moment the screen popped, the
 * log evaporated, and a power-cycle wiped it without a trace. S217
 * closes the gap with the same lightweight NVS pattern that already
 * backs the composer save slots (`PhoneComposerStorage`, S123) and
 * the per-pet stats (`PhoneVirtualPetService`, S129): a single blob
 * keyed `log` under namespace `mpcalls`.
 *
 * Why NVS and not SPIFFS:
 *   - Same reasoning as `PhoneComposerStorage`: SPIFFS pays for a
 *     filesystem inode tree we don't need, NVS is already opened
 *     at boot for `Settings`, `Highscore`, and the Composer slots.
 *   - The full ring at MaxEntries = 32 entries x 40 bytes = 1280
 *     bytes plus a 4-byte header fits comfortably in a single NVS
 *     blob.
 *
 * Blob layout:
 *   [0]      magic 'M'                                    (1 byte)
 *   [1]      magic 'H'  (MakerphoneHistory)               (1 byte)
 *   [2]      version (currently 1)                        (1 byte)
 *   [3]      count (0..MaxEntries)                        (1 byte)
 *   [4..]    count entries, each kEntryStride bytes:
 *              [0]      type (0=Incoming / 1=Outgoing /   (1 byte)
 *                       2=Missed)
 *              [1..25]  name (nul-terminated, MaxNameLen+1
 *                       bytes incl. terminator)           (25 bytes)
 *              [26..34] timestamp (nul-terminated,
 *                       MaxTsLen+1 bytes incl. term.)     (9 bytes)
 *              [35..38] durationSeconds (uint32_t LE)     (4 bytes)
 *              [39]     avatarSeed                        (1 byte)
 *
 * The kEntryStride is therefore 40 bytes, matched by a static_assert
 * in the .cpp so a future tweak to the entry geometry trips a build
 * error rather than a silent on-disk format drift.
 *
 * Sample-vs-real semantics:
 *   - `PhoneCallHistory` calls `loadAll()` from its constructor; if
 *     the persisted log is empty it falls back to its existing
 *     `seedSampleEntries()` demo set so a freshly-flashed device
 *     still reads as a real call log on first boot. The sample
 *     entries are NOT persisted -- they are an in-memory placeholder
 *     until a real call lands.
 *   - The first `addEntry()` call after the screen detects an empty
 *     log clears the demo set, so once a real entry arrives the
 *     demo data is gone forever (and the log is now the persisted
 *     truth).
 *
 * Begin behaviour:
 *   - `begin()` is idempotent. Lazy-initialise pattern matches
 *     `PhoneComposerStorage`: call it from setup() if you can, but
 *     `loadAll` / `saveAll` / `clearLog` will also call it on first
 *     touch so the screen never has to plumb the lifecycle.
 *
 * Threading:
 *   - The MAKERphone runs single-threaded against LoopManager, so
 *     there is no locking. NVS itself serialises writes.
 *
 * No global instance:
 *   - Static methods only. The NVS handle is opened on first use and
 *     reused for the device's lifetime, which mirrors the way
 *     `Highscore` and `PhoneComposerStorage` keep their handles
 *     alive.
 */
class PhoneCallHistoryStorage {
public:
	/** Soft cap on retained entries. Tracks PhoneCallHistory::MaxEntries
	 *  so the in-memory ring and the on-NVS ring never disagree. */
	static constexpr uint8_t MaxEntries = PhoneCallHistory::MaxEntries;

	/** Open the NVS handle. Idempotent. Safe to call before any other
	 *  method, but every other entry point will call it lazily so a
	 *  caller can also simply omit it. */
	static void begin();

	/** True if the persisted log currently holds at least one entry. */
	static bool hasLog();

	/**
	 * Load the persisted log into `out`. Up to `maxOut` entries are
	 * filled (oldest at index 0, newest at index N-1, matching the
	 * PhoneCallHistory::entries[] convention). Returns the number of
	 * entries actually written into `out`.
	 *
	 * Returns 0 on:
	 *   - empty / missing blob
	 *   - corrupt magic / version mismatch
	 *   - NVS open failure
	 *
	 * `out` is left untouched past the returned count, so a caller
	 * can use a stack array sized to MaxEntries without zeroing it
	 * first.
	 */
	static uint8_t loadAll(PhoneCallHistory::Entry* out, uint8_t maxOut);

	/**
	 * Persist the supplied ring (oldest at [0], newest at [count-1])
	 * as the new log. `count` is clamped to MaxEntries -- a caller
	 * that overflows simply has its tail dropped, mirroring the
	 * in-memory ring-buffer semantics of `PhoneCallHistory::addEntry`.
	 *
	 * Returns true on a successful nvs_set_blob + nvs_commit.
	 */
	static bool saveAll(const PhoneCallHistory::Entry* entries,
	                    uint8_t                        count);

	/** Erase the persisted log. Returns true if NVS confirmed the
	 *  delete (or the slot was already empty). */
	static bool clearLog();
};

#endif // MAKERPHONE_PHONECALLHISTORYSTORAGE_H
