#include "PhoneAlarmTone.h"

#include "PhoneRingtoneLibrary.h"
#include "PhoneComposerStorage.h"
#include "PhoneComposerPlayback.h"
#include "../Screens/PhoneComposer.h"

#include <Settings.h>
#include <Notes.h>

#include <stdio.h>
#include <string.h>

// =====================================================================
// PhoneAlarmTone -- factory + library + composer resolver for the
// alarm-clock ringtone choice persisted in `Settings.alarmTone`.
//
// The factory melody is the legacy four-note arpeggio originally
// hard-coded inside PhoneAlarmService::triggerFire(). It now lives
// here so the encoding can stay self-contained: a single resolver
// covers "factory + library + composer" with no special-case branch
// at the service layer.
//
// Composer resolution mirrors PhoneContactRingtone's pattern -- pull
// the slot via PhoneComposerStorage, run the same conversion table
// PhoneComposerPlayback uses, stash the result in a static buffer
// that outlives the resolve() call. Only one alarm rings at a time
// so single-buffering is sufficient and the engine can dereference
// the supplied Note* for the lifetime of playback.
// =====================================================================

namespace {

// Factory alarm: rising 4-note arpeggio that loops while the alarm is
// firing. Identical (note, duration, gap) to the kAlarmMelody that
// shipped with S124, just relocated so a user that has never visited
// the new picker hears the exact same audio they always have.
const PhoneRingtoneEngine::Note kFactoryAlarmNotes[] = {
		{ NOTE_E5,  240 },
		{ NOTE_A5,  240 },
		{ NOTE_C6,  240 },
		{ NOTE_A5,  240 },
};

const PhoneRingtoneEngine::Melody kFactoryAlarmMelody = {
		kFactoryAlarmNotes,
		sizeof(kFactoryAlarmNotes) / sizeof(kFactoryAlarmNotes[0]),
		70,        // gapMs -- same family as PhoneTimer's TimerAlm
		true,      // loop until dismissed
		"AlarmClk",
};

// Composer-resolution buffers. Sized to PhoneComposer::MaxNotes so a
// largest-possible composition fits without truncation. Single buffer
// because only one alarm rings at a time -- resolving a different id
// silently invalidates the previous resolution.
PhoneRingtoneEngine::Note   s_engineBuffer[PhoneComposer::MaxNotes];
PhoneRingtoneEngine::Melody s_resolvedMelody{};
char                        s_resolvedName[PhoneComposerStorage::MaxNameLen + 4] = {0};

// Stack-cheap scratch for nameOf() composer reads.
constexpr uint8_t kNameScratchNoteMax = PhoneComposer::MaxNotes;

} // namespace

// ---------- id classification ----------

bool PhoneAlarmTone::isFactoryId(uint8_t id) {
	return id == FactoryId;
}

bool PhoneAlarmTone::isLibraryId(uint8_t id) {
	return id >= LibraryBase && id < (uint8_t)(LibraryBase + LibraryCount);
}

bool PhoneAlarmTone::isComposerId(uint8_t id) {
	return id >= ComposerSlotBase &&
	       id <  (uint8_t)(ComposerSlotBase + ComposerSlotCount);
}

uint8_t PhoneAlarmTone::libraryIndex(uint8_t id) {
	if(!isLibraryId(id)) return 0xFF;
	return (uint8_t)(id - LibraryBase);
}

uint8_t PhoneAlarmTone::composerSlotIndex(uint8_t id) {
	if(!isComposerId(id)) return 0xFF;
	return (uint8_t)(id - ComposerSlotBase);
}

uint8_t PhoneAlarmTone::encodeLibrary(uint8_t libraryIdx) {
	if(libraryIdx >= LibraryCount) return DefaultId;
	return (uint8_t)(LibraryBase + libraryIdx);
}

uint8_t PhoneAlarmTone::encodeComposerSlot(uint8_t slotIdx) {
	if(slotIdx >= ComposerSlotCount) return DefaultId;
	return (uint8_t)(ComposerSlotBase + slotIdx);
}

bool PhoneAlarmTone::isUsable(uint8_t id) {
	if(isFactoryId(id))  return true;
	if(isLibraryId(id))  return true;
	if(isComposerId(id)) {
		const uint8_t slot = composerSlotIndex(id);
		return PhoneComposerStorage::hasSlot(slot);
	}
	return false;
}

uint8_t PhoneAlarmTone::validatedOrDefault(uint8_t id) {
	return isUsable(id) ? id : DefaultId;
}

// ---------- display ----------

void PhoneAlarmTone::nameOf(uint8_t id, char* outName, size_t outLen) {
	if(outName == nullptr || outLen == 0) return;
	outName[0] = '\0';

	if(isFactoryId(id)) {
		strncpy(outName, "Factory", outLen - 1);
		outName[outLen - 1] = '\0';
		return;
	}

	if(isLibraryId(id)) {
		const uint8_t libIdx = libraryIndex(id);
		const char* n = PhoneRingtoneLibrary::nameOf(
				(PhoneRingtoneLibrary::Id) libIdx);
		if(n == nullptr) n = "Tone";
		strncpy(outName, n, outLen - 1);
		outName[outLen - 1] = '\0';
		return;
	}

	if(isComposerId(id)) {
		const uint8_t slot = composerSlotIndex(id);
		if(!PhoneComposerStorage::hasSlot(slot)) {
			snprintf(outName, outLen, "Slot %u (empty)",
			         (unsigned)(slot + 1));
			return;
		}
		PhoneComposer::Note tmp[kNameScratchNoteMax];
		uint8_t  cnt = 0;
		uint16_t bpm = 0;
		char     nm[PhoneComposerStorage::MaxNameLen + 1] = {0};
		const bool ok = PhoneComposerStorage::loadSlot(slot,
		                tmp, kNameScratchNoteMax, &cnt,
		                nm,  sizeof(nm), &bpm);
		if(ok && nm[0] != '\0') {
			snprintf(outName, outLen, "* %s", nm);
		} else {
			snprintf(outName, outLen, "Slot %u", (unsigned)(slot + 1));
		}
		return;
	}

	// Unknown id -- show the same dim placeholder PhoneContactRingtone
	// uses so a corrupted record never reads as a real ringtone label.
	strncpy(outName, "(default)", outLen - 1);
	outName[outLen - 1] = '\0';
}

// ---------- playback ----------

const PhoneRingtoneEngine::Melody* PhoneAlarmTone::factoryMelody() {
	return &kFactoryAlarmMelody;
}

const PhoneRingtoneEngine::Melody* PhoneAlarmTone::resolve(uint8_t id) {
	if(isFactoryId(id)) return &kFactoryAlarmMelody;

	if(isLibraryId(id)) {
		const uint8_t libIdx = libraryIndex(id);
		return &PhoneRingtoneLibrary::get(
				(PhoneRingtoneLibrary::Id) libIdx);
	}

	if(isComposerId(id)) {
		const uint8_t slot = composerSlotIndex(id);
		if(!PhoneComposerStorage::hasSlot(slot)) {
			// Slot was emptied since the user picked it. Fall back
			// to factory so the alarm always rings -- the user
			// hears the safe default rather than silence.
			return &kFactoryAlarmMelody;
		}

		PhoneComposer::Note rawNotes[PhoneComposer::MaxNotes];
		uint8_t  rawCount = 0;
		uint16_t bpm      = 0;
		char     nm[PhoneComposerStorage::MaxNameLen + 1] = {0};
		const bool ok = PhoneComposerStorage::loadSlot(slot,
		                rawNotes, PhoneComposer::MaxNotes, &rawCount,
		                nm, sizeof(nm), &bpm);
		if(!ok || rawCount == 0) {
			return &kFactoryAlarmMelody;
		}

		const uint16_t built = PhoneComposerPlayback::buildEngineNotes(
				rawNotes, rawCount, bpm,
				s_engineBuffer, PhoneComposer::MaxNotes);
		if(built == 0) {
			return &kFactoryAlarmMelody;
		}

		// Prepare the display name for the engine's currentName().
		if(nm[0] != '\0') {
			snprintf(s_resolvedName, sizeof(s_resolvedName), "* %s", nm);
		} else {
			snprintf(s_resolvedName, sizeof(s_resolvedName),
			         "Slot %u", (unsigned)(slot + 1));
		}

		s_resolvedMelody.notes  = s_engineBuffer;
		s_resolvedMelody.count  = built;
		s_resolvedMelody.gapMs  = 30;
		s_resolvedMelody.loop   = true;   // alarm loops until dismissed
		s_resolvedMelody.name   = s_resolvedName;
		return &s_resolvedMelody;
	}

	// Unknown encoding -- factory is the safe alarm-always-rings fallback.
	return &kFactoryAlarmMelody;
}

const PhoneRingtoneEngine::Melody* PhoneAlarmTone::resolveActive() {
	return resolve(getActiveId());
}

// ---------- picker iteration ----------

uint8_t PhoneAlarmTone::pickerCount() {
	uint8_t n = 1 + LibraryCount;   // Factory + library always shown.
	for(uint8_t s = 0; s < ComposerSlotCount; ++s) {
		if(PhoneComposerStorage::hasSlot(s)) n++;
	}
	return n;
}

uint8_t PhoneAlarmTone::pickerIdAt(uint8_t idx) {
	if(idx == 0) return FactoryId;

	uint8_t cursor = idx - 1;
	if(cursor < LibraryCount) {
		return encodeLibrary(cursor);
	}
	cursor -= LibraryCount;

	uint8_t composerSeen = 0;
	for(uint8_t s = 0; s < ComposerSlotCount; ++s) {
		if(!PhoneComposerStorage::hasSlot(s)) continue;
		if(composerSeen == cursor) {
			return encodeComposerSlot(s);
		}
		composerSeen++;
	}
	return DefaultId;
}

uint8_t PhoneAlarmTone::pickerIndexOf(uint8_t id) {
	if(isFactoryId(id))  return 0;
	if(isLibraryId(id))  return (uint8_t)(1 + libraryIndex(id));
	if(isComposerId(id)) {
		const uint8_t slot = composerSlotIndex(id);
		if(!PhoneComposerStorage::hasSlot(slot)) return 0;
		uint8_t composerSeen = 0;
		for(uint8_t s = 0; s < ComposerSlotCount; ++s) {
			if(!PhoneComposerStorage::hasSlot(s)) continue;
			if(s == slot) {
				return (uint8_t)(1 + LibraryCount + composerSeen);
			}
			composerSeen++;
		}
	}
	return 0;
}

// ---------- persisted accessor ----------

uint8_t PhoneAlarmTone::getActiveId() {
	const uint8_t raw = Settings.get().alarmTone;
	return validatedOrDefault(raw);
}

void PhoneAlarmTone::setActiveId(uint8_t id) {
	Settings.get().alarmTone = validatedOrDefault(id);
	Settings.store();
}
