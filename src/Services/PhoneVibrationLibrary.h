#ifndef CHATTER_FIRMWARE_PHONE_VIBRATION_LIBRARY_H
#define CHATTER_FIRMWARE_PHONE_VIBRATION_LIBRARY_H

#include <Arduino.h>
#include "PhoneVibrationEngine.h"

/**
 * S161 - PhoneVibrationLibrary
 *
 * Per-ringtone vibration choreography library. Each of the five
 * default ringtones from PhoneRingtoneLibrary (S40) gets a matching
 * vibration pattern -- rhythmic on/off pulses that mirror the
 * melody's character so a user in Meeting profile recognises which
 * ringtone is playing purely by the buzz pattern in their pocket.
 *
 * Mapping (mirrors PhoneRingtoneLibrary::Id):
 *   0. Synthwave - long-short-short waltz, breathy gap (matches the
 *                  A-minor arpeggio loop's slow groove).
 *   1. Classic   - twin double-buzz double-buzz (the Sony-Ericsson
 *                  "brrring brrring" cadence, felt instead of
 *                  heard).
 *   2. Beep      - polite double-pulse with long rest (matches the
 *                  Beep ringtone's two-bursts-then-silence shape).
 *   3. Boss      - dramatic staccato (long pulse, three short
 *                  stutters, long pulse, longer rest -- maps onto
 *                  the JRPG fanfare's bombast).
 *   4. Silent    - single zero-length pulse with long off (effectively
 *                  silent; preserved so the lookup never returns
 *                  nullptr and the vibration engine drives the same
 *                  state machine as the audible ringer).
 *
 * For composer-authored ringtones (PhoneContactRingtone ids 100..103
 * the encoding documents) we fall back to a generic "Composer"
 * pattern -- a clean two-pulse-and-rest cadence that reads as a
 * neutral default; future S180+ work can layer composer-derived
 * patterns on top without churn.
 *
 * Like PhoneRingtoneLibrary, every pattern is a static const
 * Pulse[] + Pattern struct so it can be handed straight to
 * Vibrate.play() without runtime allocation. The data layer is
 * deliberately a read-only catalogue -- selection / persistence
 * lives on the call-screen + service layer that picks the matching
 * ringtone.
 */
class PhoneVibrationLibrary {
public:
	enum Id : uint8_t {
		Synthwave = 0,
		Classic,
		Beep,
		Boss,
		Silent,
		Composer,   // generic fallback for composer-authored ringtones
		Count
	};

	/** Number of available patterns (== Count). */
	static uint8_t count();

	/** Display name for an Id (UTF-8, never null). */
	static const char* nameOf(Id id);

	/** Get a specific pattern by Id. */
	static const PhoneVibrationEngine::Pattern& get(Id id);

	/** Get a pattern by 0-based index, wrapping if out of range. */
	static const PhoneVibrationEngine::Pattern& byIndex(uint8_t idx);

	/**
	 * Resolve a `PhoneContactRingtone`-style ringtone id (the
	 * encoding documented in PhoneContactRingtone.h: 0..4 for
	 * library tones, 100..103 for composer slots) into the
	 * matching vibration pattern. Library ids map 1:1 to the
	 * Synthwave..Silent slots above; composer ids collapse to the
	 * generic Composer fallback. Unknown ids fall back to
	 * Synthwave so the call screen always has *something* to
	 * pulse with -- defensive against a corrupted NVS byte.
	 */
	static const PhoneVibrationEngine::Pattern& forRingtoneId(uint8_t ringtoneId);
};

#endif // CHATTER_FIRMWARE_PHONE_VIBRATION_LIBRARY_H
