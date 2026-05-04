#include "PhoneContactRingtone.h"

#include "PhoneRingtoneLibrary.h"
#include "PhoneComposerStorage.h"
#include "PhoneComposerPlayback.h"
#include "../Screens/PhoneComposer.h"

#include <stdio.h>
#include <string.h>

// =====================================================================
// PhoneContactRingtone — id classification + lazy composer resolution.
//
// Library tones come for free as static const PhoneRingtoneEngine::Melody
// structs supplied by PhoneRingtoneLibrary::get(). Composer tones need a
// load + decode step: read the slot via PhoneComposerStorage::loadSlot,
// run the same conversion table PhoneComposerPlayback uses, and stash
// the result in a static Note buffer that outlives the resolve() call.
//
// The engine dereferences the supplied Note pointer for the lifetime of
// playback, so the buffer must be static (or otherwise long-lived). We
// use a single buffer because only one incoming call rings at a time —
// resolving a different id silently invalidates the previous resolution.
// =====================================================================

namespace {

// Sized to PhoneComposer::MaxNotes so the largest possible composition
// fits without truncation. Same buffer shape PhoneComposerPlayback uses
// internally, but kept private here so a composer preview running
// concurrently with a per-contact ringer cannot race over the same
// memory.
static PhoneRingtoneEngine::Note   s_engineBuffer[PhoneComposer::MaxNotes];
static PhoneRingtoneEngine::Melody s_resolvedMelody{};
// Cached "this is the melody the engine buffer currently describes" id.
// 0xFF means "buffer is empty / never loaded". Used as a tiny cache so a
// caller that looks the contact up twice in quick succession (resolve
// for the engine, then nameOf for the toast) doesn't double-hit NVS.
static uint8_t                     s_resolvedFor  = 0xFF;
// Display name held inside the static Melody so the engine can surface
// it via `currentName()`. Sized to PhoneComposerStorage::MaxNameLen plus
// the "♪ " glyph + nul terminator.
static char                        s_resolvedName[PhoneComposerStorage::MaxNameLen + 4] = {0};

// Stack-cheap scratch for nameOf() composer reads. PhoneComposer::Note
// is a 5-byte POD, so 64 of them is 320 B — fine on the call stack of
// a list-style picker that only repaints on cursor steps.
static constexpr uint8_t kNameScratchNoteMax = PhoneComposer::MaxNotes;

} // namespace

// ---------- id classification ----------

bool PhoneContactRingtone::isLibraryId(uint8_t id) {
	return id < LibraryCount;
}

bool PhoneContactRingtone::isComposerId(uint8_t id) {
	return id >= ComposerSlotBase &&
		   id <  (uint8_t)(ComposerSlotBase + ComposerSlotCount);
}

uint8_t PhoneContactRingtone::composerSlotIndex(uint8_t id) {
	if(!isComposerId(id)) return 0xFF;
	return (uint8_t)(id - ComposerSlotBase);
}

uint8_t PhoneContactRingtone::encodeComposerSlot(uint8_t slotIdx) {
	if(slotIdx >= ComposerSlotCount) return DefaultId;
	return (uint8_t)(ComposerSlotBase + slotIdx);
}

bool PhoneContactRingtone::isUsable(uint8_t id) {
	if(isLibraryId(id))  return true;
	if(isComposerId(id)) {
		const uint8_t slot = composerSlotIndex(id);
		return PhoneComposerStorage::hasSlot(slot);
	}
	return false;
}

uint8_t PhoneContactRingtone::validatedOrDefault(uint8_t id) {
	return isUsable(id) ? id : DefaultId;
}

// ---------- display ----------

void PhoneContactRingtone::nameOf(uint8_t id, char* outName, size_t outLen) {
	if(outName == nullptr || outLen == 0) return;
	outName[0] = '\0';

	if(isLibraryId(id)) {
		const char* n = PhoneRingtoneLibrary::nameOf(
				(PhoneRingtoneLibrary::Id) id);
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

	// Unknown id — render an obvious dim placeholder so a corrupted
	// record never reads as a real ringtone label.
	strncpy(outName, "(default)", outLen - 1);
	outName[outLen - 1] = '\0';
}

// ---------- playback ----------

const PhoneRingtoneEngine::Melody* PhoneContactRingtone::resolve(uint8_t id) {
	if(isLibraryId(id)) {
		// Library ids return references to static const Melody
		// structs that the engine can dereference safely.
		s_resolvedFor = 0xFF; // composer cache no longer authoritative
		return &PhoneRingtoneLibrary::get(
				(PhoneRingtoneLibrary::Id) id);
	}

	if(!isComposerId(id)) return nullptr;

	const uint8_t slot = composerSlotIndex(id);
	if(!PhoneComposerStorage::hasSlot(slot)) return nullptr;

	// Pull the slot through the storage parser into a stack-local
	// PhoneComposer::Note buffer, then convert into the static
	// engine-shaped buffer the Melody points at.
	PhoneComposer::Note rawNotes[PhoneComposer::MaxNotes];
	uint8_t  rawCount = 0;
	uint16_t bpm      = 0;
	char     nm[PhoneComposerStorage::MaxNameLen + 1] = {0};
	const bool ok = PhoneComposerStorage::loadSlot(slot,
					rawNotes, PhoneComposer::MaxNotes, &rawCount,
					nm, sizeof(nm), &bpm);
	if(!ok || rawCount == 0) return nullptr;

	const uint16_t built = PhoneComposerPlayback::buildEngineNotes(
			rawNotes, rawCount, bpm,
			s_engineBuffer, PhoneComposer::MaxNotes);
	if(built == 0) return nullptr;

	// Compose the final display name in the static buffer so
	// PhoneRingtoneEngine::currentName() returns something legible.
	if(nm[0] != '\0') {
		snprintf(s_resolvedName, sizeof(s_resolvedName),
				 "* %s", nm);
	} else {
		snprintf(s_resolvedName, sizeof(s_resolvedName),
				 "Slot %u", (unsigned)(slot + 1));
	}

	s_resolvedMelody.notes  = s_engineBuffer;
	s_resolvedMelody.count  = built;
	// Same gap PhoneComposerPlayback uses so audio stays consistent
	// between the composer's preview and the call ringer.
	s_resolvedMelody.gapMs  = PhoneComposerPlayback::InterNoteGapMs;
	s_resolvedMelody.loop   = true;  // ringer keeps cycling until pickup
	s_resolvedMelody.name   = s_resolvedName;

	s_resolvedFor = id;
	return &s_resolvedMelody;
}

// ---------- picker iteration ----------

uint8_t PhoneContactRingtone::pickerCount() {
	uint8_t total = LibraryCount;
	for(uint8_t i = 0; i < ComposerSlotCount; ++i) {
		if(PhoneComposerStorage::hasSlot(i)) ++total;
	}
	return total;
}

uint8_t PhoneContactRingtone::pickerIdAt(uint8_t idx) {
	if(idx < LibraryCount) return idx;
	uint8_t composerOrdinal = (uint8_t)(idx - LibraryCount);
	for(uint8_t i = 0; i < ComposerSlotCount; ++i) {
		if(!PhoneComposerStorage::hasSlot(i)) continue;
		if(composerOrdinal == 0) return encodeComposerSlot(i);
		--composerOrdinal;
	}
	return DefaultId;
}

uint8_t PhoneContactRingtone::pickerIndexOf(uint8_t id) {
	if(isLibraryId(id)) return id;
	if(isComposerId(id)) {
		const uint8_t target = composerSlotIndex(id);
		uint8_t ordinal = 0;
		for(uint8_t i = 0; i < ComposerSlotCount; ++i) {
			if(!PhoneComposerStorage::hasSlot(i)) continue;
			if(i == target) return (uint8_t)(LibraryCount + ordinal);
			++ordinal;
		}
	}
	return 0;
}
