#include "PhoneVibrationLibrary.h"

// =====================================================================
// 0. Synthwave - slow waltz: one long pulse + two short pulses, then a
//    breathy gap. The 1-3 cadence echoes the melody's loop structure
//    (a long anchor note followed by a couple of staccato arpeggio
//    pickups), so a user feeling the buzz in their pocket recognises
//    which ringtone is playing.
// =====================================================================
static const PhoneVibrationEngine::Pulse kSynthwavePulses[] = {
	{ 220,  90 },
	{  90,  60 },
	{  90, 600 },
};
static const PhoneVibrationEngine::Pattern kSynthwave = {
	kSynthwavePulses,
	(uint16_t)(sizeof(kSynthwavePulses) / sizeof(kSynthwavePulses[0])),
	true,
	0,    // use DefaultFreq
	"Synthwave"
};

// =====================================================================
// 1. Classic - the Sony-Ericsson / Nokia twin "brrring brrring"
//    cadence felt rather than heard. Five quick stutter pulses, a
//    short gap, five more, then a long rest -- a 1:1 rhythm match
//    with the audible Classic ringtone's two-burst structure.
// =====================================================================
static const PhoneVibrationEngine::Pulse kClassicPulses[] = {
	{ 60, 40 }, { 60, 40 }, { 60, 40 }, { 60, 40 }, { 60, 250 },
	{ 60, 40 }, { 60, 40 }, { 60, 40 }, { 60, 40 }, { 60, 800 },
};
static const PhoneVibrationEngine::Pattern kClassic = {
	kClassicPulses,
	(uint16_t)(sizeof(kClassicPulses) / sizeof(kClassicPulses[0])),
	true,
	0,
	"Classic"
};

// =====================================================================
// 2. Beep - polite double-pulse with a long rest. Mirrors the
//    audible Beep ringtone's "two short beeps and a long pause"
//    shape. Reads as a discreet "you have a thing" buzz rather
//    than a sustained alert.
// =====================================================================
static const PhoneVibrationEngine::Pulse kBeepPulses[] = {
	{ 120, 100 },
	{ 120, 1000 },
};
static const PhoneVibrationEngine::Pattern kBeep = {
	kBeepPulses,
	(uint16_t)(sizeof(kBeepPulses) / sizeof(kBeepPulses[0])),
	true,
	0,
	"Beep"
};

// =====================================================================
// 3. Boss - dramatic staccato. A long opening pulse, three short
//    stutters that map onto the descending fanfare's accent notes,
//    and a long closing pulse that lands like the C-major resolve.
//    Followed by a longer rest so the pattern reads as "arrival"
//    rather than continuous noise -- matches the audible Boss
//    ringtone's bombastic call-and-response shape.
// =====================================================================
static const PhoneVibrationEngine::Pulse kBossPulses[] = {
	{ 260, 100 },
	{  80,  80 },
	{  80,  80 },
	{  80,  80 },
	{ 260, 600 },
};
static const PhoneVibrationEngine::Pattern kBoss = {
	kBossPulses,
	(uint16_t)(sizeof(kBossPulses) / sizeof(kBossPulses[0])),
	true,
	0,
	"Boss"
};

// =====================================================================
// 4. Silent - single zero-length pulse with a long off. The engine
//    still drives the loop so the call-screen state machine looks
//    identical to an audible ring, but the piezo never asserts.
//    Mirrors the audible Silent ringtone's single-rest design.
// =====================================================================
static const PhoneVibrationEngine::Pulse kSilentPulses[] = {
	{ 0, 1000 },
};
static const PhoneVibrationEngine::Pattern kSilent = {
	kSilentPulses,
	(uint16_t)(sizeof(kSilentPulses) / sizeof(kSilentPulses[0])),
	true,
	0,
	"Silent"
};

// =====================================================================
// 5. Composer - generic fallback for composer-authored ringtones
//    (PhoneContactRingtone ids 100..103). A clean two-pulse cadence
//    with a medium rest -- distinct from Beep (which has a longer
//    rest) and Classic (which has stutter bursts) so the user can
//    tell at a glance that the buzzing alert is from a custom
//    composition rather than a stock library tone. A future
//    S180-class session can hash the composer's note sequence into
//    a per-tune pattern; the API surface
//    (forRingtoneId / Pattern struct) is already shaped for that.
// =====================================================================
static const PhoneVibrationEngine::Pulse kComposerPulses[] = {
	{ 150, 120 },
	{ 150, 700 },
};
static const PhoneVibrationEngine::Pattern kComposer = {
	kComposerPulses,
	(uint16_t)(sizeof(kComposerPulses) / sizeof(kComposerPulses[0])),
	true,
	0,
	"Composer"
};

// =====================================================================
// Registry
// =====================================================================
static const PhoneVibrationEngine::Pattern* const kAll[] = {
	&kSynthwave,
	&kClassic,
	&kBeep,
	&kBoss,
	&kSilent,
	&kComposer,
};

uint8_t PhoneVibrationLibrary::count(){
	return (uint8_t) PhoneVibrationLibrary::Count;
}

const char* PhoneVibrationLibrary::nameOf(Id id){
	switch(id){
		case Synthwave: return "Synthwave";
		case Classic:   return "Classic";
		case Beep:      return "Beep";
		case Boss:      return "Boss";
		case Silent:    return "Silent";
		case Composer:  return "Composer";
		default:        return "Unknown";
	}
}

const PhoneVibrationEngine::Pattern& PhoneVibrationLibrary::get(Id id){
	uint8_t i = (uint8_t) id;
	if(i >= (uint8_t) PhoneVibrationLibrary::Count) i = 0;
	return *kAll[i];
}

const PhoneVibrationEngine::Pattern& PhoneVibrationLibrary::byIndex(uint8_t idx){
	if(idx >= (uint8_t) PhoneVibrationLibrary::Count){
		idx = (uint8_t)(idx % (uint8_t) PhoneVibrationLibrary::Count);
	}
	return *kAll[idx];
}

// PhoneContactRingtone encoding: 0..4 = library tones, 100..103 =
// composer save slots. We mirror that here without dragging in the
// header so this library stays self-contained.
const PhoneVibrationEngine::Pattern& PhoneVibrationLibrary::forRingtoneId(uint8_t ringtoneId){
	if(ringtoneId <= 4){
		// Library 1:1 mapping: Synthwave (0), Classic (1), Beep (2),
		// Boss (3), Silent (4) -- same order as PhoneRingtoneLibrary::Id
		// so the audible and tactile intents stay aligned.
		return *kAll[ringtoneId];
	}
	if(ringtoneId >= 100 && ringtoneId <= 103){
		// Composer slots (PhoneComposerStorage 0..3) collapse to the
		// generic Composer fallback today.
		return kComposer;
	}
	// Anything else -- defensive default to Synthwave so a stale
	// NVS byte never crashes playback.
	return kSynthwave;
}
