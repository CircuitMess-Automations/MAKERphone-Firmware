#ifndef MAKERPHONE_MISSEDCALLLOG_H
#define MAKERPHONE_MISSEDCALLLOG_H

#include <Arduino.h>
#include <stdint.h>
#include <vector>

/**
 * S49 — MissedCallLog
 *
 * Tiny in-memory singleton that tracks the most recent missed calls so
 * the lock screen (and, eventually, the homescreen + call history) has
 * a single source of truth to render a "Y MISSED CALLS" preview.
 *
 * Why a separate service rather than reusing PhoneCallHistory?
 *  - PhoneCallHistory is a *screen*, instantiated only while focused.
 *    Its in-memory entries do not survive being popped off the stack,
 *    and its sample data is purely demo material.
 *  - The lock screen needs a notification source that exists for the
 *    full duration of the app — independent of which screen is active —
 *    so a tiny module-scoped service is the natural fit.
 *  - S28 (incoming-call wiring) and S58 (low-battery flow) can both
 *    push entries into this log without coupling to any UI surface.
 *
 * Design notes:
 *  - Bounded ring (newest at the back). Older entries are dropped past
 *    `MaxEntries` so the heap cost is fixed and small.
 *  - Listener pattern is deliberately minimal — `MissedCallLogListener`
 *    has a single `onMissedCallsChanged()` callback. The lock-screen
 *    preview can re-pull the count + latest name on the next frame
 *    without any payload marshaling.
 *  - Code-only (no SPIFFS, no persistent storage). The log resets on
 *    reboot, mirroring how Sony-Ericsson handsets cleared their
 *    notification stack on power cycle.
 *  - All entry strings are copied into a fixed-size internal buffer so
 *    callers don't have to keep their pointers alive.
 *
 * Public API is intentionally conservative; future sessions can extend
 * with timestamp formatting / per-peer dedupe without breaking callers.
 */

class MissedCallLogListener {
public:
	virtual ~MissedCallLogListener() = default;
	virtual void onMissedCallsChanged() = 0;
};

class MissedCallLog {
public:
	struct Entry {
		// Display name (caller). Always NUL-terminated; empty when
		// the caller is anonymous.
		char     name[24 + 1];
		// PhonePixelAvatar seed. 0 means "use the generic missed-call
		// glyph"; >0 reserves the option to render a per-caller avatar
		// in a future expansion of the preview.
		uint8_t  avatarSeed;
		// millis() at the time the call was registered. The lock-screen
		// preview uses (now - addedAtMs) to render a coarse "Xm ago"
		// label without dragging in a full RTC stack.
		uint32_t addedAtMs;
	};

	/** Soft cap on retained entries. Older ones are dropped first. */
	static constexpr uint8_t MaxEntries = 16;

	/** Return the process-wide singleton. */
	static MissedCallLog& instance();

	/**
	 * Register a fresh missed call. `name` is copied into the log's
	 * internal buffer (truncated past 24 chars). nullptr name is
	 * stored as the empty string. Notifies all listeners.
	 *
	 * @return The flat index of the appended entry, or
	 *         entries.size()-1 once the bound has been hit and the
	 *         oldest entry was dropped.
	 */
	uint8_t add(const char* name, uint8_t avatarSeed = 0);

	/** Drop every entry. Notifies listeners only if the log was non-empty. */
	void clear();

	/** Number of entries currently in the log. */
	uint8_t count() const { return (uint8_t) entries.size(); }

	/** True when count() == 0. */
	bool empty() const { return entries.empty(); }

	/**
	 * Latest (most recently appended) entry. Behaviour is undefined
	 * when the log is empty; pair with empty() / count() before calling.
	 */
	const Entry& latest() const { return entries.back(); }

	/** Convenience accessor — returns "" when the log is empty. */
	const char* latestName() const;

	/**
	 * Random-access fetch (oldest at idx=0, newest at idx=count()-1).
	 * UB if `idx >= count()`.
	 */
	const Entry& get(uint8_t idx) const { return entries[idx]; }

	void addListener(MissedCallLogListener* listener);
	void removeListener(MissedCallLogListener* listener);

private:
	MissedCallLog() = default;

	std::vector<Entry>                  entries;
	std::vector<MissedCallLogListener*> listeners;

	void notify();
};

#endif // MAKERPHONE_MISSEDCALLLOG_H
