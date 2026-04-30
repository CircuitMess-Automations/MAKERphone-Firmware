#ifndef MAKERPHONE_PHONEBATTERYICON_H
#define MAKERPHONE_PHONEBATTERYICON_H

#include <Arduino.h>
#include <lvgl.h>
#include <Loop/LoopListener.h>
#include "../Interface/LVObject.h"

/**
 * PhoneBatteryIcon
 *
 * Reusable retro pixel battery indicator for MAKERphone 2.0. The intent
 * is to retire the generic `BatteryElement` glyph (which renders four
 * SPIFFS-backed `S:/Battery/<level>.bin` images) in favour of a fully
 * code-only, palette-coherent battery that matches the Sony-Ericsson
 * silhouette every other Phone* widget already uses:
 *
 *      +---------------+-+
 *      | || || || || | | |
 *      +---------------+-+
 *
 *      ^ outline body (1 px MP_TEXT) + 4 fill cells (MP_ACCENT) + tip
 *
 * Implementation notes:
 *  - 100 % code-only - one outline rectangle, four inner cells, one
 *    tip nub. No SPIFFS assets, no canvas backing buffer. Same primitive
 *    style as `PhoneSignalIcon` so the two read as the same family
 *    when sat next to each other in a status bar or info screen.
 *  - Compact 16x9 footprint. 15 px outline body + 1 px tip; 4 fill
 *    cells 2x5 px with 1 px gaps inside the body.
 *  - The widget DOES poll `Battery.getLevel()` automatically (same
 *    cadence as `BatteryElement` and the inline `PhoneStatusBar`
 *    battery glyph) so callers get a drop-in replacement: just
 *    construct it with a parent and forget. Manual override via
 *    `setLevel()` / `setAutoUpdate(false)` is supported for screens
 *    that want to drive the icon from a synthetic value (settings
 *    preview, low-battery modal mock, animated splash, ...).
 *  - Charging animation: when `setCharging(true)` is called, the cells
 *    cycle filling 1 -> 2 -> 3 -> 4 at `ChargeIntervalMs` regardless of
 *    the underlying battery reading. Stopping charge snaps the icon
 *    back to the actual level.
 *  - Low-battery pulse: when level == 0 (and not charging) the single
 *    bottom-most cell flashes between MP_ACCENT and a warning red. The
 *    same timer powers the charge cycle and the pulse, so we only ever
 *    own one `lv_timer_t*` per instance and tear it down cleanly in
 *    the destructor.
 *  - Palette stays consistent with every other Phone* widget. Outline +
 *    tip use `MP_TEXT` warm cream. Active cells use `MP_ACCENT` sunset
 *    orange. Inactive cells use `MP_DIM` muted purple. Low-battery
 *    pulse uses the hard-coded warning red `(255, 60, 60)` already
 *    employed by `PhoneStatusBar::updateBattery`.
 */
class PhoneBatteryIcon : public LVObject, public LoopListener {
public:
	/**
	 * Build a battery icon inside `parent`. Auto-update is on by default,
	 * so the icon reflects `Battery.getLevel()` from the moment it's
	 * mounted with no further wiring required.
	 */
	PhoneBatteryIcon(lv_obj_t* parent);
	virtual ~PhoneBatteryIcon();

	/** LoopListener: polls Battery.getLevel() while auto-update is on. */
	void loop(uint micros) override;

	/**
	 * Show a fixed level (0..4). Disables auto-update so the icon stays
	 * pinned at the manually-applied value until `setAutoUpdate(true)`
	 * is called.
	 */
	void setLevel(uint8_t level);

	/**
	 * Re-enable auto-update from the BatteryService. Called implicitly
	 * by the constructor.
	 */
	void setAutoUpdate(bool on);

	/**
	 * Toggle the charging cycle animation. While charging, the cells
	 * sweep 1 -> 4 at `ChargeIntervalMs` and ignore both the manual
	 * level and the underlying Battery reading. Stopping charge restores
	 * the most recently applied level.
	 */
	void setCharging(bool on);

	uint8_t getLevel()      const { return level; }
	bool    isCharging()    const { return charging; }
	bool    isAutoUpdate()  const { return autoUpdate; }

	// Footprint constants - kept public so screens can lay this widget
	// out in tight spaces (status bars, modals) without reaching into
	// the implementation file.
	static constexpr uint16_t IconWidth        = 16; // body 15 + tip 1
	static constexpr uint16_t IconHeight       = 9;
	static constexpr uint8_t  CellCount        = 4;

	// How fast the charging cycle steps. 320 ms gives the same calm
	// "filling" cadence early-2000s phones used.
	static constexpr uint16_t ChargeIntervalMs = 320;
	// Low-battery pulse cadence (also drives the timer when level == 0).
	static constexpr uint16_t PulseIntervalMs  = 480;

private:
	lv_obj_t*   outline = nullptr;
	lv_obj_t*   tip     = nullptr;
	lv_obj_t*   cells[CellCount];
	lv_timer_t* animTimer = nullptr;

	uint8_t  level      = 4;        // last applied level (0..4)
	uint8_t  animStep   = 0;        // 0..3 charge step, or 0/1 pulse step
	bool     charging   = false;
	bool     autoUpdate = true;
	bool     pulseOn    = false;    // is the low-battery pulse running?

	void buildOutline();
	void buildTip();
	void buildCells();
	void redrawCells(uint8_t activeCells, bool warning);

	void evaluateAnimation();           // start / stop / repurpose the timer
	void startTimer(uint16_t intervalMs);
	void stopTimer();
	static void animTimerCb(lv_timer_t* timer);
};

#endif //MAKERPHONE_PHONEBATTERYICON_H
