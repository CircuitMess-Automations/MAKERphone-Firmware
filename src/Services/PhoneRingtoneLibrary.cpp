#include "PhoneRingtoneLibrary.h"

// =====================================================================
// 0. Synthwave — moody A-minor arpeggio that loops.
//
//   Frequencies (Hz):
//     A3=220, F4=349, A4=440, C5=523, E5=659, F5=698, G5=784,
//     A5=880. Bar lengths chosen to fit a slow ~85 BPM groove.
// =====================================================================
static const PhoneRingtoneEngine::Note kSynthwaveNotes[] = {
	{ 220, 150 },  // A3
	{ 523, 150 },  // C5
	{ 659, 150 },  // E5
	{ 880, 220 },  // A5
	{   0,  80 },
	{ 784, 150 },  // G5
	{ 659, 150 },  // E5
	{ 523, 150 },  // C5
	{ 440, 220 },  // A4
	{   0,  80 },
	{ 349, 150 },  // F4
	{ 440, 150 },  // A4
	{ 523, 150 },  // C5
	{ 698, 220 },  // F5
	{   0,  80 },
	{ 659, 150 },  // E5
	{ 523, 150 },  // C5
	{ 440, 150 },  // A4
	{ 349, 220 },  // F4
	{   0, 280 },
};
static const PhoneRingtoneEngine::Melody kSynthwave = {
	kSynthwaveNotes,
	(uint16_t)(sizeof(kSynthwaveNotes) / sizeof(kSynthwaveNotes[0])),
	20,    // gapMs — tiny breath between notes
	true,  // loop
	"Synthwave"
};

// =====================================================================
// 1. Classic — twin "brrring brrring" bursts then a long pause.
// =====================================================================
static const PhoneRingtoneEngine::Note kClassicNotes[] = {
	{ 1320,  60 }, {    0,  40 },
	{ 1320,  60 }, {    0,  40 },
	{ 1320,  60 }, {    0,  40 },
	{ 1320,  60 }, {    0,  40 },
	{ 1320,  60 }, {    0,  40 },
	{ 1320,  60 }, {    0, 250 },

	{ 1320,  60 }, {    0,  40 },
	{ 1320,  60 }, {    0,  40 },
	{ 1320,  60 }, {    0,  40 },
	{ 1320,  60 }, {    0,  40 },
	{ 1320,  60 }, {    0,  40 },
	{ 1320,  60 }, {    0, 800 },
};
static const PhoneRingtoneEngine::Melody kClassic = {
	kClassicNotes,
	(uint16_t)(sizeof(kClassicNotes) / sizeof(kClassicNotes[0])),
	0,     // tight burst — gapMs handled inside the array via rests
	true,
	"Classic"
};

// =====================================================================
// 2. Beep — polite, short double beep that loops with a long silence.
// =====================================================================
static const PhoneRingtoneEngine::Note kBeepNotes[] = {
	{ 1760,  90 },
	{    0,  90 },
	{ 1760,  90 },
	{    0, 900 },
};
static const PhoneRingtoneEngine::Melody kBeep = {
	kBeepNotes,
	(uint16_t)(sizeof(kBeepNotes) / sizeof(kBeepNotes[0])),
	0,
	true,
	"Beep"
};

// =====================================================================
// 3. Boss — dramatic JRPG-style fanfare. Loud, memorable, loops.
//
//   C5=523, D5=587, E5=659, F5=698, G5=784, A5=880, B5=988, C6=1046
// =====================================================================
static const PhoneRingtoneEngine::Note kBossNotes[] = {
	{  523, 180 },
	{  659, 180 },
	{  784, 180 },
	{ 1046, 380 },
	{    0,  60 },
	{  988, 180 },
	{  880, 180 },
	{  784, 180 },
	{  659, 180 },
	{    0,  60 },
	{  698, 180 },
	{  880, 180 },
	{ 1046, 180 },
	{  988, 380 },
	{    0,  60 },
	{  784, 180 },
	{  659, 180 },
	{  523, 380 },
	{    0, 600 },
};
static const PhoneRingtoneEngine::Melody kBoss = {
	kBossNotes,
	(uint16_t)(sizeof(kBossNotes) / sizeof(kBossNotes[0])),
	20,
	true,
	"Boss"
};

// =====================================================================
// 4. Silent — single long rest that loops. The engine keeps the
// playhead moving so call-screen code can call Ringtone.stop() the same
// way it would for an audible ringtone.
// =====================================================================
static const PhoneRingtoneEngine::Note kSilentNotes[] = {
	{ 0, 1000 },
};
static const PhoneRingtoneEngine::Melody kSilent = {
	kSilentNotes,
	(uint16_t)(sizeof(kSilentNotes) / sizeof(kSilentNotes[0])),
	0,
	true,
	"Silent"
};

// =====================================================================
// Registry
// =====================================================================
static const PhoneRingtoneEngine::Melody* const kAll[] = {
	&kSynthwave,
	&kClassic,
	&kBeep,
	&kBoss,
	&kSilent,
};

uint8_t PhoneRingtoneLibrary::count(){
	return (uint8_t) PhoneRingtoneLibrary::Count;
}

const char* PhoneRingtoneLibrary::nameOf(Id id){
	switch(id){
		case Synthwave: return "Synthwave";
		case Classic:   return "Classic";
		case Beep:      return "Beep";
		case Boss:      return "Boss";
		case Silent:    return "Silent";
		default:        return "Unknown";
	}
}

const PhoneRingtoneEngine::Melody& PhoneRingtoneLibrary::get(Id id){
	uint8_t i = (uint8_t) id;
	if(i >= (uint8_t) PhoneRingtoneLibrary::Count) i = 0;
	return *kAll[i];
}

const PhoneRingtoneEngine::Melody& PhoneRingtoneLibrary::byIndex(uint8_t idx){
	if(idx >= (uint8_t) PhoneRingtoneLibrary::Count){
		idx = (uint8_t)(idx % (uint8_t) PhoneRingtoneLibrary::Count);
	}
	return *kAll[idx];
}
