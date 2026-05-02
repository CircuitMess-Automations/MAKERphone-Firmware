#ifndef MAKERPHONE_PHONESIMON_H
#define MAKERPHONE_PHONESIMON_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneSimon (S97)
 *
 * Phase-N arcade entry: a memory + buzzer-tone Simon Says game. Four
 * coloured pads sit in a 2x2 cluster at the centre of the panel; each
 * pad is bound to a dialer key (1 = top-left cyan, 3 = top-right
 * orange, 7 = bottom-left magenta, 9 = bottom-right yellow) and to a
 * distinct musical pitch on the piezo. Each round, the device adds one
 * more step to the sequence, plays it back with light + tone flashes,
 * then the player has to mirror it back. A miss ends the round; the
 * longest sequence ever cleared during the session is shown as HI.
 *
 *   +------------------------------------+
 *   | ||||  12:34                  ##### | <- PhoneStatusBar  (10 px)
 *   | ROUND 04   HI 12     WATCH        | <- HUD strip       (12 px)
 *   |     +------------+------------+   |
 *   |     |     1      |     3      |   |
 *   |     |    CYAN    |   ORANGE   |   |   <- 2x2 pad cluster
 *   |     +------------+------------+   |      (each pad 56x42 px;
 *   |     |     7      |     9      |   |       cluster 116x88 px;
 *   |     |   MAGENTA  |   YELLOW   |   |       centred at x = 22, y = 22).
 *   |     +------------+------------+   |
 *   |     SEQ 4   ECHO 0/4               | <- progress strip (10 px)
 *   |   START              BACK         | <- PhoneSoftKeyBar (10 px)
 *   +------------------------------------+
 *
 * Controls:
 *   - BTN_1 / BTN_3 / BTN_7 / BTN_9 : tap the matching pad. The pad
 *                                     flashes brighter + plays its
 *                                     pitch on the piezo.
 *   - BTN_4 / BTN_2 / BTN_6 / BTN_8 : aliased to the four pads so a
 *                                     player who is thumb-driving
 *                                     instead of dialer-driving can
 *                                     still play (4->1, 2->3, 8->7,
 *                                     6->9).
 *   - BTN_LEFT / BTN_RIGHT          : alias to the two left pads
 *                                     (1 / 7) and the two right pads
 *                                     (3 / 9) for a dialless single-
 *                                     finger play style. We pick the
 *                                     top row by default; the d-pad
 *                                     UP/DOWN keys are aliased on the
 *                                     numeric pad already.
 *   - BTN_5 / BTN_ENTER             : during Idle / GameOver, start a
 *                                     new round; ignored during play
 *                                     and watch states.
 *   - BTN_R                         : restart the round (works in
 *                                     every state).
 *   - BTN_BACK (B)                  : pop back to PhoneGamesScreen.
 *
 * State machine:
 *   Idle      -- start screen with the rules in the centre overlay.
 *                 Soft-keys: START / BACK.
 *   Watch     -- the device is playing back the sequence: pads light
 *                 up + tones fire one at a time. Player input is
 *                 ignored during this state.
 *   Echo      -- the player is mirroring the sequence; each correct
 *                 key extends the matched prefix; a wrong key ends
 *                 the round.
 *   GameOver  -- "GAME OVER" overlay; soft-keys: AGAIN / BACK.
 *
 * Implementation notes:
 *  - 100% code-only -- each pad is a small rounded LVGL panel that
 *    we re-tint between "dim" and "lit". No SPIFFS asset cost.
 *  - Sequence storage: a fixed-size array of pad indices (0..3),
 *    grown by one element per round, reset on startRound(). Cap at
 *    kSeqMax = 32 -- well past the human memory ceiling, generous
 *    headroom for the fixed buffer to keep RAM stable on ESP32.
 *  - The watch-state animation is driven by a single repeating
 *    lv_timer running at kTickMs intervals: each tick decrements the
 *    current step's remaining ms; when it rolls over we either
 *    transition from "lit" -> "gap" or advance to the next sequence
 *    step. When the playhead has covered every pad in the sequence,
 *    the timer transitions us into the Echo state.
 *  - Tones come from the global Piezo (same path PhoneHapticsScreen
 *    and PhonePowerDown use). Each pad has a distinct pitch so
 *    the player learns sequences by sound + sight. The tones honour
 *    Settings.get().sound -- if the user has muted, we still flash
 *    pads but skip the audio so the visuals remain useful.
 *  - We deliberately do NOT use PhoneRingtoneEngine here. Simon's
 *    short staccato pings are too tight for a melody timeline and
 *    we don't want a ringer-style queue interfering with arrival
 *    chimes that fire from elsewhere in the firmware.
 *  - Session high score (longest sequence cleared) survives "play
 *    again" but resets when the screen is popped, matching every
 *    other Phone* game in the v1.0 + Phase-N roadmap.
 */
class PhoneSimon : public LVScreen, private InputListener {
public:
	PhoneSimon();
	virtual ~PhoneSimon() override;

	void onStart() override;
	void onStop() override;

	// ---- Layout constants (160 x 128 panel) ---------------------------
	static constexpr lv_coord_t StatusBarH = 10;
	static constexpr lv_coord_t SoftKeyH   = 10;
	static constexpr lv_coord_t HudY       = 10;
	static constexpr lv_coord_t HudH       = 12;
	static constexpr lv_coord_t ProgressH  = 10;

	// 2x2 pad cluster centred on the panel. Each pad is 56 wide x
	// 42 tall, with a 4 px gap between pads horizontally + vertically.
	// Total cluster 116 x 88, centred horizontally on the 160 px panel,
	// tucked just under the HUD with the progress strip + soft-key bar
	// below it.
	static constexpr uint8_t   PadCount   = 4;
	static constexpr lv_coord_t PadW      = 56;
	static constexpr lv_coord_t PadH      = 42;
	static constexpr lv_coord_t PadGapX   = 4;
	static constexpr lv_coord_t PadGapY   = 4;
	static constexpr lv_coord_t ClusterW  = PadW * 2 + PadGapX;        // 116
	static constexpr lv_coord_t ClusterH  = PadH * 2 + PadGapY;        // 88
	static constexpr lv_coord_t ClusterX  = (160 - ClusterW) / 2;      // 22
	static constexpr lv_coord_t ClusterY  = StatusBarH + HudH;         // 22

	// ---- Game tuning --------------------------------------------------
	// Tick period of the watch-state playback loop. 60 ms is fine
	// enough that pad lifetime + gap pacing feel responsive while
	// being light on the LVGL timer scheduler.
	static constexpr uint16_t kTickMs       = 60;

	// Per-step "lit" duration during Watch playback. Ramps from
	// kLitStartMs at sequence length 1 down to kLitFloorMs at
	// sequence length kRampLen, clamped at the floor afterwards.
	static constexpr uint16_t kLitStartMs   = 540;
	static constexpr uint16_t kLitFloorMs   = 220;
	static constexpr uint16_t kRampLen      = 16;

	// Inter-step gap during Watch playback. Always exactly half of
	// the current "lit" duration, with a hard floor so two pads in
	// a row don't visually merge.
	static constexpr uint16_t kGapFloorMs   = 110;

	// Player-pad flash duration during Echo. Independent of the
	// watch-state cadence -- this is just visual feedback for a tap.
	static constexpr uint16_t kEchoFlashMs  = 180;

	// Sequence buffer cap. Realistically a player will never reach
	// double digits, but the buffer is fixed-size so we don't churn
	// the heap during a session.
	static constexpr uint8_t  kSeqMax       = 32;

	// Failure tone used on a missed-step. A brief low buzz so it
	// reads as "wrong" without resembling a pad pitch.
	static constexpr uint16_t kFailFreq     = 175;
	static constexpr uint16_t kFailMs       = 360;

private:
	// Pad pitches (Hz). Defined out-of-line in the .cpp so the
	// linker has a definition without the constexpr inline ODR
	// gymnastics that ESP32 + Arduino sometimes trip over. Picked
	// to be a recognisable rising arpeggio (cyan ~G4, orange ~C5,
	// magenta ~E5, yellow ~G5).
	static const uint16_t kPadFreq[PadCount];

	// ---- LVGL node graph ----------------------------------------------
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* hudRoundLabel = nullptr;
	lv_obj_t* hudHiLabel    = nullptr;
	lv_obj_t* hudStateLabel = nullptr;

	lv_obj_t* overlayPanel  = nullptr;
	lv_obj_t* overlayTitle  = nullptr;
	lv_obj_t* overlayLines  = nullptr;

	lv_obj_t* progressLabel = nullptr;

	// One panel + one digit-label + one colour-name caption per pad.
	lv_obj_t* padPanels[PadCount];
	lv_obj_t* padLabels[PadCount];
	lv_obj_t* padNames[PadCount];

	// ---- game state ---------------------------------------------------
	enum class GameState : uint8_t {
		Idle,        // start screen, waiting for the user to begin
		Watch,       // device is playing back the sequence
		Echo,        // player is mirroring the sequence
		GameOver,    // sequence broken; overlay shown
	};

	GameState state = GameState::Idle;

	// Sequence buffer + counters.
	uint8_t  sequence[kSeqMax];
	uint8_t  seqLen      = 0;       // current sequence length
	uint8_t  echoIdx     = 0;       // 0..seqLen during Echo
	uint8_t  watchIdx    = 0;       // 0..seqLen during Watch
	uint8_t  highScore   = 0;       // best seqLen cleared this session

	// Watch-state pacing (ms remaining for the current step).
	int16_t  watchTimerMs = 0;
	bool     watchInLit   = false;  // true = pad lit, false = inter-step gap

	// Per-pad flash countdown for Echo taps. When non-zero, the pad
	// renders in its "lit" colour; tick decrements it back to zero.
	int16_t  padFlashMs[PadCount] = { 0, 0, 0, 0 };

	// Currently-lit pad during Watch (or 0xFF when none).
	uint8_t  watchLitPad = 0xFF;

	// Failure-tone countdown during the GameOver transition.
	int16_t  failToneMs  = 0;

	// Running tick driver. One repeating lv_timer is enough.
	lv_timer_t* tickTimer = nullptr;

	// ---- build helpers ------------------------------------------------
	void buildHud();
	void buildOverlay();
	void buildProgress();
	void buildPads();

	// ---- state transitions --------------------------------------------
	void enterIdle();
	void startRound();
	void beginWatch();
	void beginEcho();
	void endRound(bool failed);

	// Game-loop step (called from the tick timer). Drives Watch playback
	// and per-pad flash decay during Echo, plus the failure-tone hold.
	void tick();

	// Watch helpers.
	void lightPadWatch(uint8_t padIdx);
	void unlightPadWatch();

	// Echo helper. Returns true if the tap matched the expected step.
	bool echoTap(uint8_t padIdx);

	// ---- helpers ------------------------------------------------------
	uint16_t currentLitMs() const;
	uint16_t currentGapMs() const;
	uint8_t  buttonToPadIdx(uint i) const;

	// ---- audio --------------------------------------------------------
	void playPadTone(uint8_t padIdx);
	void playFailTone();
	void silencePiezo();

	// ---- rendering ----------------------------------------------------
	void renderAllPads();
	void renderPad(uint8_t padIdx);
	void refreshHud();
	void refreshSoftKeys();
	void refreshOverlay();
	void refreshProgress();

	// ---- timer helpers ------------------------------------------------
	void startTickTimer();
	void stopTickTimer();
	static void onTickStatic(lv_timer_t* timer);

	// ---- input --------------------------------------------------------
	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONESIMON_H
