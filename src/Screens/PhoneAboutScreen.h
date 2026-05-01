#ifndef MAKERPHONE_PHONEABOUTSCREEN_H
#define MAKERPHONE_PHONEABOUTSCREEN_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneAboutScreen
 *
 * Phase-J sub-screen (S55): the final settings sub-page reachable from
 * PhoneSettingsScreen (S50). Replaces the ABOUT placeholder stub with
 * a read-only diagnostics page that shows the device id (efuse MAC),
 * firmware version, free heap, uptime, and paired peer count -- the
 * five facts a user (or a developer triaging a field bug) actually
 * wants to copy off the screen.
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |               ABOUT                    | <- pixelbasic7 cyan caption
 *   |                                        |
 *   |  DEVICE ID                             | <- pixelbasic7 dim label
 *   |  ABCD1234EF56                          | <- pixelbasic7 cream value
 *   |  FIRMWARE                              |
 *   |  MAKERphone v0.55                      |
 *   |  FREE HEAP / UPTIME                    |
 *   |  142 KB        00:12:34                |
 *   |  PEERS                                 |
 *   |  3 paired                              |
 *   |                                        |
 *   |                                 BACK   | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * The page is intentionally text-only -- there is nothing the user can
 * adjust here, so the screen has no SAVE softkey and no cursor: BACK
 * is the only meaningful action. Free heap and uptime are live-updated
 * via an lv_timer (1 s tick) so a user watching the screen sees memory
 * fluctuate and the uptime tick forward in real time, which is the
 * standard "is this thing alive?" diagnostic affordance.
 *
 * Behavior:
 *  - BTN_BACK / BTN_ENTER pop back to PhoneSettingsScreen. ENTER is
 *    accepted as a friendly second way out so a user who landed on the
 *    page from the chevron-row "OPEN" affordance does not have to
 *    hunt for the BACK key. There is no other input.
 *  - The 1 Hz lv_timer refreshes the heap + uptime labels in place;
 *    the device id, firmware string, and peer count are static for
 *    the screen's lifetime and are written once on construction.
 *
 * Implementation notes:
 *  - Code-only, zero SPIFFS. Reuses PhoneSynthwaveBg / PhoneStatusBar /
 *    PhoneSoftKeyBar so the screen feels visually part of the rest of
 *    the MAKERphone family.
 *  - Device id is the ESP32 efuse MAC (as used by every other peer-
 *    aware screen in this codebase -- see PhoneContactsScreen,
 *    ConvoScreen, PairScreen). Rendered as a 12-hex-digit uppercase
 *    string with no separators so it fits in the 152 px label column.
 *  - Firmware version is a single inlined kFirmwareVersion string. It
 *    intentionally lives in this header so a future commit (e.g. S70's
 *    v1.0 changelog) can bump it in one place; there is no project-
 *    wide version macro yet.
 *  - Peer count is `Storage.Friends.all().size() - 1` so we strip the
 *    self-record (the device's own efuse MAC, always the first entry
 *    in Friends.all()) the same way InboxScreen does. A negative or
 *    underflow result clamps to 0.
 *  - Free heap is `ESP.getFreeHeap()` printed in KB (rounded down)
 *    so the readout fits in 6 chars even when the heap is full.
 *  - Uptime is `millis() / 1000` rendered as `HH:MM:SS` capped at
 *    99:59:59 so the label width stays deterministic on devices that
 *    have been up for days. millis() rolls over after ~49 days but
 *    that is well past any practical use of this page.
 *  - 160x128 budget: 10 px status bar (y = 0..10), 10 px caption
 *    strip (y = 12..20), four label-pair groups (label = pixelbasic7
 *    dim, value = pixelbasic7 cream) starting at y = 24 with a 9 px
 *    label row + 9 px value row + 2 px gap = 20 px per group. Four
 *    groups fit cleanly inside the 96 px visible content area without
 *    scrolling. The "FREE HEAP / UPTIME" group splits its value row
 *    into a left/right pair so the two live-updating fields can sit
 *    side-by-side without burning a row each.
 */
class PhoneAboutScreen : public LVScreen, private InputListener {
public:
	PhoneAboutScreen();
	virtual ~PhoneAboutScreen() override;

	void onStart() override;
	void onStop() override;

	/** Firmware version string baked into this build. */
	static constexpr const char* kFirmwareVersion = "MAKERphone v0.55";

	/** Live-update tick period (ms) for the heap + uptime fields. */
	static constexpr uint32_t kRefreshPeriodMs = 1000;

	/**
	 * Format the device id (efuse MAC, 48 bits) into a 12-hex-digit
	 * uppercase string with no separators. Static so unit tests / hosts
	 * can introspect the formatting without standing up the screen.
	 *
	 * @param mac    Efuse MAC value.
	 * @param out    Destination buffer, must be at least 13 bytes.
	 * @param outLen Capacity of `out`.
	 */
	static void formatDeviceId(uint64_t mac, char* out, size_t outLen);

	/**
	 * Format an uptime in seconds into a fixed-width HH:MM:SS string.
	 * Cap is 99:59:59. Same out/outLen contract as formatDeviceId.
	 */
	static void formatUptime(uint32_t seconds, char* out, size_t outLen);

	/**
	 * Format a free-heap value in bytes into a "NNN KB" string. Rounds
	 * down so the readout never overestimates available memory.
	 */
	static void formatHeapKB(uint32_t bytes, char* out, size_t outLen);

	/**
	 * Count the paired peers as displayed on the About screen --
	 * `Storage.Friends.all().size() - 1` clamped to 0. Exposed so a
	 * host (e.g. tests) can sanity-check the peer-count calculation
	 * without standing up the screen.
	 */
	static uint16_t computePeerCount();

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* captionLabel;

	// Group 1: DEVICE ID (static -- never changes after construction).
	lv_obj_t* deviceIdLabel;
	lv_obj_t* deviceIdValue;

	// Group 2: FIRMWARE (static).
	lv_obj_t* firmwareLabel;
	lv_obj_t* firmwareValue;

	// Group 3: FREE HEAP / UPTIME (both live, refreshed via tickTimer).
	lv_obj_t* runtimeLabel;
	lv_obj_t* heapValue;     // left half of the runtime value row
	lv_obj_t* uptimeValue;   // right half of the runtime value row

	// Group 4: PEERS (static -- recomputed only at construction; the
	// peer set changes rarely enough that a 1 Hz refresh would be
	// wasted work and Storage.Friends.all() is a non-trivial scan).
	lv_obj_t* peersLabel;
	lv_obj_t* peersValue;

	lv_timer_t* tickTimer;

	void buildCaption();
	void buildBody();
	void refreshLiveFields();

	static void onTickTimer(lv_timer_t* timer);

	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONEABOUTSCREEN_H
