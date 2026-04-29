#ifndef MAKERPHONE_PHONESTATUSBAR_H
#define MAKERPHONE_PHONESTATUSBAR_H

#include <Arduino.h>
#include <lvgl.h>
#include <Loop/LoopListener.h>
#include "../Interface/LVObject.h"

/**
 * PhoneStatusBar
 *
 * Reusable retro feature-phone status bar (160x10) intended to sit at the top
 * of every MAKERphone screen. It is the visual anchor of the MAKERphone 2.0 UI:
 *
 *   [|||| ]               12:34                       [###  ]
 *    signal               clock                        battery
 *
 * Implementation notes:
 *  - Code-only (no SPIFFS assets) so it adds zero data partition cost.
 *  - Signal bars are drawn with plain LVGL objects so the bar count can be
 *    re-bound to LoRa RSSI later without touching layout.
 *  - The clock counts uptime in HH:MM (Chatter has no RTC); when an RTC/
 *    sync source is added, only updateClock() needs to change.
 *  - Battery level polled from the existing BatteryService, identical
 *    cadence to BatteryElement so the two stay visually consistent.
 *  - Add this to any LVScreen by simply constructing it with the screen's
 *    obj as parent. It uses LV_OBJ_FLAG_IGNORE_LAYOUT so it cooperates with
 *    parents that already use flex/grid layouts.
 */
class PhoneStatusBar : public LVObject, public LoopListener {
public:
	PhoneStatusBar(lv_obj_t* parent);
	virtual ~PhoneStatusBar();

	void loop(uint micros) override;

	/** Set signal strength bars manually (0..4). */
	void setSignal(uint8_t bars);

	static constexpr uint16_t BarHeight = 10;
	static constexpr uint8_t SignalBarCount = 4;

private:
	lv_obj_t* signalBars[SignalBarCount];
	lv_obj_t* clockLabel;
	lv_obj_t* battOutline;
	lv_obj_t* battFill;
	lv_obj_t* battTip;

	uint8_t  level    = 255;        // last battery level seen (0..4)
	uint8_t  signal   = 4;          // last drawn signal bars
	uint16_t lastMin  = 0xFFFF;     // last drawn HH*60+MM

	void buildSignal();
	void buildClock();
	void buildBattery();

	void updateBattery();
	void updateClock();
	void redrawSignal();
};

#endif //MAKERPHONE_PHONESTATUSBAR_H
