#ifndef MAKERPHONE_PHONEBEATMAKER_H
#define MAKERPHONE_PHONEBEATMAKER_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"
#include "PhoneDrumKitScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneBeatMaker — S194
 *
 * A 16-step drum sequencer toy. Sits in the same Phase-S Easter-egg
 * neighbourhood as PhoneDrumKitScreen (S176): the dialer detects the
 * service code `*#808#` (a Roland TR-808 nod, the obvious cousin to
 * the TR-909 reference behind PhoneDrumKitScreen) and pushes this
 * screen.
 *
 * The user gets a 4-track / 16-step grid that loops at a
 * user-controllable BPM. Each cell is either off (dim) or on
 * (sunset-orange), and a cyan playhead marches across the columns
 * one step per 16th note. When a step has any active cell, the
 * piezo fires the highest-priority drum voice for that step (KICK
 * outranks SNARE outranks HI-HAT outranks CLAP — same-step
 * collisions on a monophonic buzzer are resolved by playing the
 * "low" backbone voice, which is the drum-machine-correct
 * compromise on hardware that physically can't mix two voices).
 *
 * Drum voices are reused verbatim from PhoneDrumKitScreen::kDrums
 * so the Beat-Maker shares its timbre with the per-key drum kit
 * — no second envelope table to maintain, and the two screens
 * sound like the same instrument.
 *
 * Layout (160x128):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar
 *   |              BEAT MAKER                | <- pixelbasic7 cyan
 *   |  BPM 120     STEP 01/16     >          | <- meta strip
 *   |     +-+-+-+-+ +-+-+-+-+ +-+-+-+-+ +-+-+|  <- 4 tracks x 16 steps
 *   |  KIK| | |o| | |o| | | | |o| | | | |o| ||
 *   |  SNR|o| | | | |o| | | | |o| | | | |o| ||
 *   |  HHT| |o| |o| | |o| |o| | |o| |o| | |o||
 *   |  CLP| | | | | | | | | | | | | | | | | ||
 *   |  L/R TRACK   <- ->  STEP   ENT TOGGLE  | <- hint line
 *   |                                  BACK  | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Controls:
 *   - BTN_LEFT / BTN_RIGHT  : move cursor across columns (with wrap).
 *   - BTN_L / BTN_R         : move cursor across tracks (with wrap).
 *   - BTN_ENTER             : toggle the cell at the cursor on/off.
 *   - BTN_0                 : start / stop the transport.
 *   - BTN_5                 : clear the entire pattern.
 *   - BTN_2 / BTN_8         : nudge BPM up / down by 5 (clamped 60..240).
 *   - BTN_BACK              : stop transport, silence buzzer, pop().
 *
 * Implementation notes:
 *   - Code-only, zero SPIFFS. Reuses PhoneSynthwaveBg / PhoneStatusBar
 *     / PhoneSoftKeyBar verbatim so the toy lives in the same visual
 *     family as the rest of Phase-S.
 *   - Two lv_timers are owned by the screen: a `stepTimer` that walks
 *     the playhead one step every (60000/BPM/4) ms, and an `envTimer`
 *     that walks the active drum's envelope frame-by-frame on the
 *     piezo. The envelope timer mirrors PhoneDrumKitScreen exactly,
 *     so a future tweak to the drum frame format propagates through
 *     a single helper.
 *   - Sound respect: the visual playhead always advances and cells
 *     always flash; piezo output is gated on Settings.get().sound,
 *     so the screen is still usable as a silent "rhythm canvas" in
 *     Mute / Vibrate profile.
 *   - Default seed pattern is the canonical boom-tss-bap-tss feature-
 *     phone groove (KICK on 1 & 9, SNARE on 5 & 13, HI-HAT on every
 *     other 16th) so the screen sounds musical the moment the user
 *     hits PLAY, even before they've toggled anything.
 *   - Single-voice (monophonic) by hardware design — when two tracks
 *     fire on the same step we pick the lowest-numbered active track
 *     (KICK > SNARE > HI-HAT > CLAP), which keeps the kick steady —
 *     exactly what a real cheap drum machine would do.
 */
class PhoneBeatMaker : public LVScreen, private InputListener {
public:
	PhoneBeatMaker();
	virtual ~PhoneBeatMaker() override;

	void onStart() override;
	void onStop() override;

	/** 16th-note grid — fixed, the entire screen is built around this. */
	static constexpr uint8_t NumSteps  = 16;

	/** Four tracks (KICK / SNARE / HI-HAT / CLAP). */
	static constexpr uint8_t NumTracks = 4;

	/** BPM clamp range for the up/down nudge keys. */
	static constexpr uint8_t MinBpm = 60;
	static constexpr uint8_t MaxBpm = 240;

	/** Default starting BPM. 120 BPM is the textbook "moderate dance
	 *  tempo" everyone has in their muscle memory and matches the
	 *  classic 909/808 demo patterns the screen pays homage to. */
	static constexpr uint8_t DefaultBpm = 120;

	/** BPM nudge step in beats-per-minute. 5 BPM is small enough to
	 *  feel responsive (nine taps walks the full range, end to end)
	 *  and large enough that one tap is audible. */
	static constexpr uint8_t BpmNudge = 5;

	/** Mapping from track index 0..3 to the digit slot in
	 *  PhoneDrumKitScreen::kDrums (so we get matching timbre and
	 *  short-name labels for free). 0 = KICK, 1 = SNARE,
	 *  2 = HI-HAT (closed), 3 = CLAP. */
	static const uint8_t kTrackDrumDigit[NumTracks];

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	// ---- header / meta strip ----------------------------------
	lv_obj_t* captionLabel;        // "BEAT MAKER"
	lv_obj_t* bpmLabel;            // "BPM 120"
	lv_obj_t* stepLabel;           // "STEP 01/16"
	lv_obj_t* transportLabel;      // ">" or "||"
	lv_obj_t* hintLabel;           // bottom hint strip

	// ---- grid -------------------------------------------------
	lv_obj_t* cells[NumTracks][NumSteps];
	bool      pattern[NumTracks][NumSteps];

	// ---- cursor -----------------------------------------------
	uint8_t cursorTrack = 0;
	uint8_t cursorStep  = 0;

	// ---- transport --------------------------------------------
	bool        playing       = false;
	uint8_t     bpm           = DefaultBpm;
	uint8_t     playStep      = 0;
	lv_timer_t* stepTimer     = nullptr;

	// ---- envelope (mirrors PhoneDrumKitScreen) ---------------
	const PhoneDrumKitScreen::Drum* activeDrum    = nullptr;
	uint8_t                         activeFrameIx = 0;
	lv_timer_t*                     envTimer      = nullptr;

	void buildHeader();
	void buildGrid();
	void buildCell(uint8_t track, uint8_t step,
				   lv_coord_t x, lv_coord_t y);
	void buildHint();
	void seedDefaultPattern();

	void refreshBpmLabel();
	void refreshStepLabel();
	void refreshTransportLabel();

	void refreshCell(uint8_t track, uint8_t step);
	void refreshAllCells();

	void moveCursor(int8_t deltaTrack, int8_t deltaStep);
	void toggleCell();

	void clearPattern();
	void nudgeBpm(int8_t delta);

	void startTransport();
	void stopTransport();
	void rearmStepTimer();
	uint32_t stepIntervalMs() const;

	void onStepTick();
	void fireStep(uint8_t step);

	void startEnvelope(const PhoneDrumKitScreen::Drum* drum);
	void stepEnvelope();
	void stopEnvelope();

	static void onStepTimerStatic(lv_timer_t* t);
	static void onEnvTimerStatic(lv_timer_t* t);

	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONEBEATMAKER_H
