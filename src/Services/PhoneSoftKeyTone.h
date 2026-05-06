#ifndef MAKERPHONE_PHONESOFTKEYTONE_H
#define MAKERPHONE_PHONESOFTKEYTONE_H

#include <Arduino.h>

/**
 * PhoneSoftKeyToneLib (S183)
 *
 * Tiny, code-only catalogue of soft-key click tones the user can pick
 * between for the two Sony-Ericsson-style soft-key hardware buttons
 * (BTN_LEFT and BTN_RIGHT, the buttons PhoneSoftKeyBar visually labels).
 * Every tone is a single (frequency, duration) pair so the BuzzerService
 * fast-path stays a single Piezo.tone() call -- no envelope state, no
 * scheduler involvement, no SPIFFS asset cost.
 *
 * The library is read both from BuzzerService::buttonPressed() (where
 * it replaces the legacy hard-coded NOTE_B4 / 25 ms entry in the noteMap
 * for BTN_LEFT / BTN_RIGHT) and from PhoneSoftKeyToneScreen (where it
 * drives the picker rows + audible preview). Keeping the catalogue here,
 * outside the screen, lets a future "Settings -> About -> Reset to
 * defaults" path or a unit-test driver introspect the available tones
 * without instantiating the screen.
 *
 * The Classic entry MUST stay at id 0 with frequency NOTE_B4 (494 Hz) and
 * duration 25 ms so a freshly-flashed device sounds byte-identical to
 * every prior MAKERphone firmware on its soft-keys -- no audible
 * regression on first boot.
 */
class PhoneSoftKeyToneLib {
public:
	enum Id : uint8_t {
		Classic = 0,   // NOTE_B4 / 25 ms - legacy default.
		Click   = 1,   // NOTE_C6 /  8 ms - snappy high-pitched click.
		Bloop   = 2,   // NOTE_A3 / 30 ms - low buzzy blip.
		Chirp   = 3,   // NOTE_E5 / 18 ms - mid-range chirp.
		Silent  = 4,   // No tone (suppressed even in Loud profile).
		Count   = 5
	};

	/** Default tone id used when persisted byte is out-of-range. */
	static constexpr uint8_t DefaultId = Classic;

	/** Number of catalogued tones. Equal to Id::Count. */
	static uint8_t count();

	/** Display name (uppercase, fits the picker name column). */
	static const char* name(uint8_t id);

	/** Short description (fits the picker desc column at 7 px font). */
	static const char* desc(uint8_t id);

	/** Tone frequency in Hz, 0 if the tone is silent. */
	static uint16_t freq(uint8_t id);

	/** Tone duration in ms, 0 if the tone is silent. */
	static uint16_t durationMs(uint8_t id);

	/** True iff id is in [0..Count-1]. */
	static bool valid(uint8_t id);

	/**
	 * Read the persisted soft-key tone byte (Settings.softKeyTone) and
	 * clamp it into the [0..Count-1] range. Out-of-range values (e.g.
	 * a 0xFF after an NVS-resize wipe) collapse to DefaultId so a
	 * stale Settings blob never crashes the BuzzerService fast path.
	 */
	static uint8_t getActive();

	/** Persist the chosen id and call Settings.store(). Clamps id. */
	static void setActive(uint8_t id);

	/**
	 * Drive the Piezo with the given tone id. Used by the picker
	 * screen for live preview AND by the BuzzerService for in-flight
	 * soft-key clicks. Silent and invalid ids are no-ops; the caller
	 * never has to special-case them.
	 *
	 * Note: this bypasses the Settings.sound (Loud / Mute / Vibrate)
	 * gate by design -- the gate is handled at the call site so the
	 * preview path can produce audio regardless of the active sound
	 * profile, while the live BuzzerService path can keep the gate
	 * exactly where it already lives.
	 */
	static void play(uint8_t id);

	/** Convenience: play the persisted active tone (no Settings.sound gate). */
	static void playActive();
};

#endif // MAKERPHONE_PHONESOFTKEYTONE_H
