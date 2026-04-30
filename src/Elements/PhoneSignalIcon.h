#ifndef MAKERPHONE_PHONESIGNALICON_H
#define MAKERPHONE_PHONESIGNALICON_H

#include <Arduino.h>
#include <lvgl.h>
#include "../Interface/LVObject.h"

/**
 * PhoneSignalIcon
 *
 * Reusable retro feature-phone signal indicator for MAKERphone 2.0. The
 * widget is the "self-contained, animated" companion to the static signal
 * bars already drawn inside `PhoneStatusBar` - whenever a screen wants the
 * signal indicator outside of the status bar (the dialer's "no service"
 * banner, the Pair / Call / Settings status hints, the future signal
 * meter on `PhoneAboutScreen`) it can drop a `PhoneSignalIcon` in and get
 * the same look plus the scanning animation:
 *
 *     [|      ]   [||     ]   [|||    ]   [||||   ]
 *
 *      ^ scanning state cycles 1 -> 2 -> 3 -> 4 bars at ScanIntervalMs,
 *      then snaps back to 1 and continues. While paired/locked the bars
 *      stay solid at the level last passed to `setLevel()`.
 *
 * Implementation notes:
 *  - 100% code-only - 4 plain `lv_obj` rectangles, no SPIFFS assets and no
 *    canvas backing buffer. Same primitive style as the signal cluster
 *    inside `PhoneStatusBar`, kept here as a separate atom so screens can
 *    embed the same indicator without depending on the full status bar.
 *  - Uses `LV_OBJ_FLAG_IGNORE_LAYOUT` and a fixed footprint
 *    (`IconWidth` x `IconHeight`) so it cooperates with parents that
 *    already use a flex / grid layout. Callers position it explicitly via
 *    `lv_obj_set_pos()` or `lv_obj_set_align()` on `getLvObj()`.
 *  - The widget intentionally does NOT poll a hardware service - the
 *    concrete LoRa link-strength wiring lands in Phase D / Phase E, when
 *    the phone screens that consume signal arrive. For now we expose a
 *    deterministic API:
 *      - `setLevel(0..4)` sets a fixed bar count, freezes any scan,
 *      - `setScanning(true)` runs the cycle animation,
 *      - `setRSSI(dBm)`     is the convenience used once LoRa wires it up.
 *  - Palette stays consistent with every other Phone* widget: active bars
 *    use `MP_HIGHLIGHT` cyan, inactive bars use `MP_DIM` muted purple. The
 *    "no signal" state (level 0, not scanning) draws a single 1px
 *    `MP_ACCENT` slash through the icon so the user can tell it apart
 *    from a still-mounting widget at a glance.
 *  - The scanning timer is owned per-instance and torn down in the
 *    destructor, so creating a `PhoneSignalIcon` and immediately deleting
 *    its parent screen never leaves a stale `lv_timer_t*` callback
 *    pointing into freed memory.
 */
class PhoneSignalIcon : public LVObject {
public:
	/**
	 * Build a signal icon inside `parent`. By default the widget starts
	 * in the scanning animation - which is the state every "fresh" boot
	 * sees before LoRa has paired with anything.
	 */
	PhoneSignalIcon(lv_obj_t* parent);
	virtual ~PhoneSignalIcon();

	/**
	 * Show a fixed bar count (0..4). Stops the scanning animation if it
	 * was running. Calling `setLevel(0)` shows the "no signal" slash.
	 */
	void setLevel(uint8_t bars);

	/**
	 * Enable or disable the scanning animation. While scanning the
	 * widget cycles 1 -> 4 bars at `ScanIntervalMs` and ignores any
	 * level previously passed to `setLevel()` until scanning stops.
	 */
	void setScanning(bool on);

	/**
	 * Convenience: convert a dBm RSSI reading to a 0..4 bar count and
	 * apply it via `setLevel()`. Thresholds are tuned for LLCC68 LoRa,
	 * which is what the Chatter radio reports - see implementation for
	 * the exact dBm cutoffs.
	 */
	void setRSSI(int rssi);

	uint8_t getLevel()    const { return level; }
	bool    isScanning()  const { return scanning; }

	// Compact 4-bar footprint. 4 bars * 2 px wide + 3 * 1 px gap = 11 px.
	// Tallest bar 9 px, leaving 1 px below for the "no signal" slash.
	static constexpr uint16_t IconWidth        = 11;
	static constexpr uint16_t IconHeight       = 10;
	static constexpr uint8_t  BarCount         = 4;
	// How fast the scan cycle steps. 220 ms feels like classic
	// "searching for service" cadence on early-2000s feature phones.
	static constexpr uint16_t ScanIntervalMs   = 220;

private:
	lv_obj_t*   bars[BarCount];
	lv_obj_t*   slash    = nullptr;       // "no signal" diagonal
	lv_timer_t* scanTimer = nullptr;

	uint8_t  level     = 4;               // last applied level (0..4)
	uint8_t  scanStep  = 0;                // current step inside the scan cycle
	bool     scanning  = false;

	void buildBars();
	void buildSlash();
	void redrawBars(uint8_t activeBars);

	void startScan();
	void stopScan();
	static void scanTimerCb(lv_timer_t* timer);
};

#endif //MAKERPHONE_PHONESIGNALICON_H
