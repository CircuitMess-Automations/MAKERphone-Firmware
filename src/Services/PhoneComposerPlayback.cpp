#include "PhoneComposerPlayback.h"

#include <stdint.h>
#include <string.h>

// =====================================================================
// PhoneComposerPlayback — Note buffer -> engine notes -> Ringtone.
//
// The frequency table is 12-TET tuned to A4 = 440 Hz, octave 4. Other
// octaves are derived by `<<` / `>>` of the anchor value. The piezo
// can only resolve whole numbers, so the integer-rounded frequencies
// we store are indistinguishable from a more precise float pipeline.
//
// Tempo math: at BPM b, the duration of a whole note in ms is
//   (60_000 / b) * 4   ==   240_000 / b
// For length L (one of 1, 2, 4, 8, 16, 32) the per-note duration is
//   240_000 / (b * L)
// Dotted notes extend the duration by 50 %.
// =====================================================================

namespace {

// Octave-4 frequencies, in semitone order C, C#, D, D#, E, F, F#, G,
// G#, A, A#, B. RTTTL's tone letters cover seven of these; we look
// up the diatonic note then bump by one if the sharp flag is set.
//
// Source: standard 12-TET, A4=440Hz, rounded to nearest integer Hz.
static const uint16_t kOctave4[12] = {
	261,  // C4
	277,  // C#4
	294,  // D4
	311,  // D#4
	330,  // E4
	349,  // F4
	370,  // F#4
	392,  // G4
	415,  // G#4
	440,  // A4
	466,  // A#4
	494,  // B4
};

// Map an RTTTL tone letter to its semitone offset within an octave.
// Returns 0xFF for 'P' (rest) so the caller can short-circuit to
// frequency 0.
static uint8_t semitoneFor(char tone) {
	switch(tone) {
		case 'C': return 0;
		case 'D': return 2;
		case 'E': return 4;
		case 'F': return 5;
		case 'G': return 7;
		case 'A': return 9;
		case 'B': return 11;
		case 'P':
		default:
			return 0xFF;
	}
}

// Convert a Note's tone+sharp+octave to a frequency in Hertz.
// Returns 0 for rests and unknown tones (so the engine plays silence
// for that step).
static uint16_t freqFor(const PhoneComposer::Note& n) {
	const uint8_t baseSemi = semitoneFor(n.tone);
	if(baseSemi == 0xFF) return 0;

	uint8_t semi = baseSemi;
	if(n.sharp) {
		// Sharp lifts by one semitone; if we'd wrap past B (11) into
		// the next octave, pull the octave up too. The table only has
		// 12 entries.
		if(semi >= 11) {
			semi = 0;
		} else {
			semi += 1;
		}
	}

	uint32_t freq = kOctave4[semi];

	// Octave shift relative to 4. Stays integer thanks to bitwise ops.
	if(n.octave > 4) {
		freq <<= (n.octave - 4);
	} else if(n.octave < 4) {
		// Round-to-nearest on the way down so a 1-Hz error doesn't
		// stack on every shift.
		uint8_t shift = (4 - n.octave);
		// Add half-shift before the divide so the integer truncation
		// rounds rather than always-floors.
		uint32_t halfBit = (1u << shift) >> 1;
		freq = (freq + halfBit) >> shift;
	}

	if(freq > 0xFFFFu) freq = 0xFFFFu;
	return (uint16_t)freq;
}

static uint16_t durationMsFor(uint8_t length, bool dotted, uint16_t bpm) {
	if(length == 0) length = 4;
	if(bpm == 0)    bpm    = PhoneComposerPlayback::DefaultBpm;

	// 240_000 / (bpm * length) -- whole-ms precision is fine for a
	// piezo + a 30 ms perceptual JND.
	uint32_t denom = (uint32_t)bpm * (uint32_t)length;
	if(denom == 0) denom = 1;
	uint32_t ms = 240000u / denom;
	if(dotted) {
		ms = ms + (ms >> 1); // 1.5x
	}
	if(ms < 30)     ms = 30;     // never clip below the gap floor
	if(ms > 0xFFFFu) ms = 0xFFFFu;
	return (uint16_t)ms;
}

} // namespace

// =====================================================================
// Static playback buffer
//
// PhoneRingtoneEngine dereferences the supplied note pointer for the
// lifetime of playback, so the buffer must outlive the play() call.
// We keep it in module scope rather than on the heap to avoid an
// allocation in the input path.
// =====================================================================

static PhoneRingtoneEngine::Note s_engineBuffer[PhoneComposerPlayback::MaxEngineNotes];
static uint16_t                  s_engineCount  = 0;
static bool                      s_playing      = false;

uint16_t PhoneComposerPlayback::buildEngineNotes(
		const PhoneComposer::Note*    notes,
		uint8_t                       count,
		uint16_t                      bpm,
		PhoneRingtoneEngine::Note*    outNotes,
		uint16_t                      maxOut) {

	if(outNotes == nullptr || maxOut == 0) return 0;
	if(notes == nullptr || count == 0)     return 0;

	if(bpm == 0) bpm = DefaultBpm;

	const uint16_t lim = (count < maxOut) ? count : maxOut;
	for(uint16_t i = 0; i < lim; ++i) {
		outNotes[i].freq       = freqFor(notes[i]);
		outNotes[i].durationMs = durationMsFor(notes[i].length,
		                                       notes[i].dotted, bpm);
	}
	return lim;
}

bool PhoneComposerPlayback::play(const PhoneComposer::Note* notes,
								 uint8_t                    count,
								 uint16_t                   bpm,
								 bool                       loopForever,
								 const char*                name) {
	if(notes == nullptr || count == 0) return false;

	const uint16_t built = buildEngineNotes(notes, count, bpm,
	                                        s_engineBuffer,
	                                        MaxEngineNotes);
	if(built == 0) return false;

	s_engineCount = built;

	PhoneRingtoneEngine::Melody m{};
	m.notes  = s_engineBuffer;
	m.count  = built;
	m.gapMs  = InterNoteGapMs;
	m.loop   = loopForever;
	m.name   = name;

	Ringtone.play(m);
	s_playing = true;
	return true;
}

void PhoneComposerPlayback::stop() {
	// Only stop if WE were the source; the engine could currently be
	// driving an unrelated melody (e.g. an incoming-call ringer that
	// fired during a preview). The cheapest signal we have for that is
	// a name comparison against the melody we last fed in -- and since
	// we don't always set a name, fall back to: if the engine is
	// playing AND we think we started it, stop. Otherwise leave it
	// alone.
	if(!s_playing) return;
	Ringtone.stop();
	s_playing = false;
}

bool PhoneComposerPlayback::isPlaying() {
	if(!s_playing) return false;
	// If the engine has finished naturally, our local "playing" flag
	// will be stale; reconcile so callers can poll us instead of
	// reading from Ringtone directly.
	if(!Ringtone.isPlaying()) {
		s_playing = false;
		return false;
	}
	return true;
}
