#ifndef CHATTER_FIRMWARE_PHONE_DELIVERED_CHIME_H
#define CHATTER_FIRMWARE_PHONE_DELIVERED_CHIME_H

#include <Arduino.h>

/**
 * S157 — PhoneDeliveredChime
 *
 * Tiny "double-tick" chime fired the moment an outgoing SMS
 * transitions from sent (single tick, in-flight) to delivered
 * (double tick, ACK landed). Mirrors the two-pip silhouette of the
 * delivered glyph PhoneChatBubble draws (introduced by S34) — two
 * short, identical pips with a hair of silence between them, so the
 * audible cue and the on-screen indicator describe the same event.
 *
 * The hook lives in `MessageService::receiveAck`, which fires the
 * chime only on the genuine `received: false -> true` edge for
 * outgoing messages — so a duplicate ACK from a chatty peer (or any
 * future replay path) doesn't chirp twice for the same delivery.
 *
 * Playback is routed through the global `PhoneRingtoneEngine`, so
 * Settings.sound mute is honoured automatically — silent profiles
 * still see the delivered tick on the bubble but produce no audible
 * chime, exactly mirroring the existing S150 charge-complete and
 * S148/S149 boot/power-off chime behaviour.
 *
 * Two safety knobs round out the design:
 *
 *   - `CooldownMs` debounces ACK bursts. If the user fires off five
 *     SMS in a row and all five ACKs land within a frame, we want
 *     one chirp, not five — the bubble status flips on each, but
 *     the buzzer stays civilised.
 *
 *   - `BootGuardMs` suppresses the chime for the first second after
 *     boot. The LoRa stack drops in-flight ACK frames the radio had
 *     buffered before begin() ran, so this guard shouldn't be
 *     strictly necessary today, but it's free insurance against any
 *     future change that replays pending ACKs at startup.
 */
class PhoneDeliveredChime {
public:
	/** Lifecycle. Idempotent — a second call clears the cooldown
	 *  and re-arms the boot guard, which is the right behaviour
	 *  after a deep sleep wake-up. */
	void begin();

	/** Called by `MessageService::receiveAck` on the
	 *  Sent -> Delivered edge for an outgoing message. */
	void notifyDelivered();

	/** Test-friendly accessor. */
	uint32_t lastChimeMs() const { return lastChimeAt; }

	// ---- tunables --------------------------------------------------

	/** Minimum gap between two audible chimes (ms). A burst of ACKs
	 *  inside this window collapses into a single chirp. */
	static constexpr uint16_t CooldownMs  = 1500;

	/** Boot-time silence window (ms). Any ACK seen within this
	 *  window of begin() updates state but does not play. */
	static constexpr uint16_t BootGuardMs = 1500;

private:
	uint32_t bootMs      = 0;
	uint32_t lastChimeAt = 0;
};

extern PhoneDeliveredChime DeliveredChime;

#endif // CHATTER_FIRMWARE_PHONE_DELIVERED_CHIME_H
