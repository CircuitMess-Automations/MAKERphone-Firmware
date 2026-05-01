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
 * underlying unlock mechanism (BTN_R hold or the new S48 Left->Right
 * input chord) is unaffected.
 *
 * Implementation notes:
 *  - 100 % code-only (no SPIFFS): one transparent 160x11 px slab with
 *    a label on the left and three `>` chevron labels on the right.
 *  - Chevron sweep is a multi-step opacity rotation driven from
 *    `loop()`. In idle it ticks at ~3.5 Hz; in S48 "boost" mode (the
 *    chord has been armed) it ticks ~3x faster, all chevrons brighten
 *    to cyan together, and the caption flips to the chord prompt.
 *  - A 4-phase opacity shimmer is also driven from `loop()` so the
 *    caption pulses gently in idle and strobes hard in boost - the
 *    moving highlight that S48 calls "hint shimmer".
 *  - Anchored with `LV_OBJ_FLAG_IGNORE_LAYOUT` so the host screen can
 *    keep its flex/column layout for unread-message rows untouched.
 *  - Palette stays in sync with the rest of the Phone* family
 *    (`MP_TEXT` cream caption, `MP_HIGHLIGHT` cyan chevrons,
 *    `MP_ACCENT` sunset orange when boosted).
 */
class PhoneLockHint : public LVObject, public LoopListener {
public:
	explicit PhoneLockHint(lv_obj_t* parent);
	virtual ~PhoneLockHint();

	void loop(uint micros) override;

	/** Pause / resume the chevron sweep without removing the widget. */
	void setActive(bool active);

	/**
	 * S48: enter / leave the "chord armed" boost state. While boosted the
	 * caption changes to the next-step prompt, all chevrons brighten and
	 * strobe together, and the shimmer runs faster. Toggling boost off
	 * restores the idle "SLIDE TO UNLOCK" sweep.
	 */
	void setBoost(bool boost);

	static constexpr uint16_t HintWidth   = 160;
	static constexpr uint16_t HintHeight  = 11;
	static constexpr uint8_t  ChevronN    = 3;

	/** Step interval of the L->R chevron sweep, in milliseconds. */
	static constexpr uint32_t StepMs        = 280;
	/** Faster chevron sweep used while the chord is armed (S48). */
	static constexpr uint32_t BoostStepMs   = 90;
	/** Caption / chevron shimmer phase advance interval, in milliseconds. */
	static constexpr uint32_t ShimmerStepMs = 110;

private:
	lv_obj_t* caption;
	lv_obj_t* chevrons[ChevronN];

	uint32_t lastStepMs    = 0;
	uint32_t lastShimmerMs = 0;
	uint8_t  activeIdx     = 0;     // index of the currently-bright chevron
	uint8_t  shimmerPhase  = 0;     // 0..3 - drives the caption opacity wave
	bool     running       = true;  // controls whether loop() advances the sweep
	bool     boost         = false; // S48 chord-armed shimmer mode

	void buildLabels();
	void redrawChevrons();
	void redrawCaption();
};

#endif //MAKERPHONE_PHONELOCKHINT_H
