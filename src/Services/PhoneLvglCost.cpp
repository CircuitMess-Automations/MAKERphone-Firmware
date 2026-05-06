#include "PhoneLvglCost.h"
#include <Loop/LoopManager.h>

/*
 * S198 - PhoneLvglCost
 *
 * See PhoneLvglCost.h for the design rationale. Implementation is a
 * tiny ring buffer + a 1 Hz windowed counter. No allocation, no
 * LVGL state, no Input subscription - just a passive LoopListener
 * that the diag screen polls at 1 Hz to render the readout.
 */

PhoneLvglCost LvglCost;

PhoneLvglCost::PhoneLvglCost() {
	for(uint16_t i = 0; i < kSampleCount; i++) samples[i] = 0;
}

void PhoneLvglCost::begin() {
	// Idempotent: LoopManager dedupes by pointer so re-calling begin()
	// (e.g. from a future test harness) is safe.
	LoopManager::addListener(this);
	windowStartMs = millis();
	windowLoops = 0;
	lastSecondLoops = 0;
}

void PhoneLvglCost::loop(uint micros) {
	// Hot path: write the sample, bump the counters, and check if the
	// rolling 1 s window has rolled over. Everything here is integer.

	samples[sampleHead] = (uint32_t) micros;
	sampleHead = (sampleHead + 1) & kSampleMask;
	if(sampleCount < kSampleCount) sampleCount++;

	totalLoops++;
	windowLoops++;

	const uint32_t now = millis();
	if((uint32_t)(now - windowStartMs) >= kRateWindowMs) {
		lastSecondLoops = windowLoops;
		windowLoops = 0;
		windowStartMs = now;
	}
}

uint32_t PhoneLvglCost::avgUs() const {
	if(sampleCount == 0) return 0;

	// Sum is at most kSampleCount (64) * uint32_max which is way
	// beyond uint32_t - but in practice each `micros` delta is a few
	// thousand microseconds (1 ms-ish per loop tick), so the sum is
	// well under 2^32 and 32-bit accumulation is fine. We use
	// uint64_t here as a defensive belt-and-braces for the (rare)
	// case where a service stalls a loop for several seconds.
	uint64_t sum = 0;
	for(uint16_t i = 0; i < sampleCount; i++) sum += samples[i];
	return (uint32_t)(sum / sampleCount);
}

uint32_t PhoneLvglCost::peakUs() const {
	if(sampleCount == 0) return 0;
	uint32_t peak = 0;
	for(uint16_t i = 0; i < sampleCount; i++) {
		if(samples[i] > peak) peak = samples[i];
	}
	return peak;
}
