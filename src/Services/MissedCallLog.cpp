#include "MissedCallLog.h"
#include <string.h>
#include <algorithm>

/**
 * S49 — MissedCallLog implementation. The class is a process-wide
 * singleton; everything is owned by the single static instance returned
 * by instance(). All operations are O(1) amortised aside from the rare
 * front-erase that runs when the ring fills up.
 */

MissedCallLog& MissedCallLog::instance() {
	static MissedCallLog inst;
	return inst;
}

uint8_t MissedCallLog::add(const char* name, uint8_t avatarSeed) {
	// Drop the oldest entry once the bound is hit. erase(begin()) is
	// O(N) but N is capped at MaxEntries (16), and add() is rare, so
	// the cost is negligible — and the calling code keeps the simple
	// "newest-at-back" semantics that listeners rely on.
	if(entries.size() >= MaxEntries) {
		entries.erase(entries.begin());
	}

	Entry e{};
	e.avatarSeed = avatarSeed;
	e.addedAtMs  = millis();

	if(name != nullptr) {
		// strncpy guarantees the buffer is filled but not necessarily
		// terminated. Pin the trailing NUL ourselves.
		strncpy(e.name, name, sizeof(e.name) - 1);
		e.name[sizeof(e.name) - 1] = '\0';
	} else {
		e.name[0] = '\0';
	}

	entries.push_back(e);

	// S158 - latch the "next-wake flash" flag so the lock screen
	// fires its inverted-color pulse on the next onStarting() pass.
	// We raise the flag on every add() (not just the first since the
	// last clear) so a second / third missed call arriving while the
	// user is still away still produces a flash the next time the
	// device wakes - mirroring the Sony-Ericsson behaviour where the
	// screen flashes once per wake regardless of how many notifications
	// piled up while the user was gone.
	pendingFlash_ = true;

	notify();

	return (uint8_t)(entries.size() - 1);
}

bool MissedCallLog::consumePendingFlash() {
	// Read-and-clear in one shot so concurrent onStarting() passes
	// (e.g. a quick lock -> unlock -> lock cycle) can never replay
	// the flash for the same arrival. Cheap enough to be inlined,
	// but kept out-of-line for symmetry with the rest of the
	// non-trivial accessors and so future debug hooks can land
	// here without a header churn.
	const bool was = pendingFlash_;
	pendingFlash_ = false;
	return was;
}

void MissedCallLog::clear() {
	if(entries.empty()) return;     // skip the listener walk on a no-op clear

	entries.clear();
	notify();
}

const char* MissedCallLog::latestName() const {
	if(entries.empty()) return "";
	return entries.back().name;
}

void MissedCallLog::addListener(MissedCallLogListener* listener) {
	if(listener == nullptr) return;
	if(std::find(listeners.begin(), listeners.end(), listener) != listeners.end()) return;
	listeners.push_back(listener);
}

void MissedCallLog::removeListener(MissedCallLogListener* listener) {
	auto it = std::find(listeners.begin(), listeners.end(), listener);
	if(it != listeners.end()) listeners.erase(it);
}

void MissedCallLog::notify() {
	// Snapshot the listener vector so callbacks that mutate the list
	// (a listener removing itself, for example) do not invalidate the
	// iterator we are walking right now.
	auto snapshot = listeners;
	for(auto* l : snapshot) {
		if(l) l->onMissedCallsChanged();
	}
}
