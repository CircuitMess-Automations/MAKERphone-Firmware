#ifndef MAKERPHONE_PHONEEQUALIZERVISUALISER_H
#define MAKERPHONE_PHONEEQUALIZERVISUALISER_H

#include <Arduino.h>
#include <lvgl.h>
#include "../Interface/LVObject.h"

/**
 * PhoneEqualizerVisualiser
 *
 * S191 — code-only retro equalizer bars that dance to whatever melody the
 * global `Ringtone` engine (S39) is currently pushing through the piezo.
 * Drops into any screen that uses `PhoneRingtoneEngine` for audio so the
 * music player gets a satisfying visual companion to the audio it is
 * already producing — the classic "spectrum analyser" feature-phone vibe
 * without paying for an FFT or any SPIFFS asset cost.
 *
 *     |  |   .  |    |    .  |  |     <- frame N
 *     |  |   |  |    |   .|  |  |     <- frame N+1
 *     |  |  .|  |   .|   ||  |  |     <- frame N+2
 *
 * Implementation notes:
 *  - 100% code-only. `BarCount` plain `lv_obj` rectangles, no SPIFFS, no
 *    `lv_canvas` backing buffer. Same primitive style as `PhoneSignalIcon`,
 *    parented to `obj` so LVGL frees them with the widget.
 *  - The widget owns a per-instance `lv_timer_t*` that ticks every
 *    `TickPeriodMs` and polls `Ringtone.currentFreq()` /
 *    `Ringtone.isPlaying()`. No host wiring required: drop the widget on
 *    a screen, position it, and it animates itself.
 *  - Heights ease toward target heights every tick: bars snap up quickly
 *    (gives the user the feel that "the note made the bar jump") and
 *    fall slowly (gravity / VU-meter feel). When the engine is idle or
 *    in a rest the bars decay smoothly to a 1 px baseline.
 *  - Palette stays consistent with every other Phone* widget — `MP_DIM`
 *    base colour, `MP_HIGHLIGHT` cyan for medium bars, `MP_ACCENT`
 *    sunset orange for the tallest bars. Gradient across the bar's
 *    height tier is the only non-trivial colour rule, kept simple
 *    enough to fit in a single `redrawBars()` switch.
 *  - Uses `LV_OBJ_FLAG_IGNORE_LAYOUT` and a fixed footprint
 *    (`WidgetWidth` x `WidgetHeight`) so it cooperates with parents that
 *    already use a flex / grid layout. Callers position via
 *    `lv_obj_set_pos()` on `getLvObj()`.
 *  - Pinned at 7 bars × 6 px wide + 6 px between-gap × 2 px = 54 px.
 *    Max bar height 14 px, leaving 1 px of breathing room on the
 *    160×128 panel between the music player's progress caption (y≈64)
 *    and its transport icons (y=84).
 */
class PhoneRingtoneEngine;

class PhoneEqualizerVisualiser : public LVObject {
public:
	/**
	 * Build an equalizer visualiser inside `parent`. The widget starts
	 * idle (bars at baseline) and begins animating the moment the
	 * `Ringtone` engine reports `isPlaying() == true`.
	 */
	PhoneEqualizerVisualiser(lv_obj_t* parent);
	virtual ~PhoneEqualizerVisualiser();

	/**
	 * Hint to the visualiser that the parent considers the player
	 * paused — bars chase down to the baseline even if the ringtone
	 * engine briefly reports `isPlaying() == true` mid-stop. Optional;
	 * the widget already polls `Ringtone.isPlaying()` and idles
	 * automatically.
	 */
	void setActive(bool on);

	bool isActive() const { return active; }

	// 7 bars * 6 px + 6 gaps * 2 px = 54 px wide. Tallest bar 14 px.
	static constexpr uint16_t WidgetWidth  = 54;
	static constexpr uint16_t WidgetHeight = 14;
	static constexpr uint8_t  BarCount     = 7;
	static constexpr uint16_t TickPeriodMs = 60;
	static constexpr uint8_t  BarWidth     = 6;
	static constexpr uint8_t  BarGap       = 2;
	static constexpr uint8_t  MinHeight    = 1;

private:
	lv_obj_t*   bars[BarCount];
	lv_timer_t* tickTimer = nullptr;

	uint8_t  heights[BarCount];   // current displayed height (1..WidgetHeight)
	uint8_t  targets[BarCount];   // chase target height
	uint16_t tickCounter = 0;
	bool     active      = true;  // host-visible "should react" hint

	void buildBars();
	void redrawBars();
	void computeTargets();

	static void tickCb(lv_timer_t* timer);
};

#endif // MAKERPHONE_PHONEEQUALIZERVISUALISER_H
