#include "PhoneDeliveredChime.h"

#include "PhoneRingtoneEngine.h"

#include <Arduino.h>
#include <Notes.h>

// =====================================================================
// S157 — PhoneDeliveredChime
//
// Implementation overview:
//
//   1. The chime is two short, identical pips of NOTE_E7 with a hair
//      of silence between them. Pip duration (30 ms) sits at the
//      lower bound of what the piezo can articulate as a distinct
//      "click"; anything shorter blurs into a single chirp. The
//      inter-pip gap (60 ms) is just long enough that the human ear
//      hears two separate events — the audio analogue of two visual
//      ticks.
//
//   2. The pitch (E7, ~2.6 kHz) is high enough to read as a
//      "confirmation" rather than a ringtone-grade note, and is
//      distinct from the existing chime family:
//        - S148 boot:  G-major arpeggio, ascending across an octave
//        - S149 power: descending arpeggio
//        - S150 charge: C–E–G ascending chord, longest held note
//        - S157 delivered (this one): two equal high pips
//      That keeps the four phone-system cues mutually distinguishable
//      even from the next room.
//
//   3. Total length: 30 + 60 + 30 = 120 ms. Short enough that the
//      message-delivered toast / bubble flip lands in roughly the
//      same frame, so the audio and visual feedback feel like a
//      single event rather than a delayed ping.
//
//   4. Routing through `Ringtone.play()` means an active ringtone or
//      a playing music-player tune wins automatically (the engine
//      replaces the current melody on play()). That's the right
//      behaviour: a ringing call shouldn't get interrupted by a
//      delivery tick. The previous implementation did consider
//      gating the chime on `Ringtone.isPlaying()`, but the engine's
//      replacement semantics already produce the desired result and
//      keep the call site one line.
//
//   5. Cooldown + boot guard: see header. Both keep the chime polite
//      under bursty traffic and across reboots without complicating
//      the call site (the hook in MessageService is unconditional).
// =====================================================================

PhoneDeliveredChime DeliveredChime;

namespace {

// Two-pip "delivered" cue. See the implementation comment above for
// the design rationale behind the pitch, duration and gap choices.
const PhoneRingtoneEngine::Note kDeliveredNotes[] = {
		{ NOTE_E7, 30 },
		{ NOTE_E7, 30 },
};

const PhoneRingtoneEngine::Melody kDeliveredMelody = {
		kDeliveredNotes,
		sizeof(kDeliveredNotes) / sizeof(kDeliveredNotes[0]),
		60,        // gapMs — the silence between the two ticks
		false,     // one-shot
		"Dlvrd",
};

} // namespace

void PhoneDeliveredChime::begin(){
	// Capture the current `millis()` value as the boot reference. The
	// boot guard starts here regardless of whether begin() is the
	// post-power-up call or a wake-from-sleep call — both situations
	// genuinely benefit from a brief quiet window while LoRa settles.
	bootMs      = millis();
	lastChimeAt = 0;
}

void PhoneDeliveredChime::notifyDelivered(){
	const uint32_t now = millis();

	// Boot guard: any ACK that arrives in the first BootGuardMs of a
	// fresh begin() is almost certainly either:
	//   (a) a stale frame the radio had buffered before begin() ran,
	//   (b) a duplicate ACK from a peer that retried while we were
	//       still booting, or
	//   (c) a synthetic event from a post-boot smoke test.
	// In all three cases the user has not just hit "send" — they have
	// just turned the device on, and a chirp from out of nowhere
	// would feel jarring. We swallow the chime but otherwise behave
	// as if it had played, so any subsequent ACK in the same burst
	// is also collapsed by the cooldown rather than stacking.
	if(now - bootMs < BootGuardMs){
		lastChimeAt = now;
		return;
	}

	// Cooldown — collapses bursts of ACKs into a single chirp. We
	// special-case lastChimeAt == 0 (no prior chime) so the very
	// first delivery after the boot guard expires fires immediately
	// rather than waiting CooldownMs from t=0.
	if(lastChimeAt != 0 && (now - lastChimeAt) < CooldownMs) return;

	lastChimeAt = now;

	// PhoneRingtoneEngine internally short-circuits the piezo when
	// Settings.sound is false, so a muted device still observes the
	// state transition but produces no audible chime — same contract
	// the rest of the chime family relies on.
	Ringtone.play(kDeliveredMelody);
}
