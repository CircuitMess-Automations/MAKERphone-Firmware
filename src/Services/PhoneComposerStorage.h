#ifndef CHATTER_FIRMWARE_PHONE_COMPOSER_STORAGE_H
#define CHATTER_FIRMWARE_PHONE_COMPOSER_STORAGE_H

#include <Arduino.h>
#include "../Screens/PhoneComposer.h"

/**
 * S123 — PhoneComposerStorage
 *
 * Tiny NVS-backed persistence layer for the user's hand-authored
 * compositions. The PhoneComposer screen (S121) builds a buffer of
 * Note structs in RAM; the RTTTL codec (S122) round-trips that buffer
 * to/from a canonical RTTTL string; this service now persists those
 * strings into a small fixed array of "slots" so the user can keep
 * their work between sessions.
 *
 * Why NVS and not SPIFFS:
 *   - The data partition is intentionally small (most asset cost
 *     lives in the LVGL theme) -- NVS already hosts the game high-
 *     scores and is well under its quota.
 *   - The composition is at most ~ 768 bytes of text, which fits in
 *     NVS comfortably. No file-system tree needed.
 *   - The same NVS handle pattern (`Highscore`) compiles cleanly on
 *     the self-hosted CI runner, so we know the headers are wired.
 *
 * Slot model:
 *   - Fixed 4 slots (0..3). Each slot is a single blob holding the
 *     name (up to MaxNameLen + 1 chars), the BPM (uint16_t), and the
 *     RTTTL body that PhoneComposerRtttl::serialize emitted.
 *   - The blob is the canonical RTTTL string itself plus a tiny
 *     8-byte preamble so we can recover the BPM without a re-parse.
 *     Layout:
 *       [0]      magic "MC"  (2 bytes -- "MakerphoneComposer")
 *       [2]      version (1 byte, currently 1)
 *       [3]      reserved (1 byte, must be 0)
 *       [4..5]   bpm (uint16_t little-endian)
 *       [6..7]   reserved (2 bytes, must be 0)
 *       [8..]    nul-terminated RTTTL string
 *   - A slot with `hasSlot()==false` reads cleanly as "empty" and
 *     `loadSlot()` will not touch the caller's buffer.
 *
 * Begin behaviour:
 *   - `begin()` is idempotent. Lazy-initialise pattern matches
 *     `Highscore`: call it from setup() if you can, but `saveSlot` /
 *     `loadSlot` will also call it on first touch so the screen never
 *     has to plumb the lifecycle.
 *
 * Threading:
 *   - The MAKERphone runs single-threaded against LoopManager, so
 *     there's no locking. NVS itself serializes writes.
 *
 * No global instance:
 *   - Static methods only -- there is no per-screen state to track,
 *     so there's no point holding a singleton. The NVS handle is
 *     opened on first use and reused for the device's lifetime, which
 *     mirrors the way `Highscore` keeps its handle alive.
 *
 * Future hooks:
 *   - S153 (per-contact ringtone) and S160 (per-profile ringtone) will
 *     read these same slots through `loadSlot` -> playback, so the
 *     storage shape was deliberately chosen to be Notes + name + bpm.
 */
class PhoneComposerStorage {
public:
	/** Number of save slots accessible to the user. Kept small so the
	 *  NVS namespace stays tidy and the slot picker UI fits. */
	static constexpr uint8_t  MaxSlots    = 4;

	/** Maximum length (excluding terminator) of a slot's display name.
	 *  Matches the RTTTL header convention (10 chars). */
	static constexpr uint8_t  MaxNameLen  = 10;

	/** Open the NVS handle. Idempotent. Safe to call before any other
	 *  method, but every other entry point will call it lazily so a
	 *  caller can also simply omit it. */
	static void begin();

	/** True if `slot` (0..MaxSlots-1) currently has a saved
	 *  composition. Returns false for out-of-range slots. */
	static bool hasSlot(uint8_t slot);

	/** Persist the supplied Note buffer into `slot`. The notes are
	 *  serialised through PhoneComposerRtttl, prefixed with an 8-byte
	 *  header (magic + version + bpm) and stored as a single NVS blob.
	 *
	 *  - Returns true on success, false if the slot was out of range,
	 *    NVS could not be opened, or the buffer would have overflowed
	 *    PhoneComposerRtttl::SerializedCap. The slot is left untouched
	 *    on failure. */
	static bool saveSlot(uint8_t                     slot,
						 const PhoneComposer::Note*  notes,
						 uint8_t                     count,
						 const char*                 name,
						 uint16_t                    bpm);

	/** Load the slot back into a Note buffer. `outNotes`/`outCount`
	 *  receive the parsed buffer; `outName`/`outBpm` are optional.
	 *
	 *  - Returns true if the slot existed AND parsed cleanly. On
	 *    failure (empty slot, malformed blob, NVS error) the
	 *    out-buffers are left untouched. */
	static bool loadSlot(uint8_t                     slot,
						 PhoneComposer::Note*        outNotes,
						 uint8_t                     maxNotes,
						 uint8_t*                    outCount,
						 char*                       outName,
						 size_t                      outNameLen,
						 uint16_t*                   outBpm);

	/** Erase a slot. No-op for out-of-range slots. Returns true if
	 *  NVS confirmed the delete (or the slot was already empty). */
	static bool clearSlot(uint8_t slot);

	/** Erase every slot. Useful for a Settings -> "Reset" path. */
	static bool clearAll();
};

#endif // CHATTER_FIRMWARE_PHONE_COMPOSER_STORAGE_H
