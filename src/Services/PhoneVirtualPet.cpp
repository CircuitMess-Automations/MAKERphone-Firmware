#include "PhoneVirtualPet.h"

#include "PhoneClock.h"

#include <Arduino.h>
#include <Loop/LoopManager.h>

#include <nvs.h>
#include <esp_log.h>
#include <stdio.h>
#include <string.h>

// =====================================================================
// S129 — PhoneVirtualPet (service)
//
// Implementation overview:
//
//   1. Persistence is one NVS blob containing a tiny header + the
//      live stat block + a 32-bit ageMinutes counter. We read on
//      first begin(), write on every user-facing state change, and
//      additionally write once per minute boundary inside loop().
//
//   2. Tick logic is a minute-quantised state machine, mirror image
//      of PhoneAlarmService:
//        - Each loop() converts PhoneClock::nowEpoch() to "epoch
//          minutes" (epoch / 60).
//        - If we have not crossed into a new minute since the last
//          eval, return immediately. One division + one comparison.
//        - On a fresh minute we run applyMinuteTick() exactly once
//          per minute crossed, then persist. If the device's wall
//          clock has jumped forward (user edited the clock in the
//          Date & Time picker) we still apply only one tick — the
//          pet is forgiving by design.
//
//   3. Decay / recovery numbers live in PhoneVirtualPet.h as
//      `static constexpr` so a future tuning pass touches one block.
// =====================================================================

PhoneVirtualPetService Pet;

namespace {

constexpr const char* kNamespace = "mppet";
constexpr const char* kBlobKey   = "p";

constexpr uint8_t kMagic0   = 'M';
constexpr uint8_t kMagic1   = 'P';
constexpr uint8_t kVersion  = 1;

constexpr size_t  kBlobSize = 12;   // header(4) + stats(4) + age(4)

// Single shared NVS handle. Mirrors PhoneAlarmService / Composer-
// Storage's lazy-open pattern so we never spam nvs_open() retries.
nvs_handle s_handle    = 0;
bool       s_attempted = false;

bool ensureOpen() {
	if(s_handle != 0) return true;
	if(s_attempted)   return false;
	s_attempted = true;
	auto err = nvs_open(kNamespace, NVS_READWRITE, &s_handle);
	if(err != ESP_OK) {
		ESP_LOGW("VirtualPet",
		         "nvs_open(%s) failed: %d -- pet runs without persistence",
		         kNamespace, (int)err);
		s_handle = 0;
		return false;
	}
	return true;
}

} // namespace

// ---------- helpers --------------------------------------------------

uint32_t PhoneVirtualPetService::epochToMinute(uint32_t epoch) {
	// 60-second buckets give a stable "this minute / next minute"
	// boundary. Wraparound is only a concern past 2106-02 which is
	// comfortably outside the device's life.
	return epoch / 60UL;
}

uint8_t PhoneVirtualPetService::subClamp(uint8_t v, uint8_t d) {
	return (d >= v) ? 0 : (uint8_t)(v - d);
}

uint8_t PhoneVirtualPetService::addClamp(uint8_t v, uint8_t d) {
	const uint16_t sum = (uint16_t)v + (uint16_t)d;
	return (sum > StatMax) ? StatMax : (uint8_t)sum;
}

void PhoneVirtualPetService::formatAge(uint32_t ageMinutes,
                                uint16_t& daysOut,
                                uint8_t&  hoursOut,
                                uint8_t&  minsOut) {
	const uint32_t totalMin = ageMinutes;
	const uint32_t days     = totalMin / (60UL * 24UL);
	const uint32_t rem      = totalMin - days * 60UL * 24UL;
	const uint32_t hours    = rem / 60UL;
	const uint32_t mins     = rem - hours * 60UL;

	// Days saturate at 9999 — well past any reasonable Tamagotchi
	// lifetime; the screen formats with %u so anything below 65535
	// is fine, but keeping it explicit makes the contract obvious.
	daysOut  = (days  > 9999U) ? 9999 : (uint16_t)days;
	hoursOut = (uint8_t)hours;
	minsOut  = (uint8_t)mins;
}

// ---------- lifecycle ------------------------------------------------

void PhoneVirtualPetService::begin() {
	if(!nvsLoaded) {
		loadFromNvs();
		nvsLoaded = true;
	}

	// Anchor the minute counter at the current wall-clock minute so
	// the very first applyMinuteTick() doesn't pretend a stale minute
	// just rolled over — the pet should not lose 1 hunger the instant
	// the firmware boots.
	lastEvalMinute = epochToMinute(PhoneClock::nowEpoch());

	LoopManager::addListener(this);
}

void PhoneVirtualPetService::loadFromNvs() {
	if(!ensureOpen()) return;

	uint8_t blob[kBlobSize] = {};
	size_t  size = sizeof(blob);
	auto err = nvs_get_blob(s_handle, kBlobKey, blob, &size);
	if(err != ESP_OK)        return;
	if(size < kBlobSize)     return;
	if(blob[0] != kMagic0)   return;
	if(blob[1] != kMagic1)   return;
	if(blob[2] != kVersion)  return;

	const uint8_t hh = blob[4];
	const uint8_t hp = blob[5];
	const uint8_t en = blob[6];
	const uint8_t sl = blob[7];

	// Clamp on read so a corrupt blob can never give the pet
	// stat=200 hunger or similar nonsense.
	st.hunger   = (hh <= StatMax) ? hh : StatMax;
	st.happy    = (hp <= StatMax) ? hp : StatMax;
	st.energy   = (en <= StatMax) ? en : StatMax;
	st.sleeping = (sl != 0);

	const uint32_t age =
	      (uint32_t)blob[8]
	    | ((uint32_t)blob[9]  <<  8)
	    | ((uint32_t)blob[10] << 16)
	    | ((uint32_t)blob[11] << 24);
	st.ageMinutes = age;
}

void PhoneVirtualPetService::saveToNvs() const {
	if(!ensureOpen()) return;

	uint8_t blob[kBlobSize] = {};
	blob[0]  = kMagic0;
	blob[1]  = kMagic1;
	blob[2]  = kVersion;
	blob[3]  = 0;
	blob[4]  = st.hunger;
	blob[5]  = st.happy;
	blob[6]  = st.energy;
	blob[7]  = st.sleeping ? 1 : 0;
	blob[8]  = (uint8_t)( st.ageMinutes        & 0xFF);
	blob[9]  = (uint8_t)((st.ageMinutes >>  8) & 0xFF);
	blob[10] = (uint8_t)((st.ageMinutes >> 16) & 0xFF);
	blob[11] = (uint8_t)((st.ageMinutes >> 24) & 0xFF);

	auto err = nvs_set_blob(s_handle, kBlobKey, blob, sizeof(blob));
	if(err != ESP_OK) {
		ESP_LOGW("VirtualPet", "nvs_set_blob failed: %d", (int)err);
		return;
	}
	err = nvs_commit(s_handle);
	if(err != ESP_OK) {
		ESP_LOGW("VirtualPet", "nvs_commit failed: %d", (int)err);
	}
}

// ---------- mood -----------------------------------------------------

PhoneVirtualPetService::Mood PhoneVirtualPetService::mood() const {
	if(st.sleeping)                    return Mood::Asleep;
	if(st.hunger < HungryThreshold)    return Mood::Hungry;
	if(st.happy  < SadThreshold)       return Mood::Sad;
	if(st.energy < TiredThreshold)     return Mood::Tired;
	return Mood::Happy;
}

// ---------- user actions --------------------------------------------

void PhoneVirtualPetService::feed() {
	if(st.sleeping) return;   // wake first; screen enforces

	// Over-feeding penalty: if hunger is already above the cap minus
	// the feed amount we're stuffing the pet, so it loses a little
	// happiness. Same idea as the original toy's "no thank you"
	// reaction when you keep mashing the food button.
	if(st.hunger >= (StatMax - FeedAmount)) {
		st.happy = subClamp(st.happy, OverfeedPenalty);
	}

	st.hunger = addClamp(st.hunger, FeedAmount);
	saveToNvs();
}

void PhoneVirtualPetService::play() {
	if(st.sleeping) return;   // wake first; screen enforces

	// Playing requires energy. If the pet is too tired we still
	// register the action but the happiness gain is much smaller
	// (half) and the energy/hunger costs still apply — exactly how
	// the original toy made you wake-and-feed before play.
	const uint8_t happyGain = (st.energy < EnergyPerPlay)
	                              ? (uint8_t)(PlayAmount / 2)
	                              : PlayAmount;

	st.happy  = addClamp(st.happy,  happyGain);
	st.energy = subClamp(st.energy, EnergyPerPlay);
	st.hunger = subClamp(st.hunger, HungerPerPlay);
	saveToNvs();
}

void PhoneVirtualPetService::wakeOrSleep() {
	st.sleeping = !st.sleeping;
	// Reset the "every other sleep minute" parity counter so the
	// hunger-while-sleeping cadence is predictable from the moment
	// the pet drops off.
	sleepHungerTick = 0;
	saveToNvs();
}

void PhoneVirtualPetService::reset() {
	st = State{};   // hunger=100, happy=100, energy=100, sleeping=false, age=0
	sleepHungerTick = 0;
	saveToNvs();
}

// ---------- minute tick ---------------------------------------------

void PhoneVirtualPetService::applyMinuteTick() {
	// Age accumulates regardless of awake/asleep — the pet does not
	// stop ageing while it sleeps, that's the whole point of a
	// Tamagotchi-style lifespan.
	if(st.ageMinutes < 0xFFFFFFFFUL) {
		st.ageMinutes += 1;
	}

	if(st.sleeping) {
		// Hunger drops at half cadence while sleeping — every other
		// minute. Parity tracked by sleepHungerTick so the pattern
		// is deterministic from the moment the pet falls asleep.
		sleepHungerTick++;
		if((sleepHungerTick & 0x1) == 0) {
			st.hunger = subClamp(st.hunger, AwakeHungerDrop);
		}
		st.energy = addClamp(st.energy, SleepEnergyGain);
		st.happy  = addClamp(st.happy,  SleepHappyGain);
		// Long-term sleeping eventually fully restores energy; once
		// the pet is at max energy and max happy it will naturally
		// re-wake on the next user interaction with the screen,
		// which the screen enforces via wakeOrSleep().
	} else {
		st.hunger = subClamp(st.hunger, AwakeHungerDrop);
		st.happy  = subClamp(st.happy,  AwakeHappyDrop);
		st.energy = subClamp(st.energy, AwakeEnergyDrop);
	}
}

// ---------- background tick -----------------------------------------

void PhoneVirtualPetService::loop(uint /*micros*/) {
	const uint32_t epoch  = PhoneClock::nowEpoch();
	const uint32_t minNow = epochToMinute(epoch);

	// O(1) early-out for the common case where we are still inside
	// the same minute we last evaluated.
	if(minNow == lastEvalMinute) return;
	lastEvalMinute = minNow;

	applyMinuteTick();
	saveToNvs();
}
