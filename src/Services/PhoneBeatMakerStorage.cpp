#include "PhoneBeatMakerStorage.h"

#include <nvs.h>
#include <esp_log.h>
#include <stdio.h>
#include <string.h>

// =====================================================================
// PhoneBeatMakerStorage -- NVS-backed implementation.
//
// Storage layout (single 16-byte blob "pat"):
//
//   [0]      'M'                        (magic byte 0)
//   [1]      'B'                        (magic byte 1, "MakerphoneBeat")
//   [2]      version (currently 1)
//   [3]      reserved (must be 0)
//   [4]      bpm (clamped to PhoneBeatMaker::MinBpm..MaxBpm)
//   [5..7]   reserved (must be 0)
//   [8..15]  pattern bits, track-major (LSB = step 0)
//
// We keep the entire payload in a single blob for the same reason
// PhoneCallHistoryStorage / PhoneComposerStorage do: a partial write
// either matches the magic + version or it doesn't, so a power-loss
// while persisting can never produce a half-loaded pattern.
//
// The handle is kept open for the device lifetime once opened, the
// same way Highscore / PhoneComposerStorage / PhoneCallHistoryStorage
// manage theirs.
// =====================================================================

namespace {

constexpr const char* kNamespace      = "mpbeat";
constexpr const char* kKey            = "pat";
constexpr uint8_t     kMagic0         = 'M';
constexpr uint8_t     kMagic1         = 'B';
constexpr uint8_t     kVersion        = 1;

constexpr size_t      kHeaderSize     = 4;   // magic + version + reserved
constexpr size_t      kBpmOffset      = 4;
constexpr size_t      kPatternOffset  = 8;   // bit-packed grid begins here
constexpr size_t      kPatternBytes   =
    ((size_t)PhoneBeatMaker::NumTracks * PhoneBeatMaker::NumSteps + 7) / 8;
constexpr size_t      kBlobSize       = kPatternOffset + kPatternBytes;

// Pin the on-disk shape. If a future session changes the grid
// geometry without bumping kVersion AND walking the load/save
// offsets below, the build breaks here -- that is the desired
// failure mode.
static_assert(PhoneBeatMaker::NumTracks == 4,
              "PhoneBeatMakerStorage layout assumes NumTracks=4; "
              "bump kVersion + walk the load/save offsets if this changes.");
static_assert(PhoneBeatMaker::NumSteps == 16,
              "PhoneBeatMakerStorage layout assumes NumSteps=16; "
              "bump kVersion + walk the load/save offsets if this changes.");
static_assert(kPatternBytes == 8,
              "PhoneBeatMakerStorage pattern stride drifted; "
              "bump kVersion + walk the load/save offsets if this changes.");
static_assert(kBlobSize == 16,
              "PhoneBeatMakerStorage blob size drifted; "
              "bump kVersion + walk the load/save offsets if this changes.");

// Single shared handle, opened lazily, kept alive for the device
// lifetime. Mirrors the Highscore / PhoneComposerStorage /
// PhoneCallHistoryStorage pattern.
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
		ESP_LOGW("BeatMakerStorage",
		         "nvs_open(%s) failed: %d -- beat-maker persistence disabled",
		         kNamespace, (int)err);
		s_handle = 0;
		return false;
	}
	return true;
}

uint8_t clampBpm(uint8_t bpm) {
	if(bpm < PhoneBeatMaker::MinBpm) return PhoneBeatMaker::MinBpm;
	if(bpm > PhoneBeatMaker::MaxBpm) return PhoneBeatMaker::MaxBpm;
	return bpm;
}

// Pack a NumTracks x NumSteps bool grid into the 8-byte pattern
// region. Track-major: bytes 0-1 hold track 0 steps 0..7 / 8..15,
// bytes 2-3 hold track 1, and so on. The LSB of each byte is the
// lowest-numbered step in its half (step 0 / step 8).
void packPattern(const bool pattern[PhoneBeatMaker::NumTracks][PhoneBeatMaker::NumSteps],
                 uint8_t*   dst) {
	memset(dst, 0, kPatternBytes);
	for(uint8_t t = 0; t < PhoneBeatMaker::NumTracks; ++t) {
		for(uint8_t s = 0; s < PhoneBeatMaker::NumSteps; ++s) {
			if(!pattern[t][s]) continue;
			const size_t  bit  = (size_t)t * PhoneBeatMaker::NumSteps + s;
			const size_t  byte = bit >> 3;
			const uint8_t mask = (uint8_t)(1u << (bit & 7));
			dst[byte] |= mask;
		}
	}
}

// Inverse of packPattern. Defensively writes every cell so a stale
// caller-side pattern can never bleed through.
void unpackPattern(const uint8_t* src,
                   bool outPattern[PhoneBeatMaker::NumTracks][PhoneBeatMaker::NumSteps]) {
	for(uint8_t t = 0; t < PhoneBeatMaker::NumTracks; ++t) {
		for(uint8_t s = 0; s < PhoneBeatMaker::NumSteps; ++s) {
			const size_t  bit  = (size_t)t * PhoneBeatMaker::NumSteps + s;
			const size_t  byte = bit >> 3;
			const uint8_t mask = (uint8_t)(1u << (bit & 7));
			outPattern[t][s] = (src[byte] & mask) != 0;
		}
	}
}

} // namespace

void PhoneBeatMakerStorage::begin() {
	(void)ensureOpen();
}

bool PhoneBeatMakerStorage::hasSaved() {
	if(!ensureOpen()) return false;
	size_t size = 0;
	auto err = nvs_get_blob(s_handle, kKey, nullptr, &size);
	if(err == ESP_ERR_NVS_NOT_FOUND) return false;
	if(err != ESP_OK)                return false;
	return size == kBlobSize;
}

bool PhoneBeatMakerStorage::loadInto(
        bool     outPattern[PhoneBeatMaker::NumTracks][PhoneBeatMaker::NumSteps],
        uint8_t* outBpm) {
	if(outPattern == nullptr || outBpm == nullptr) return false;
	if(!ensureOpen())                              return false;

	size_t size = 0;
	auto err = nvs_get_blob(s_handle, kKey, nullptr, &size);
	if(err != ESP_OK)        return false;
	if(size != kBlobSize)    return false;

	uint8_t blob[kBlobSize] = {};
	err = nvs_get_blob(s_handle, kKey, blob, &size);
	if(err != ESP_OK)        return false;
	if(size != kBlobSize)    return false;

	if(blob[0] != kMagic0)   return false;
	if(blob[1] != kMagic1)   return false;
	if(blob[2] != kVersion)  return false;
	// blob[3] reserved -- ignored on read so a future writer can
	// flag a re-encoding without invalidating the present version.

	*outBpm = clampBpm(blob[kBpmOffset]);
	unpackPattern(blob + kPatternOffset, outPattern);
	return true;
}

bool PhoneBeatMakerStorage::save(
        const bool pattern[PhoneBeatMaker::NumTracks][PhoneBeatMaker::NumSteps],
        uint8_t    bpm) {
	if(pattern == nullptr) return false;
	if(!ensureOpen())      return false;

	uint8_t blob[kBlobSize] = {};
	blob[0]            = kMagic0;
	blob[1]            = kMagic1;
	blob[2]            = kVersion;
	blob[3]            = 0;
	blob[kBpmOffset]   = clampBpm(bpm);
	// blob[5..7] left zero by the {} initialiser above.
	packPattern(pattern, blob + kPatternOffset);

	auto err = nvs_set_blob(s_handle, kKey, blob, kBlobSize);
	if(err != ESP_OK) {
		ESP_LOGW("BeatMakerStorage", "nvs_set_blob(%s) failed: %d", kKey, (int)err);
		return false;
	}
	err = nvs_commit(s_handle);
	if(err != ESP_OK) {
		ESP_LOGW("BeatMakerStorage", "nvs_commit(%s) failed: %d", kKey, (int)err);
		return false;
	}
	return true;
}

bool PhoneBeatMakerStorage::clear() {
	if(!ensureOpen()) return false;
	auto err = nvs_erase_key(s_handle, kKey);
	if(err == ESP_ERR_NVS_NOT_FOUND) {
		// Already empty -- treat as success so a callsite can safely
		// "clear-then-save" without juggling error codes.
		return true;
	}
	if(err != ESP_OK) {
		ESP_LOGW("BeatMakerStorage", "nvs_erase_key(%s) failed: %d", kKey, (int)err);
		return false;
	}
	err = nvs_commit(s_handle);
	if(err != ESP_OK) {
		ESP_LOGW("BeatMakerStorage", "nvs_commit(%s) failed: %d", kKey, (int)err);
		return false;
	}
	return true;
}
