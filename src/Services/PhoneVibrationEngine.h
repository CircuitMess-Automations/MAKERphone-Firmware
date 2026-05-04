#ifndef CHATTER_FIRMWARE_PHONE_VIBRATION_ENGINE_H
#define CHATTER_FIRMWARE_PHONE_VIBRATION_ENGINE_H

#include <Arduino.h>
#include <Loop/LoopListener.h>

/**
 * S161 - PhoneVibrationEngine
 *
 * Buzzer-pulse choreography that simulates a vibration motor on the
 * Chatter's piezo. Chatter has no haptic actuator today, so the
 * "vibration" is a sequence of low-pitch rhythmic bursts -- a real
 * piezo, driven at ~80 Hz with on/off pulses, rattles its diaphragm
 * in a way that reads as a buzzy tactile cue when the device is in
 * the user's hand or pocket. Same trick the original Sony-Ericsson
 * T-series fell back to in "courtesy" profiles with a worn-out
 * vibration motor.
 *
 * The engine is structurally identical to PhoneRingtoneEngine (S39)
 * but takes a `Pattern` of (onMs, offMs) pulses rather than a melody
 * of (freq, durationMs) notes. One Pattern uses one base frequency
 * for every pulse so a vibration pattern reads as a single rhythmic
 * "feel" rather than a tune. Patterns are static const arrays
 * declared in PhoneVibrationLibrary so playback never allocates.
 *
 * Hardware multiplexing: the Chatter has a single piezo, so this
 * engine and PhoneRingtoneEngine cannot run simultaneously --
 * whichever called play() last wins the speaker. Callers
 * (PhoneIncomingCall in the S161 wiring) are responsible for picking
 * one alert path per call: ringtone in Loud profiles, vibration in
 * Meeting. The engine itself does NOT gate on Settings.sound -- the
 * whole point of vibration is to fire when the audible ringer is
 * muted -- so callers explicitly decide which path runs.
 *
 * Behaviour notes:
 *   - Patterns can opt in to looping (used for the call-screen
 *     "ring" cycle, same as the audible ringtone).
 *   - stop() silences the piezo and detaches from LoopManager so a
 *     subsequent BuzzerService keypress tone (Piezo.tone(...)) is
 *     not stomped by a stale pulse.
 *   - The base frequency is fixed at 80 Hz by default, with
 *     per-pattern override via `Pattern.freq`. We pick 80 Hz because
 *     the piezo's resonance falls off below ~50 Hz (the rumble
 *     becomes inaudible) and rises above ~150 Hz into "tone"
 *     territory (the user hears a distinct pitch rather than feeling
 *     a buzz). 80 Hz hits the sweet spot where a pulse is felt but
 *     not heard as a clean note.
 *
 * Pattern.pulses must point to memory that outlives the playback.
 * Static const arrays (the pattern PhoneVibrationLibrary uses) are
 * the intended source.
 */
class PhoneVibrationEngine : public LoopListener {
public:
	struct Pulse {
		uint16_t onMs;       // length of the buzz pulse
		uint16_t offMs;      // silence after the pulse
	};

	struct Pattern {
		const Pulse* pulses;
		uint16_t count;
		bool     loop;       // restart from start when finished
		uint16_t freq;       // Hz, 0 = use DefaultFreq (80 Hz)
		const char* name;    // optional display name (may be nullptr)
	};

	/**
	 * Default piezo frequency used when Pattern.freq is 0. Tuned to
	 * sit in the felt-but-not-heard band of the Chatter speaker.
	 */
	static constexpr uint16_t DefaultFreq = 80;

	void begin();

	/** Start (or replace) a vibration pattern. */
	void play(const Pattern& pattern);

	/** Stop pulsing immediately and silence the piezo. */
	void stop();

	/** True while a pattern is being driven by the engine. */
	bool isPlaying() const { return playing; }

	/** Name of the currently playing pattern, or nullptr. */
	const char* currentName() const { return current.name; }

	void loop(uint micros) override;

private:
	Pattern current{ nullptr, 0, false, DefaultFreq, nullptr };
	bool playing = false;

	uint16_t step = 0;          // index of the current pulse
	uint32_t stepElapsedUs = 0; // microseconds spent on the current step
	bool inOff = false;         // currently in the inter-pulse silence

	void enterStep(uint16_t i);
	void emitPulse();
	uint16_t effectiveFreq() const;
};

extern PhoneVibrationEngine Vibrate;

#endif // CHATTER_FIRMWARE_PHONE_VIBRATION_ENGINE_H
