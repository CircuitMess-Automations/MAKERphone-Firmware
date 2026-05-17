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
 * PhoneContacts namespace — 28 functions, all no-ops.
 *
 * Header in src/Storage/PhoneContacts.h declares each. Most are
 * either getters (return default) or mutators (return false).
 * ------------------------------------------------------------------ */

namespace PhoneContacts {

PhoneContact getOrDefault(UID_t uid)
{
    PhoneContact c{};
    c.uid = uid;
    return c;
}

bool exists(UID_t /*uid*/)              { return false; }
bool upsert(const PhoneContact &)       { return false; }
bool remove(UID_t /*uid*/)              { return false; }

const char *displayNameOf(UID_t /*uid*/) { return ""; }
uint8_t avatarSeedOf(UID_t /*uid*/)      { return 0; }
uint8_t ringtoneOf(UID_t /*uid*/)        { return 0; }

bool setDisplayName(UID_t, const char *) { return false; }
bool clearDisplayName(UID_t)             { return false; }
bool setAvatarSeed(UID_t, uint8_t)       { return false; }
bool setRingtone(UID_t, uint8_t)         { return false; }
bool setFavorite(UID_t, bool)            { return false; }
bool setMuted(UID_t, bool)               { return false; }
bool setGroup(UID_t, uint8_t)            { return false; }

bool isFavorite(UID_t)                   { return false; }
bool isMuted(UID_t)                      { return false; }

bool markInteraction(UID_t)              { return false; }
bool markInteractionAt(UID_t, uint32_t)  { return false; }

bool setBirthday(UID_t, uint8_t, uint8_t)        { return false; }
bool clearBirthday(UID_t)                        { return false; }
bool hasBirthday(UID_t)                          { return false; }
bool birthdayOf(UID_t, uint8_t *outMonth, uint8_t *outDay)
{
    if (outMonth) *outMonth = 0;
    if (outDay)   *outDay   = 0;
    return false;
}

bool setWallpaper(UID_t, uint8_t)        { return false; }
bool clearWallpaper(UID_t)               { return false; }
bool hasWallpaper(UID_t)                 { return false; }
uint8_t wallpaperOf(UID_t)               { return 0; }

uint8_t deriveSeed(UID_t uid)
{
    /* Small deterministic hash so PhonePixelAvatar gets a stable
     * visual per UID even in stub mode. Mirrors upstream's
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
