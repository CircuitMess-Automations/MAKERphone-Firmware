#include "PhoneContacts.h"
#include "Storage.h"
#include "../Model/Friend.hpp"
#include <Arduino.h>
#include <string.h>

namespace PhoneContacts {

static constexpr const char* kPlaceholderName = "Contact";

PhoneContact getOrDefault(UID_t uid){
	if(Storage.PhoneContacts.exists(uid)){
		return Storage.PhoneContacts.get(uid);
	}
	PhoneContact c;
	c.uid = uid;
	return c;
}

bool exists(UID_t uid){
	return Storage.PhoneContacts.exists(uid);
}

bool upsert(const PhoneContact& contact){
	if(Storage.PhoneContacts.exists(contact.uid)){
		return Storage.PhoneContacts.update(contact);
	}
	return Storage.PhoneContacts.add(contact);
}

bool remove(UID_t uid){
	if(!Storage.PhoneContacts.exists(uid)) return false;
	return Storage.PhoneContacts.remove(uid);
}

uint8_t deriveSeed(UID_t uid){
	// 64 -> 8 bit fold (xor of all bytes). Cheap and deterministic;
	// good enough to spread the 256 PhonePixelAvatar variants across
	// random LoRa peer ids.
	uint8_t s = 0;
	for(uint8_t i = 0; i < 8; ++i){
		s ^= (uint8_t) (uid >> (i * 8));
	}
	// Mix once more so that consecutive uids don't look identical
	// after the fold.
	s ^= (s << 3);
	s ^= (s >> 5);
	return s;
}

const char* displayNameOf(UID_t uid){
	// Static scratch buffer - the helper documents the lifetime so
	// callers either copy it out or use it within the same statement.
	static char scratch[DisplayNameMax + 1] = {0};

	if(Storage.PhoneContacts.exists(uid)){
		PhoneContact c = Storage.PhoneContacts.get(uid);
		if(c.flags & ContactFlag_HasDisplayName && c.displayName[0] != 0){
			strncpy(scratch, c.displayName, DisplayNameMax);
			scratch[DisplayNameMax] = 0;
			return scratch;
		}
	}

	if(Storage.Friends.exists(uid)){
		Friend f = Storage.Friends.get(uid);
		if(f.profile.nickname[0] != 0){
			strncpy(scratch, f.profile.nickname, DisplayNameMax);
			scratch[DisplayNameMax] = 0;
			return scratch;
		}
	}

	return kPlaceholderName;
}

uint8_t avatarSeedOf(UID_t uid){
	if(Storage.PhoneContacts.exists(uid)){
		PhoneContact c = Storage.PhoneContacts.get(uid);
		if(c.flags & ContactFlag_HasAvatarSeed){
			return c.avatarSeed;
		}
	}
	return deriveSeed(uid);
}

uint8_t ringtoneOf(UID_t uid){
	if(Storage.PhoneContacts.exists(uid)){
		return Storage.PhoneContacts.get(uid).ringtoneId;
	}
	return 0;
}

bool setDisplayName(UID_t uid, const char* name){
	PhoneContact c = getOrDefault(uid);
	if(name == nullptr || name[0] == 0){
		c.displayName[0] = 0;
		c.flags &= ~ContactFlag_HasDisplayName;
	}else{
		strncpy(c.displayName, name, DisplayNameMax);
		c.displayName[DisplayNameMax - 1] = 0;
		c.flags |= ContactFlag_HasDisplayName;
	}
	return upsert(c);
}

bool clearDisplayName(UID_t uid){
	return setDisplayName(uid, nullptr);
}

bool setAvatarSeed(UID_t uid, uint8_t seed){
	PhoneContact c = getOrDefault(uid);
	c.avatarSeed = seed;
	c.flags |= ContactFlag_HasAvatarSeed;
	return upsert(c);
}

bool setRingtone(UID_t uid, uint8_t ringtoneId){
	PhoneContact c = getOrDefault(uid);
	c.ringtoneId = ringtoneId;
	return upsert(c);
}

bool setFavorite(UID_t uid, bool favorite){
	PhoneContact c = getOrDefault(uid);
	c.favorite = favorite ? 1 : 0;
	return upsert(c);
}

bool setMuted(UID_t uid, bool muted){
	PhoneContact c = getOrDefault(uid);
	if(muted){
		c.flags |= ContactFlag_Muted;
	}else{
		c.flags &= ~ContactFlag_Muted;
	}
	return upsert(c);
}

bool setGroup(UID_t uid, uint8_t group){
	if(group >= ContactGroup_Count) group = ContactGroup_All;
	PhoneContact c = getOrDefault(uid);
	c.group = group;
	return upsert(c);
}

bool isFavorite(UID_t uid){
	if(!Storage.PhoneContacts.exists(uid)) return false;
	return Storage.PhoneContacts.get(uid).favorite != 0;
}

bool isMuted(UID_t uid){
	if(!Storage.PhoneContacts.exists(uid)) return false;
	return (Storage.PhoneContacts.get(uid).flags & ContactFlag_Muted) != 0;
}

bool markInteraction(UID_t uid){
	return markInteractionAt(uid, millis());
}

bool markInteractionAt(UID_t uid, uint32_t timestamp){
	PhoneContact c = getOrDefault(uid);
	c.lastInteraction = timestamp;
	return upsert(c);
}

// ----------------------------------------------------------------------
// S135 — birthday reminders
//
// Two-byte (month, day) per-contact field. Year is intentionally not
// stored — birthday reminders are a recurring "this calendar day"
// notification, not an age tracker. Flag bit gates the read path so
// a zero-initialised record (month=0, day=0) is never accidentally
// treated as "January 0th".
//
// `setBirthday` clamps month to 1..12 and day to 1..31. We accept
// Feb 29 for leap-day birthdays even though PhoneClock uses a
// leap-year-free 28-day February — the reminder simply never matches
// in that case, which is the same nostalgic quirk the original
// Sony Ericsson Organiser had.
// ----------------------------------------------------------------------

bool setBirthday(UID_t uid, uint8_t month, uint8_t day){
	if(month < 1 || month > 12) return false;
	if(day   < 1 || day   > 31) return false;

	PhoneContact c = getOrDefault(uid);
	c.birthdayMonth = month;
	c.birthdayDay   = day;
	c.flags |= ContactFlag_HasBirthday;
	return upsert(c);
}

bool clearBirthday(UID_t uid){
	if(!Storage.PhoneContacts.exists(uid)){
		// Nothing to clear; treat as a no-op success so callers don't
		// need to special-case "never set" vs "set then cleared".
		return true;
	}
	PhoneContact c = Storage.PhoneContacts.get(uid);
	c.birthdayMonth = 0;
	c.birthdayDay   = 0;
	c.flags &= ~ContactFlag_HasBirthday;
	return upsert(c);
}

bool hasBirthday(UID_t uid){
	if(!Storage.PhoneContacts.exists(uid)) return false;
	const PhoneContact c = Storage.PhoneContacts.get(uid);
	if((c.flags & ContactFlag_HasBirthday) == 0) return false;
	if(c.birthdayMonth < 1 || c.birthdayMonth > 12) return false;
	if(c.birthdayDay   < 1 || c.birthdayDay   > 31) return false;
	return true;
}

bool birthdayOf(UID_t uid, uint8_t* outMonth, uint8_t* outDay){
	if(!hasBirthday(uid)) return false;
	const PhoneContact c = Storage.PhoneContacts.get(uid);
	if(outMonth != nullptr) *outMonth = c.birthdayMonth;
	if(outDay   != nullptr) *outDay   = c.birthdayDay;
	return true;
}

}
