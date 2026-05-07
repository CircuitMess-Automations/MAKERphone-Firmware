#include "PhoneCallHistoryStorage.h"

#include <nvs.h>
#include <esp_log.h>
#include <stdio.h>
#include <string.h>

// =====================================================================
// PhoneCallHistoryStorage -- NVS-backed implementation.
//
// Storage layout (single blob "log"):
//
//   [0]      'M'                        (magic byte 0)
//   [1]      'H'                        (magic byte 1, "MakerphoneHistory")
//   [2]      version (currently 1)
//   [3]      count (0..MaxEntries)
//   [4..]    count entries, each kEntryStride bytes:
//              [0]      type (0/1/2)
//              [1..25]  name (nul-terminated, MaxNameLen+1 bytes)
//              [26..34] timestamp (nul-terminated, MaxTsLen+1 bytes)
//              [35..38] durationSeconds (uint32_t LE)
//              [39]     avatarSeed
//
// We keep the entire ring in a single blob for the same reason
// PhoneComposerStorage keeps a slot in a single blob: a partial write
// either matches the magic byte and the count, or it doesn't, so a
// power-loss while persisting can never produce a half-loaded log.
//
// The handle is kept open for the device lifetime once opened, the
// same way Highscore and PhoneComposerStorage manage theirs.
// =====================================================================

namespace {

constexpr const char* kNamespace      = "mpcalls";
constexpr const char* kKey            = "log";
constexpr uint8_t     kMagic0         = 'M';
constexpr uint8_t     kMagic1         = 'H';
constexpr uint8_t     kVersion        = 1;

// Serialised entry stride in bytes. Pinned by static_assert below so a
// future tweak to PhoneCallHistory::Entry surfaces as a build error
// rather than a silent on-disk format drift.
//
//   1 (type) + 25 (name) + 9 (timestamp) + 4 (durationSeconds) + 1 (avatarSeed) = 40
constexpr size_t      kEntryStride    = 1 + (PhoneCallHistory::MaxNameLen + 1)
                                          + (PhoneCallHistory::MaxTsLen   + 1)
                                          + 4 + 1;

constexpr size_t      kHeaderSize     = 4;

// Worst-case blob size: header + every entry slot. ~ 1.3 KiB which is
// fine for both an NVS blob and a stack scratch buffer on the ESP32
// main task.
constexpr size_t      kMaxBlobSize    = kHeaderSize +
                                        (size_t)PhoneCallHistoryStorage::MaxEntries * kEntryStride;

// Pin the shape of the on-disk record. If a future session grows the
// per-entry struct (a new field, a wider name buffer, etc) without
// also bumping kVersion AND walking the layout below, the build
// breaks here -- that is the desired failure mode.
static_assert(PhoneCallHistory::MaxNameLen == 24,
              "PhoneCallHistoryStorage layout assumes MaxNameLen=24; "
              "bump kVersion + walk the load/save offsets if this changes.");
static_assert(PhoneCallHistory::MaxTsLen   == 8,
              "PhoneCallHistoryStorage layout assumes MaxTsLen=8; "
              "bump kVersion + walk the load/save offsets if this changes.");
static_assert(kEntryStride == 40,
              "PhoneCallHistoryStorage entry stride drifted; "
              "bump kVersion + walk the load/save offsets if this changes.");

// Single shared handle, opened lazily, kept alive for the device
// lifetime. Mirrors the Highscore / PhoneComposerStorage pattern.
static nvs_handle s_handle    = 0;
static bool       s_attempted = false;

bool ensureOpen() {
	if(s_handle != 0) return true;
	if(s_attempted && s_handle == 0) {
		// Tried once and failed; do not spam NVS with retries every
		// keypress. A reboot will get a fresh attempt.
		return false;
	}
	s_attempted = true;
	auto err = nvs_open(kNamespace, NVS_READWRITE, &s_handle);
	if(err != ESP_OK) {
		ESP_LOGW("CallHistStorage",
		         "nvs_open(%s) failed: %d -- call-history persistence disabled",
		         kNamespace, (int)err);
		s_handle = 0;
		return false;
	}
	return true;
}

// Pack one PhoneCallHistory::Entry into kEntryStride bytes at `dst`.
void packEntry(const PhoneCallHistory::Entry& e, uint8_t* dst) {
	// type
	dst[0] = (uint8_t)e.type;
	// name (fixed 25-byte field, always nul-terminated by the source
	// per copyName() / copyTimestamp() in PhoneCallHistory.cpp)
	memcpy(dst + 1, e.name, sizeof(e.name));
	// timestamp (fixed 9-byte field)
	memcpy(dst + 1 + sizeof(e.name), e.timestamp, sizeof(e.timestamp));
	// durationSeconds little-endian
	const uint8_t* off = dst + 1 + sizeof(e.name) + sizeof(e.timestamp);
	uint32_t d = e.durationSeconds;
	((uint8_t*)off)[0] = (uint8_t)(d & 0xFF);
	((uint8_t*)off)[1] = (uint8_t)((d >> 8) & 0xFF);
	((uint8_t*)off)[2] = (uint8_t)((d >> 16) & 0xFF);
	((uint8_t*)off)[3] = (uint8_t)((d >> 24) & 0xFF);
	// avatarSeed
	dst[1 + sizeof(e.name) + sizeof(e.timestamp) + 4] = e.avatarSeed;
}

// Unpack kEntryStride bytes at `src` into a PhoneCallHistory::Entry.
// Defensively re-NUL-terminates the string fields so a corrupt blob
// can never let strncpy / lv_label_set_text walk off the end of the
// internal buffer.
void unpackEntry(const uint8_t* src, PhoneCallHistory::Entry& e) {
	uint8_t t = src[0];
	if(t > (uint8_t)PhoneCallHistory::Type::Missed) t = (uint8_t)PhoneCallHistory::Type::Missed;
	e.type = (PhoneCallHistory::Type)t;

	memcpy(e.name, src + 1, sizeof(e.name));
	e.name[sizeof(e.name) - 1] = '\0';

	memcpy(e.timestamp, src + 1 + sizeof(e.name), sizeof(e.timestamp));
	e.timestamp[sizeof(e.timestamp) - 1] = '\0';

	const uint8_t* off = src + 1 + sizeof(e.name) + sizeof(e.timestamp);
	uint32_t d = (uint32_t)off[0]
	           | ((uint32_t)off[1] <<  8)
	           | ((uint32_t)off[2] << 16)
	           | ((uint32_t)off[3] << 24);
	e.durationSeconds = d;

	e.avatarSeed = src[1 + sizeof(e.name) + sizeof(e.timestamp) + 4];
}

} // namespace

void PhoneCallHistoryStorage::begin() {
	(void)ensureOpen();
}

bool PhoneCallHistoryStorage::hasLog() {
	if(!ensureOpen()) return false;
	size_t size = 0;
	auto err = nvs_get_blob(s_handle, kKey, nullptr, &size);
	if(err != ESP_OK)         return false;
	if(size < kHeaderSize)    return false;

	// Peek the count byte without pulling the full blob into RAM. NVS
	// has no partial-blob read, so we copy just the header.
	uint8_t hdr[kHeaderSize] = {};
	size_t  hdrSize = sizeof(hdr);
	err = nvs_get_blob(s_handle, kKey, hdr, &hdrSize);
	if(err != ESP_OK)         return false;
	if(hdrSize < kHeaderSize) return false;
	if(hdr[0] != kMagic0 || hdr[1] != kMagic1) return false;
	if(hdr[2] != kVersion)    return false;
	return hdr[3] > 0;
}

uint8_t PhoneCallHistoryStorage::loadAll(PhoneCallHistory::Entry* out, uint8_t maxOut) {
	if(out == nullptr || maxOut == 0) return 0;
	if(!ensureOpen())                 return 0;

	uint8_t blob[kMaxBlobSize] = {};
	size_t  size = sizeof(blob);
	auto err = nvs_get_blob(s_handle, kKey, blob, &size);
	if(err != ESP_OK)                 return 0;
	if(size < kHeaderSize)            return 0;
	if(blob[0] != kMagic0 || blob[1] != kMagic1) return 0;
	if(blob[2] != kVersion)           return 0;

	uint8_t count = blob[3];
	if(count > MaxEntries) count = MaxEntries;
	if(count > maxOut)     count = maxOut;

	// Defensive: the persisted count must agree with the blob size.
	// If the writer was interrupted or a future format grew the
	// stride, refuse to load rather than walking off the end.
	const size_t needed = kHeaderSize + (size_t)count * kEntryStride;
	if(size < needed)                 return 0;

	for(uint8_t i = 0; i < count; ++i) {
		const uint8_t* src = blob + kHeaderSize + (size_t)i * kEntryStride;
		unpackEntry(src, out[i]);
	}
	return count;
}

bool PhoneCallHistoryStorage::saveAll(const PhoneCallHistory::Entry* entries,
                                     uint8_t                        count) {
	if(entries == nullptr && count > 0) return false;
	if(!ensureOpen())                   return false;
	if(count > MaxEntries) count = MaxEntries;

	uint8_t blob[kMaxBlobSize] = {};
	blob[0] = kMagic0;
	blob[1] = kMagic1;
	blob[2] = kVersion;
	blob[3] = count;

	for(uint8_t i = 0; i < count; ++i) {
		uint8_t* dst = blob + kHeaderSize + (size_t)i * kEntryStride;
		packEntry(entries[i], dst);
	}

	const size_t total = kHeaderSize + (size_t)count * kEntryStride;
	auto err = nvs_set_blob(s_handle, kKey, blob, total);
	if(err != ESP_OK) {
		ESP_LOGW("CallHistStorage", "nvs_set_blob(%s) failed: %d", kKey, (int)err);
		return false;
	}
	err = nvs_commit(s_handle);
	if(err != ESP_OK) {
		ESP_LOGW("CallHistStorage", "nvs_commit(%s) failed: %d", kKey, (int)err);
		return false;
	}
	return true;
}

bool PhoneCallHistoryStorage::clearLog() {
	if(!ensureOpen()) return false;
	auto err = nvs_erase_key(s_handle, kKey);
	if(err == ESP_ERR_NVS_NOT_FOUND) {
		// Already empty -- treat as success so a callsite can safely
		// "clear-then-save" without juggling error codes.
		return true;
	}
	if(err != ESP_OK) {
		ESP_LOGW("CallHistStorage", "nvs_erase_key(%s) failed: %d", kKey, (int)err);
		return false;
	}
	err = nvs_commit(s_handle);
	if(err != ESP_OK) {
		ESP_LOGW("CallHistStorage", "nvs_commit(%s) failed: %d", kKey, (int)err);
		return false;
	}
	return true;
}
