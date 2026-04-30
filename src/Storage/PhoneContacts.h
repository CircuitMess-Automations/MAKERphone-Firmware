#ifndef MAKERPHONE_PHONECONTACTS_H
#define MAKERPHONE_PHONECONTACTS_H

#include <Arduino.h>
#include "../Types.hpp"
#include "../Model/PhoneContact.hpp"

/**
 * PhoneContacts
 *
 * Thin helper layer on top of `Storage.PhoneContacts` (the Repo) that
 * the rest of MAKERphone 2.0 should prefer over poking the repo
 * directly. The job of this layer is to *fuse* the optional override
 * record with the underlying `Friend` so calling code doesn't need
 * to know whether the user has already customised the contact.
 *
 * Typical use:
 *
 *   PhoneContact c = PhoneContacts::getOrDefault(uid);
 *   const char* shown = PhoneContacts::displayNameOf(uid);
 *   uint8_t seed = PhoneContacts::avatarSeedOf(uid);
 *
 * Mutations (`setDisplayName`, `setFavorite`, `markInteraction`...)
 * upsert through the repo and persist immediately.
 *
 * NOTE: this header introduces no UI; it's strictly the persistence
 * surface S36 / S37 / S38 will build on. Wired callsites (e.g. call
 * end -> markInteraction) are deferred to those sessions to keep S35
 * compile-clean and side-effect-free.
 */
namespace PhoneContacts {

// Length of the `displayName` field inclusive of the terminator.
constexpr size_t DisplayNameMax = sizeof(((PhoneContact*) nullptr)->displayName);

// Returns the stored override record for `uid`. If no record exists
// yet, a zero-initialised `PhoneContact` with `uid` set is returned
// (callers can then mutate + `upsert()` it).
PhoneContact getOrDefault(UID_t uid);

// Whether an override record currently exists for this uid.
bool exists(UID_t uid);

// Persist a record. Inserts if missing, updates if present.
bool upsert(const PhoneContact& contact);

// Remove the override layer for `uid`. The underlying `Friend` is
// left untouched.
bool remove(UID_t uid);

// Best-available display name: the override if set, else the paired
// `Friend`'s broadcast nickname, else a placeholder ("Contact").
// The pointer is stable for the lifetime of the call (returns into
// a small static scratch buffer); callers that need to keep the
// string around should `strdup` / copy.
const char* displayNameOf(UID_t uid);

// Best-available avatar seed for `PhonePixelAvatar`. Falls back to
// a deterministic hash of the uid when no override is set, so two
// contacts without a customised seed still look distinct.
uint8_t avatarSeedOf(UID_t uid);

// Best-available ringtone id (S40 default = 0 / Synthwave).
uint8_t ringtoneOf(UID_t uid);

// Convenience setters - read-modify-upsert in one call.
bool setDisplayName(UID_t uid, const char* name);
bool clearDisplayName(UID_t uid);
bool setAvatarSeed(UID_t uid, uint8_t seed);
bool setRingtone(UID_t uid, uint8_t ringtoneId);
bool setFavorite(UID_t uid, bool favorite);
bool setMuted(UID_t uid, bool muted);
bool setGroup(UID_t uid, uint8_t group);

// Predicates.
bool isFavorite(UID_t uid);
bool isMuted(UID_t uid);

// Stamp the last-interaction timestamp with the current `millis()`.
// Future call/message wiring will call this; provided here so the
// hook exists when S36/S37 land.
bool markInteraction(UID_t uid);
bool markInteractionAt(UID_t uid, uint32_t timestamp);

// Deterministic fallback seed derived from a uid - exposed so other
// widgets (e.g. an inbox row that shows an avatar without ever
// touching the repo) match the contacts screen.
uint8_t deriveSeed(UID_t uid);

}

#endif //MAKERPHONE_PHONECONTACTS_H
