#ifndef CHATTER_FIRMWARE_PHONE_COMPOSER_PLAYBACK_H
#define CHATTER_FIRMWARE_PHONE_COMPOSER_PLAYBACK_H

#include <Arduino.h>
#include "../Screens/PhoneComposer.h"
#include "PhoneRingtoneEngine.h"

/**
 * S123 — PhoneComposerPlayback
 *
 * Bridges a PhoneComposer::Note buffer onto the PhoneRingtoneEngine
 * (S39) so the user can audition a composition through the device's
 * piezo while editing. Two responsibilities, kept separate so a
 * future test harness can exercise the conversion table without
 * booting the engine:
 *
 *   1. `buildEngineNotes()` -- pure conversion. Walks the Note buffer,
 *      computes RTTTL-style frequencies and millisecond durations
 *      from a tempo (BPM) and writes engine-shaped Note structs into
 *      a caller-owned array. Zero side effects.
 *
 *   2. `play()` / `stop()` / `isPlaying()` -- thin wrapper around the
 *      global `Ringtone` instance. Owns a static 64-slot Note buffer
 *      that outlives the call, which is what `PhoneRingtoneEngine`
 *      needs (it dereferences the supplied pointer for the lifetime
 *      of playback). Calling `play()` again replaces an in-flight
 *      preview without touching the engine's loop registration.
 *
 * Frequency table:
 *   Standard 12-TET, octave-4 anchored at A4 = 440 Hz, rounded to
 *   the nearest integer Hertz. Octave shifts use `>>` / `<<` on the
 *   anchor value -- the piezo only resolves whole numbers anyway, so
 *   the rounding error is well below the audible threshold.
 *
 * Duration model:
 *   At BPM `b`, a quarter note = 60_000 / b ms. For length L the
 *   duration is `(60_000 * 4 / b) / L`. Dotted notes get a 50 %
 *   length extension. Rests are emitted as freq=0.
 *
 * The buffer is sized to PhoneComposer::MaxNotes (64) which matches
 * the screen's Note buffer. There is no "queue more after" facility
 * because the composer always loads the whole composition for
 * preview -- callers that want longer streams should round-trip
 * through PhoneComposerRtttl first.
 *
 * Looping playback is exposed verbatim to PhoneRingtoneEngine so
 * the same path can drive both the composer's "preview once" and a
 * future S153 per-contact ringtone hook.
 */
class PhoneComposerPlayback {
public:
	/** Maximum number of engine notes a single composition can yield.
	 *  One PhoneComposer::Note maps to exactly one engine note (a
	 *  single freq+duration pair), so the cap matches. */
	static constexpr uint16_t MaxEngineNotes = PhoneComposer::MaxNotes;

	/** Default tempo to use when the caller does not supply one. The
	 *  PhoneComposerRtttl::DefaultBpm constant encodes the same
	 *  number; we shadow it here so callers don't have to include the
	 *  codec just to get a sensible BPM. */
	static constexpr uint16_t DefaultBpm = 63;

	/** Per-note inter-step gap in milliseconds. The engine inserts a
	 *  short silence between successive notes so two same-pitch
	 *  notes are still audibly separated. The composer plays the
	 *  user's intent literally, so the gap is small but non-zero. */
	static constexpr uint16_t InterNoteGapMs = 18;

	/**
	 * Convert a PhoneComposer::Note buffer to PhoneRingtoneEngine
	 * notes. Pure function -- no global state, no allocations.
	 *
	 *  - `notes`/`count` : input. May be nullptr if `count==0`.
	 *  - `bpm`           : tempo. 0 -> DefaultBpm.
	 *  - `outNotes`/`maxOut` : caller-owned output array.
	 *
	 *  Returns the number of engine notes written. Caps at `maxOut`
	 *  so a stray oversized input is silently truncated rather than
	 *  overflowing the buffer.
	 */
	static uint16_t buildEngineNotes(const PhoneComposer::Note*    notes,
									 uint8_t                       count,
									 uint16_t                      bpm,
									 PhoneRingtoneEngine::Note*    outNotes,
									 uint16_t                      maxOut);

	/**
	 * Begin previewing the buffer through the global Ringtone engine.
	 *
	 *  - `notes`/`count` may not be nullptr/0; the call is a no-op
	 *    that returns false in that case.
	 *  - `bpm == 0` falls back to DefaultBpm.
	 *  - `loopForever == true` keeps the preview running until
	 *    `stop()` is called (used by future per-contact ringtone
	 *    callers); the composer screen passes false so the preview
	 *    self-terminates at the end of the buffer.
	 *  - `name` is forwarded verbatim onto the Melody (may be
	 *    nullptr).
	 *
	 *  Returns true if the engine accepted the melody.
	 */
	static bool play(const PhoneComposer::Note* notes,
					 uint8_t                    count,
					 uint16_t                   bpm,
					 bool                       loopForever = false,
					 const char*                name = nullptr);

	/** Stop a preview that this service started. Safe to call when
	 *  nothing is playing. Does NOT clear the engine's static buffer
	 *  -- it just stops playback so a subsequent `play()` can re-use
	 *  the same backing memory. */
	static void stop();

	/** True if a preview started by this service is still running. */
	static bool isPlaying();
};

#endif // CHATTER_FIRMWARE_PHONE_COMPOSER_PLAYBACK_H
