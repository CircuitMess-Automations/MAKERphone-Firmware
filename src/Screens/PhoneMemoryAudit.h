#ifndef MAKERPHONE_PHONEMEMORYAUDIT_H
#define MAKERPHONE_PHONEMEMORYAUDIT_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneMemoryAudit
 *
 * Phase-V launch (S197): a developer-facing memory-leak audit that
 * cycles a representative set of Phone* widget compositions through
 * construct/destruct kTotalIterations times and reports the resulting
 * heap delta on screen + over Serial. Reachable from the dialer via
 * the service code `*#1971#` (S197 keypad-mnemonic), so the page
 * lives outside the user-facing menu hierarchy and never lures a
 * non-developer into running it accidentally.
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |             MEM AUDIT                  | <- pixelbasic7 cyan caption
 *   |  BEFORE: 142336 B                      | <- pixelbasic7 dim/cream pair
 *   |  NOW:    142336 B                      |
 *   |  DELTA:  +0 B                          |
 *   |  ITER:   0 / 1000                      |
 *   |  CYCLE:  PhoneSynthwaveBg              | <- which audit kind ran last
 *   |  STATUS: RUNNING                       | <- RUNNING / PAUSED / DONE PASS
 *   |  PAUSE                          BACK   | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * Why no actual `push()` / `pop()` cycling
 *  - LVGL's `lv_scr_load_anim()` runs a 500 ms transition; pushing /
 *    popping a real screen 1000 times therefore takes 500+ seconds and
 *    blocks every other timer the firmware needs (input polling, the
 *    ringtone engine, the LoRa service). That makes a "push every
 *    screen 1000x" test impractical to run on-device in one sitting.
 *  - What actually leaks (or stays flat) is the
 *    `new ...Screen()` / `delete this` lifecycle: the LVScreen base
 *    ctor allocates an `lv_obj_t*` and an `lv_group_t*`, the screen's
 *    own ctor allocates whatever child widgets the layout needs, and
 *    `~LVScreen` cascades the delete via the LVGL DELETE event. That
 *    is exactly what we audit here, repeatedly.
 *  - Each iteration constructs a transient host container as a child
 *    of this audit screen's `obj`, attaches the heaviest Phone*
 *    widgets (PhoneSynthwaveBg for kind 0, PhoneStatusBar + softkey
 *    bar for kind 1) to it, and immediately `lv_obj_del()`s the
 *    container -- which fires DELETE events that route through
 *    LVObject's destructor and exercise the same cascade a real
 *    screen drop walks. Kind 2 stands up a full PhoneAppStubScreen
 *    via `new` / `delete` so the LVScreen base lifecycle is exercised
 *    too (lv_group_create + the per-screen LV_EVENT_SCREEN_LOADED
 *    callback wiring).
 *
 * Pass / fail criterion
 *  - The audit reports |finalHeap - startHeap| as the delta. If the
 *    delta is below kPassThresholdBytes the status switches to
 *    "DONE PASS"; otherwise "DONE DRIFT N B" so the developer sees
 *    the magnitude. The threshold is intentionally permissive
 *    (kPassThresholdBytes = 256) because even on a bone-flat
 *    allocator FreeRTOS / LVGL fragment a few dozen bytes that are
 *    legitimately recoverable on the next allocation pattern -- a
 *    near-zero, stable delta after 1000 iterations is what we are
 *    after, not bit-perfect equality.
 *
 * Behavior:
 *  - BTN_ENTER toggles RUNNING <-> PAUSED. The left soft-key label
 *    flips between PAUSE and RUN to track the toggle.
 *  - BTN_BACK pops back to whoever pushed us (typically the dialer).
 *    Aborting mid-run is fine -- the audit owns no allocations
 *    outside `obj`'s subtree, so the standard ~LVScreen cascade
 *    cleans everything up.
 *  - When the audit finishes (iter == kTotalIterations) the timer is
 *    detached and the soft-key label clears the PAUSE/RUN affordance,
 *    leaving only BACK active. The summary stays on-screen until the
 *    user dismisses it.
 *
 * Implementation notes:
 *  - Code-only, zero SPIFFS. Reuses PhoneSynthwaveBg / PhoneStatusBar /
 *    PhoneSoftKeyBar so the audit page feels visually part of the
 *    same MAKERphone family rather than a debug terminal.
 *  - The work timer fires every kTickMs (50 ms) and runs kBatchSize
 *    iterations per fire so the page completes the 1000-iteration
 *    target in roughly four seconds (50 ms * 200 ticks * 5 iter).
 *    This keeps the LVGL frame loop responsive (each fire returns
 *    well under a frame budget) while still finishing on a human
 *    timescale.
 *  - On the very first tick we also publish the result over Serial
 *    via `Serial.printf("[mem-audit] before=%u\n", ...)` so a
 *    tethered developer can capture the run from the USB console
 *    without having to read the small on-screen labels.
 *  - startHeap is captured once during onStart() (not the constructor)
 *    so the baseline includes whatever LVGL allocations the
 *    PhoneSynthwaveBg / PhoneStatusBar / PhoneSoftKeyBar children
 *    of *this* screen needed -- the delta then reflects only the
 *    audit-loop churn, not the audit-page's own setup.
 */
class PhoneMemoryAudit : public LVScreen, private InputListener {
public:
	PhoneMemoryAudit();
	virtual ~PhoneMemoryAudit() override;

	void onStart() override;
	void onStop() override;

	/** How many construct/destruct cycles the audit runs in total. */
	static constexpr uint32_t kTotalIterations    = 1000;

	/** lv_timer period, in milliseconds. */
	static constexpr uint32_t kTickMs             = 50;

	/** Iterations performed per timer fire. */
	static constexpr uint32_t kBatchSize          = 5;

	/** Heap-delta tolerance for the PASS verdict (bytes). */
	static constexpr uint32_t kPassThresholdBytes = 256;

	/** Number of distinct audit "kinds" rotated through per cycle. */
	static constexpr uint8_t  kKindCount          = 3;

private:
	enum class State : uint8_t {
		Running = 0,
		Paused  = 1,
		Done    = 2,
	};

	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* captionLabel;

	// Body labels. Five label/value pairs + a status row -- we keep
	// the value halves as separate objects rather than reformatting
	// a single combined string so the repaint per tick is cheap
	// (only the value half of each pair changes between ticks).
	lv_obj_t* beforeLabel;
	lv_obj_t* beforeValue;
	lv_obj_t* nowLabel;
	lv_obj_t* nowValue;
	lv_obj_t* deltaLabel;
	lv_obj_t* deltaValue;
	lv_obj_t* iterLabel;
	lv_obj_t* iterValue;
	lv_obj_t* cycleLabel;
	lv_obj_t* cycleValue;
	lv_obj_t* statusLabel;
	lv_obj_t* statusValue;

	lv_timer_t* tickTimer;

	uint32_t  startHeap;
	uint32_t  iter;
	uint8_t   lastKind;
	State     state;

	void buildCaption();
	void buildBody();

	void refreshNowFields();
	void refreshIterFields(uint8_t kindUsed);
	void refreshStatusField();
	void refreshSoftKeys();

	/**
	 * Run a single audit iteration of the requested kind. Each kind
	 * exercises a different combination of Phone* widget churn so the
	 * 1000-iteration total covers a realistic spread of the
	 * lifecycles a real screen drop walks.
	 */
	void runIteration(uint8_t kind);

	/**
	 * Drain `kBatchSize` iterations in one timer fire. Split out from
	 * the timer callback so future hosts (e.g. a unit test) can drive
	 * the audit synchronously without going through lv_timer.
	 */
	void runBatch();

	/** Stop the timer, freeze the audit, paint the final summary. */
	void finish();

	static void onTickTimer(lv_timer_t* timer);

	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONEMEMORYAUDIT_H
