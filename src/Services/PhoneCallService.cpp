#include "PhoneCallService.h"

#include <string>
#include <stdio.h>
#include <string.h>

#include "../Storage/Storage.h"
#include "../Model/Friend.hpp"
#include "../Model/Message.h"
#include "../Model/Convo.hpp"
#include "../Interface/LVScreen.h"
#include "../Screens/PhoneIncomingCall.h"
#include "../Screens/PhoneActiveCall.h"
#include "../Screens/PhoneCallEnded.h"

#include "../Storage/PhoneContacts.h"
#include "PhoneContactRingtone.h"

// ---- singleton ----
//
// Same global-instance pattern the rest of the firmware's services use
// (`LoRa`, `Messages`, `Profiles`, `Sleep`, `Piezo`, ...). The static
// screen callbacks below read state off this global because the
// PhoneIncomingCall / PhoneActiveCall / PhoneCallEnded callback
// signatures are bare function pointers with no user-data slot.
PhoneCallService Phone;

// CALL_REQUEST magic body. The SOH (\x01) control prefix is what makes
// this distinguishable from any user-typed text — Storage's T9 / on-
// screen-keyboard composer cannot produce a SOH byte, so an
// accidentally-typed match is impossible. Kept short to fit comfortably
// inside the 60-char text-message cap MessageService::sendText enforces.
static const char* kCallRequestMagic = "\x01<<MP_CALL>>";

const char* PhoneCallService::CallRequestMagic() {
	return kCallRequestMagic;
}

PhoneCallService::PhoneCallService() {
	// Nothing to do here — `begin()` is what wires us into the message
	// pipeline. We keep the constructor empty so the global instance
	// can be defined at file scope without dragging in MessageService
	// initialisation order requirements.
}

void PhoneCallService::begin() {
	// Subscribe to inbound text messages. addReceivedListener() is the
	// existing fan-out point MessageService already drives whenever a
	// TextMessage arrives from a paired peer (see receiveMessage()).
	// Hooking here means we get every payload without modifying
	// LoRaPacket::Type or the LoRa wire format.
	Messages.addReceivedListener(this);
}

void PhoneCallService::placeCall(UID_t peer) {
	// Outgoing side. We pipe through Messages.sendText() so the
	// existing encrypted-and-ACKed delivery path takes care of the
	// actual radio work. sendText() guards against unknown peers
	// internally (it returns an empty Message if the UID is not in
	// the Friends repo), so we don't need to re-validate here.
	if(peer == 0) return;
	Messages.sendText(peer, std::string(kCallRequestMagic));
}

void PhoneCallService::msgReceived(const Message& message) {
	// We only care about TEXT messages whose body matches the magic
	// CALL_REQUEST token. Anything else is a normal chat payload and
	// must pass straight through to whatever other listener (e.g.
	// InboxScreen, the unread-counter) wants it.
	if(message.getType() != Message::TEXT) return;
	if(message.outgoing) return; // our own echo back from sendMessage

	std::string body = message.getText();
	if(body != kCallRequestMagic) return;

	// Magic token detected. Silently scrub the placeholder message +
	// trim the convo so the inbox does not gain a "<<MP_CALL>>" entry
	// every time the peer rings us. We use deleteMessage on the
	// service instead of the lower-level Storage repos so the
	// in-memory `lastMessages` cache + unread flags stay consistent.
	UID_t peer = message.convo;
	if(peer != 0 && message.uid != 0) {
		Messages.deleteMessage(peer, message.uid);
	}

	// Drop a second CALL_REQUEST arriving while a screen is already
	// up. Phase-D doesn't model call-waiting; doing so would scope-
	// creep S28 and a future session can swap callActive for a
	// proper state machine.
	if(callActive) return;
	if(peer == 0) return;

	showIncomingCall(peer);
}

void PhoneCallService::showIncomingCall(UID_t peer) {
	// Resolve the peer to a display name + avatar seed. Falls back to
	// "UNKNOWN" / 0 if the peer is not in Friends, which can only
	// happen if Storage was wiped between pairing and the call —
	// we still want to show *something* so the user knows the radio
	// fired rather than no-op'ing silently.
	const char* displayName = "UNKNOWN";
	uint8_t     avatar      = 0;
	if(Storage.Friends.exists(peer)) {
		Friend fren = Storage.Friends.get(peer);
		displayName = fren.profile.nickname;
		avatar      = fren.profile.avatar;
	}

	// Cache per-call state on the service so the static handlers can
	// reach it after we hand control off to LVGL. strncpy + explicit
	// terminator — same defensive pattern PhoneIncomingCall itself
	// uses for callerName / callerNumber.
	activePeer   = peer;
	activeAvatar = avatar;
	strncpy(activeName, displayName, sizeof(activeName) - 1);
	activeName[sizeof(activeName) - 1] = '\0';
	// Render a "0xUID" form for the secondary line so the user has a
	// breadcrumb identifier even before the contacts work in Phase F
	// gives every peer a real "phone number". printf %llx prints the
	// 48-bit ESP MAC the firmware uses as UID_t.
	snprintf(activeNumber, sizeof(activeNumber), "0x%llx",
			 (unsigned long long) peer);

	// Find a screen to push on top of. LVScreen::getCurrent() returns
	// whatever the user is looking at right now — homescreen, menu,
	// dialer, inbox, etc. If we are mid-boot (no current screen yet)
	// we just bail; a dropped call request is preferable to crashing.
	LVScreen* host = LVScreen::getCurrent();
	if(host == nullptr) return;

	auto* incoming = new PhoneIncomingCall(activeName, activeNumber, activeAvatar);
	incoming->setOnAnswer(&PhoneCallService::onAnswer);
	incoming->setOnReject(&PhoneCallService::onReject);

	// S153 — per-contact custom ringtone. PhoneContacts::ringtoneOf
	// reads the contact override (default 0 = Synthwave). The
	// PhoneContactRingtone helper validates the id (falling back to
	// the default for empty composer slots) and resolves either a
	// library tone or the user's composed ringtone into a Melody
	// pointer. The pointer is owned by static storage in the helper
	// for the lifetime of this incoming-call screen, which is the
	// lifetime semantics PhoneIncomingCall::setRingtone documents.
	const uint8_t storedRingtoneId =
			PhoneContactRingtone::validatedOrDefault(
					PhoneContacts::ringtoneOf(peer));
	if(const auto* m = PhoneContactRingtone::resolve(storedRingtoneId)) {
		incoming->setRingtone(m);
	}

	callActive = true;
	host->push(incoming);
}

// ----- static handlers -----
//
// The Phone* call screens take plain function pointers, so the actual
// "what should happen when the user taps ANSWER" lives on the file-
// scope handlers below. Each handler reads cached call info off the
// `Phone` singleton and either pops back / pushes the next screen in
// the incoming -> active -> ended chain.

void PhoneCallService::onAnswer(PhoneIncomingCall* self) {
	if(self == nullptr) return;
	// Pop the incoming-call screen and immediately push the active-
	// call screen on top of whatever is now current (the homescreen
	// or whatever the user was on when the call arrived). Doing the
	// push *after* the pop gives the user the visual hand-off they
	// expect — the incoming screen disappears, the active screen
	// slides in.
	self->pop();

	LVScreen* host = LVScreen::getCurrent();
	if(host == nullptr) {
		Phone.callActive = false;
		Phone.activePeer = 0;
		return;
	}

	auto* active = new PhoneActiveCall(Phone.activeName,
									   Phone.activeNumber,
									   Phone.activeAvatar);
	active->setOnEnd(&PhoneCallService::onEnd);
	host->push(active);
}

void PhoneCallService::onReject(PhoneIncomingCall* self) {
	// User tapped REJECT — close the screen and clear all per-call
	// state so the next CALL_REQUEST is free to fire again.
	if(self != nullptr) self->pop();
	Phone.callActive = false;
	Phone.activePeer = 0;
}

void PhoneCallService::onEnd(PhoneActiveCall* self) {
	// User tapped END on the active call. Capture the duration before
	// pop() destroys the screen, then push the call-ended overlay on
	// top of whatever screen is now current.
	if(self == nullptr) {
		Phone.callActive = false;
		Phone.activePeer = 0;
		return;
	}

	uint32_t duration = self->getDurationSeconds();
	self->pop();

	LVScreen* host = LVScreen::getCurrent();
	if(host == nullptr) {
		Phone.callActive = false;
		Phone.activePeer = 0;
		return;
	}

	auto* ended = new PhoneCallEnded(Phone.activeName, duration,
									 Phone.activeAvatar);
	ended->setOnDismiss(&PhoneCallService::onDismiss);
	host->push(ended);
}

void PhoneCallService::onDismiss(PhoneCallEnded* self) {
	// PhoneCallEnded auto-dismisses after ~1.5s or on any-key press.
	// Either way, this is the moment the call sequence is fully
	// retired — pop the overlay and clear per-call state.
	if(self != nullptr) self->pop();
	Phone.callActive = false;
	Phone.activePeer = 0;
}
