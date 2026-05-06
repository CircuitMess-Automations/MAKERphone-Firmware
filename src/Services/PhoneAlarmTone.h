#ifndef CHATTER_FIRMWARE_PHONE_ALARM_TONE_H
#define CHATTER_FIRMWARE_PHONE_ALARM_TONE_H

#include <Arduino.h>
#include "PhoneRingtoneEngine.h"

/**
 * S193 - PhoneAlarmTone
 *
 * Helper layer that resolves `Settings.alarmTone` into a usable
 * `PhoneRingtoneEngine::Melody*` for `PhoneAlarmService::triggerFire()`
 * and exposes a tiny picker iteration the new `PhoneAlarmTonePicker`
 * screen drives for the user-facing list view.
 *
 * Encoding (kept inside a single `Settings.alarmTone` byte so adding
 * the feature does not bloat the NVS blob beyond the one new field):
 *
 *   0          - Factory alarm (the legacy four-note arpeggio
 *                originally bound directly inside `PhoneAlarmService`
 *                via the `kAlarmMelody` static, now owned by this
 *                module so the encoding can stay self-contained).
 *   1..5       - `PhoneRingtoneLibrary::Id` shifted by one. Reading
 *                an id `n` in this range maps to library id `n - 1`
 *                (Synthwave, Classic, Beep, Boss, Silent).
 *   100..103   - PhoneComposer save slots 0..3, encoding shared
 *                with `PhoneContactRingtone` so a composition saved
 *                from PhoneComposer is reachable both as a contact
 *                ringer (S153) and as the alarm tone (this module)
 *                without a duplicate NVS slot.
 *   anything else - clamps to `FactoryId` at the resolver layer so
 *                a stale / corrupted NVS blob never crashes
 *                triggerFire(); the user just hears the legacy
 *                arpeggio, which is the correct safe fallback.
 *
 * The shift-by-one for library ids exists for a single reason: a
 * fresh / NVS-resized device reads the new byte as 0, and we want
 * 0 to mean "Factory" (the byte-identical pre-S193 behaviour) rather
 * than mapping zero straight to Synthwave. This keeps the upgrade
 * path silent -- a user that never visits the new picker hears the
 * exact same alarm sound they always have.
 *
 * Picker iteration intentionally short-circuits empty composer slots
 * via `PhoneComposerStorage::hasSlot`, so a freshly-flashed device
 * shows just the six built-in entries (Factory + 5 library) and a
 * user who has saved compositions automatically sees them appear
 * without an additional code path.
 */
class PhoneAlarmTone {
public:
	/** Reserved id for the legacy four-note arpeggio. Factory default
	 *  for a freshly-flashed or NVS-resized device. */
	static constexpr uint8_t FactoryId         = 0;

	/** First id used for library-tone entries. Library ids are stored
	 *  shifted by one so id 0 stays reserved for Factory. */
	static constexpr uint8_t LibraryBase       = 1;

	/** Number of library tones the picker offers. Mirrors
	 *  PhoneContactRingtone::LibraryCount so any future addition to
	 *  the library flows through here without an extra edit. */
	static constexpr uint8_t LibraryCount      = 5;

	/** First id used for composer slots. Matches the existing
	 *  PhoneContactRingtone encoding so a saved composition slot has
	 *  the same id whether read as a contact ringer or as the alarm
	 *  tone. */
	static constexpr uint8_t ComposerSlotBase  = 100;

	/** Maximum number of composer save slots the picker exposes.
	 *  Mirrors PhoneComposerStorage::MaxSlots. */
	static constexpr uint8_t ComposerSlotCount = 4;

	/** Default id used when the persisted byte is unrecognised. */
	static constexpr uint8_t DefaultId         = FactoryId;

	/** Buffer size to pass into nameOf() for guaranteed display fit. */
	static constexpr size_t  NameBufferSize    = 20;

	/** Maximum number of picker entries the table can ever produce
	 *  (Factory + 5 library + 4 composer). Used as the static array
	 *  size by the picker screen. */
	static constexpr uint8_t MaxPickerEntries  = 1 + LibraryCount + ComposerSlotCount;

	// ---------- id classification ----------

	static bool    isFactoryId(uint8_t id);
	static bool    isLibraryId(uint8_t id);
	static bool    isComposerId(uint8_t id);

	/** 0..LibraryCount-1 for a library id, 0xFF otherwise. */
	static uint8_t libraryIndex(uint8_t id);

	/** 0..ComposerSlotCount-1 for a composer id, 0xFF otherwise. */
	static uint8_t composerSlotIndex(uint8_t id);

	/** Wrap a library index (0..LibraryCount-1) into a stored id. */
	static uint8_t encodeLibrary(uint8_t libraryIdx);

	/** Wrap a composer slot index (0..ComposerSlotCount-1) into a
	 *  stored id. */
	static uint8_t encodeComposerSlot(uint8_t slotIdx);

	/** True iff the id resolves to a usable melody. Factory and library
	 *  ids are always usable; composer ids are only usable when their
	 *  underlying slot is populated. */
	static bool    isUsable(uint8_t id);

	/** Either the stored id (when usable) or `DefaultId`. */
	static uint8_t validatedOrDefault(uint8_t id);

	// ---------- display ----------

	/** Write a short, user-visible name for `id` into `outName`.
	 *  Factory yields "Factory"; library entries forward to
	 *  `PhoneRingtoneLibrary::nameOf`; composer entries reuse the
	 *  composer-slot label format from PhoneContactRingtone. Empty
	 *  composer slots render as "Slot N (empty)". `outName` is
	 *  always nul-terminated when `outLen >= 1`. */
	static void    nameOf(uint8_t id, char* outName, size_t outLen);

	// ---------- playback ----------

	/** Resolve `id` into a Melody pointer suitable for handing to
	 *  `PhoneRingtoneEngine::play()`. Always returns a non-null
	 *  pointer -- unrecognised / corrupted ids fall back to the
	 *  factory melody so the alarm always rings even when the
	 *  persisted byte is garbage. */
	static const PhoneRingtoneEngine::Melody* resolve(uint8_t id);

	/** Convenience: resolve the persisted active id. */
	static const PhoneRingtoneEngine::Melody* resolveActive();

	/** Pointer to the static factory-alarm melody. Exposed so a
	 *  caller (test harness, future "preview Factory" affordance)
	 *  can reach the legacy arpeggio without going through the
	 *  encoding. */
	static const PhoneRingtoneEngine::Melody* factoryMelody();

	// ---------- picker iteration ----------

	/** Number of currently-selectable ids. Always >= 1 + LibraryCount
	 *  (Factory + library always shown); composer slots only
	 *  contribute when populated. */
	static uint8_t pickerCount();

	/** Id at picker index `idx` (0..pickerCount()-1). Out-of-range
	 *  indices clamp to `DefaultId`. */
	static uint8_t pickerIdAt(uint8_t idx);

	/** Inverse of `pickerIdAt` -- returns the picker index for an
	 *  id, or 0 (the Factory slot) when the id is not currently in
	 *  the picker. */
	static uint8_t pickerIndexOf(uint8_t id);

	// ---------- persisted accessor ----------

	/** Read `Settings.alarmTone`, validate, and return the result.
	 *  An invalid / unusable persisted byte collapses to `DefaultId`
	 *  so callers always receive a playable id even if NVS holds
	 *  garbage. */
	static uint8_t getActiveId();

	/** Persist `id` into `Settings.alarmTone` and call
	 *  `Settings.store()`. Out-of-range / unusable ids clamp to
	 *  `DefaultId` so a malformed picker never writes unplayable
	 *  junk into NVS. */
	static void    setActiveId(uint8_t id);
};

#endif // CHATTER_FIRMWARE_PHONE_ALARM_TONE_H
