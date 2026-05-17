/*
 * mp24/components/chatter_app/shim/MessageServiceStub.cpp
 *
 * Replacement for src/Services/MessageService.cpp. The upstream
 * version pulls in LoRaService → RadioLib → Util/Task chain that
 * we explicitly don't want on MP2.4 (Decision B: cellular modem
 * replaces LoRa). This stub provides the same public symbols as
 * no-ops so consuming code (NotificationElement, every message
 * screen) compiles + links cleanly.
 *
 * Same pattern as shim/InputLVGL.cpp from S-MP17a: the upstream
 * header is unmodified (and is the source of the class
 * declaration we implement here); the upstream .cpp is excluded
 * from the chatter_app build via not being in SRCS.
 *
 * What this stub gives the rest of the codebase:
 *
 *   - `MessageService Messages;` — global singleton instance.
 *     Any extern reference to it resolves here.
 *   - All public methods + the LoopListener::loop override.
 *     Methods return "empty" values (default-constructed Message,
 *     false for bool, etc.). Side-effect methods (sendText, etc.)
 *     do nothing — logging an info-level message is the maximum
 *     side effect.
 *   - addListener / removeListener inherited from
 *     WithListeners<T> — no implementation needed here, those
 *     are template inlines in CircuitOS.
 *
 * Private methods (sendMessage, sendPacket, receiveMessage,
 * receiveAck, notifyUnread) are intentionally NOT defined. They
 * are only callable from within MessageService class methods,
 * and since our public methods don't call them, the linker has
 * no references to satisfy.
 *
 * What this stub DOES NOT provide:
 *
 *   - Actual messaging. Send / receive are no-ops. The expected
 *     fate is replacement by a SMS-backed adapter (S-MP15) that
 *     routes sendText through the Quectel EG912U-GL modem via
 *     hal/sms. That work depends on having a working modem +
 *     contact list and is its own session.
 */

#include "Services/MessageService.h"

#include "esp_log.h"

static const char *TAG = "MessageService(stub)";

/* Global singleton — every TU that does `extern MessageService
 * Messages;` resolves to this definition. */
MessageService Messages;

MessageService::MessageService()
{
    ESP_LOGI(TAG, "stub constructed");
}

void MessageService::begin()
{
    ESP_LOGI(TAG, "begin() — stub no-op");
    /* Real impl would: load Storage::convos, register self as
     * LoRaService receive callback, prime lastMessages from
     * persistent storage. None of that exists in this build. */
}

void MessageService::loop(uint /*micros*/)
{
    /* LoopListener tick. Real impl would dispatch queued
     * inbound packets to listeners. Stub has no queue. */
}

/* ---- send / receive / mutate ------------------------------------ */

Message MessageService::sendText(UID_t /*convo*/, const std::string &text)
{
    ESP_LOGI(TAG, "sendText(\"%s\") — stub no-op", text.c_str());
    return Message();
}

Message MessageService::sendPic(UID_t /*convo*/, uint16_t /*index*/)
{
    return Message();
}

Message MessageService::resend(UID_t /*convo*/, UID_t /*message*/)
{
    return Message();
}

bool MessageService::deleteMessage(UID_t /*convo*/, UID_t /*msg*/)
{
    return false;
}

Message MessageService::getLastMessage(UID_t /*convo*/)
{
    return Message();
}

/* ---- listener convenience wrappers ----------------------------- */

void MessageService::addReceivedListener(MsgReceivedListener *l)
{
    /* WithListeners<MsgReceivedListener>::addListener is inherited.
     * Static_cast to disambiguate which base we want. */
    static_cast<WithListeners<MsgReceivedListener> *>(this)->addListener(l);
}

void MessageService::addChangedListener(MsgChangedListener *l)
{
    static_cast<WithListeners<MsgChangedListener> *>(this)->addListener(l);
}

void MessageService::addUnreadListener(UnreadListener *l)
{
    static_cast<WithListeners<UnreadListener> *>(this)->addListener(l);
}

void MessageService::removeReceivedListener(MsgReceivedListener *l)
{
    static_cast<WithListeners<MsgReceivedListener> *>(this)->removeListener(l);
}

void MessageService::removeChangedListener(MsgChangedListener *l)
{
    static_cast<WithListeners<MsgChangedListener> *>(this)->removeListener(l);
}

void MessageService::removeUnreadListener(UnreadListener *l)
{
    static_cast<WithListeners<UnreadListener> *>(this)->removeListener(l);
}

/* ---- unread / read-state ---------------------------------------- */

bool MessageService::hasUnread() const
{
    return unread;
}

bool MessageService::markRead(UID_t /*convoUID*/)
{
    if (unread) {
        unread = false;
        return true;
    }
    return false;
}

bool MessageService::markUnread(UID_t /*convoUID*/)
{
    if (!unread) {
        unread = true;
        return true;
    }
    return false;
}

bool MessageService::deleteFriend(UID_t /*uid*/)
{
    return false;
}
