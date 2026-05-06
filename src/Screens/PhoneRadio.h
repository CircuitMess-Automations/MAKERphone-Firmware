#ifndef MAKERPHONE_PHONERADIO_H
#define MAKERPHONE_PHONERADIO_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"
#include "../Services/PhoneRingtoneEngine.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneRadio (S195)
 *
 * Phase-U "fake FM dial" — eight stations of pre-canned looping melody
 * snippets the user can flick through with the L/R buttons. The UI
 * mimics the 88.0..108.0 MHz analog FM band a Sony-Ericsson Walkman
 * phone would show: a horizontal scale with tick marks, a tuning
 * cursor that snaps to the focused station, big cyan frequency
 * readout, station call-sign and tagline, and an "ON AIR" indicator
 * that lights up while the piezo is being driven.
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |              FM RADIO                  | <- caption (cyan, pixelbasic7)
 *   |                                        |
 *   |   88                              108  | <- scale labels (dim purple)
 *   |   |    |    |    *    |    |    |  |   | <- tick row + cursor (orange)
 *   |                                        |
 *   |             99.1 FM                    | <- pixelbasic16 cyan
 *   |              NEON 99                   | <- station name (orange)
 *   |          "Synthwave Classics"          | <- tagline (cream)
 *   |                                        |
 *   |             [ ON AIR ]                 | <- status pill
 *   |                                        |
 *   |   PLAY                          EXIT   | <- soft keys
 *   +----------------------------------------+
 *
 * Stations (8, hard-coded):
 *   0. 88.5 MHz  RETRO 88     Synthwave throwback
 *   1. 92.3 MHz  BEAT FM      Drum & pulse
 *   2. 95.7 MHz  ARCADE 95    8-bit hits
 *   3. 99.1 MHz  NEON 99      Synthpop
 *   4. 101.5 MHz CRYSTAL FM   Chillwave
 *   5. 104.3 MHz POWER 104    High energy
 *   6. 106.7 MHz DRIFT 106    Lo-fi cruise
 *   7. 108.0 MHz STATIC 108   Pure noise
 *
 * Input contract:
 *   - BTN_LEFT  / BTN_4 / BTN_L : tune to previous station (clamps at 0)
 *   - BTN_RIGHT / BTN_6 / BTN_R : tune to next station    (clamps at 7)
 *   - BTN_ENTER (BTN_A)         : toggle play / pause
 *   - BTN_BACK  (BTN_B)         : stop and pop the screen
 *
 * Implementation notes:
 *  - 100 % code-only — no SPIFFS assets. Reuses PhoneSynthwaveBg /
 *    PhoneStatusBar / PhoneSoftKeyBar plus a row of plain LVGL
 *    rectangles for the dial ticks. Each station's melody is a static
 *    const Note[] living inside the .cpp so the data partition stays
 *    untouched.
 *  - Playback is delegated to the global PhoneRingtoneEngine (S39).
 *    Every station melody opts into looping (`loop = true`) so as long
 *    as the user keeps the screen open and doesn't press STOP, the
 *    selected station keeps playing. Tuning to a different station
 *    while playing crossfades cleanly: stop() the previous melody +
 *    play() the new one; the engine handles the silence between in a
 *    single tick.
 *  - The scale tick row uses 8 ticks across the 12..148 px band so the
 *    cursor lands on a clean integer pixel for every station. The
 *    cursor itself is a 3 x 8 sunset-orange rectangle. Fits the
 *    160x128 budget cleanly with room to spare.
 *  - Leaving the screen (BTN_BACK or onStop) always calls
 *    Ringtone.stop() so the radio cannot leak a buzzing piezo into
 *    the parent screen, mirroring PhoneMusicPlayer's safe default.
 */
class PhoneRadio : public LVScreen, private InputListener {
public:
	PhoneRadio();
	virtual ~PhoneRadio() override;

	void onStart() override;
	void onStop() override;

	/** Number of stations on the dial. */
	static constexpr uint8_t StationCount = 8;

	/** Index of the station currently under the dial cursor. */
	uint8_t getStationIndex() const { return stationIndex; }

	/** True while the engine is being driven by this screen. */
	bool isOnAir() const { return playing; }

	/** Public so a host could pre-tune + auto-play after push(). */
	void tuneTo(uint8_t index);
	void play();
	void stop();
	void togglePlay();
	void next();
	void prev();

	/** Display name for a station (call-sign). Bounds-checked; out-of-
	 *  range index returns the wrapped value. Useful for unit-test
	 *  introspection without exposing the internal table. */
	static const char* stationName(uint8_t index);

	/** Display tagline ("Synthwave Classics" etc.). Bounds-checked. */
	static const char* stationTagline(uint8_t index);

	/** Frequency in MHz * 10 (e.g. 991 == 99.1 MHz) for a station. */
	static uint16_t stationFreqDeci(uint8_t index);

	/** Looping melody for a station. Pointer is into static storage. */
	static const PhoneRingtoneEngine::Melody* stationMelody(uint8_t index);

	/**
	 * S205 — true when the active phone profile silences ringer
	 * audio (`Settings.get().sound == false`, i.e. SILENT or
	 * MEETING from `PhoneProfileScreen`'s five-state vocabulary).
	 *
	 * Used by `startPlayback()` and `tuneTo()` to short-circuit the
	 * `PhoneRingtoneEngine::play()` call so the radio cannot drive
	 * the piezo at all under a silent profile -- not even for the
	 * micro-interval before the engine's per-loop mute kicks in --
	 * and by `refreshStatus()` to render a "MUTED" pill instead of
	 * the bright sunset-orange "ON AIR" pill so the user knows
	 * exactly why the dial is silent.
	 */
	static bool isSilenced();

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* captionLabel;       // "FM RADIO" header
	lv_obj_t* scaleLeftLabel;     // "88"
	lv_obj_t* scaleRightLabel;    // "108"
	lv_obj_t* dialTrack;          // dim horizontal background line
	lv_obj_t* dialTicks[StationCount]; // 8 short orange tick marks
	lv_obj_t* dialCursor;         // tall sunset-orange cursor
	lv_obj_t* freqLabel;          // "99.1 FM" big cyan
	lv_obj_t* stationLabel;       // "NEON 99" sunset orange
	lv_obj_t* taglineLabel;       // "Synthwave Classics" warm cream
	lv_obj_t* statusPill;         // background of the [ ON AIR ] pill
	lv_obj_t* statusLabel;        // text inside the pill

	uint8_t stationIndex;
	bool    playing;

	void buildCaption();
	void buildDial();
	void buildReadout();
	void buildStatus();

	void refreshFrequency();
	void refreshStation();
	void refreshCursor();
	void refreshStatus();

	void startPlayback();
	void stopPlayback();

	void buttonPressed(uint i) override;

	// =====================================================================
	// Geometry constants — pinned to the 160x128 display.
	// =====================================================================

	/** Y of the "FM RADIO" header. */
	static constexpr lv_coord_t CaptionY = 12;

	/** Y of the dial tick row (centre line). */
	static constexpr lv_coord_t DialY = 28;

	/** Left edge of the dial band. */
	static constexpr lv_coord_t DialLeft = 12;

	/** Right edge of the dial band. */
	static constexpr lv_coord_t DialRight = 148;

	/** Y of the big "99.1 FM" frequency label (top). */
	static constexpr lv_coord_t FreqY = 44;

	/** Y of the station-name label. */
	static constexpr lv_coord_t StationY = 68;

	/** Y of the tagline. */
	static constexpr lv_coord_t TaglineY = 82;

	/** Y of the "[ ON AIR ]" pill (top). */
	static constexpr lv_coord_t PillY = 100;
};

#endif // MAKERPHONE_PHONERADIO_H
