#ifndef CHATTER_FIRMWARE_PHONE_MUSIC_PLAYLISTS_H
#define CHATTER_FIRMWARE_PHONE_MUSIC_PLAYLISTS_H

#include <Arduino.h>
#include "PhoneRingtoneEngine.h"

/**
 * S189 — PhoneMusicPlaylists
 *
 * Playlist support for the MAKERphone music app. Where S43's
 * PhoneMusicLibrary is the flat catalogue of all ten tunes, this
 * service groups subsets of those tunes into named playlists the user
 * can pick between in the new PhonePlaylistsScreen, before drilling
 * into PhoneMusicPlayer.
 *
 * Every playlist is a small ordered selection of indices into
 * PhoneMusicLibrary plus a display name. The four built-in playlists:
 *
 *   0. "All Tracks"     — every tune in catalogue order (10 tracks).
 *   1. "Chill Vibes"    — slow / atmospheric: Cyber Dawn, Crystal Cave,
 *                          Moonlit Drift, Sunset Blvd.
 *   2. "Energy Boost"   — fast / upbeat: Neon Drive, Hyperloop,
 *                          Arcade Hero, Pixel Sunrise.
 *   3. "Synthwave Drive"— the credits-roll mix: Neon Drive, Pixel
 *                          Sunrise, Sunset Blvd.
 *
 * The service is a stateless namespace-style helper: the playlists
 * themselves live in `static const` storage in the .cpp, and the
 * `tracks(id)` accessor returns a stable pointer-of-pointer-to-Melody
 * that matches PhoneMusicPlayer::setTracks()'s contract exactly. No
 * heap, no NVS, no static-initialisation-order pitfalls.
 *
 * The four built-ins are intentionally fixed for S189; later phases
 * can add user-editable favourites (each playlist is just a uint8_t
 * track-index list, so an NVS-backed "Favorites" entry is a small
 * follow-up) without breaking this API.
 */
class PhoneMusicPlaylists {
public:
	enum Id : uint8_t {
		AllTracks = 0,
		ChillVibes,
		EnergyBoost,
		SynthwaveDrive,
		Count
	};

	/** Number of built-in playlists (== Count). */
	static uint8_t count();

	/** Display name for a playlist (UTF-8, never null). */
	static const char* nameOf(uint8_t id);

	/** Short caption shown under the name on the picker. Never null. */
	static const char* captionOf(uint8_t id);

	/** Number of tracks in a playlist. 0 if id is out of range. */
	static uint8_t trackCount(uint8_t id);

	/**
	 * PhoneMusicLibrary index for the `pos`-th track of `id`. Returns
	 * 0 (defensive) if either argument is out of range so the caller
	 * never reads garbage; real callers should range-check first.
	 */
	static uint8_t trackIdAt(uint8_t id, uint8_t pos);

	/**
	 * Pointer-array of Melody pointers that PhoneMusicPlayer::setTracks()
	 * accepts directly. The returned pointer is stable for the firmware's
	 * lifetime (backed by static storage), so the music player can hold
	 * on to it across screen pushes/pops without copying.
	 *
	 * Returns nullptr when id is out of range.
	 */
	static const PhoneRingtoneEngine::Melody* const* tracks(uint8_t id);
};

#endif // CHATTER_FIRMWARE_PHONE_MUSIC_PLAYLISTS_H
