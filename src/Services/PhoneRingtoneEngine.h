#ifndef CHATTER_FIRMWARE_PHONE_RINGTONE_ENGINE_H
#define CHATTER_FIRMWARE_PHONE_RINGTONE_ENGINE_H

#include <Arduino.h>
#include <Loop/LoopListener.h>

/**
 * S39 — PhoneRingtoneEngine
 *
 * Non-blocking melody framework that drives the existing piezo via the
 * global LoopManager. A "melody" is just a sequence of (frequency,
 * duration) steps — frequency 0 is a rest. Playback advances on the
 * micro-tick supplied to loop(), so the UI stays fully responsive (no
 * delay() anywhere).
 *
 * Designed to be the audio backbone for:
 *   S40 — five default ringtone melodies (Synthwave, Classic, Beep, Boss, Silent)
 *   S41 — playback wired into PhoneIncomingCall
 *   S42/S43 — the in-app PhoneMusicPlayer + tune library
 *
 * Behaviour notes:
 *   - melodies can opt-in to looping (used for the call ringer)
 *   - the engine respects Settings.sound: if the user mutes mid-ring it
 *     hushes the piezo but keeps the playhead moving, so unmuting picks
 *     the melody back up where it would have been
 *   - BuzzerService keypress beeps (Piezo.tone(freq, 25)) coexist fine —
 *     they retune the piezo for 25 ms, and the engine re-asserts its own
 *     note on the next tick
 *
 * Melody.notes must point to memory that outlives the playback. Static
 * const arrays (the pattern S40 will use) are the intended source.
 */
class PhoneRingtoneEngine : public LoopListener {
public:
	struct Note {
		uint16_t freq;       // Hz, 0 = rest
		uint16_t durationMs; // length of this step in ms
	};

	struct Melody {
		const Note* notes;
		uint16_t count;
		uint16_t gapMs;     // silence between notes (0 = back-to-back)
		bool loop;          // restart from start when finished
		const char* name;   // optional display name (may be nullptr)
	};

	void begin();

	/** Start (or replace) a melody. */
	void play(const Melody& melody);

	/** Convenience: build a Melody inline from a note array. */
	void play(const Note* notes, uint16_t count, bool loopForever = false,
			  uint16_t gapMs = 30, const char* name = nullptr);

	/** Stop playback immediately and silence the piezo. */
	void stop();

	/** True while a melody is being driven by the engine. */
	bool isPlaying() const { return playing; }

	/** Name of the currently playing melody, or nullptr. */
	const char* currentName() const { return current.name; }

	void loop(uint micros) override;

private:
	Melody current{ nullptr, 0, 0, false, nullptr };
	bool playing = false;

	uint16_t step = 0;          // index of the current note
	uint32_t stepElapsedUs = 0; // microseconds spent on the current step
	bool inGap = false;         // currently in the inter-note silence

	void enterStep(uint16_t i);
	void emitTone();
};

extern PhoneRingtoneEngine Ringtone;

#endif // CHATTER_FIRMWARE_PHONE_RINGTONE_ENGINE_H
