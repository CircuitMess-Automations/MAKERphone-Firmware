#ifndef CHATTER_FIRMWARE_PHONE_MUSIC_LIBRARY_H
#define CHATTER_FIRMWARE_PHONE_MUSIC_LIBRARY_H

#include <Arduino.h>
#include "PhoneRingtoneEngine.h"

/**
 * S43 — PhoneMusicLibrary
 *
 * The MAKERphone in-app "music player" tune catalogue: ten short,
 * original retro-style chiptune melodies designed to be sung through
 * the piezo by the S39 PhoneRingtoneEngine. The library is intentionally
 * shaped exactly like S40's PhoneRingtoneLibrary (static const Note[] +
 * Melody, lazy "byIndex"-style accessor) so that PhoneMusicPlayer's
 * setTracks() contract — `const Melody* const* tracks, uint8_t count` —
 * accepts both with no UI rework.
 *
 * Every tune here is non-looping — the music player auto-advances to the
 * next track when one finishes, giving the screen the "album playing
 * through" feel described in S42's docstring. Ringtones loop, music does
 * not. That's the only structural difference between this library and
 * the ringtone one.
 *
 * Track list (all original compositions, no covers):
 *   0. Neon Drive       — driving synthwave pulse-arp.
 *   1. Pixel Sunrise    — bright major arpeggio over a walking bass.
 *   2. Cyber Dawn       — moody minor melody, slow tempo.
 *   3. Crystal Cave     — haunting pentatonic phrase.
 *   4. Hyperloop        — fast techno bassline + high pings.
 *   5. Starfall         — descending major scale exploration.
 *   6. Retro Quest      — JRPG-flavoured adventure march.
 *   7. Moonlit Drift    — slow ballad of wide leaps.
 *   8. Arcade Hero      — fast chiptune flourish.
 *   9. Sunset Boulevard — long synthwave groove (the "credits roll" of
 *                          the catalogue).
 *
 * Design notes:
 *   - Frequencies are clamped to the buzzer's comfortable 200–2200 Hz
 *     range; piezo speakers below ~180 Hz come out as clicks, above
 *     ~2.5 kHz they get harsh.
 *   - Per-step duration is the only timing knob — gapMs is intentionally
 *     small (10–20 ms) so the listener perceives the melody as legato
 *     rather than a stutter of staccato beeps.
 *   - Each melody is short (around 20–40 notes, < 10 s) so the music
 *     player feels responsive when the user skips around with L/R.
 *
 * S70's UX-QA pass may swap out a couple of these for crowd favourites,
 * but the API (count(), byIndex(), get()) is fixed.
 */
class PhoneMusicLibrary {
public:
	enum Id : uint8_t {
		NeonDrive = 0,
		PixelSunrise,
		CyberDawn,
		CrystalCave,
		Hyperloop,
		Starfall,
		RetroQuest,
		MoonlitDrift,
		ArcadeHero,
		SunsetBoulevard,
		Count
	};

	/** Number of tunes in the library (== Count). */
	static uint8_t count();

	/** Display name for an Id (UTF-8, never null). */
	static const char* nameOf(Id id);

	/** Get a specific tune by Id. */
	static const PhoneRingtoneEngine::Melody& get(Id id);

	/** Get a tune by 0-based index, wrapping if out of range. */
	static const PhoneRingtoneEngine::Melody& byIndex(uint8_t idx);

	/**
	 * Pointer-array accessor matching the contract PhoneMusicPlayer's
	 * setTracks() expects (`const Melody* const*`). Lazy-built on first
	 * call to dodge static-initialisation-order pitfalls between this
	 * TU and the screen TU.
	 */
	static const PhoneRingtoneEngine::Melody* const* tracks();
};

#endif // CHATTER_FIRMWARE_PHONE_MUSIC_LIBRARY_H
