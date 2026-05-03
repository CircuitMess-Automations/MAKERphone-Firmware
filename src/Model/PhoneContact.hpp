#ifndef MAKERPHONE_PHONECONTACT_HPP
#define MAKERPHONE_PHONECONTACT_HPP

#include <Arduino.h>
#include "Entity.hpp"
#include "../Types.hpp"

/**
 * PhoneContact
 *
 * MAKERphone 2.0 phone-book entry that augments the existing `Friend`
 * model with retro-feature-phone metadata. The UID matches the
 * underlying `Friend` UID (same convention as `Convo`), so a contact
 * is conceptually an *override layer* on top of the paired LoRa peer:
 *
 *   - the `Friend` model stays the source of truth for pairing /
 *     encryption / nickname-as-broadcast-by-the-peer,
 *   - the `PhoneContact` model carries phone-book extras the user
 *     edits locally: a custom display name, a `PhonePixelAvatar` seed
 *     (different from the legacy SPIFFS-backed image avatar id on
 *     `Profile`), a per-contact ringtone, a favorite flag, a group
 *     tag and a "last interaction" timestamp used to sort the recent
 *     list later in Phase F.
 *
 * Persistence uses the same fixed-size-record `Repo<T>` template as
 * `Friend` / `Convo`, so the struct must remain POD and stable in
 * size. A small `reserved[]` tail is kept so that future fields
 * (e.g. mute, pinned-position, custom hue) can be added without a
 * SPIFFS migration.
 *
 * Helpers in `Storage/PhoneContacts.h` know how to combine this
 * record with the underlying `Friend` to produce a "best available"
 * display name / avatar even when the user hasn't yet customised
 * the contact, so calling code (S36 list screen, S37 detail screen,
 * S38 edit screen) doesn't have to special-case the missing entry.
 *
 * S135 — birthday reminders
 *
 * The `birthdayMonth` (1..12) and `birthdayDay` (1..31) fields claim
 * two bytes from the original `reserved[8]` tail without changing
 * the on-disk record size. They are only meaningful when the
 * `ContactFlag_HasBirthday` bit is set in `flags`; otherwise the
 * fields are ignored and the contact is excluded from
 * PhoneBirthdayReminders. Storing a leap-day birthday (Feb 29) is
 * intentionally allowed — the leap-year-free calendar PhoneClock
 * already uses (28-day Feb) means the reminder simply never matches
 * an exact day, which is the same nostalgic quirk the original
 * Sony Ericsson Organiser had with leap-day entries.
 */
struct PhoneContact : Entity {
	// User-edited display name. When `flags & ContactFlag_HasDisplayName`
	// is unset this is ignored and the helpers fall back to the paired
	// peer's broadcast nickname (`Friend::profile.nickname`).
	char displayName[24] = {0};

	// Seed for `PhonePixelAvatar`. Independent from `Profile::avatar`,
	// which still drives the legacy image-based `Avatar` element.
	uint8_t avatarSeed = 0;

	// Ringtone index 0..4 mapping into the (forthcoming) S40 melody
	// table. Default 0 is the "Synthwave" tone.
	uint8_t ringtoneId = 0;

	// Favorite contacts surface at the top of the contacts list and
	// in the dialer's quick-call shortcut.
	uint8_t favorite = 0;

	// Group tag. 0=All / Uncategorized, 1=Family, 2=Friends, 3=Work.
	uint8_t group = 0;

	// Bit flags for "user has explicitly overridden ..." so future
	// edits can distinguish "default" from "intentionally blank".
	uint8_t flags = 0;

	// Padding for 4-byte alignment (kept explicit so sizeof(PhoneContact)
	// is identical across compiles).
	uint8_t _pad = 0;

	// `millis()`-domain timestamp of the most recent call/message
	// activity. 0 means "no interaction recorded yet". The recents
	// sort order in S27 / S36 uses this value.
	uint32_t lastInteraction = 0;

	// S135 — birthday reminder fields. Claim 2 of the original
	// reserved[8] bytes without changing the on-disk record size.
	// Only meaningful when ContactFlag_HasBirthday is set in `flags`.
	// Month is 1..12, day is 1..31. Year is intentionally not stored
	// — birthday reminders are "this month / this day", not "this
	// person turns N", to match the Sony Ericsson Organiser
	// behaviour S135 reproduces.
	uint8_t birthdayMonth = 0;
	uint8_t birthdayDay   = 0;

	// Reserved bytes for future struct evolution (mute toggle, pinned
	// flag, custom palette index, ...). New fields claim from the tail
	// without changing the on-disk record size. Originally 8; S135
	// claimed 2 (birthdayMonth + birthdayDay) so 6 remain.
	uint8_t reserved[6] = {0};
};

// Bit-flag constants for `PhoneContact::flags`.
enum PhoneContactFlag : uint8_t {
	ContactFlag_HasDisplayName = 1 << 0,
	ContactFlag_HasAvatarSeed  = 1 << 1,
	ContactFlag_Muted          = 1 << 2,
	// S135 — set when `birthdayMonth` / `birthdayDay` carry a
	// user-supplied date. Cleared when the user removes the birthday.
	ContactFlag_HasBirthday    = 1 << 3,
};

// Group tag constants for `PhoneContact::group`. Kept tiny so the
// alphabetical contacts list can render a one-glyph badge per row.
enum PhoneContactGroup : uint8_t {
	ContactGroup_All    = 0,
	ContactGroup_Family = 1,
	ContactGroup_Friend = 2,
	ContactGroup_Work   = 3,
	ContactGroup_Count  = 4,
};

#endif //MAKERPHONE_PHONECONTACT_HPP
