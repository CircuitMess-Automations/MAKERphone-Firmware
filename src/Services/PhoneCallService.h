#ifndef MAKERPHONE_PHONECALLSERVICE_H
#define MAKERPHONE_PHONECALLSERVICE_H

#include <Arduino.h>
#include "../Types.hpp"
#include "MessageService.h"

/**
 * PhoneCallService — S28
 *
 * The bridge between the LoRa-backed messaging stack (already shipping
 * peer-to-peer pairing + chat) and the Phase-D call screens
 * (PhoneIncomingCall / PhoneActiveCall / PhoneCallEnded).
 *
 * Why this lives behind MessageService instead of directly on top of
 * LoRaService: extending the LoRa packet enum to add a new
 * "CALL_REQUEST" wire type would touch every code-path that decodes
 * LoRaPacket::Type and risk regressing the messaging / pairing flow
 * that is already on real hardware. Tunnelling the call request
 * through a TextMessage with a magic body string is non-invasive,
 * keeps the wire-compat story stable, and re-uses the existing
 * encrypted-and-ACKed delivery path the messaging stack relies on.
 *
 * Receive flow (the actual S28 deliverable):
 *
 *   peer A   --[ TextMessage("\x01<<MP_CALL>>") ]-->  peer B
 *                                                       |
 *                                                       v
 *                                               LoRaService inbox
 *                                                       |
 *                                                       v
 *                                            MessageService::receiveMessage
 *                                                       |
 *                                                       v
 *                                  MsgReceivedListener::msgReceived (this)
 *                                                       |
 *                                       detect magic body, look up the
 *                                       Friend record for packet sender,
 *                                       silently delete the placeholder
 *                                       message + convo entry, push
 *                                       PhoneIncomingCall on top of
 *                                       LVScreen::getCurrent() with
 *                                       answer / reject hooked up.
 *
 * Outgoing flow (provided so a future dialer-CALL wiring can drop in
 * without revisiting this file):
 *
 *   placeCall(peer)  =  Messages.sendText(peer, "\x01<<MP_CALL>>")
 *
 * The static handlers (answerCb / rejectCb / endCb / muteCb /
 * dismissCb) are plain function pointers because the Phone* call
 * screens expose `void(*)(SelfPtr)` callbacks, not `std::function`.
 * State is held on the singleton instance below (the same pattern
 * `LoRa` and `Messages` use) so the handlers can read it via the
 * `Phone` global without a captured closure.
 *
 * Re-entrancy / single-call invariant:
 *  - At most one call screen is on the stack at any time. If a
 *    second CALL_REQUEST arrives while a call is already active or
 *    a PhoneIncomingCall is already showing, it is dropped silently
 *    (the placeholder message is still cleaned up). Phase-D doesn't
 *    yet model "call waiting" and shipping that here would scope-
 *    creep S28; a follow-up session can replace `callActive` with a
 *    proper state machine.
 *  - The class drives the screen lifecycle entirely through
 *    LVScreen::push / pop, so there is nothing to undo on failure
 *    paths beyond clearing `callActive`.
 */
class PhoneCallService : public MsgReceivedListener {
public:
	PhoneCallService();

	/**
	 * Register with MessageService so msgReceived() starts firing
	 * for every incoming TextMessage. Call once during setup, after
	 * Messages.begin() — the same place LoRa.begin() / Profiles.begin()
	 * are kicked off.
	 */
	void begin();

	/**
	 * Outgoing-call helper. Sends the magic CALL_REQUEST text to the
	 * paired peer; the peer's PhoneCallService picks it up and shows
	 * its incoming-call screen. No-op if `peer` is not in the local
	 * Friends repo (Messages.sendText() guards on that internally).
	 *
	 * Note: S28 does not yet wire any UI button to this — the dialer's
	 * CALL softkey landing in PhoneActiveCall is a separate piece of
	 * polish that an upcoming session can flip on. The method is here
	 * now so the symmetrical receive/send wiring lives in one file.
	 */
	void placeCall(UID_t peer);

	/**
	 * Body string that marks a TextMessage as a call request. Public
	 * so a unit test or future dialer can compare against the same
	 * constant without forking it. The leading 0x01 byte is a SOH
	 * control character that the on-screen T9 / keyboard composer
	 * cannot type, so a chatty user cannot accidentally hand-craft
	 * the magic string and trigger a phantom call.
	 */
	static const char* CallRequestMagic();

	/**
	 * True while a Phase-D call screen owned by this service is on
	 * the LVScreen stack. Lets future code (e.g. SleepService) gate
	 * idle-dim on whether a call is in progress without us having
	 * to publish a separate listener interface.
	 */
	bool isCallActive() const { return callActive; }

private:
	// MsgReceivedListener override. Called by MessageService after the
	// inbound TextMessage has been written to Storage and added to the
	// peer's Convo. We sniff the body, and if it's the magic CALL token
	// we (a) silently delete the placeholder convo entry so it doesn't
	// pollute the inbox and (b) raise the call screen.
	void msgReceived(const Message& message) override;

	// Push PhoneIncomingCall on top of LVScreen::getCurrent() and wire
	// answer/reject -> active/end chain. callerNumberBuf is rendered
	// as a "0xUID" hex string so the user has *something* to identify
	// the peer by even before the contacts work in Phase F.
	void showIncomingCall(UID_t peer);

	// Static screen handlers. They read state off the `Phone` global
	// rather than capturing it (the call-screen callback signature is
	// `void(*)(Self*)` with no user-data slot).
	static void onAnswer(class PhoneIncomingCall* self);
	static void onReject(class PhoneIncomingCall* self);
	static void onEnd(class PhoneActiveCall* self);
	static void onDismiss(class PhoneCallEnded* self);

	// Per-call working state — populated by showIncomingCall and read
	// back by the static callbacks above. uid stays 0 outside of a
	// call so a stray callback fires safely as a no-op.
	UID_t   activePeer  = 0;
	uint8_t activeAvatar = 0;
	char    activeName  [24 + 1] = {0};
	char    activeNumber[24 + 1] = {0};

	bool callActive = false;
};

extern PhoneCallService Phone;

#endif // MAKERPHONE_PHONECALLSERVICE_H
