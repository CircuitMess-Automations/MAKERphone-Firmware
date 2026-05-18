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
#include <algorithm>

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


/* ------------------------------------------------------------------
 * S-MP23/3 -- Repo<Friend> NVS-backed.
 *
 * The primary template above provides no-op defaults for every
 * Repo<T>. Here we specialize the Friend instantiation to talk to
 * NVS so paired Friends survive a reset. Records live in NVS
 * namespace "frd", one blob per Friend keyed by 14-char base32
 * ('f' + 13 chars). A separate "__idx" blob holds the packed array
 * of UID_t's needed by Repo<Friend>::all().
 *
 * Friend is POD-trivially-copyable (Profile is POD; encKey is a
 * uint8 array), so a raw nvs_set_blob over &Friend is correct.
 * sizeof(Friend) is stable on xtensa-esp-elf because the only
 * non-byte member, Profile::hue, is uint16_t which the platform
 * lays out with no internal padding (uid + 8-aligned struct,
 * Profile 24 bytes, encKey[32] = 64 bytes total).
 *
 * Repo<PhoneContact>, Repo<Message>, Repo<Convo> remain the all-
 * no-op primary template -- the PhoneContacts namespace already
 * has its own NVS-backed implementation (S-MP23/2), and
 * Message/Convo need custom serialisation (they hold non-POD
 * std::string / std::vector / void* members) which lands in a
 * later sub-step.
 *
 * Note: this restores the displayNameOf() Friend fallback that
 * S-MP23/2 dropped, since Storage.Friends.get(uid) now returns
 * the real persisted Friend record.
 * ------------------------------------------------------------------ */

namespace {

// NVS namespace + key scheme for Repo<Friend>.
constexpr const char *kFriendNamespace = "frd";

// Index key inside kFriendNamespace -- holds a packed array of
// UID_t (uint64_t, little-endian on xtensa) listing every stored
// friend. Kept in sync on add/remove. "__idx" is reserved and
// will never collide with a 'f'-prefixed record key.
constexpr const char *kFriendIndexKey = "__idx";

// 'f' + 13 base32 chars = 14-char key, comfortably under
// NVS_KEY_NAME_MAX_SIZE-1 = 15.
static void friendKey(UID_t uid, char out[16])
{
    static const char alphabet[] = "0123456789abcdefghijklmnopqrstuv";
    out[0] = 'f';
    for (int i = 0; i < 13; ++i) {
        unsigned shift = i * 5;
        out[1 + (12 - i)] = alphabet[(uid >> shift) & 0x1F];
    }
    out[14] = '\0';
}

static bool friendLoad(UID_t uid, Friend &out)
{
    nvs_handle_t h;
    if (nvs_open(kFriendNamespace, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    char key[16];
    friendKey(uid, key);
    size_t sz = sizeof(Friend);
    esp_err_t err = nvs_get_blob(h, key, &out, &sz);
    nvs_close(h);
    if (err != ESP_OK || sz != sizeof(Friend)) {
        return false;
    }
    if (out.uid != uid) {
        // Defensive: stored blob disagrees with key. Treat as miss.
        return false;
    }
    return true;
}

static bool friendSave(const Friend &f)
{
    nvs_handle_t h;
    if (nvs_open(kFriendNamespace, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    char key[16];
    friendKey(f.uid, key);
    esp_err_t err = nvs_set_blob(h, key, &f, sizeof(Friend));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err == ESP_OK;
}

static bool friendErase(UID_t uid)
{
    nvs_handle_t h;
    if (nvs_open(kFriendNamespace, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    char key[16];
    friendKey(uid, key);
    esp_err_t err = nvs_erase_key(h, key);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    // NOT_FOUND treated as success (contract: "record is gone").
    return err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND;
}

// Read the friend-index blob into `out`. Returns true on a clean
// read (including the "no index yet" case, which yields an empty
// vector). Returns false only on unexpected NVS errors so callers
// can refuse to corrupt the index.
static bool friendIdxLoad(std::vector<UID_t> &out)
{
    out.clear();
    nvs_handle_t h;
    esp_err_t open = nvs_open(kFriendNamespace, NVS_READONLY, &h);
    if (open == ESP_ERR_NVS_NOT_FOUND) {
        // Namespace doesn't exist yet -- empty repo.
        return true;
    }
    if (open != ESP_OK) {
        return false;
    }
    size_t sz = 0;
    esp_err_t err = nvs_get_blob(h, kFriendIndexKey, nullptr, &sz);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(h);
        return true;  // empty index, treated as no friends.
    }
    if (err != ESP_OK) {
        nvs_close(h);
        return false;
    }
    if (sz == 0 || (sz % sizeof(UID_t)) != 0) {
        nvs_close(h);
        return false;  // corrupt blob -- bail loudly rather than guess.
    }
    out.resize(sz / sizeof(UID_t));
    err = nvs_get_blob(h, kFriendIndexKey, out.data(), &sz);
    nvs_close(h);
    if (err != ESP_OK) {
        out.clear();
        return false;
    }
    return true;
}

static bool friendIdxSave(const std::vector<UID_t> &v)
{
    nvs_handle_t h;
    if (nvs_open(kFriendNamespace, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    esp_err_t err;
    if (v.empty()) {
        err = nvs_erase_key(h, kFriendIndexKey);
        // NOT_FOUND is fine -- already gone.
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    } else {
        err = nvs_set_blob(h, kFriendIndexKey, v.data(),
                           v.size() * sizeof(UID_t));
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err == ESP_OK;
}

static bool friendIdxContains(const std::vector<UID_t> &v, UID_t uid)
{
    for (UID_t u : v) {
        if (u == uid) return true;
    }
    return false;
}

}  // anonymous namespace (S-MP23/3)

/* ------------------------------------------------------------------
 * Per-method specializations for Repo<Friend>.
 *
 * The non-specialized members (begin, reserve, write, read,
 * getPath) fall through to the primary template, which is fine --
 * write/read are protected and unused here, getPath is unused, and
 * begin/reserve are no-ops by design (the cache member exists but
 * we don't use it; reads always hit NVS).
 * ------------------------------------------------------------------ */

template <>
bool Repo<Friend>::add(const Friend &object)
{
    std::vector<UID_t> idx;
    if (!friendIdxLoad(idx)) return false;
    // Treat add-with-existing-uid as a no-op on the index side
    // and as a blob overwrite on the record side. PairService
    // checks exists() before calling add(), so the duplicate
    // case is defensive only.
    if (!friendIdxContains(idx, object.uid)) {
        idx.push_back(object.uid);
        if (!friendIdxSave(idx)) return false;
    }
    return friendSave(object);
}

template <>
bool Repo<Friend>::update(const Friend &object)
{
    // Upstream Repo<T>::update only persists if the record exists;
    // mirror that semantics so callers that branch on the return
    // can detect "tried to update a missing friend".
    std::vector<UID_t> idx;
    if (!friendIdxLoad(idx)) return false;
    if (!friendIdxContains(idx, object.uid)) return false;
    return friendSave(object);
}

template <>
bool Repo<Friend>::remove(UID_t uid)
{
    std::vector<UID_t> idx;
    if (!friendIdxLoad(idx)) return false;
    if (!friendIdxContains(idx, uid)) {
        // Already gone -- upstream behaviour returns false.
        return false;
    }
    // Erase blob first; if the blob erase succeeds but the index
    // save fails, on next boot we'll have an index entry pointing
    // to a missing record. friendLoad() handles that defensively
    // (returns false), so a partial failure degrades to "Friend
    // looks missing", not corruption.
    if (!friendErase(uid)) return false;
    idx.erase(std::remove(idx.begin(), idx.end(), uid), idx.end());
    return friendIdxSave(idx);
}

template <>
Friend Repo<Friend>::get(UID_t uid, bool /*bypassCache*/)
{
    Friend f{};
    if (friendLoad(uid, f)) {
        return f;
    }
    // Match the no-op stub: return a default-constructed Friend on
    // miss. Upstream Repo<T>::get also returns T{} on a miss after
    // logging a warning. The uid field stays 0, which call sites
    // tolerate (e.g. PhoneCallService checks exists() first).
    return Friend{};
}

template <>
std::vector<UID_t> Repo<Friend>::all(bool /*bypassCache*/)
{
    std::vector<UID_t> idx;
    if (!friendIdxLoad(idx)) {
        return std::vector<UID_t>{};
    }
    return idx;
}

template <>
bool Repo<Friend>::exists(UID_t uid)
{
    std::vector<UID_t> idx;
    if (!friendIdxLoad(idx)) return false;
    return friendIdxContains(idx, uid);
}

template <>
void Repo<Friend>::clear()
{
    // Walk the index, erase each blob, then clear the index.
    // We open the namespace in READWRITE once would be ideal, but
    // the per-key helper already keeps each commit atomic; the
    // cost (a few extra commits) is negligible for a "clear all
    // friends" operation that essentially never runs.
    std::vector<UID_t> idx;
    if (friendIdxLoad(idx)) {
        for (UID_t u : idx) {
            (void)friendErase(u);
        }
    }
    std::vector<UID_t> empty;
    (void)friendIdxSave(empty);
    cache.clear();
}


/* ------------------------------------------------------------------
 * Repo<PhoneContact> NVS-backed (S-MP23/4)
 *
 * Same pattern as Repo<Friend> in S-MP23/3 -- one blob per record
 * keyed by base32 of the UID, plus an index blob holding the packed
 * array of stored UIDs. The differences:
 *
 *   - NVS namespace is `"pc"`, shared with the PhoneContacts
 *     namespace's per-contact records from S-MP23/2. Keeping both
 *     in one namespace avoids fragmenting the contact store across
 *     two NVS namespaces; the actual blobs don't collide because:
 *       * PhoneContacts namespace writes keys `"c<base32>"`,
 *       * Repo<PhoneContact> writes keys `"p<base32>"`,
 *       * the index lives at `"__idx"` (reserved, no record key
 *         starts with `_`).
 *     The two layers therefore each maintain their own per-UID
 *     blob; in normal use both layers get poked at the same UIDs
 *     by upstream code, but the storage paths are independent.
 *
 *   - PhoneContact is POD (Entity + char[24] + 6*uint8 + uint32 +
 *     3*uint8 + uint8[5] = ~52 bytes once aligned), no internal
 *     pointers or std::* members. Raw `nvs_set_blob` over
 *     `&PhoneContact` is correct on xtensa-esp-elf.
 *
 *   - Partial-failure semantics mirror the Friend code: on remove,
 *     erase the blob first; if the index save fails afterwards, the
 *     index briefly points to a missing record and pcRepoLoad()
 *     returns false (defensive uid mismatch check), so the orphan
 *     degrades to "PhoneContact looks missing" rather than corruption.
 *
 *   - The PhoneContacts namespace's `upsert`/`remove` from S-MP23/2
 *     does NOT update the Repo<PhoneContact> index, and vice versa.
 *     Upstream code that walks `Storage.PhoneContacts.all()` via the
 *     Repo<PhoneContact> path sees only what Repo::add() recorded;
 *     code that calls `PhoneContacts::exists(uid)` directly still
 *     works against its own keystore. Unifying the two layers (so
 *     PhoneContacts::upsert auto-maintains the Repo index) is a
 *     separate fire -- the existing call sites today either go
 *     through one path or the other, not both, so leaving them
 *     independent doesn't break any current consumer.
 * ------------------------------------------------------------------ */

namespace {

// NVS namespace shared with the PhoneContacts namespace (S-MP23/2).
constexpr const char *kPCRepoNamespace = "pc";

// Index key inside kPCRepoNamespace -- holds a packed array of
// UID_t. Reserved prefix `_` means no record key can collide.
constexpr const char *kPCRepoIndexKey = "__idx";

// 'p' + 13 base32 chars = 14-char key, comfortably under
// NVS_KEY_NAME_MAX_SIZE-1 = 15. The prefix differs from the
// PhoneContacts namespace's `'c'` so the two layers don't shadow
// each other's blobs inside the shared `"pc"` namespace.
static void pcRepoKey(UID_t uid, char out[16])
{
    static const char alphabet[] = "0123456789abcdefghijklmnopqrstuv";
    out[0] = 'p';
    for (int i = 0; i < 13; ++i) {
        unsigned shift = i * 5;
        out[1 + (12 - i)] = alphabet[(uid >> shift) & 0x1F];
    }
    out[14] = '\0';
}

static bool pcRepoLoad(UID_t uid, PhoneContact &out)
{
    nvs_handle_t h;
    if (nvs_open(kPCRepoNamespace, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    char key[16];
    pcRepoKey(uid, key);
    size_t sz = sizeof(PhoneContact);
    esp_err_t err = nvs_get_blob(h, key, &out, &sz);
    nvs_close(h);
    if (err != ESP_OK || sz != sizeof(PhoneContact)) {
        return false;
    }
    if (out.uid != uid) {
        // Defensive: stored blob disagrees with key. Treat as miss.
        return false;
    }
    return true;
}

static bool pcRepoSave(const PhoneContact &c)
{
    nvs_handle_t h;
    if (nvs_open(kPCRepoNamespace, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    char key[16];
    pcRepoKey(c.uid, key);
    esp_err_t err = nvs_set_blob(h, key, &c, sizeof(PhoneContact));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err == ESP_OK;
}

static bool pcRepoErase(UID_t uid)
{
    nvs_handle_t h;
    if (nvs_open(kPCRepoNamespace, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    char key[16];
    pcRepoKey(uid, key);
    esp_err_t err = nvs_erase_key(h, key);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    // NOT_FOUND treated as success (contract: "record is gone").
    return err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND;
}

// Read the index blob. Returns true on clean read (including the
// "no index yet" case → empty vector). Returns false only on
// unexpected NVS errors so callers can refuse to corrupt it.
static bool pcRepoIdxLoad(std::vector<UID_t> &out)
{
    out.clear();
    nvs_handle_t h;
    esp_err_t open = nvs_open(kPCRepoNamespace, NVS_READONLY, &h);
    if (open == ESP_ERR_NVS_NOT_FOUND) {
        return true;  // namespace doesn't exist yet → empty repo
    }
    if (open != ESP_OK) {
        return false;
    }
    size_t sz = 0;
    esp_err_t err = nvs_get_blob(h, kPCRepoIndexKey, nullptr, &sz);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(h);
        return true;  // empty index
    }
    if (err != ESP_OK) {
        nvs_close(h);
        return false;
    }
    if (sz == 0 || (sz % sizeof(UID_t)) != 0) {
        nvs_close(h);
        return false;  // corrupt blob -- bail loudly
    }
    out.resize(sz / sizeof(UID_t));
    err = nvs_get_blob(h, kPCRepoIndexKey, out.data(), &sz);
    nvs_close(h);
    if (err != ESP_OK) {
        out.clear();
        return false;
    }
    return true;
}

static bool pcRepoIdxSave(const std::vector<UID_t> &v)
{
    nvs_handle_t h;
    if (nvs_open(kPCRepoNamespace, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    esp_err_t err;
    if (v.empty()) {
        err = nvs_erase_key(h, kPCRepoIndexKey);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    } else {
        err = nvs_set_blob(h, kPCRepoIndexKey, v.data(),
                           v.size() * sizeof(UID_t));
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err == ESP_OK;
}

static bool pcRepoIdxContains(const std::vector<UID_t> &v, UID_t uid)
{
    for (UID_t u : v) {
        if (u == uid) return true;
    }
    return false;
}

}  // anonymous namespace (S-MP23/4)

/* ------------------------------------------------------------------
 * Per-method specializations for Repo<PhoneContact>.
 *
 * Mirrors Repo<Friend> from S-MP23/3 verbatim apart from the type
 * and helper names. Non-specialized members (begin/reserve/write/
 * read/getPath) fall through to the primary template, which is
 * fine: write/read are protected and unused, begin/reserve are
 * no-ops by design (we never use the cache member -- reads always
 * hit NVS).
 * ------------------------------------------------------------------ */

template <>
bool Repo<PhoneContact>::add(const PhoneContact &object)
{
    std::vector<UID_t> idx;
    if (!pcRepoIdxLoad(idx)) return false;
    if (!pcRepoIdxContains(idx, object.uid)) {
        idx.push_back(object.uid);
        if (!pcRepoIdxSave(idx)) return false;
    }
    return pcRepoSave(object);
}

template <>
bool Repo<PhoneContact>::update(const PhoneContact &object)
{
    // Upstream Repo<T>::update only persists if the record exists;
    // mirror that so callers branching on the return can detect
    // "tried to update a missing contact".
    std::vector<UID_t> idx;
    if (!pcRepoIdxLoad(idx)) return false;
    if (!pcRepoIdxContains(idx, object.uid)) return false;
    return pcRepoSave(object);
}

template <>
bool Repo<PhoneContact>::remove(UID_t uid)
{
    std::vector<UID_t> idx;
    if (!pcRepoIdxLoad(idx)) return false;
    if (!pcRepoIdxContains(idx, uid)) {
        return false;  // already gone -- match upstream
    }
    // Erase blob first; on partial failure (blob gone, index still
    // referencing) pcRepoLoad() returns false because the missing
    // blob no longer satisfies sz/uid checks, so the orphan
    // degrades to "PhoneContact looks missing" not corruption.
    if (!pcRepoErase(uid)) return false;
    idx.erase(std::remove(idx.begin(), idx.end(), uid), idx.end());
    return pcRepoIdxSave(idx);
}

template <>
PhoneContact Repo<PhoneContact>::get(UID_t uid, bool /*bypassCache*/)
{
    PhoneContact c{};
    if (pcRepoLoad(uid, c)) {
        return c;
    }
    // Match the no-op stub: default-constructed PhoneContact on
    // miss. Upstream Repo<T>::get also returns T{} after warning.
    // The uid field stays 0, which call sites tolerate (they
    // check exists() first in practice).
    return PhoneContact{};
}

template <>
std::vector<UID_t> Repo<PhoneContact>::all(bool /*bypassCache*/)
{
    std::vector<UID_t> idx;
    if (!pcRepoIdxLoad(idx)) {
        return std::vector<UID_t>{};
    }
    return idx;
}

template <>
bool Repo<PhoneContact>::exists(UID_t uid)
{
    std::vector<UID_t> idx;
    if (!pcRepoIdxLoad(idx)) return false;
    return pcRepoIdxContains(idx, uid);
}

template <>
void Repo<PhoneContact>::clear()
{
    // Walk the index, erase each blob, then clear the index.
    // Per-key helpers keep each commit atomic; the extra commit
    // cost is negligible for a "clear all contacts" operation.
    std::vector<UID_t> idx;
    if (pcRepoIdxLoad(idx)) {
        for (UID_t u : idx) {
            (void)pcRepoErase(u);
        }
    }
    std::vector<UID_t> empty;
    (void)pcRepoIdxSave(empty);
    cache.clear();
}


/* ------------------------------------------------------------------
 * Repo<Message> NVS-backed (S-MP23/5)
 *
 * Unlike Friend (POD) and PhoneContact (POD), Message holds non-POD
 * state: a `void* content` that points at a heap-allocated
 * `std::string` (TEXT) or `uint8_t` (PIC). Raw nvs_set_blob over
 * &Message would persist the pointer value, not the payload, and
 * round-trip into garbage. So we serialise to a packed wire format
 * with an explicit variable-length tail:
 *
 *     [ uid:8 | convo:8 | flags:1 | type:1 | len:2 | data:N ]
 *
 *   flags bit0 = outgoing, bit1 = received    (remaining bits zero)
 *   type  matches Message::Type enum:
 *     0 = TEXT, 1 = PIC, 2 = NONE              (cast-stable)
 *   len   = N (uint16 LE)
 *   data  = N bytes:
 *     TEXT  → std::string contents, no NUL terminator
 *     PIC   → 1 byte picIndex
 *     NONE  → no bytes, len=0
 *
 * Fixed header is 20 bytes; total blob is 20 + N. NVS blob size is
 * comfortably bounded — N is capped at kMsgTextCap below to keep us
 * safely under the per-blob limit on the ESP-IDF NVS partition.
 *
 * Records live in NVS namespace "msg", keyed by 14-char base32
 * ('m' + 13 chars). A separate "__idx" blob holds the packed array
 * of UID_t's needed by Repo<Message>::all().
 *
 * MessageRepo::write / MessageRepo::read remain no-op overrides
 * below — our specializations of Repo<Message>::add/update/get/etc.
 * never reach them. Same arrangement as Friend / PhoneContact.
 *
 * Repo<Convo> stays the all-no-op primary template; it ships in
 * the follow-up sub-step (S-MP23/6) because Convo's variable-length
 * messages-vector needs its own serialisation.
 * ------------------------------------------------------------------ */

namespace {

// NVS namespace for Repo<Message>. Distinct from "frd" (Friend),
// "pc" (PhoneContact + the PhoneContacts namespace), and any
// future "cnv" / "msg" namespaces, so no key prefix collisions
// are possible.
constexpr const char *kMsgRepoNamespace = "msg";

// Index key inside kMsgRepoNamespace. The reserved underscore
// prefix can't collide with a record key (all record keys start
// with 'm').
constexpr const char *kMsgRepoIndexKey = "__idx";

// Cap on the serialised TEXT payload. NVS blobs on ESP-IDF v5.5
// can in principle grow to ~4000 bytes, but the chat UI tops out
// long before that. 2048 leaves comfortable headroom and refuses
// to persist absurd payloads silently.
constexpr size_t kMsgTextCap = 2048;

// Fixed header size on the wire (see comment above).
constexpr size_t kMsgHdrSize = 8 /*uid*/ + 8 /*convo*/ + 1 /*flags*/
                             + 1 /*type*/ + 2 /*len*/;  // = 20

// 'm' + 13 base32 chars = 14-char key, under
// NVS_KEY_NAME_MAX_SIZE-1 = 15.
static void msgKey(UID_t uid, char out[16])
{
    static const char alphabet[] = "0123456789abcdefghijklmnopqrstuv";
    out[0] = 'm';
    for (int i = 0; i < 13; ++i) {
        unsigned shift = i * 5;
        out[1 + (12 - i)] = alphabet[(uid >> shift) & 0x1F];
    }
    out[14] = '\0';
}

// Serialise Message → packed blob. Returns false if the message
// is in a corrupt state (TEXT with null content, length over cap)
// rather than persist garbage.
static bool msgSerialize(const Message &m, std::vector<uint8_t> &out)
{
    Message::Type t = m.getType();
    uint16_t len = 0;
    const uint8_t *body = nullptr;
    uint8_t picByte = 0;
    std::string textCopy;

    if (t == Message::TEXT) {
        textCopy = m.getText();
        if (textCopy.size() > kMsgTextCap) return false;
        len = static_cast<uint16_t>(textCopy.size());
        body = reinterpret_cast<const uint8_t *>(textCopy.data());
    } else if (t == Message::PIC) {
        picByte = m.getPic();
        len = 1;
        body = &picByte;
    } else {
        // NONE — no body.
        len = 0;
        body = nullptr;
    }

    out.resize(kMsgHdrSize + len);
    uint8_t *p = out.data();

    UID_t uid = m.uid;
    UID_t convo = m.convo;
    for (int i = 0; i < 8; ++i) { p[i]     = static_cast<uint8_t>(uid   >> (i * 8)); }
    for (int i = 0; i < 8; ++i) { p[8 + i] = static_cast<uint8_t>(convo >> (i * 8)); }
    uint8_t flags = 0;
    if (m.outgoing) flags |= 0x1;
    if (m.received) flags |= 0x2;
    p[16] = flags;
    p[17] = static_cast<uint8_t>(t);
    p[18] = static_cast<uint8_t>(len & 0xFF);
    p[19] = static_cast<uint8_t>((len >> 8) & 0xFF);
    if (len && body) {
        std::memcpy(p + kMsgHdrSize, body, len);
    }
    return true;
}

// Deserialise packed blob → Message. Returns false on any format
// violation (short blob, unknown type, len mismatch). On success
// `out` is freshly populated; any prior content is cleared by
// Message's setText / setPic / =Message{}.
static bool msgDeserialize(const uint8_t *buf, size_t sz, Message &out)
{
    if (sz < kMsgHdrSize) return false;
    UID_t uid = 0, convo = 0;
    for (int i = 0; i < 8; ++i) { uid   |= static_cast<UID_t>(buf[i])     << (i * 8); }
    for (int i = 0; i < 8; ++i) { convo |= static_cast<UID_t>(buf[8 + i]) << (i * 8); }
    uint8_t flags = buf[16];
    uint8_t typeByte = buf[17];
    uint16_t len = static_cast<uint16_t>(buf[18]) |
                   (static_cast<uint16_t>(buf[19]) << 8);
    if (kMsgHdrSize + len != sz) return false;

    Message fresh;
    if (typeByte == Message::TEXT) {
        if (len > kMsgTextCap) return false;
        std::string text(reinterpret_cast<const char *>(buf + kMsgHdrSize), len);
        fresh.setText(text);
    } else if (typeByte == Message::PIC) {
        if (len != 1) return false;
        fresh.setPic(buf[kMsgHdrSize]);
    } else if (typeByte == Message::NONE) {
        if (len != 0) return false;
        // type stays NONE by default-construction.
    } else {
        return false;  // unknown type byte
    }
    fresh.uid = uid;
    fresh.convo = convo;
    fresh.outgoing = (flags & 0x1) != 0;
    fresh.received = (flags & 0x2) != 0;
    out = fresh;  // operator= deep-copies the content
    return true;
}

static bool msgLoad(UID_t uid, Message &out)
{
    nvs_handle_t h;
    if (nvs_open(kMsgRepoNamespace, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    char key[16];
    msgKey(uid, key);
    size_t sz = 0;
    esp_err_t err = nvs_get_blob(h, key, nullptr, &sz);
    if (err != ESP_OK || sz < kMsgHdrSize || sz > kMsgHdrSize + kMsgTextCap) {
        nvs_close(h);
        return false;
    }
    std::vector<uint8_t> buf(sz);
    err = nvs_get_blob(h, key, buf.data(), &sz);
    nvs_close(h);
    if (err != ESP_OK) return false;
    if (!msgDeserialize(buf.data(), sz, out)) return false;
    if (out.uid != uid) return false;  // defensive: key/blob mismatch
    return true;
}

static bool msgSave(const Message &m)
{
    std::vector<uint8_t> buf;
    if (!msgSerialize(m, buf)) return false;
    nvs_handle_t h;
    if (nvs_open(kMsgRepoNamespace, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    char key[16];
    msgKey(m.uid, key);
    esp_err_t err = nvs_set_blob(h, key, buf.data(), buf.size());
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err == ESP_OK;
}

static bool msgErase(UID_t uid)
{
    nvs_handle_t h;
    if (nvs_open(kMsgRepoNamespace, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    char key[16];
    msgKey(uid, key);
    esp_err_t err = nvs_erase_key(h, key);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND;
}

static bool msgIdxLoad(std::vector<UID_t> &out)
{
    out.clear();
    nvs_handle_t h;
    esp_err_t open = nvs_open(kMsgRepoNamespace, NVS_READONLY, &h);
    if (open == ESP_ERR_NVS_NOT_FOUND) {
        return true;  // namespace doesn't exist yet → empty repo
    }
    if (open != ESP_OK) return false;
    size_t sz = 0;
    esp_err_t err = nvs_get_blob(h, kMsgRepoIndexKey, nullptr, &sz);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(h);
        return true;  // empty index
    }
    if (err != ESP_OK) {
        nvs_close(h);
        return false;
    }
    if (sz == 0 || (sz % sizeof(UID_t)) != 0) {
        nvs_close(h);
        return false;  // corrupt blob
    }
    out.resize(sz / sizeof(UID_t));
    err = nvs_get_blob(h, kMsgRepoIndexKey, out.data(), &sz);
    nvs_close(h);
    if (err != ESP_OK) {
        out.clear();
        return false;
    }
    return true;
}

static bool msgIdxSave(const std::vector<UID_t> &v)
{
    nvs_handle_t h;
    if (nvs_open(kMsgRepoNamespace, NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    esp_err_t err;
    if (v.empty()) {
        err = nvs_erase_key(h, kMsgRepoIndexKey);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    } else {
        err = nvs_set_blob(h, kMsgRepoIndexKey, v.data(),
                           v.size() * sizeof(UID_t));
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err == ESP_OK;
}

static bool msgIdxContains(const std::vector<UID_t> &v, UID_t uid)
{
    for (UID_t u : v) {
        if (u == uid) return true;
    }
    return false;
}

}  // anonymous namespace (S-MP23/5)

/* ------------------------------------------------------------------
 * Per-method specializations for Repo<Message>.
 *
 * Mirrors Repo<PhoneContact> in shape, with msgSave/msgLoad in
 * place of pcRepoSave/pcRepoLoad — those route through the
 * serialiser instead of a raw blob copy. Non-specialized members
 * (begin/reserve/write/read/getPath) fall through to the primary
 * template, same as Friend / PhoneContact.
 * ------------------------------------------------------------------ */

template <>
bool Repo<Message>::add(const Message &object)
{
    std::vector<UID_t> idx;
    if (!msgIdxLoad(idx)) return false;
    if (!msgIdxContains(idx, object.uid)) {
        idx.push_back(object.uid);
        if (!msgIdxSave(idx)) return false;
    }
    return msgSave(object);
}

template <>
bool Repo<Message>::update(const Message &object)
{
    std::vector<UID_t> idx;
    if (!msgIdxLoad(idx)) return false;
    if (!msgIdxContains(idx, object.uid)) return false;
    return msgSave(object);
}

template <>
bool Repo<Message>::remove(UID_t uid)
{
    std::vector<UID_t> idx;
    if (!msgIdxLoad(idx)) return false;
    if (!msgIdxContains(idx, uid)) return false;
    if (!msgErase(uid)) return false;
    idx.erase(std::remove(idx.begin(), idx.end(), uid), idx.end());
    return msgIdxSave(idx);
}

template <>
Message Repo<Message>::get(UID_t uid, bool /*bypassCache*/)
{
    Message m;
    if (msgLoad(uid, m)) {
        return m;
    }
    // Match the no-op stub: default-constructed Message on miss.
    // uid stays 0, type stays NONE, content stays nullptr.
    return Message{};
}

template <>
std::vector<UID_t> Repo<Message>::all(bool /*bypassCache*/)
{
    std::vector<UID_t> idx;
    if (!msgIdxLoad(idx)) {
        return std::vector<UID_t>{};
    }
    return idx;
}

template <>
bool Repo<Message>::exists(UID_t uid)
{
    std::vector<UID_t> idx;
    if (!msgIdxLoad(idx)) return false;
    return msgIdxContains(idx, uid);
}

template <>
void Repo<Message>::clear()
{
    std::vector<UID_t> idx;
    if (msgIdxLoad(idx)) {
        for (UID_t u : idx) {
            (void)msgErase(u);
        }
    }
    std::vector<UID_t> empty;
    (void)msgIdxSave(empty);
    cache.clear();
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
    // S-MP23/3 -- restored Friend fallback. Repo<Friend> is now
    // NVS-backed, so a paired peer's broadcast nickname is the
    // sensible second-tier display name when the user hasn't
    // entered a custom contact name. Mirrors upstream
    // PhoneContacts.cpp.
    if (Storage.Friends.exists(uid)) {
        Friend f = Storage.Friends.get(uid);
        if (f.profile.nickname[0] != 0) {
            strncpy(scratch, f.profile.nickname, DisplayNameMax);
            scratch[DisplayNameMax] = 0;
            return scratch;
        }
    }
    // Last resort: empty string. Callers that want a placeholder
    // ("Contact") should layer it on themselves.
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
