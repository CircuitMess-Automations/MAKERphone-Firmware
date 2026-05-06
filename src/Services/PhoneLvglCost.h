#ifndef CHATTER_FIRMWARE_PHONE_LVGL_COST_H
#define CHATTER_FIRMWARE_PHONE_LVGL_COST_H

#include <Arduino.h>
#include <Loop/LoopListener.h>

/**
 * S198 - PhoneLvglCost
 *
 * Tiny passive metrics service that turns the LoopManager's per-tick
 * `loop(uint micros)` callback into a rolling LVGL/loop-cost readout
 * for the Battery-life pass diagnostics.
 *
 * The MAKERphone main super-loop is dominated by `lv_timer_handler()`
 * (LVGL redraws + animations) and a fan-out of `LoopManager::loop()`
 * service ticks. The shared `micros` argument every LoopListener
 * receives is exactly the time elapsed since the previous loop tick,
 * so by counting frames and accumulating the elapsed value we get a
 * decent proxy for "how long is one full main-loop iteration taking
 * right now" - which is in turn a decent proxy for LVGL frame cost
 * (since LVGL is the wall-clock-dominant cost). No instrumentation
 * around `lv_timer_handler()` itself, no platform timing hooks: we
 * just observe what LoopManager already gives every other listener
 * for free.
 *
 * What's exposed:
 *
 *   - `loopsPerSec()` - integer ticks per second over the trailing
 *     1-second window. Updates once a second on the wall-clock; in
 *     between calls the value stays stable so a 1 Hz UI refresh
 *     never reads a half-built sample.
 *
 *   - `avgUs()` / `peakUs()` - mean and peak per-tick elapsed time
 *     in microseconds, computed over a 64-sample ring buffer of the
 *     `micros` deltas. The ring is cheap (256 bytes for the buffer
 *     itself) and gives us a "last ~1 s of cadence" view: at a
 *     comfortable 60 fps the buffer covers ~1 s; at a heavily
 *     loaded 30 fps it stretches to ~2 s, which still feels
 *     responsive when watching the readout live.
 *
 *   - `totalLoopsSinceBoot()` - monotonically increasing 32-bit
 *     counter for any code that wants a deterministic "how many
 *     ticks have happened" value (e.g. a future scheduled-task
 *     watchdog, or a memory-leak audit that wants to know how
 *     many frames it ran for).
 *
 * Idle-cost: a single `if`, three integer adds, one ring-index
 * mask, and a `millis()` compare per loop tick. Everything is
 * unsigned-32-bit arithmetic - no float, no division on the hot
 * path. The averages are computed only when the consumer asks for
 * them (e.g. once a second from the diag screen's lv_timer), so
 * the hot path stays as small as possible.
 *
 * Coexistence: the service simply observes the LoopManager tick
 * stream. It registers as a listener once at boot in setup() and
 * never unregisters; there is no allocation, no Input subscription,
 * no LVGL state. Safe to query from any context that has a stable
 * pointer to the singleton (`extern PhoneLvglCost LvglCost`).
 */
class PhoneLvglCost : public LoopListener {
public:
	PhoneLvglCost();
	void begin();

	void loop(uint micros) override;

	/** Loops observed in the last completed 1-second window. */
	uint16_t loopsPerSec() const { return lastSecondLoops; }

	/** Mean per-tick elapsed time in microseconds, over the last
	 *  kSampleCount ticks. Returns 0 until the first sample lands. */
	uint32_t avgUs() const;

	/** Peak per-tick elapsed time observed in the last kSampleCount
	 *  ticks. Returns 0 until the first sample lands. */
	uint32_t peakUs() const;

	/** Monotonically increasing tick counter. Wraps at 2^32 ticks
	 *  (well past any realistic device uptime even at 100+ fps). */
	uint32_t totalLoopsSinceBoot() const { return totalLoops; }

	/** Power of two so the ring index can be masked. 64 samples is
	 *  enough that a single transient hiccup is still visible in
	 *  the peak readout but the average smooths out the noise. */
	static constexpr uint16_t kSampleCount = 64;
	static constexpr uint16_t kSampleMask  = kSampleCount - 1;

	// Window over which loopsPerSec() is computed. One second matches
	// the 1 Hz refresh on the diag screen and on PhoneAboutScreen, so
	// the displayed value never updates faster than the UI can
	// observe it.
	static constexpr uint32_t kRateWindowMs = 1000;

private:
	uint32_t samples[kSampleCount];
	uint16_t sampleHead = 0;     // next slot to write
	uint16_t sampleCount = 0;    // 0..kSampleCount, saturates

	uint32_t totalLoops = 0;

	uint32_t windowStartMs = 0;
	uint16_t windowLoops = 0;
	uint16_t lastSecondLoops = 0;
};

extern PhoneLvglCost LvglCost;

#endif // CHATTER_FIRMWARE_PHONE_LVGL_COST_H
