#ifndef CHATTER_FIRMWARE_PHONE_VIRTUAL_PET_H
#define CHATTER_FIRMWARE_PHONE_VIRTUAL_PET_H

#include <Arduino.h>
#include <Loop/LoopListener.h>
#include <stdint.h>

/**
 * S129 — PhoneVirtualPet (service)
 *
 * Background Tamagotchi-style virtual pet. Lives as a global
 * LoopListener so the pet keeps ageing, getting hungry, getting
 * tired and so on while the user is on a different screen. The
 * matching PhoneVirtualPet *screen* is a pure viewer/editor over
 * this state — the screen never owns the timing.
 *
 * Three live stats (hunger, happiness, energy) each in [0..100].
 * 100 is "full / happy / rested"; 0 is "starving / sad / exhausted".
 * One ageMinutes counter ticks every wall-clock minute the device
 * is alive (sleeping or awake — the pet still ages). One bool
 * `sleeping` toggles between awake decay and asleep recovery.
 *
 * Persistence is one 12-byte NVS blob in namespace "mppet" / key
 * "p", same magic-prefixed pattern PhoneAlarmService and
 * PhoneComposerStorage use:
 *
 *   [0]    magic 'M'
 *   [1]    magic 'P'
 *   [2]    version (1)
 *   [3]    reserved
 *   [4]    hunger 0..100
 *   [5]    happy  0..100
 *   [6]    energy 0..100
 *   [7]    sleeping (0/1)
 *   [8..11] ageMinutes uint32_t little-endian
 *
 * The blob is rewritten on every state-changing user call (feed /
 * play / wakeOrSleep / reset) and on each minute boundary inside
 * loop(), so a power loss can never push the saved age more than
 * one minute behind reality. A partially written blob fails the
 * magic check and reads as a fresh pet — same fail-soft contract.
 *
 * Idle cost: loop() short-circuits in O(1) unless we have crossed
 * a minute boundary, mirror image of PhoneAlarmService's tick.
 */
class PhoneVirtualPetService : public LoopListener {
public:
	/** Stat ceiling — every live stat is clamped to this. */
	static constexpr uint8_t StatMax = 100;

	/** Per-action deltas. Tuned so the user can keep the pet alive
	 *  with a feed every ~80 minutes and a play every ~30 — matches
	 *  the muscle memory of the original 1996 toy without being so
	 *  needy it gets in the way of the rest of the firmware. */
	static constexpr uint8_t FeedAmount      = 30;
	static constexpr uint8_t PlayAmount      = 25;
	static constexpr uint8_t EnergyPerPlay   = 10;
	static constexpr uint8_t HungerPerPlay   = 5;
	static constexpr uint8_t OverfeedPenalty = 5;   // happy -=

	/** Background decay (per real-time minute, while awake). */
	static constexpr uint8_t AwakeHungerDrop = 1;
	static constexpr uint8_t AwakeHappyDrop  = 1;
	static constexpr uint8_t AwakeEnergyDrop = 1;

	/** Background recovery while asleep. Hunger still drops, just
	 *  half as fast — applied as a 1-every-other-minute counter. */
	static constexpr uint8_t SleepEnergyGain = 2;
	static constexpr uint8_t SleepHappyGain  = 1;

	/** Mood thresholds. */
	static constexpr uint8_t HungryThreshold = 30;
	static constexpr uint8_t SadThreshold    = 30;
	static constexpr uint8_t TiredThreshold  = 30;

	/** Categorical mood derived from the live stats. The screen
	 *  uses this to pick which sprite face to show. */
	enum class Mood : uint8_t {
		Asleep = 0,
		Hungry,
		Sad,
		Tired,
		Happy,
	};

	/** Lifecycle. begin() is idempotent; safe to call from setup()
	 *  or the splash callback. Loads the persisted state then
	 *  registers the loop listener. */
	void begin();

	/** Read-only accessors. */
	uint8_t  hunger()     const { return st.hunger; }
	uint8_t  happiness()  const { return st.happy;  }
	uint8_t  energy()     const { return st.energy; }
	uint32_t ageMinutes() const { return st.ageMinutes; }
	bool     isSleeping() const { return st.sleeping; }

	/** v1 keeps it simple: the pet never dies, it just looks
	 *  miserable. The flag is exposed for a future session that
	 *  wants to add a death animation. */
	bool     isAlive()    const { return true; }

	/** User actions. Each successful state-changing call persists. */
	void feed();
	void play();
	void wakeOrSleep();
	void reset();

	/** Background tick driven by LoopManager. */
	void loop(uint micros) override;

	/** Static helper: convert ageMinutes to D days, H hours, M
	 *  minutes. The screen uses this to format the age caption. */
	static void formatAge(uint32_t ageMinutes,
	                      uint16_t& daysOut,
	                      uint8_t&  hoursOut,
	                      uint8_t&  minsOut);

	Mood mood() const;

private:
	struct State {
		uint8_t  hunger     = 100;
		uint8_t  happy      = 100;
		uint8_t  energy     = 100;
		bool     sleeping   = false;
		uint32_t ageMinutes = 0;
	};

	State    st;
	bool     nvsLoaded      = false;

	/** Wall-clock minute we last evaluated. Same epoch/60 cadence
	 *  PhoneAlarmService uses so the two services share the same
	 *  minute boundary. */
	uint32_t lastEvalMinute = 0;

	/** Counter for the "hunger drops every two sleep-minutes" rule.
	 *  Only parity matters; wraparound is harmless. */
	uint16_t sleepHungerTick = 0;

	void loadFromNvs();
	void saveToNvs() const;
	static uint32_t epochToMinute(uint32_t epoch);

	/** Apply per-minute decay/recovery once. */
	void applyMinuteTick();

	static uint8_t subClamp(uint8_t v, uint8_t d);
	static uint8_t addClamp(uint8_t v, uint8_t d);
};

extern PhoneVirtualPetService Pet;

#endif // CHATTER_FIRMWARE_PHONE_VIRTUAL_PET_H
