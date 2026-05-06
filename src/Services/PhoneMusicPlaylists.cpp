#include "PhoneMusicPlaylists.h"

#include "PhoneMusicLibrary.h"

// ---------------------------------------------------------------------------
// Built-in playlists (S189). Each entry is an ordered list of indices
// into PhoneMusicLibrary plus a display name and short caption. We keep
// the data layout dead-simple — uint8_t arrays of indices — so a future
// session can layer a NVS-backed "Favorites" playlist on top with no
// API churn (just append a fifth Id and load its index list at boot).
//
// Track-index ordering across the curated playlists:
//   PhoneMusicLibrary::Id values:
//     0 NeonDrive   1 PixelSunrise  2 CyberDawn    3 CrystalCave
//     4 Hyperloop   5 Starfall      6 RetroQuest   7 MoonlitDrift
//     8 ArcadeHero  9 SunsetBlvd
// ---------------------------------------------------------------------------

namespace {

// "All Tracks" — every tune in catalogue order. The default playlist
// the user lands on, equivalent to S42's pre-playlist behaviour.
constexpr uint8_t kAllTracks[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };

// "Chill Vibes" — the four slowest / most atmospheric tunes, queued
// up so they read as a single mellow set when played back-to-back.
constexpr uint8_t kChillVibes[] = {
	(uint8_t) PhoneMusicLibrary::CyberDawn,
	(uint8_t) PhoneMusicLibrary::CrystalCave,
	(uint8_t) PhoneMusicLibrary::MoonlitDrift,
	(uint8_t) PhoneMusicLibrary::SunsetBoulevard,
};

// "Energy Boost" — fast / upbeat tunes, leaning on the chiptune
// flourishes. Same four-track length as Chill Vibes for visual
// symmetry on the playlists screen.
constexpr uint8_t kEnergyBoost[] = {
	(uint8_t) PhoneMusicLibrary::NeonDrive,
	(uint8_t) PhoneMusicLibrary::Hyperloop,
	(uint8_t) PhoneMusicLibrary::ArcadeHero,
	(uint8_t) PhoneMusicLibrary::PixelSunrise,
};

// "Synthwave Drive" — the synthwave-flavoured set, the "credits roll"
// of the catalogue. Three tracks, demonstrating that playlists can be
// any length up to PhoneMusicLibrary::Count.
constexpr uint8_t kSynthwaveDrive[] = {
	(uint8_t) PhoneMusicLibrary::NeonDrive,
	(uint8_t) PhoneMusicLibrary::PixelSunrise,
	(uint8_t) PhoneMusicLibrary::SunsetBoulevard,
};

struct PlaylistDesc {
	const char*     name;
	const char*     caption;
	const uint8_t*  trackIds;
	uint8_t         count;
};

constexpr PlaylistDesc kPlaylists[] = {
	{ "All Tracks",      "Every tune",       kAllTracks,
	  (uint8_t)(sizeof(kAllTracks)      / sizeof(kAllTracks[0])) },
	{ "Chill Vibes",     "Slow / dreamy",    kChillVibes,
	  (uint8_t)(sizeof(kChillVibes)     / sizeof(kChillVibes[0])) },
	{ "Energy Boost",    "Fast / upbeat",    kEnergyBoost,
	  (uint8_t)(sizeof(kEnergyBoost)    / sizeof(kEnergyBoost[0])) },
	{ "Synthwave Drive", "Sunset cruise",    kSynthwaveDrive,
	  (uint8_t)(sizeof(kSynthwaveDrive) / sizeof(kSynthwaveDrive[0])) },
};

static_assert(sizeof(kPlaylists) / sizeof(kPlaylists[0]) ==
              (size_t) PhoneMusicPlaylists::Count,
              "kPlaylists must mirror PhoneMusicPlaylists::Id enum");

// Per-playlist Melody-pointer caches. These are static arrays that are
// filled lazily on first call to tracks(id). They mirror kPlaylists's
// trackIds arrays but resolved through PhoneMusicLibrary::tracks() so
// PhoneMusicPlayer::setTracks() can take the pointer directly.
//
// Sized to PhoneMusicLibrary::Count which is the upper bound for any
// playlist (the "All Tracks" entry hits exactly this). Smaller
// playlists leave the trailing slots unused.
const PhoneRingtoneEngine::Melody* g_resolved
		[(size_t) PhoneMusicPlaylists::Count]
		[(size_t) PhoneMusicLibrary::Count] = {};
bool g_resolvedReady[(size_t) PhoneMusicPlaylists::Count] = { false, false, false, false };

void buildResolvedTracks(uint8_t id) {
	if(id >= (uint8_t) PhoneMusicPlaylists::Count) return;
	if(g_resolvedReady[id]) return;

	const PlaylistDesc& d = kPlaylists[id];
	const PhoneRingtoneEngine::Melody* const* libTracks =
			PhoneMusicLibrary::tracks();
	const uint8_t libCount = PhoneMusicLibrary::count();

	for(uint8_t pos = 0; pos < d.count; ++pos) {
		uint8_t libIdx = d.trackIds[pos];
		if(libIdx >= libCount) libIdx = 0; // defensive
		g_resolved[id][pos] = libTracks[libIdx];
	}
	g_resolvedReady[id] = true;
}

} // namespace

uint8_t PhoneMusicPlaylists::count() {
	return (uint8_t) PhoneMusicPlaylists::Count;
}

const char* PhoneMusicPlaylists::nameOf(uint8_t id) {
	if(id >= (uint8_t) PhoneMusicPlaylists::Count) return "Unknown";
	return kPlaylists[id].name;
}

const char* PhoneMusicPlaylists::captionOf(uint8_t id) {
	if(id >= (uint8_t) PhoneMusicPlaylists::Count) return "";
	return kPlaylists[id].caption;
}

uint8_t PhoneMusicPlaylists::trackCount(uint8_t id) {
	if(id >= (uint8_t) PhoneMusicPlaylists::Count) return 0;
	return kPlaylists[id].count;
}

uint8_t PhoneMusicPlaylists::trackIdAt(uint8_t id, uint8_t pos) {
	if(id >= (uint8_t) PhoneMusicPlaylists::Count) return 0;
	const PlaylistDesc& d = kPlaylists[id];
	if(pos >= d.count) return 0;
	return d.trackIds[pos];
}

const PhoneRingtoneEngine::Melody* const* PhoneMusicPlaylists::tracks(uint8_t id) {
	if(id >= (uint8_t) PhoneMusicPlaylists::Count) return nullptr;
	buildResolvedTracks(id);
	return g_resolved[id];
}
