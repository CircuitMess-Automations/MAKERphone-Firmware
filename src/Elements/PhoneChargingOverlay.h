#ifndef MAKERPHONE_PHONECHARGINGOVERLAY_H
#define MAKERPHONE_PHONECHARGINGOVERLAY_H

#include <Arduino.h>
#include <lvgl.h>
#include <Loop/LoopListener.h>
#include "../Interface/LVObject.h"

/**
 * PhoneChargingOverlay
 *
 * S59 — code-only "charging" cue that overlays the lock screen and
 * homescreen while the battery is detected to be charging. The
 * Sony-Ericsson-era inspiration is the little brick-shaped chip that
 * appeared near the clock with a pulsing bolt and a percentage:
 *
 *      +--------------------------+
 *      |  /\  Charging   87%      |
 *      |  \/                      |
 *      +--------------------------+
 *
 *      ^ angular bolt cycling ACCENT <-> HIGHLIGHT every ~320 ms
 *        + bg pill in MP_DIM with MP_HIGHLIGHT 1 px border.
 *
 * Implementation notes:
 *  - 100 % code-only - one rounded background slab, four small
 *    rectangles for the bolt, two pixelbasic7 labels. No SPIFFS
 *    assets, no canvas backing buffer. Same primitive pattern as
 *    PhoneBatteryIcon / PhoneSignalIcon so the family reads coherent.
 *  - Compact 96 x 13 footprint. Hosts position it explicitly on
 *    their layout (LockScreen anchors above the lock hint, the
 *    homescreen drops it just above the soft-key bar).
 *  - Hidden by default. Visibility flips through `setCharging(bool)`.
 *    The chip simply hides / shows so the change is always observable
 *    even when LVGL's animation thread is starved.
 *  - Optional auto-detect: when `setAutoDetect(true)` is on the widget
 *    polls `Battery.getVoltage()` once per second and considers the
 *    device to be charging when the voltage trend across an 8-sample
 *    ring buffer rises by > +AutoDetectMv mV. Trend going flat or
 *    negative for at least `AutoDetectStopMs` ms turns the chip off
 *    again. The heuristic intentionally errs on the side of NOT
 *    showing the overlay - false positives are louder than false
 *    negatives here.
 *  - Hosts may also drive the chip explicitly (`setCharging(true)`)
 *    e.g. from a USB-detect signal in the future. Both modes coexist.
 *  - Palette stays identical with every other Phone* widget
 *    (`MP_BG_DARK`, `MP_ACCENT`, `MP_HIGHLIGHT`, `MP_DIM`, `MP_TEXT`).
 */
class PhoneChargingOverlay : public LVObject, public LoopListener {
public:
	explicit PhoneChargingOverlay(lv_obj_t* parent);
	virtual ~PhoneChargingOverlay();

	void loop(uint micros) override;

	/**
	 * Show / hide the chip. While shown the bolt animates and the
	 * percentage label is live-updated from `Battery.getPercentage()`
	 * once per second. While hidden the chip is fully invisible
	 * (LV_OBJ_FLAG_HIDDEN) and consumes no draw work. Disables the
	 * auto-detect overrides for the next AutoDetectStopMs window so
	 * a manual `setCharging(false)` is not immediately re-overridden.
	 */
	void setCharging(bool on);

	/**
	 * Enable / disable the voltage-trend heuristic. When on, the
	 * widget owns its visibility based on the trend - explicit
	 * `setCharging` calls are still honoured but the heuristic will
	 * eventually re-evaluate. Default off so the host opts in.
	 */
	void setAutoDetect(bool on);

	bool isCharging()  const { return charging; }
	bool isAutoDetect() const { return autoDetect; }

	// Footprint constants - kept public so screens can lay this widget
	// out without reaching into the implementation file.
	static constexpr uint16_t ChipWidth   = 96;
	static constexpr uint16_t ChipHeight  = 13;

	// Bolt color cycle period (ACCENT <-> HIGHLIGHT), in milliseconds.
	static constexpr uint16_t BoltStepMs        = 320;
	// Percentage label refresh cadence, in milliseconds.
	static constexpr uint16_t PercentRefreshMs  = 1000;
	// Voltage trend sampling cadence, in milliseconds.
	static constexpr uint16_t TrendSampleMs     = 1000;
	// Number of trend samples kept in the ring buffer.
	static constexpr uint8_t  TrendSamples      = 8;
	// Voltage rise (mV) over the trend window required to declare
	// "charging" via the auto-detect heuristic.
	static constexpr int16_t  AutoDetectMv      = 30;
	// How long the trend has to be flat / negative before the
	// auto-detect path turns the chip back off, in milliseconds.
	static constexpr uint16_t AutoDetectStopMs  = 4000;
	// Manual override grace period after `setCharging(false)` during
	// which the auto-detect heuristic is suppressed, in milliseconds.
	static constexpr uint16_t ManualGuardMs     = 6000;

private:
	// Visual children of the chip.
	lv_obj_t* pill         = nullptr;
	lv_obj_t* boltSegments[3];   // three small rects forming an angular bolt
	lv_obj_t* boltOutline[3];    // dim shadow rects sitting under the segments
	lv_obj_t* labelTitle   = nullptr;
	lv_obj_t* labelPercent = nullptr;

	bool     charging   = false;
	bool     autoDetect = false;
	bool     boltBright = false;

	uint32_t lastBoltMs    = 0;
	uint32_t lastPercentMs = 0;
	uint32_t lastSampleMs  = 0;
	uint32_t lastRiseMs    = 0;
	uint32_t manualGuardUntil = 0;

	uint16_t trendBuf[TrendSamples];
	uint8_t  trendCount = 0;
	uint8_t  trendHead  = 0;

	uint8_t  lastPercent = 0xFF;

	void buildPill();
	void buildBolt();
	void buildLabels();

	void redrawBolt();
	void refreshPercentLabel();

	void applyVisibility(bool show);

	void sampleTrend(uint32_t nowMs);
	bool evaluateTrend() const; // true => looks like charging
};

#endif //MAKERPHONE_PHONECHARGINGOVERLAY_H
