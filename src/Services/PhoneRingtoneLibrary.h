#ifndef CHATTER_FIRMWARE_PHONE_RINGTONE_LIBRARY_H
#define CHATTER_FIRMWARE_PHONE_RINGTONE_LIBRARY_H

#include <Arduino.h>
#include "PhoneRingtoneEngine.h"

/**
 * S40 — PhoneRingtoneLibrary
 *
 * Five default ringtone melodies for the MAKERphone, all built on top of
 * the S39 PhoneRingtoneEngine. Each melody is a static const Note[] +
 * Melody struct so it can be handed straight to Ringtone.play() without
 * any runtime allocation.
 *
 *   0. Synthwave — moody arpeggio loop, fits the homescreen vibe.
 *   1. Classic   — classic feature-phone "brrring brrring" double-burst.
 *   2. Beep      — short polite double beep, repeats slowly.
 *   3. Boss      — dramatic JRPG-style fanfare, loud and memorable.
 *   4. Silent    — single long rest that loops; Ringtone keeps the
 *                   playhead alive but never asserts the piezo, so call
 *                   screens can drive the same code path as the audible
 *                   ringtones without making any sound.
 *
 * S41 will pick one of these as the default and play it from the
 * PhoneIncomingCall screen. The library is intentionally a
 * read-only catalogue — selection / persistence lives elsewhere.
 */
class PhoneRingtoneLibrary {
public:
	enum Id : uint8_t {
		Synthwave = 0,
		Classic,
		Beep,
		Boss,
		Silent,
		Count
	};

	/** Number of available ringtones (== Count). */
	static uint8_t count();

	/** Display name for an Id (UTF-8, never null). */
	static const char* nameOf(Id id);

	/** Get a specific ringtone by Id. */
	static const PhoneRingtoneEngine::Melody& get(Id id);

	/** Get a ringtone by 0-based index, wrapping if out of range. */
	static const PhoneRingtoneEngine::Melody& byIndex(uint8_t idx);
};

#endif // CHATTER_FIRMWARE_PHONE_RINGTONE_LIBRARY_H
