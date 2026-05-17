/*
 * mp24/components/chatter_app/shim/LoRaServiceStub.cpp
 *
 * Replacement for src/Services/LoRaService.cpp. The upstream
 * version drives the SX1278/LLCC68 LoRa radio via RadioLib, runs
 * its own FreeRTOS Task for RX/TX, manages packet inbox/outbox,
 * and is the transport beneath MessageService::sendText etc.
 *
 * MP2.4 Decision B replaced LoRa with the Quectel EG912U-GL
 * cellular modem on UART. The eventual replacement for this
 * file is an SMS-adapter that takes a 'LoRa.send(...)' call and
 * routes the MessagePacket through hal/sms instead. Until that
 * lands (planned S-MP15 work), this stub provides every public
 * symbol LoRaService.h declares, with no-op behaviour.
 *
 * Same pattern as MessageServiceStub.cpp / StorageStub.cpp:
 *   - Upstream header (src/Services/LoRaService.h) unchanged.
 *   - Upstream .cpp excluded from chatter_app SRCS (just not
 *     listed).
 *   - shim_includes/RadioLib.h provides the empty LLCC68 class
 *     so the header compiles.
 *
 * Constructor caveats:
 *   The class has two members with no default constructor:
 *   - RingBuffer inputBuffer  (CircuitOS, ctor takes size_t)
 *   - Task task              (CircuitOS, ctor takes name + fn)
 *   Both must be initialised in the member-init list. The
 *   sizes/names are arbitrary — the stub never actually uses
 *   either of them, but they must be constructed.
 *
 * Private methods (LoRaReceive, LoRaSend, LoRaProcessBuffer,
 * LoRaProcessPacket, LoRaProcessPackets, LoRaRandom, encDec,
 * moduleInterrupt, loop) are NOT defined here. They're only
 * callable from within LoRaService methods, and our public
 * methods don't reach them; the linker has no references.
 *
 * Static class members that need definitions:
 *   - PacketHeader / PacketTrailer (uint8_t[8]) — declared but
 *     only referenced internally by the private LoRaProcess*
 *     methods. Not defining them is fine as long as no consumer
 *     references them directly (they're private).
 *   - mux (portMUX_TYPE) — same.
 *   - available (volatile bool) — same.
 *   None of the static members are public, so none need
 *   definitions in the stub.
 */

#include "Services/LoRaService.h"

#include "esp_log.h"

static const char *TAG = "LoRaService(stub)";

/* Helpers for empty ReceivedPacket<T> returns. The struct holds
 * `UID_t sender` + `T* content`; we return sender=0, content=nullptr
 * meaning 'no packet available' — exactly what 'queue empty' should
 * look like to consuming code. */
template <typename T>
static ReceivedPacket<T> empty_received()
{
    ReceivedPacket<T> r;
    r.sender  = 0;
    r.content = nullptr;
    return r;
}

/* Global singleton. Consumers do `extern LoRaService LoRa;` and
 * resolve to this. */
LoRaService LoRa;

LoRaService::LoRaService()
    /* RingBuffer has no default ctor — give it a token size. We
     * never read or write to it in the stub. */
    : inputBuffer(16),
      /* Task has no default ctor — give it a name and a null
       * function pointer. The task is never started by the stub's
       * begin(), so the null function pointer is safe. The size_t
       * stack-size argument has a default value of 2048 in the
       * Task ctor; we pass 0 explicitly to flag this is a no-op
       * placeholder. */
      task("LoRa(stub)", nullptr, 0)
{
    ESP_LOGI(TAG, "stub constructed");
}

bool LoRaService::begin()
{
    ESP_LOGI(TAG, "begin() — stub no-op (cellular replaces LoRa)");
    return true;
}

void LoRaService::send(UID_t /*receiver*/, LoRaPacket::Type /*type*/,
                      const Packet * /*content*/)
{
    /* Real impl serialises content via Packet::pack, queues it for
     * the radio task. Stub drops the packet. */
}

void LoRaService::taskFunc(Task * /*task*/)
{
    /* Static class method that's normally the task entry point.
     * Never invoked by the stub's begin() so this body is just
     * a linker satisfier. */
}

/* ---- receive-queue accessors ------------------------------------ */

ReceivedPacket<MessagePacket> LoRaService::getMessage()
{
    return empty_received<MessagePacket>();
}

ReceivedPacket<ProfilePacket> LoRaService::getProfile()
{
    return empty_received<ProfilePacket>();
}

ReceivedPacket<AdvertisePair> LoRaService::getPairBroadcast()
{
    return empty_received<AdvertisePair>();
}

ReceivedPacket<RequestPair> LoRaService::getPairRequest()
{
    return empty_received<RequestPair>();
}

ReceivedPacket<AckPair> LoRaService::getPairAck()
{
    return empty_received<AckPair>();
}

void LoRaService::clearPairPackets()
{
    /* Real impl drains the three pairing queues. Stub queues are
     * already empty so this is a no-op. */
}

/* ---- random ---------------------------------------------------- */

int32_t LoRaService::rand()
{
    /* Real impl pulls from a 24-byte hardware-derived entropy pool
     * filled by the LoRa task. Stub falls back to Arduino's
     * software random() — deterministic but adequate for any UI
     * code that just wants a non-zero seed (e.g. PhoneFortuneCookie
     * etc., which already have their own seed paths anyway). */
    return random();
}

int32_t LoRaService::rand(int32_t max)
{
    if (max <= 0) return 0;
    return random(max);
}

int32_t LoRaService::rand(int32_t min, int32_t max)
{
    if (max <= min) return min;
    return random(min, max);
}

UID_t LoRaService::randUID()
{
    /* Real impl draws 8 bytes from the entropy pool. Stub builds
     * a UID_t from two random() calls — good enough as a non-zero
     * placeholder for any code that wants a 'fresh UID' (e.g.
     * pair-handshake flows that never run on MP2.4 anyway). */
    uint64_t hi = static_cast<uint64_t>(random()) & 0xffffffffULL;
    uint64_t lo = static_cast<uint64_t>(random()) & 0xffffffffULL;
    return (hi << 32) | lo;
}

/* ---- misc ------------------------------------------------------ */

void LoRaService::copyEncKeys()
{
    /* Real impl walks Storage.Friends and caches each Friend's
     * encKey into a UID->key map for outgoing-packet encryption.
     * Stub has nothing to cache (Storage is itself stubbed and
     * always empty), so this is a no-op. */
}

std::map<UID_t, size_t> *LoRaService::getHashmapCopy()
{
    /* Real impl returns a NEW heap-allocated copy of the hashMap
     * member (caller takes ownership and deletes it). Stub returns
     * a heap-allocated empty map so callers' delete still works. */
    return new std::map<UID_t, size_t>{};
}

void LoRaService::initStateless()
{
    /* Re-enable the radio without resetting it — meaningful only
     * on real hardware after a wake-from-sleep. Stub no-op. */
}
