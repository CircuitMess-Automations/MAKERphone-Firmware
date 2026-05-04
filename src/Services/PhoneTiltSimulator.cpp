#include "PhoneTiltSimulator.h"

#include "../Interface/LVScreen.h"

#include <Arduino.h>
#include <Input/Input.h>
#include <Loop/LoopManager.h>
#include <Pins.hpp>

// =====================================================================
// S168 - PhoneTiltSimulator
//
// See PhoneTiltSimulator.h for the design rationale. Implementation is
// deliberately tiny: two booleans (L/R held), a single millis()
// timestamp, and a one-shot fire flag. The loop tick does nothing
// when the chord is not active, so the gesture detector adds zero
// frame budget on every screen that does not opt-in to onShake().
// =====================================================================

PhoneTiltSimulator Tilt;

// ---------- lifecycle ------------------------------------------------

void PhoneTiltSimulator::begin() {
	Input::getInstance()->addListener(this);
	LoopManager::addListener(this);
	reset();
}

void PhoneTiltSimulator::reset() {
	lHeld = false;
	rHeld = false;
	firedThisHold = false;
	bothHeldStartMs = 0;
}

// ---------- input transitions ----------------------------------------

void PhoneTiltSimulator::buttonPressed(uint i) {
	const uint8_t btn = static_cast<uint8_t>(i);

	if(btn != BTN_L && btn != BTN_R) return;

	const bool wasArmed = lHeld && rHeld;

	if(btn == BTN_L) lHeld = true;
	if(btn == BTN_R) rHeld = true;

	// Just transitioned into the both-held state - record the start
	// timestamp so loop() knows when the HoldMs window opens. We do
	// NOT clear firedThisHold here; that flag is tied to *this*
	// chord only and is cleared on release of either side, so a
	// stuck-key edge case (press L, press R, see fire, then a
	// platform-level re-press of L without a release in between)
	// cannot replay the shake within the same hold.
	if(!wasArmed && lHeld && rHeld) {
		bothHeldStartMs = millis();
	}
}

void PhoneTiltSimulator::buttonReleased(uint i) {
	const uint8_t btn = static_cast<uint8_t>(i);

	if(btn != BTN_L && btn != BTN_R) return;

	if(btn == BTN_L) lHeld = false;
	if(btn == BTN_R) rHeld = false;

	// Either side just lifted - the chord is broken. Clear the fire
	// flag so the next chord can fire afresh. If neither side is
	// held, also reset the timestamp for tidiness (loop() already
	// guards on `lHeld && rHeld` so this is purely cosmetic state).
	if(!(lHeld && rHeld)) {
		firedThisHold = false;
		if(!lHeld && !rHeld) {
			bothHeldStartMs = 0;
		}
	}
}

// ---------- loop tick (the actual fire decision) ---------------------

void PhoneTiltSimulator::loop(uint /*micros*/) {
	// Idle path: nothing held, or only one side held, or we have
	// already fired this hold. Early return keeps the cost at a few
	// CPU cycles per loop.
	if(!lHeld || !rHeld) return;
	if(firedThisHold)    return;

	const uint32_t now = millis();
	if((uint32_t)(now - bothHeldStartMs) < HoldMs) return;

	// Window elapsed: latch the gesture and dispatch.
	firedThisHold = true;
	fireShake();
}

// ---------- dispatch -------------------------------------------------

void PhoneTiltSimulator::fireShake() {
	// Whichever screen is currently visible gets the hook. Default
	// LVScreen::onShake() is a no-op so any screen that does not
	// opt-in silently absorbs the gesture; PhonePinball (which uses
	// L+R as flipper triggers) is exactly that case and inherits
	// the default, leaving its gameplay unaffected.
	LVScreen* current = LVScreen::getCurrent();
	if(current != nullptr) {
		current->onShake();
	}
}
