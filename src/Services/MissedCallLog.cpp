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
	notify();

	return (uint8_t)(entries.size() - 1);
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
