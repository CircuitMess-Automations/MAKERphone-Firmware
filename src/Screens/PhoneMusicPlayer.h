#ifndef MAKERPHONE_PHONEMUSICPLAYER_H
#define MAKERPHONE_PHONEMUSICPLAYER_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"
#include "../Services/PhoneRingtoneEngine.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;
class PhoneEqualizerVisualiser;

/**
 * PhoneMusicPlayer
 *
 * S42 - the MAKERphone in-app music player UI. First Phase-G screen and
 * the natural successor to the PhoneAppStubScreen("MUSIC") that the main
 * menu wired to as a placeholder. Composes the existing Phase-A widgets
 * (PhoneSynthwaveBg, PhoneStatusBar, PhoneSoftKeyBar) with a custom
 * track-name + progress-bar + transport panel into a feature-phone
 * music-app silhouette:
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |                                        |
 *   |              SYNTHWAVE                 | <- pixelbasic16 track name
 *   |             Track 1 / 5                | <- pixelbasic7 caption
 *   |                                        |
 *   |  [################................]    | <- progress bar (0..100%)
 *   |              0:18 / 0:42               | <- elapsed / total caption
 *   |                                        |
 *   |        [<<]    [|>]    [>>]            | <- transport icons
 *   |                                        |
 *   |  PAUSE                           EXIT->| <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Track set: by default the player is seeded with the five S40 ringtone
 * melodies (Synthwave, Classic, Beep, Boss, Silent) so the screen is
 * driveable today on real hardware. S43 ships a proper 10-tune music
 * library and replaces the seeded list via setTracks(). Both call sites
 * use the same const Melody* contract, so swapping libraries is a one-
 * line change with no UI rework.
 *
 * Input contract - full feature-phone muscle memory:
 *   - BTN_LEFT  / BTN_L : previous track (wraps to last on track 0)
 *   - BTN_RIGHT / BTN_R : next track     (wraps to first on track N-1)
 *   - BTN_ENTER (BTN_A) : toggle play / pause
 *   - BTN_BACK         : stop playback and pop the screen
 *
 * Implementation notes:
 *  - Code-only (no SPIFFS assets). Reuses PhoneSynthwaveBg / PhoneStatusBar
 *    / PhoneSoftKeyBar plus three small lv_obj rectangles for the
 *    transport icons and two for the progress bar. Data partition cost
 *    stays zero, fits the 160x128 budget cleanly.
 *  - Playback is delegated to the global Ringtone engine (S39); the
 *    screen never touches the piezo directly. This keeps audio behavior
 *    identical to PhoneIncomingCall (S41) - including muting respect
 *    via Settings.sound - and means the screen has only one piece of
 *    state to manage: the index of the current track.
 *  - Progress is computed in the screen rather than queried from the
 *    engine: on every play() we record the track's total duration
 *    (sum of note + per-note-gap durations) and millis() at start, and
 *    a single lv_timer running every UpdatePeriodMs ticks the progress
 *    bar + elapsed/total caption. For looping melodies the bar wraps
 *    naturally via modulo; for non-loop melodies the timer auto-detects
 *    end-of-track via Ringtone.isPlaying() == false and advances to
 *    the next track (or pauses on the last one).
 *  - The play/pause icon swaps in place - when paused it shows a single
 *    triangle ">", when playing two vertical bars "||". Same widget
 *    objects reused; no flicker.
 *  - Leaving the screen (BTN_BACK or onStop) always calls Ringtone.stop()
 *    so the player cannot leak a buzzing piezo into a parent screen. If
 *    a future caller wants the music to keep playing after pop, we add
 *    a setStopOnPop(false) flag - keeping the safe default for S42.
 */
class PhoneMusicPlayer : public LVScreen, private InputListener {
public:
	PhoneMusicPlayer();
	virtual ~PhoneMusicPlayer() override;

	void onStart() override;
	void onStop() override;

	/**
	 * Replace the playable track list. The screen does not take ownership
	 * of the array or the melodies - both must outlive the screen. The
	 * default ctor seeds the list with the 10-tune PhoneMusicLibrary
	 * (S43), so calling setTracks() is only needed when a host wants a
	 * different catalogue (e.g. the ringtone library for a preview view).
	 *
	 * Passing count == 0 (or tracks == nullptr) puts the player into a
	 * "no tracks" state where the title shows a dash and the transport
	 * inputs are no-ops. This lets the screen still render meaningfully
	 * if a future caller has nothing to play.
	 */
	void setTracks(const PhoneRingtoneEngine::Melody* const* tracks, uint8_t count);

	/**
	 * S189 — display the active playlist's name above the per-track
	 * caption ("Chill Vibes 2/4" instead of "Track 2 / 10"). Set to
	 * nullptr or "" to revert to the bare "Track X / N" formatting
	 * the player ships with by default. The pointer is *not* copied —
	 * the caller (typically PhoneMusicPlaylists::nameOf) must outlive
	 * the screen, which is the case for every built-in playlist
	 * because their names live in static const storage.
	 */
	void setPlaylistName(const char* name);

	/** Currently displayed playlist label, or nullptr if none. */
	const char* getPlaylistName() const { return playlistName; }

	/** Switch to the given track index (wraps). Auto-plays. */
	void selectTrack(uint8_t index);

	/** Index of the currently visible track, or 0 if no tracks. */
	uint8_t getTrackIndex() const { return trackIndex; }

	/** True while the engine is being driven by this screen. */
	bool isPlaying() const { return playing; }

	/**
	 * S190 — playback mode selector. Cycles through the classic feature-
	 * phone modes; setPlayMode() persists the choice via Settings.store()
	 * so a reboot keeps the user's selection. Out-of-range values clamp
	 * to Continuous at the setter so a stale NVS blob never sticks the
	 * player into a mode the screen does not understand.
	 */
	enum PlayMode : uint8_t {
		Continuous = 0,   // advance, stop after last track (factory default)
		RepeatAll  = 1,   // advance, wrap to first track at end of list
		RepeatOne  = 2,   // replay same track at end of track
		Shuffle    = 3,   // pick a random next track at end / manual next
		ModeCount  = 4
	};

	/** Set the playback mode. Persists via Settings.store(). */
	void setPlayMode(uint8_t mode);

	/** Currently active playback mode (always in [0..ModeCount-1]). */
	uint8_t getPlayMode() const { return playMode; }

	/** Cycle to the next playback mode (wraps). */
	void cyclePlayMode();

	/** Public so a host can pre-load a track + auto-play after push(). */
	void play();
	void pause();
	void togglePlay();
	void next();
	void prev();

	/** UI tick period (ms). Slow enough to be cheap, fast enough to feel
	 *  smooth on a 100-step progress bar. */
	static constexpr uint16_t UpdatePeriodMs = 100;

private:
	// ----- visual children -----
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* titleLabel;       // track name, pixelbasic16, cyan
	lv_obj_t* indexLabel;       // "Track X / N", pixelbasic7, dim purple
	lv_obj_t* progressBg;       // dim purple track for the progress bar
	lv_obj_t* progressFill;     // sunset orange fill rectangle
	lv_obj_t* timeLabel;        // "0:18 / 0:42", pixelbasic7
	lv_obj_t* prevIcon;         // [<<] frame
	lv_obj_t* playIcon;         // [|>] frame (swaps to [||] when playing)
	lv_obj_t* nextIcon;         // [>>] frame
	lv_obj_t* prevGlyph;        // pixelbasic7 "<<"
	lv_obj_t* playGlyph;        // pixelbasic7 ">" or "||"
	lv_obj_t* nextGlyph;        // pixelbasic7 ">>"
	lv_obj_t* modeLabel;        // S190 - "MODE: SHUFFLE" caption (pixelbasic7)
	lv_obj_t* karaokeHint;      // S199 - "3: KARAOKE" discoverability hint for the BTN_3 shortcut
	PhoneEqualizerVisualiser* equalizer;  // S191 — bars dance with the active melody

	// ----- track state -----
	const PhoneRingtoneEngine::Melody* const* tracks;
	uint8_t trackCount;
	uint8_t trackIndex;
	bool    playing;

	// S190 - playback mode (Continuous / RepeatAll / RepeatOne / Shuffle).
	// Loaded from Settings.musicPlayMode in the ctor and persisted via
	// setPlayMode(); kept in [0..ModeCount-1] at all times.
	uint8_t playMode;
	uint8_t lastShuffleIdx;     // remembered to avoid re-rolling the same track twice in a row

	// S189 — optional playlist label shown alongside the track index.
	// Pointer is owned by the caller and assumed to outlive the screen.
	const char* playlistName;

	// ----- progress state -----
	uint32_t trackTotalMs;  // total play time of the current track (ms)
	uint32_t playStartMs;   // millis() when the current play() began
	uint32_t pausedAtMs;    // when paused: elapsed-into-track at pause
	lv_timer_t* tickTimer;

	// ----- builders -----
	void buildTitle();
	void buildProgressBar();
	void buildTransport();
	void buildMode();          // S190 - small "MODE: ..." caption above the soft-key bar
	void buildEqualizer();     // S191 - 7-bar equalizer between progress bar and transport row
	void buildKaraokeHint();   // S199 - tiny "3: KARAOKE" hint surfaces the BTN_3 shortcut to PhoneKaraokeScreen (S196)

	// ----- helpers -----
	void refreshTrackLabels();
	void refreshPlayIcon();
	void refreshProgress();
	void refreshMode();              // S190 - paint the current PlayMode caption
	void advanceForEndOfTrack();     // S190 - end-of-track behaviour gated on playMode
	uint8_t pickShuffleIndex() const;// S190 - random index different from trackIndex when possible
	static const char* modeLabelOf(uint8_t mode);
	uint32_t currentElapsedMs() const;
	static uint32_t computeTotalMs(const PhoneRingtoneEngine::Melody* m);
	static void onTick(lv_timer_t* t);

	// ----- input -----
	void buttonPressed(uint i) override;

	/** Geometry / layout constants - pinned to the 160x128 display. */
	static constexpr uint16_t ScreenW = 160;
	static constexpr uint16_t BarLeft = 10;          // x of progress bar
	static constexpr uint16_t BarWidth = 140;        // px
	static constexpr uint16_t BarHeight = 4;         // px
	static constexpr uint16_t BarY = 50;             // y of progress bar
	static constexpr uint16_t IconW = 22;            // transport icon width
	static constexpr uint16_t IconH = 16;            // transport icon height
	static constexpr uint16_t IconY = 84;            // y of transport row
};

#endif // MAKERPHONE_PHONEMUSICPLAYER_H
