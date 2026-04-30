#ifndef MAKERPHONE_PHONECLOCKFACE_H
#define MAKERPHONE_PHONECLOCKFACE_H

#include <Arduino.h>
#include <lvgl.h>
#include <Loop/LoopListener.h>
#include "../Interface/LVObject.h"

/**
 * PhoneClockFace
 *
 * Reusable retro feature-phone clock face (160x46) intended to sit just below
 * PhoneStatusBar on any screen that wants a homescreen-style centerpiece.
 * It is the third foundational MAKERphone 2.0 widget after PhoneStatusBar and
 * PhoneSoftKeyBar:
 *
 *      ##  ##   ##  ##
 *      ##  ##   ##  ##         <- big HH:MM in pixelbasic16
 *      ##  ##   ##  ##
 *
 *           THU 30                <- day-of-week + day-of-month (pixelbasic7)
 *           APR 2026              <- month + year                (pixelbasic7)
 *
 * Implementation notes:
 *  - Code-only (no SPIFFS assets) so it adds zero data partition cost.
 *  - Uses a LoopListener to refresh the digits every minute and the date
 *    every day. millis() is the source of truth until a real RTC is wired
 *    in - at that point only updateClock()/updateDate() need to change.
 *  - Anchored with LV_OBJ_FLAG_IGNORE_LAYOUT so it cooperates with parents
 *    that already use flex/grid layouts (same pattern as the other phone
 *    widgets).
 *  - The default y offset places it directly under a 10 px PhoneStatusBar
 *    on the 128 px display; setY() lets a screen nudge it if desired.
 *  - The colon between HH and MM blinks once per second to give the face
 *    a subtle "live" feel, mirroring real Sony-Ericsson handsets.
 */
class PhoneClockFace : public LVObject, public LoopListener {
public:
	PhoneClockFace(lv_obj_t* parent);
	virtual ~PhoneClockFace();

	void loop(uint micros) override;

	/** Force an immediate redraw (useful on screen entry). */
	void refresh();

	static constexpr uint16_t FaceWidth  = 160;
	static constexpr uint16_t FaceHeight = 32;

private:
	lv_obj_t* clockLabel;   // "HH MM" - colon is a separate label so it can blink
	lv_obj_t* colonLabel;
	lv_obj_t* dowLabel;     // day-of-week + day-of-month (e.g. "THU 30")
	lv_obj_t* monthLabel;   // month + year             (e.g. "APR 2026")

	uint16_t lastMin     = 0xFFFF;  // last drawn HH*60+MM
	uint32_t lastDayKey  = 0xFFFFFFFFu; // last drawn day key (year*512 + ordinal)
	bool     colonOn     = true;
	uint32_t lastBlinkMs = 0;

	void buildLabels();
	void updateClock();
	void updateDate();
	void updateColon();
};

#endif //MAKERPHONE_PHONECLOCKFACE_H
