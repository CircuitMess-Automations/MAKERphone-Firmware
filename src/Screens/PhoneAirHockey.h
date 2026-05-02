#ifndef MAKERPHONE_PHONEAIRHOCKEY_H
#define MAKERPHONE_PHONEAIRHOCKEY_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneAirHockey (S99)
 *
 * Phase-N arcade entry: a single-screen air-hockey table against a
 * single CPU opponent. The player drives the cyan mallet at the
 * bottom of the rink, the CPU drives the sunset-orange mallet at
 * the top, and a cream-coloured puck bounces between them. First to
 * five goals wins the round; a session score keeps the running
 * win/loss tally until the screen is popped.
 *
 *   +------------------------------------+
 *   | ||||  12:34                  ##### | <- PhoneStatusBar  (10 px)
 *   | YOU 02      A I R      CPU 01      | <- HUD strip       (10 px)
 *   | +--------------------------------+ |
 *   | |   [ orange CPU mallet ]        | |
 *   | |              ::                | |
 *   | |             puck               | |  <- vertical playfield
 *   | |     centre line / centre dot   | |     108 px tall, 160 px wide
 *   | |                                | |     code-only pixel rink
 *   | |   [ cyan player mallet ]       | |
 *   | +--------------------------------+ |
 *   |    PLAY                  BACK     | <- PhoneSoftKeyBar (10 px)
 *   +------------------------------------+
 *
 * Controls:
 *   - BTN_2 / BTN_UP    : slide player mallet up
 *   - BTN_8 / BTN_DOWN  : slide player mallet down
 *   - BTN_4 / BTN_LEFT  : slide player mallet left
 *   - BTN_6 / BTN_RIGHT : slide player mallet right
 *   - BTN_5 / BTN_ENTER : serve puck / pause / resume / new round
 *   - BTN_BACK  (B)     : pop back to PhoneGamesScreen.
 *
 * State machine:
 *   Idle      -- intro overlay; soft-keys: PLAY / BACK. ENTER -> Serving.
 *   Serving   -- puck is parked at centre. ENTER -> Playing (kicks the
 *                puck toward whoever just conceded, alternating on the
 *                first serve).
 *   Playing   -- physics tick (30 Hz). Mallets push the puck around the
 *                rink, walls bounce, goals score.
 *   Paused    -- tick suspended, "PAUSED" overlay. ENTER -> Playing.
 *   PlayerWon -- player reached WinGoals. Overlay "YOU WIN".
 *                ENTER -> Idle (full reset).
 *   CpuWon    -- CPU reached WinGoals. Overlay "CPU WINS".
 *                ENTER -> Idle (full reset).
 *
 * Implementation notes:
 *  - 100% code-only -- the rink is one rounded panel with a centre line
 *    drawn as two thin rectangles + a centre-dot circle. The mallets
 *    and puck are each a small `lv_obj` with `radius = LV_RADIUS_CIRCLE`
 *    so they read as discs at this resolution. No SPIFFS asset cost.
 *  - Puck position / velocity in Q8 fixed-point (pixels << 8) so
 *    collision bookkeeping stays integer-only on the ESP32 (matches
 *    PhoneBrickBreaker, PhoneBounce, and the rest of the arcade
 *    physics screens).
 *  - Mallet-puck contact is treated as a soft circular collision: when
 *    the puck overlaps a mallet, the puck is pushed out along the
 *    contact normal and gains the mallet's recent motion as added
 *    velocity. The CPU mallet samples its own delta-per-tick so the
 *    bounce-off naturally inherits the AI's intent without a separate
 *    "force" calc.
 *  - Goals are notches at the top/bottom edges. A goal is scored when
 *    the puck crosses the goal line and is inside the goal mouth.
 *    Otherwise the puck bounces off the wall as normal.
 *  - The puck has a small friction term (~0.97x per tick) so a hard
 *    smash slowly winds down toward the wandering speed. Without it
 *    the rink occasionally ends up in a stale never-ending bounce.
 */
class PhoneAirHockey : public LVScreen, private InputListener {
public:
	PhoneAirHockey();
	virtual ~PhoneAirHockey() override;

	void onStart() override;
	void onStop() override;

	// ---- Layout constants (160 x 128 panel) -----------------------------
	static constexpr lv_coord_t StatusBarH = 10;
	static constexpr lv_coord_t SoftKeyH   = 10;
	static constexpr lv_coord_t HudY       = 10;
	static constexpr lv_coord_t HudH       = 10;

	// Rink dimensions. Anchored under the HUD, taking the full screen
	// width minus a 1-px frame so the border reads cleanly at 160 px.
	static constexpr lv_coord_t RinkX      = 0;
	static constexpr lv_coord_t RinkY      = StatusBarH + HudH;        // 20
	static constexpr lv_coord_t RinkW      = 160;
	static constexpr lv_coord_t RinkH      = 128 - StatusBarH - HudH - SoftKeyH; // 98

	// Goal mouth (centred horizontally, eats both rink edges).
	static constexpr lv_coord_t GoalW      = 44;
	static constexpr lv_coord_t GoalX      = (RinkW - GoalW) / 2;       // 58

	// Mallets + puck. Discs in LVGL via radius = LV_RADIUS_CIRCLE.
	static constexpr lv_coord_t MalletSize = 11;       // 11x11 disc
	static constexpr lv_coord_t MalletR    = MalletSize / 2;  // 5
	static constexpr lv_coord_t PuckSize   = 5;        // 5x5 disc
	static constexpr lv_coord_t PuckR      = PuckSize / 2;    // 2

	// ---- Tick / physics constants --------------------------------------
	static constexpr uint32_t TickMs       = 33;
	static constexpr int16_t  Q8           = 256;
	// Player mallet step per tick (px). HOLD-key motion stays responsive.
	static constexpr int16_t  PlayerStep   = 3;
	// Maximum CPU mallet speed per tick (px). Slightly slower than the
	// player so a careful smash still beats the AI.
	static constexpr int16_t  CpuMaxStep   = 2;

	// Puck physics: friction is applied multiplicatively per tick (Q8).
	// 250/256 ~= 0.977x -- gentle enough that smashes still land hard,
	// firm enough that the puck never drifts forever.
	static constexpr int16_t  FrictionQ8   = 250;
	// Initial serve speed (Q8 px/tick magnitude in y direction).
	static constexpr int16_t  ServeSpeedQ8 = 320;
	// Cap any puck velocity component so a perfect smash never tunnels
	// through walls. 768 = 3 px/tick; with PuckR = 2 it's safe vs the
	// 1-px wall test.
	static constexpr int16_t  PuckMaxQ8    = 768;
	// Mallet-hit kick: extra Q8 velocity added in the contact-normal
	// direction. Tuned so a stationary puck pushed by a 3-px-step
	// player launches at a satisfying clip.
	static constexpr int16_t  MalletHitQ8  = 200;

	// ---- Match tuning --------------------------------------------------
	static constexpr uint8_t  WinGoals     = 5;

private:
	// ---- LVGL node graph ----------------------------------------------
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* hudYouLabel   = nullptr;
	lv_obj_t* hudTitleLabel = nullptr;
	lv_obj_t* hudCpuLabel   = nullptr;

	// Rink panel + centre line + centre dot.
	lv_obj_t* rink         = nullptr;
	lv_obj_t* centreLineL  = nullptr;
	lv_obj_t* centreLineR  = nullptr;
	lv_obj_t* centreDot    = nullptr;

	// Goals: a single dim accent strip overlaid on the rink frame so
	// the goal mouth reads as a hole rather than just a missing wall.
	lv_obj_t* goalTop      = nullptr;
	lv_obj_t* goalBottom   = nullptr;

	// Mallets + puck.
	lv_obj_t* playerMallet = nullptr;
	lv_obj_t* cpuMallet    = nullptr;
	lv_obj_t* puck         = nullptr;

	// Centre overlay (intro + paused + game over).
	lv_obj_t* overlayLabel = nullptr;

	// ---- game state ----------------------------------------------------
	enum class GameState : uint8_t {
		Idle,
		Serving,
		Playing,
		Paused,
		PlayerWon,
		CpuWon,
	};
	GameState state = GameState::Idle;

	// Player mallet centre X/Y in pixels (rink-local coords).
	int16_t playerX = 0;
	int16_t playerY = 0;
	// Previous-tick player X/Y so a hit can carry the player's intent.
	int16_t playerPrevX = 0;
	int16_t playerPrevY = 0;

	// CPU mallet centre + delta tracking.
	int16_t cpuX = 0;
	int16_t cpuY = 0;
	int16_t cpuPrevX = 0;
	int16_t cpuPrevY = 0;

	// Puck Q8 position + velocity (rink-local coords).
	int32_t puckXQ8  = 0;
	int32_t puckYQ8  = 0;
	int16_t puckVxQ8 = 0;
	int16_t puckVyQ8 = 0;

	// Score + serve state.
	uint8_t scoreYou = 0;
	uint8_t scoreCpu = 0;
	// Whose turn it is to serve next. true = player, false = CPU.
	bool   serveToPlayer = true;

	// Player input flags. Diagonals work because we OR the four axes.
	bool holdUp    = false;
	bool holdDown  = false;
	bool holdLeft  = false;
	bool holdRight = false;

	lv_timer_t* tickTimer = nullptr;

	// ---- build helpers -------------------------------------------------
	void buildHud();
	void buildRink();
	void buildPaddles();
	void buildPuck();
	void buildOverlay();

	// ---- state transitions ---------------------------------------------
	void enterIdle();
	void startMatch();             // resets score + parks puck for serve
	void serveNext();              // parks puck at centre, sets serve dir
	void launchPuck();             // serves the parked puck
	void pauseGame();
	void resumeGame();
	void endMatchPlayerWon();
	void endMatchCpuWon();
	void onGoal(bool playerScored);

	// ---- core game ops -------------------------------------------------
	void resetMallets();
	void physicsStep();
	void stepPlayer();
	void stepCpu();
	void stepPuck();
	void resolveMalletHit(int16_t mx, int16_t my,
	                      int16_t prevX, int16_t prevY);

	// ---- rendering -----------------------------------------------------
	void render();
	void refreshHud();
	void refreshSoftKeys();
	void refreshOverlay();

	// ---- timers --------------------------------------------------------
	void startTickTimer();
	void stopTickTimer();
	static void onTickTimerStatic(lv_timer_t* timer);

	// ---- input ---------------------------------------------------------
	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
};

#endif // MAKERPHONE_PHONEAIRHOCKEY_H
