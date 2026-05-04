#ifndef MAKERPHONE_PHONECHARGEBARS_H
#define MAKERPHONE_PHONECHARGEBARS_H

#include <Arduino.h>
#include <lvgl.h>
#include <Loop/LoopListener.h>
#include "../Interface/LVObject.h"

class PhoneChargingOverlay;

/**
 * PhoneChargeBars
 *
 * S155 — wide animated battery-charge fill bars used on the lock screen
 * and the homescreen. Distinct from `PhoneBatteryIcon` (the 16x9 status-
 * bar glyph) and from `PhoneChargingOverlay` (the 96x13 rounded chip
 * carrying a bolt + percentage caption). The classic Sony-Ericsson
 * inspiration is the wide, thin "tank" that fills bar-by-bar across
 * the lock + idle screens whenever the device is plugged in:
 *
 *      +----------------------------------------+
 *      |  ##  ##  ##  ##  ##  ..  ..  ..  ..  ..|
 *      +----------------------------------------+ +
 *
 *      ^ thin 1 px frame in MP_TEXT, 10 vertical fill cells inside,
 *        + a 1 px tip nub on the right (same silhouette as the smaller
 *        PhoneBatteryIcon). The active cells sweep 1 -> 10 then loop
 *        while charging, giving the unmistakable "filling up" cue.
 *
 * Implementation notes:
 *  - 100 % code-only - one outline rectangle, ten inner cells, one
 *    tip nub. No SPIFFS assets, no canvas backing buffer. Same
 *    primitive style every other Phone* widget (`PhoneSignalIcon`,
 *    `PhoneBatteryIcon`, `PhoneChargingOverlay`) shares.
 *  - Compact 64 x 9 footprint (63 px body + 1 px tip). Hosts position
 *    it explicitly with `lv_obj_set_pos` / `lv_obj_set_align` after
 *    construction.
 *  - Hidden by default. Hosts may flip the visibility manually via
 *    `setCharging(bool)`, or pass a `PhoneChargingOverlay*` source on
 *    construction — in source-bound mode the bars become a
 *    `LoopListener` that mirrors `source->isCharging()` each tick, so
 *    the lock + home charging chip and the wide fill bars share a
 *    single auto-detect heuristic without the host having to wire
 *    anything else.
 *  - Animation: while `isCharging()` is true, the bars sweep
 *    1 -> 2 -> ... -> CellCount -> 1 -> ... at `SweepIntervalMs`. While
 *    NOT charging the entire widget hides (LV_OBJ_FLAG_HIDDEN) and
 *    consumes no draw work.
 *  - Palette: outline + tip in `MP_TEXT` warm cream (matches
 *    `PhoneBatteryIcon`), active cells in `MP_ACCENT` sunset orange
 *    (matches the chip's bolt), inactive cells in `MP_DIM` muted
 *    purple. Constants stay duplicated rather than centralised so the
 *    widget reads as one of the family without a fragile cross-include.
 */
class PhoneChargeBars : public LVObject, public LoopListener {
public:
	/**
	 * Build the wide charge-bars strip inside `parent`. When `source`
	 * is non-null the bars subscribe to LoopManager and mirror
	 * `source->isCharging()` on every tick — the typical wiring for a
	 * lock/home screen that already owns a `PhoneChargingOverlay`.
	 * Pass nullptr for a fully-manual mode where the host drives
	 * visibility via `setCharging()` itself.
	 */
	explicit PhoneChargeBars(lv_obj_t* parent, PhoneChargingOverlay* source = nullptr);
	virtual ~PhoneChargeBars();

	/** LoopListener: only registered when a source was provided. */
	void loop(uint micros) override;

	/**
	 * Show / hide the bars. While shown the cells sweep on
	 * `SweepIntervalMs`; while hidden the widget is fully invisible
	 * (LV_OBJ_FLAG_HIDDEN) and the sweep timer is torn down so an
	 * idle screen pays no LVGL cost. No-op when the requested state
	 * already matches the current state.
	 */
	void setCharging(bool on);

	/**
	 * Re-bind the source whose `isCharging()` is mirrored each tick.
	 * Pass nullptr to detach (the widget keeps whatever charging state
	 * was last applied via `setCharging()`).
	 */
	void setSource(PhoneChargingOverlay* src);

	bool isCharging() const { return charging; }

	// Footprint constants - kept public so screens can lay this widget
	// out without reaching into the implementation file.
	static constexpr uint16_t Width      = 64; // body 63 + tip 1
	static constexpr uint16_t Height     = 9;
	static constexpr uint8_t  CellCount  = 10;

	// Sweep cadence — 260 ms gives the same calm "filling up" feel
	// the smaller `PhoneBatteryIcon` charging cycle uses (320 ms over
	// 4 cells), scaled down so the wider 10-cell sweep finishes a
	// full traverse in roughly the same wall-clock window.
	static constexpr uint16_t SweepIntervalMs = 260;

private:
	lv_obj_t*   outline   = nullptr;
	lv_obj_t*   tip       = nullptr;
	lv_obj_t*   cells[CellCount];
	lv_timer_t* animTimer = nullptr;

	PhoneChargingOverlay* source = nullptr;
	bool                  loopRegistered = false;

	bool    charging = false;
	uint8_t step     = 0; // current sweep position, 0..CellCount-1

	void buildOutline();
	void buildTip();
	void buildCells();

	void redrawCells(uint8_t activeCells);
	void applyVisibility(bool show);

	void startTimer();
	void stopTimer();
	static void animTimerCb(lv_timer_t* timer);
};

#endif //MAKERPHONE_PHONECHARGEBARS_H
