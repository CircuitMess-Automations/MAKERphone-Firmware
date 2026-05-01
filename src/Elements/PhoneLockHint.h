#ifndef MAKERPHONE_PHONELOCKHINT_H
#define MAKERPHONE_PHONELOCKHINT_H

#include <Arduino.h>
#include <lvgl.h>
#include <Loop/LoopListener.h>
#include "../Interface/LVObject.h"

/**
 * PhoneLockHint
 *
 * Code-only "SLIDE TO UNLOCK" hint for the MAKERphone lock screen.
 *
 *      SLIDE TO UNLOCK   >  >  >
 *      pixelbasic7 cream         cyan chevrons that sweep L->R
 *
 * Designed to sit between the unread-message area and the existing
 * `UnlockSlide` / soft-key bar so the lock screen reads as a true
 * Sony-Ericsson-style feature phone instead of a debug stub. The
 * widget is purely visual - it does not consume any input - so the
 * underlying unlock mechanism (currently `UnlockSlide` listening for
 * BTN_R hold; replaced by a Left->Right chord in S48) is unaffected.
 *
 * Implementation notes:
 *  - 100 % code-only (no SPIFFS): one transparent 160x11 px slab with
 *    a label on the left and three `>` chevron labels on the right.
 *  - Chevron sweep is a 3-step opacity rotation driven from `loop()`
 *    at ~3.5 Hz. Each chevron is exposed as a member so S48 can attach
 *    a shimmer overlay without touching this widget's geometry.
 *  - Anchored with `LV_OBJ_FLAG_IGNORE_LAYOUT` so the host screen can
 *    keep its flex/column layout for unread-message rows untouched.
 *  - Palette stays in sync with the rest of the Phone* family
 *    (`MP_TEXT` cream caption, `MP_HIGHLIGHT` cyan chevrons).
 */
class PhoneLockHint : public LVObject, public LoopListener {
public:
	explicit PhoneLockHint(lv_obj_t* parent);
	virtual ~PhoneLockHint();

	void loop(uint micros) override;

	/** Pause / resume the chevron sweep without removing the widget. */
	void setActive(bool active);

	static constexpr uint16_t HintWidth   = 160;
	static constexpr uint16_t HintHeight  = 11;
	static constexpr uint8_t  ChevronN    = 3;

	/** Step interval of the L->R chevron sweep, in milliseconds. */
	static constexpr uint32_t StepMs      = 280;

private:
	lv_obj_t* caption;
	lv_obj_t* chevrons[ChevronN];

	uint32_t lastStepMs = 0;
	uint8_t  activeIdx  = 0;     // index of the currently-bright chevron
	bool     running    = true;  // controls whether loop() advances the sweep

	void buildLabels();
	void redrawChevrons();
};

#endif //MAKERPHONE_PHONELOCKHINT_H
