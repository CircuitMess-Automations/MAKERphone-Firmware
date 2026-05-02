#include "PhoneComposerStorage.h"
#include "PhoneComposerRtttl.h"

#include <nvs.h>
#include <esp_log.h>
#include <stdio.h>
#include <string.h>

// =====================================================================
// PhoneComposerStorage — NVS-backed implementation.
//
// Storage layout per slot:
//   blob "s0".."s3"  ->  [magic 'M','C'][version=1][reserved=0]
//                        [bpm lo][bpm hi][reserved=0][reserved=0]
//                        [rtttl ...nul-terminated...]
//
// We do NOT split name / bpm / rtttl into separate NVS keys: keeping
// it as a single blob means a partially-written record either
// matches the magic or it doesn't, so power-loss recovery can never
// produce a half-loaded slot.
//
// The handle is kept open for the device lifetime once opened, the
// same way Highscore manages its handle. NVS reads are cheap and
// callers (including the screen) reach for them on user input which
// is already a "slow" path next to the LVGL repaints.
// =====================================================================

namespace {

constexpr const char* kNamespace      = "mpcomp";
constexpr uint8_t     kMagic0         = 'M';
constexpr uint8_t     kMagic1         = 'C';
constexpr uint8_t     kVersion        = 1;
constexpr size_t      kHeaderSize     = 8;

// Worst-case blob size: header + serialised RTTTL (which already
// includes the name, defaults header and the comma-separated body)
// + a generous nul terminator. Capped well above MaxSlots * cap so
// a sane partition still has room for the high-score table next to
// us.
constexpr size_t      kMaxBlobSize    = kHeaderSize +
                                        PhoneComposerRtttl::SerializedCap + 1;

// Single shared handle. nvs_open returns ESP_OK once and the handle
// is good for the device lifetime, mirroring Highscore.cpp.
static nvs_handle s_handle    = 0;
static bool       s_attempted = false;

static const char* slotKey(uint8_t slot) {
	// Static strings keep nvs_set_blob's pointer stable across calls.
	static const char* kKeys[PhoneComposerStorage::MaxSlots] = {
		"s0", "s1", "s2", "s3"
	};
	if(slot >= PhoneComposerStorage::MaxSlots) return nullptr;
	return kKeys[slot];
}

static bool ensureOpen() {
	if(s_handle != 0) return true;
	if(s_attempted && s_handle == 0) {
		// Tried once and failed; don't spam NVS with retries every
		// keypress. If the user reboots the device a fresh attempt
		// will run.
		return false;
	}
	s_attempted = true;
	auto err = nvs_open(kNamespace, NVS_READWRITE, &s_handle);
	if(err != ESP_OK) {
		ESP_LOGW("ComposerStorage",
		         "nvs_open(%s) failed: %d -- composer slots disabled",
		         kNamespace, (int)err);
		s_handle = 0;
		return false;
	}
	return true;
}

} // namespace

void PhoneComposerStorage::begin() {
	(void)ensureOpen();
}

bool PhoneComposerStorage::hasSlot(uint8_t slot) {
	const char* key = slotKey(slot);
	if(key == nullptr) return false;
	if(!ensureOpen())  return false;
	size_t size = 0;
	auto err = nvs_get_blob(s_handle, key, nullptr, &size);
	if(err != ESP_OK) return false;
	return size >= kHeaderSize;
}

bool PhoneComposerStorage::saveSlot(uint8_t                     slot,
									const PhoneComposer::Note*  notes,
									uint8_t                     count,
									const char*                 name,
									uint16_t                    bpm) {
	const char* key = slotKey(slot);
	if(key == nullptr)   return false;
	if(notes == nullptr && count > 0) return false;
	if(!ensureOpen())    return false;

	// Stack-friendly: header + RTTTL string + spare nul. ~ 0.8 KiB
	// in the worst case which is fine for the ESP32 main task.
	uint8_t blob[kMaxBlobSize] = {};

	// Header.
	blob[0] = kMagic0;
	blob[1] = kMagic1;
	blob[2] = kVersion;
	blob[3] = 0;
	blob[4] = (uint8_t)(bpm & 0xFF);
	blob[5] = (uint8_t)((bpm >> 8) & 0xFF);
	blob[6] = 0;
	blob[7] = 0;

	// RTTTL body.
	char* rtttlBuf  = (char*)(blob + kHeaderSize);
	size_t rtttlCap = sizeof(blob) - kHeaderSize;
	size_t bytes = PhoneComposerRtttl::serialize(notes, count, name, bpm,
	                                              rtttlBuf, rtttlCap);
	if(bytes == 0) {
		// Either the buffer was too small (unlikely with the cap above)
		// or the serializer rejected the input. Either way, fall back
		// to writing just the header so a subsequent load returns an
		// empty composition rather than something garbled.
		rtttlBuf[0] = '\0';
		bytes = 0;
	}

	const size_t total = kHeaderSize + bytes + 1; // +1 for the nul
	auto err = nvs_set_blob(s_handle, key, blob, total);
	if(err != ESP_OK) {
		ESP_LOGW("ComposerStorage", "nvs_set_blob(%s) failed: %d",
		         key, (int)err);
		return false;
	}
	err = nvs_commit(s_handle);
	if(err != ESP_OK) {
		ESP_LOGW("ComposerStorage", "nvs_commit(%s) failed: %d",
		         key, (int)err);
		return false;
	}
	return true;
}

bool PhoneComposerStorage::loadSlot(uint8_t                     slot,
									PhoneComposer::Note*        outNotes,
									uint8_t                     maxNotes,
									uint8_t*                    outCount,
									char*                       outName,
									size_t                      outNameLen,
									uint16_t*                   outBpm) {
	const char* key = slotKey(slot);
	if(key == nullptr)         return false;
	if(outNotes == nullptr || maxNotes == 0) return false;
	if(!ensureOpen())          return false;

	uint8_t blob[kMaxBlobSize] = {};
	size_t  size = sizeof(blob);
	auto err = nvs_get_blob(s_handle, key, blob, &size);
	if(err != ESP_OK)          return false;
	if(size < kHeaderSize)     return false;
	if(blob[0] != kMagic0 || blob[1] != kMagic1) return false;
	if(blob[2] != kVersion)    return false;

	const uint16_t bpm = (uint16_t)blob[4] | ((uint16_t)blob[5] << 8);

	// Pull the RTTTL body out of the blob -- nul-terminate
	// defensively in case the writer overwrote it.
	const size_t bodyMax = sizeof(blob) - kHeaderSize - 1;
	const size_t bodyLen = (size > kHeaderSize) ? (size - kHeaderSize) : 0;
	const size_t copyLen = (bodyLen < bodyMax) ? bodyLen : bodyMax;
	char  body[kMaxBlobSize - kHeaderSize] = {};
	memcpy(body, blob + kHeaderSize, copyLen);
	body[copyLen] = '\0';

	uint8_t parsedCount = 0;
	uint16_t parsedBpm  = bpm;
	bool ok = PhoneComposerRtttl::parse(body,
	                                    outNotes, maxNotes, &parsedCount,
	                                    outName, outNameLen,
	                                    &parsedBpm);
	if(!ok) {
		// Empty-but-valid blob (header only) -> treat as a real
		// "saved empty slot" rather than a parse failure. The user
		// pressed save when the buffer was empty.
		if(body[0] == '\0') {
			if(outCount) *outCount = 0;
			if(outBpm)   *outBpm   = bpm;
			if(outName && outNameLen > 0) outName[0] = '\0';
			return true;
		}
		return false;
	}

	if(outCount) *outCount = parsedCount;
	if(outBpm)   *outBpm   = parsedBpm ? parsedBpm : bpm;
	return true;
}

bool PhoneComposerStorage::clearSlot(uint8_t slot) {
	const char* key = slotKey(slot);
	if(key == nullptr) return false;
	if(!ensureOpen())  return false;

	auto err = nvs_erase_key(s_handle, key);
	if(err == ESP_ERR_NVS_NOT_FOUND) {
		// Already empty -- treat as success so the caller doesn't
		// need to differentiate "wasn't there" from "removed".
		return true;
	}
	if(err != ESP_OK) {
		ESP_LOGW("ComposerStorage", "nvs_erase_key(%s) failed: %d",
		         key, (int)err);
		return false;
	}
	return nvs_commit(s_handle) == ESP_OK;
}

bool PhoneComposerStorage::clearAll() {
	bool ok = true;
	for(uint8_t i = 0; i < MaxSlots; ++i) {
		ok = clearSlot(i) && ok;
	}
	return ok;
}
