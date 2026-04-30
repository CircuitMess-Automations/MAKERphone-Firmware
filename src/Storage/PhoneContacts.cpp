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

}
