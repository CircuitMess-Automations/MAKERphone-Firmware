#include "PhoneSoftKeyTone.h"

#include <Audio/Piezo.h>
#include <Notes.h>
#include <Settings.h>

namespace {
struct ToneEntry {
	const char* name;
	const char* desc;
	uint16_t    freq;
	uint16_t    durationMs;
};

// Order MUST match PhoneSoftKeyToneLib::Id. Classic stays at index 0
// with NOTE_B4 / 25 ms so a freshly-flashed device sounds byte-identical
// to every prior MAKERphone firmware on its soft-keys.
const ToneEntry kTones[PhoneSoftKeyToneLib::Count] = {
	{ "CLASSIC", "B4 / 25 ms", NOTE_B4, 25 },
	{ "CLICK",   "Snappy",     NOTE_C6,  8 },
	{ "BLOOP",   "Low blip",   NOTE_A3, 30 },
	{ "CHIRP",   "Mid chirp",  NOTE_E5, 18 },
	{ "SILENT",  "No tone",    0,        0 },
};
} // namespace

uint8_t PhoneSoftKeyToneLib::count() {
	return Count;
}

bool PhoneSoftKeyToneLib::valid(uint8_t id) {
	return id < Count;
}

const char* PhoneSoftKeyToneLib::name(uint8_t id) {
	if(!valid(id)) id = DefaultId;
	return kTones[id].name;
}

const char* PhoneSoftKeyToneLib::desc(uint8_t id) {
	if(!valid(id)) id = DefaultId;
	return kTones[id].desc;
}

uint16_t PhoneSoftKeyToneLib::freq(uint8_t id) {
	if(!valid(id)) id = DefaultId;
	return kTones[id].freq;
}

uint16_t PhoneSoftKeyToneLib::durationMs(uint8_t id) {
	if(!valid(id)) id = DefaultId;
	return kTones[id].durationMs;
}

uint8_t PhoneSoftKeyToneLib::getActive() {
	const uint8_t raw = Settings.get().softKeyTone;
	if(!valid(raw)) return DefaultId;
	return raw;
}

void PhoneSoftKeyToneLib::setActive(uint8_t id) {
	if(!valid(id)) id = DefaultId;
	Settings.get().softKeyTone = id;
	Settings.store();
}

void PhoneSoftKeyToneLib::play(uint8_t id) {
	if(!valid(id)) return;
	const ToneEntry& e = kTones[id];
	if(e.freq == 0 || e.durationMs == 0) return;   // Silent / invalid.
	Piezo.tone(e.freq, e.durationMs);
}

void PhoneSoftKeyToneLib::playActive() {
	play(getActive());
}
