#ifndef CHATTER_FIRMWARE_PHONE_CONTACT_RINGTONE_H
#define CHATTER_FIRMWARE_PHONE_CONTACT_RINGTONE_H

#include <Arduino.h>
#include "PhoneRingtoneEngine.h"

/**
 * S153 — PhoneContactRingtone
 *
 * Helper layer that resolves a `PhoneContact::ringtoneId` into either
 * a stock library ringtone (S40) or a user-authored composer
 * composition (S121–S123). The feature lets each contact have its
 * own custom ringtone — when the call screen pushes for a known
 * peer, `PhoneCallService` reads the contact's stored id, asks this
 * service for the matching `PhoneRingtoneEngine::Melody*`, and hands
 * it to `PhoneIncomingCall::setRingtone()` before push().
 *
 * Encoding scheme (kept inside a single uint8_t so the existing
 * `PhoneContact::ringtoneId` field needs no schema change):
 *
 *   0..4        — library tones, in `PhoneRingtoneLibrary::Id` order:
 *                 0 = Synthwave, 1 = Classic, 2 = Beep,
 *                 3 = Boss,      4 = Silent.
 *   100..103    — composer save slots from `PhoneComposerStorage`:
 *                 100 + N maps to slot N (N = 0..3).
 *   anything else — falls back to `DefaultId` (Synthwave) so a
 *                   corrupted record never crashes playback.
 *
 * The 100-base for composer slots means a future S160 / S180 that
 * also persists ringtone choices on profiles / per-message channels
 * can reuse the same encoding without a flag day. We deliberately do
 * NOT try to compact the id into a tighter range — the `ringtoneId`
 * field is a full byte, and gaps make the intent obvious.
 *
 * The picker (PhoneContactRingtonePicker) iterates available choices
 * via `pickerCount()` / `pickerIdAt()` so the UI doesn't have to
 * special-case the library / composer split. Empty composer slots
 * are skipped automatically — a fresh device shows just the five
 * library tones until the user saves their first composition.
 */
class PhoneContactRingtone {
public:
	/** Number of stock ringtones in PhoneRingtoneLibrary. Mirrors
	 *  PhoneRingtoneLibrary::Count so the encoding stays decoupled
	 *  from any forward-compat additions to the library. */
	static constexpr uint8_t LibraryCount      = 5;

	/** First id used for composer slots. Library tones occupy
	 *  0..LibraryCount-1; we leave a gap before the composer range
	 *  so a future fixed-size addition to the library does not
	 *  collide. */
	static constexpr uint8_t ComposerSlotBase  = 100;

	/** Maximum number of composer save slots we expose as ringtones.
	 *  Mirrors PhoneComposerStorage::MaxSlots so a composer change
	 *  flows through here without an additional edit. */
	static constexpr uint8_t ComposerSlotCount = 4;

	/** Default id used when the stored value is unrecognised or the
	 *  contact has not customised. Library Synthwave (0) — matches
	 *  the existing PhoneIncomingCall fallback. */
	static constexpr uint8_t DefaultId         = 0;

	/** Buffer size to pass into nameOf() for guaranteed display fit. */
	static constexpr size_t  NameBufferSize    = 20;

	// ---------- id classification ----------

	static bool    isLibraryId(uint8_t id);
	static bool    isComposerId(uint8_t id);

	/** 0..ComposerSlotCount-1 for a composer id, 0xFF otherwise. */
	static uint8_t composerSlotIndex(uint8_t id);

	/** Wrap a slot index (0..ComposerSlotCount-1) into a stored id. */
	static uint8_t encodeComposerSlot(uint8_t slotIdx);

	/** True when `id` resolves to a usable ringtone. Library ids are
	 *  always usable (the library is a static catalogue); composer
	 *  ids are only usable when `PhoneComposerStorage::hasSlot` for
	 *  the underlying slot returns true. */
	static bool    isUsable(uint8_t id);

	/** Either the stored id (when usable) or `DefaultId`. Convenience
	 *  for callers that only want a guaranteed-playable id. */
	static uint8_t validatedOrDefault(uint8_t id);

	// ---------- display ----------

	/** Write a short, user-visible name for `id` into `outName`.
	 *  Library names come straight from `PhoneRingtoneLibrary::nameOf`.
	 *  Composer names come from the slot's stored title (prefixed
	 *  with a tiny "♪ " glyph hint). Empty slots render as
	 *  "Slot N (empty)". `outName` is always nul-terminated when
	 *  `outLen >= 1`. */
	static void    nameOf(uint8_t id, char* outName, size_t outLen);

	// ---------- playback ----------

	/** Resolve `id` into a Melody pointer suitable for handing to
	 *  `PhoneIncomingCall::setRingtone()`.
	 *
	 *  Library ids return references to static const Melody structs
	 *  managed by `PhoneRingtoneLibrary`. Composer ids load the slot
	 *  through `PhoneComposerStorage`, build engine notes via
	 *  `PhoneComposerPlayback::buildEngineNotes` into an internal
	 *  static buffer, and return a pointer to a static Melody that
	 *  describes that buffer.
	 *
	 *  Returns nullptr when the id is unrecognised or refers to an
	 *  empty / corrupt composer slot. The returned pointer is valid
	 *  until the next call to `resolve()`. Only one concurrent ring
	 *  is ever expected (one incoming call at a time), so single-
	 *  buffering is sufficient. */
	static const PhoneRingtoneEngine::Melody* resolve(uint8_t id);

	// ---------- picker iteration ----------

	/** Number of currently-selectable ringtone ids (library always
	 *  contributes `LibraryCount`; composer slots only contribute
	 *  when populated). Always >= LibraryCount. */
	static uint8_t pickerCount();

	/** Id at picker index `idx` (0..pickerCount()-1). Out-of-range
	 *  indices clamp to `DefaultId` so a stale picker cursor never
	 *  reads beyond the array. */
	static uint8_t pickerIdAt(uint8_t idx);

	/** Inverse of `pickerIdAt` — returns the picker index for an id,
	 *  or 0 (the DefaultId slot) when the id is not currently in
	 *  the picker. */
	static uint8_t pickerIndexOf(uint8_t id);
};

#endif // CHATTER_FIRMWARE_PHONE_CONTACT_RINGTONE_H
