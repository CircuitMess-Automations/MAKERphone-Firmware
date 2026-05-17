/*
 * mp24/components/chatter_app/shim/StorageStub.cpp
 *
 * Replacement for the entire upstream Storage layer:
 *   src/Storage/Storage.cpp
 *   src/Storage/Repo.cpp
 *   src/Storage/MessageRepo.cpp
 *   src/Storage/ConvoRepo.cpp
 *   src/Storage/PhoneContacts.cpp
 *
 * All five are excluded from chatter_app SRCS (just not listed).
 * This file provides every public symbol they would have exported,
 * with no-op behaviour and no SPIFFS persistence.
 *
 * Why we're shimming, not compiling the real thing:
 *
 *   The upstream Repo.cpp calls RamFile::open(file, false) — RamFile
 *   is a CircuitOS class that inherits fs::FileImpl. Under arduino-
 *   esp32 3.3.8 + xtensa-esp-elf 14.2.0, FileImpl has more pure
 *   virtuals than RamFile overrides, so std::make_shared<RamFile>
 *   fails with 'invalid new-expression of abstract class type' (see
 *   the revert commit message for S-MP18i). Stubbing the entire
 *   Storage layer at the public-API boundary bypasses the issue
 *   without touching upstream files OR pulling in a patched FileImpl
 *   surface.
 *
 * What this stub provides:
 *
 *   - Repo<T>::* method definitions covering every method declared
 *     in Storage/Repo.h, with explicit template instantiations for
 *     the four T's the codebase uses (Friend, PhoneContact, Message,
 *     Convo). add()/update()/remove() return false (nothing
 *     persisted), get() returns a default-constructed T, all()
 *     returns an empty vector, exists() returns false. Combined
 *     visible behaviour: "the repository is permanently empty",
 *     which is exactly what most call sites need to handle
 *     gracefully on a fresh device anyway.
 *
 *   - MessageRepo::write / MessageRepo::read — no-op overrides.
 *     Never called because Repo<T>::add etc. all short-circuit.
 *
 *   - ConvoRepo::write / ConvoRepo::read — same.
 *
 *   - `Repositories Storage = { ... };` global with the same
 *     aggregate-init shape as upstream's Storage.cpp.
 *
 *   - `Repositories::begin()` — calls each repo's begin() in
 *     sequence (no-ops in the stub).
 *
 *   - The PhoneContacts namespace's 28 functions, each implemented
 *     as the minimal-side-effect no-op that satisfies their
 *     contract: getters return default-constructed values or false,
 *     setters return false (nothing persisted).
 *
 * Eventually-replaced-by: a real Storage layer backed by SPIFFS
 * via arduino-esp32's stock fs::File API (no RamFile dependency).
 * The shim's no-op semantics are correct for a fresh device with
 * no saved messages / contacts / friends — exactly the runtime
 * state we have today. Persistence work lands later.
 */

#include "nvs.h"
#include <cstring>
#include "Storage/Storage.h"
#include "Storage/Repo.h"
#include "Storage/MessageRepo.h"
#include "Storage/ConvoRepo.h"
#include "Storage/PhoneContacts.h"

#include "Model/Friend.hpp"
#include "Model/Convo.hpp"
#include "Model/Message.h"
#include "Model/PhoneContact.hpp"

#include <vector>

/* ------------------------------------------------------------------
 * Repo<T> template method definitions
 *
 * Each method matches the signature in Storage/Repo.h. Bodies are
 * no-ops returning the "empty" sentinel that consuming code already
 * tolerates (false from mutators, default T from get, empty vector
 * from all, false from exists).
 *
 * Explicit template instantiations at the bottom force the compiler
 * to emit the symbols for every T the codebase uses, so the linker
 * resolves cross-TU references.
 * ------------------------------------------------------------------ */

template <typename T>
Repo<T>::Repo(const char *directory_, size_t /*reserve_*/)
    : directory(directory_), cached(false)
{
}

template <typename T>
void Repo<T>::begin(bool cached_)
{
    cached = cached_;
}

template <typename T>
void Repo<T>::reserve(size_t /*count*/)
{
}

template <typename T>
bool Repo<T>::add(const T & /*object*/)
{
    return false;
}

template <typename T>
bool Repo<T>::update(const T & /*object*/)
{
    return false;
}

template <typename T>
bool Repo<T>::remove(UID_t /*uid*/)
{
    return false;
}

template <typename T>
T Repo<T>::get(UID_t /*uid*/, bool /*bypassCache*/)
{
    /* Value-initialized default. Friend / PhoneContact / Convo /
     * Message all have either explicit default-member-init or are
     * trivially constructible, so T{} is well-defined. */
    return T{};
}

template <typename T>
std::vector<UID_t> Repo<T>::all(bool /*bypassCache*/)
{
    return std::vector<UID_t>{};
}

template <typename T>
bool Repo<T>::exists(UID_t /*uid*/)
{
    return false;
}

template <typename T>
void Repo<T>::clear()
{
    cache.clear();
}

template <typename T>
bool Repo<T>::write(fs::File & /*file*/, const T & /*object*/)
{
    /* Real subclasses (MessageRepo, ConvoRepo) override this. Default
     * no-op for the Friend / PhoneContact case where Repo<T> is used
     * directly without a subclass — those types have no Repo override
     * upstream either; they use Repo<T>'s default which is also
     * effectively a no-op there. */
    return false;
}

template <typename T>
bool Repo<T>::read(fs::File & /*file*/, T & /*object*/)
{
    return false;
}

template <typename T>
String Repo<T>::getPath(UID_t /*uid*/)
{
    return String{};
}

/* Explicit instantiation — the linker needs each symbol concrete. */
template class Repo<Friend>;
template class Repo<PhoneContact>;
template class Repo<Message>;
template class Repo<Convo>;

/* ------------------------------------------------------------------
 * MessageRepo / ConvoRepo overridden read+write — no-ops.
 *
 * Constructors are inherited via `using Repo::Repo` in the headers,
 * so we don't need to define them here.
 * ------------------------------------------------------------------ */

bool MessageRepo::write(fs::File & /*file*/, const Message & /*m*/)
{
    return false;
}

bool MessageRepo::read(fs::File & /*file*/, Message & /*m*/)
{
    return false;
}

bool ConvoRepo::write(fs::File & /*file*/, const Convo & /*c*/)
{
    return false;
}

bool ConvoRepo::read(fs::File & /*file*/, Convo & /*c*/)
{
    return false;
}

/* ------------------------------------------------------------------
 * Repositories Storage global + begin()
 *
 * Aggregate init matches upstream Storage.cpp byte-for-byte so any
 * code that constructs a Repositories elsewhere (unlikely) sees the
 * same shape.
 * ------------------------------------------------------------------ */

Repositories Storage = {
    MessageRepo("/Repo/Msg"),
    ConvoRepo("/Repo/Convo", 12),
    Repo<Friend>("/Repo/Friends", 12),
    Repo<PhoneContact>("/Repo/Contacts", 12),
};

void Repositories::begin()
{
    Messages.begin();
    Convos.begin(true);
    Friends.begin(true);
    PhoneContacts.begin(true);
}

/* ------------------------------------------------------------------
 * PhoneContacts namespace — NVS-backed implementation (S-MP23/2).
 *
 * Replaces the previous all-no-op stub. Records live in NVS under
 * the "pc" namespace, one blob per contact (the full PhoneContact
 * struct). The key is a 14-byte base32-encoded UID ('c' prefix +
 * 13 base32 chars = 14 chars, well under NVS_KEY_NAME_MAX_SIZE=15
 * usable). The Repo<PhoneContact> template stays a no-op — the
 * upstream PhoneContacts.cpp implementation goes through
 * Storage.PhoneContacts.exists/get/add/update/remove (Repo<T>),
 * but in this shim the namespace skips Repo<T> entirely and talks
 * to NVS directly. S-MP23/3 will fold the Repo<T> templates onto
 * the same backing store + add a UID index for all().
 *
 * Header in src/Storage/PhoneContacts.h declares each function.
 * Semantics match upstream: getters return defaults / fallbacks
 * when no record is stored, setters read-modify-write the record
 * and persist on every call. Flag bits (ContactFlag_*) decide
 * whether a stored field is "set" or just zero-initialised.
 *
 * NVS commit policy: every mutator commits immediately so a power
 * loss right after a successful return preserves the change. The
 * record is tiny (~40 B) so the write amplification is fine.
 * ------------------------------------------------------------------ */

namespace {

// NVS namespace where every PhoneContact blob lives.
constexpr const char *kPCNamespace = "pc";

// Encode a 64-bit UID into a 14-char NVS key: 'c' + 13 base32 chars
// covering all 65 bits worth of input (we waste 1 bit; fine). Keys
// must fit in NVS_KEY_NAME_MAX_SIZE-1 = 15 chars, so 14 leaves
// plenty of headroom for future prefix changes.
static void uidToKey(UID_t uid, char out[16])
{
    static const char alphabet[] = "0123456789abcdefghijklmnopqrstuv"; // base32
    out[0] = 'c';
    for (int i = 0; i < 13; ++i) {
        unsigned shift = i * 5;
        out[1 + (12 - i)] = alphabet[(uid >> shift) & 0x1F];
    }
    out[14] = '\0';
}

// Try to read the record for `uid` into `out`. Returns true on
// successful read AND when the stored uid matches the requested
// one (defensive in case of a hypothetical hash collision down
// the road). Opens nvs in READONLY; doesn't auto-create the
// namespace if it doesn't exist yet (callers handle the empty
// case the same way they handled the all-no-op stub).
static bool loadRecord(UID_t uid, PhoneContact &out)
{
    nvs_handle_t h;
    if (nvs_open(kPCNamespace, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    char key[16];
    uidToKey(uid, key);
    size_t sz = sizeof(PhoneContact);
    esp_err_t err = nvs_get_blob(h, key, &out, &sz);
    nvs_close(h);
    if (err != ESP_OK || sz != sizeof(PhoneContact)) {
        return false;
    }
    if (out.uid != uid) {
        // Defensive: stored blob's uid disagrees with the lookup
        // key. Treat as missing rather than returning bogus data.
        return false;
    }
    return true;
}

// Write `c` to NVS keyed by c.uid. Opens nvs in READWRITE (which
// creates the namespace on first call) and commits immediately
// so a crash right after the function returns preserves the
// change.
static bool saveRecord(const PhoneContact &c)
{
    nvs_handle_t h;
    if (nvs_open(kPCNamespace, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    char key[16];
    uidToKey(c.uid, key);
    esp_err_t err = nvs_set_blob(h, key, &c, sizeof(PhoneContact));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err == ESP_OK;
}

static bool eraseRecord(UID_t uid)
{
    nvs_handle_t h;
    if (nvs_open(kPCNamespace, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    char key[16];
    uidToKey(uid, key);
    esp_err_t err = nvs_erase_key(h, key);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    // NOT_FOUND is treated as success — the contract is "record is
    // gone after this call", which is already the case if it was
    // never written.
    return err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND;
}

}  // anonymous namespace

namespace PhoneContacts {

PhoneContact getOrDefault(UID_t uid)
{
    PhoneContact c{};
    if (loadRecord(uid, c)) {
        return c;
    }
    c = PhoneContact{};
    c.uid = uid;
    return c;
}

bool exists(UID_t uid)
{
    PhoneContact c{};
    return loadRecord(uid, c);
}

bool upsert(const PhoneContact &contact)
{
    return saveRecord(contact);
}

bool remove(UID_t uid)
{
    // Upstream returns false if the record never existed. Match
    // that semantics so callers that branch on the return value
    // (e.g. "did we actually delete something?") keep working.
    PhoneContact tmp{};
    if (!loadRecord(uid, tmp)) return false;
    return eraseRecord(uid);
}

const char *displayNameOf(UID_t uid)
{
    static char scratch[DisplayNameMax + 1] = {0};
    PhoneContact c{};
    if (loadRecord(uid, c) &&
        (c.flags & ContactFlag_HasDisplayName) &&
        c.displayName[0] != 0) {
        strncpy(scratch, c.displayName, DisplayNameMax);
        scratch[DisplayNameMax] = 0;
        return scratch;
    }
    // No Friend fallback in the shim — Storage.Friends is still
    // the no-op Repo<Friend>, so the upstream "fall back to
    // f.profile.nickname" branch would always miss. Return empty
    // string for now; callers that want a placeholder ("Contact")
    // should layer it on themselves. S-MP23/3 will restore the
    // Friend fallback once Repo<Friend> is NVS-backed too.
    return "";
}

uint8_t avatarSeedOf(UID_t uid)
{
    PhoneContact c{};
    if (loadRecord(uid, c) && (c.flags & ContactFlag_HasAvatarSeed)) {
        return c.avatarSeed;
    }
    return deriveSeed(uid);
}

uint8_t ringtoneOf(UID_t uid)
{
    PhoneContact c{};
    if (loadRecord(uid, c)) {
        return c.ringtoneId;
    }
    return 0;
}

bool setDisplayName(UID_t uid, const char *name)
{
    if (name == nullptr) return false;
    PhoneContact c = getOrDefault(uid);
    strncpy(c.displayName, name, sizeof(c.displayName) - 1);
    c.displayName[sizeof(c.displayName) - 1] = 0;
    c.flags |= ContactFlag_HasDisplayName;
    return saveRecord(c);
}

bool clearDisplayName(UID_t uid)
{
    PhoneContact c{};
    if (!loadRecord(uid, c)) return true;  // nothing to clear
    memset(c.displayName, 0, sizeof(c.displayName));
    c.flags &= ~ContactFlag_HasDisplayName;
    return saveRecord(c);
}

bool setAvatarSeed(UID_t uid, uint8_t seed)
{
    PhoneContact c = getOrDefault(uid);
    c.avatarSeed = seed;
    c.flags |= ContactFlag_HasAvatarSeed;
    return saveRecord(c);
}

bool setRingtone(UID_t uid, uint8_t ringtoneId)
{
    PhoneContact c = getOrDefault(uid);
    c.ringtoneId = ringtoneId;
    return saveRecord(c);
}

bool setFavorite(UID_t uid, bool favorite)
{
    PhoneContact c = getOrDefault(uid);
    c.favorite = favorite ? 1 : 0;
    return saveRecord(c);
}

bool setMuted(UID_t uid, bool muted)
{
    PhoneContact c = getOrDefault(uid);
    if (muted) c.flags |= ContactFlag_Muted;
    else       c.flags &= ~ContactFlag_Muted;
    return saveRecord(c);
}

bool setGroup(UID_t uid, uint8_t group)
{
    PhoneContact c = getOrDefault(uid);
    c.group = group;
    return saveRecord(c);
}

bool isFavorite(UID_t uid)
{
    PhoneContact c{};
    return loadRecord(uid, c) && c.favorite != 0;
}

bool isMuted(UID_t uid)
{
    PhoneContact c{};
    return loadRecord(uid, c) && (c.flags & ContactFlag_Muted) != 0;
}

bool markInteraction(UID_t uid)
{
    return markInteractionAt(uid, millis());
}

bool markInteractionAt(UID_t uid, uint32_t timestamp)
{
    PhoneContact c = getOrDefault(uid);
    c.lastInteraction = timestamp;
    return saveRecord(c);
}

bool setBirthday(UID_t uid, uint8_t month, uint8_t day)
{
    if (month < 1 || month > 12) return false;
    if (day   < 1 || day   > 31) return false;
    PhoneContact c = getOrDefault(uid);
    c.birthdayMonth = month;
    c.birthdayDay   = day;
    c.flags |= ContactFlag_HasBirthday;
    return saveRecord(c);
}

bool clearBirthday(UID_t uid)
{
    PhoneContact c{};
    if (!loadRecord(uid, c)) return true;  // nothing to clear
    c.birthdayMonth = 0;
    c.birthdayDay   = 0;
    c.flags &= ~ContactFlag_HasBirthday;
    return saveRecord(c);
}

bool hasBirthday(UID_t uid)
{
    PhoneContact c{};
    if (!loadRecord(uid, c)) return false;
    if ((c.flags & ContactFlag_HasBirthday) == 0) return false;
    if (c.birthdayMonth < 1 || c.birthdayMonth > 12) return false;
    if (c.birthdayDay   < 1 || c.birthdayDay   > 31) return false;
    return true;
}

bool birthdayOf(UID_t uid, uint8_t *outMonth, uint8_t *outDay)
{
    if (!hasBirthday(uid)) return false;
    PhoneContact c{};
    if (!loadRecord(uid, c)) return false;
    if (outMonth != nullptr) *outMonth = c.birthdayMonth;
    if (outDay   != nullptr) *outDay   = c.birthdayDay;
    return true;
}

bool setWallpaper(UID_t uid, uint8_t styleByte)
{
    PhoneContact c = getOrDefault(uid);
    c.wallpaperStyle = styleByte;
    c.flags |= ContactFlag_HasWallpaper;
    return saveRecord(c);
}

bool clearWallpaper(UID_t uid)
{
    PhoneContact c{};
    if (!loadRecord(uid, c)) return true;  // nothing to clear
    c.wallpaperStyle = 0;
    c.flags &= ~ContactFlag_HasWallpaper;
    return saveRecord(c);
}

bool hasWallpaper(UID_t uid)
{
    PhoneContact c{};
    if (!loadRecord(uid, c)) return false;
    return (c.flags & ContactFlag_HasWallpaper) != 0;
}

uint8_t wallpaperOf(UID_t uid)
{
    PhoneContact c{};
    if (loadRecord(uid, c) && (c.flags & ContactFlag_HasWallpaper)) {
        return c.wallpaperStyle;
    }
    return 0;
}

uint8_t deriveSeed(UID_t uid)
{
    /* Small deterministic hash so PhonePixelAvatar gets a stable
     * visual per UID even with no override. Mirrors upstream's
     * fold-and-xor pattern without depending on any persistence. */
    uint64_t x = static_cast<uint64_t>(uid);
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return static_cast<uint8_t>(x & 0xff);
}

}  /* namespace PhoneContacts */
