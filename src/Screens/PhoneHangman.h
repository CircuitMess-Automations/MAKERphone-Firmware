#ifndef MAKERPHONE_PHONEHANGMAN_H
#define MAKERPHONE_PHONEHANGMAN_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneHangman (S87)
 *
 * Phase-N arcade entry: classic word-guessing hangman with a code-only
 * gallows and inline word list. The player guesses one letter at a
 * time using the T9 / ITU-T E.161 keypad layout that the rest of the
 * MAKERphone firmware already speaks (PhoneT9Input, PhoneDialerKey).
 * Repeated taps of the same digit cycle through that key's letters in
 * place; after a short pause -- or an explicit ENTER -- the pending
 * letter commits as the player's guess. Already-guessed letters are
 * silently skipped while cycling so a stale-letter loop doesn't
 * eat keystrokes.
 *
 *   +------------------------------------+
 *   | ||||  12:34                  ##### | <- PhoneStatusBar (10 px)
 *   | WIN 02 LOST 01            MISS 3/6 | <- HUD strip (12 px)
 *   |  ____                              |
 *   |  |  |     R O B _ T  ____          | <- gallows + word reveal
 *   |  |  O                              |    (gallows left, word right
 *   |  | /|\         GUESS:  [b]         |     in pixelbasic16)
 *   |  | / \         abc                 |    pending letter strip
 *   |  |____|                            |
 *   |                                    |
 *   |  USED: A E I O T                   | <- used-letters strip
 *   |   GUESS              BACK          | <- PhoneSoftKeyBar (10 px)
 *   +------------------------------------+
 *
 * Controls:
 *   - BTN_2 .. BTN_9       : T9 multi-tap. First press shows the key's
 *                            first un-guessed letter; subsequent presses
 *                            within `kCommitMs` advance through that
 *                            key's ring. Pressing a different digit
 *                            commits the pending letter and starts a
 *                            new cycle on the new key.
 *   - BTN_5 / BTN_ENTER    : commit the pending letter immediately.
 *                            With no pending letter, BTN_5 starts a
 *                            cycle on '5' (j/k/l), BTN_ENTER is a no-op.
 *   - BTN_LEFT / BTN_RIGHT : nudge the pending letter back / forward
 *                            through the current key's ring without
 *                            re-typing the digit.
 *   - BTN_R                : start a new round (resets the word, keeps
 *                            the win/loss tally for the session).
 *   - BTN_BACK (B)         : pop back to PhoneGamesScreen. While a
 *                            pending letter exists it is silently
 *                            cancelled rather than committed -- the
 *                            player should never lose a guess to BACK.
 *
 * State machine:
 *   Playing  -- letters being revealed; guesses still possible.
 *   Won      -- every letter of the word revealed. Overlay shows
 *               "YOU WIN -- <word>" and locks input until R or BACK.
 *   Lost     -- six wrong guesses. Overlay shows "GAME OVER -- <word>"
 *               (the full word so the player learns it) and the figure
 *               is fully drawn.
 *
 * Implementation notes:
 *   - 100 % code-only -- gallows and figure are plain `lv_obj`
 *     rectangles in MP_LABEL_DIM (gallows wood) and MP_TEXT (figure
 *     limbs), word slots are pixelbasic16 labels, used-letter strip is
 *     a single pixelbasic7 label that we rebuild after each guess.
 *     No SPIFFS asset cost.
 *   - Word list is an inline `static constexpr` array of ~40 common
 *     5-7 letter words, all uppercase A-Z. Picked uniformly at random
 *     by `newRound()` using Arduino's `random()`. The list is small
 *     enough to never displace the firmware image (well under 1 KB)
 *     and large enough that two rounds in a row almost never repeat.
 *   - Multi-tap commit timer is one `lv_timer_t` that re-arms on every
 *     pending-letter mutation. When it fires we treat the visible
 *     pending letter as the player's guess. Same lifecycle pattern as
 *     PhoneT9Input::commitTimer.
 *   - Already-guessed letters are skipped while cycling so a key whose
 *     entire ring is exhausted (e.g. all of 'a', 'b', 'c' guessed)
 *     simply produces no pending letter on subsequent presses -- the
 *     visible UI states "no letters left under 2".
 *   - Wins / losses persist across "new round" but reset when the
 *     screen pops, matching every other Phone* arcade game.
 *   - The CPU does not play -- this is a single-player puzzle, mirror
 *     to PhoneSlidingPuzzle. The "opponent" is the timer and the
 *     dwindling stick figure.
 */
class PhoneHangman : public LVScreen, private InputListener {
public:
	PhoneHangman();
	virtual ~PhoneHangman() override;

	void onStart() override;
	void onStop() override;

	// Layout - 160 x 128 panel.
	static constexpr lv_coord_t StatusBarH   = 10;
	static constexpr lv_coord_t SoftKeyH     = 10;
	static constexpr lv_coord_t HudY         = 10;
	static constexpr lv_coord_t HudH         = 12;

	// Gallows panel sits on the left half of the play area.
	static constexpr lv_coord_t GallowsX     = 6;
	static constexpr lv_coord_t GallowsY     = StatusBarH + HudH + 2;   // 24
	static constexpr lv_coord_t GallowsW     = 60;
	static constexpr lv_coord_t GallowsH     = 64;

	// Word reveal sits to the right of the gallows.
	static constexpr lv_coord_t WordX        = GallowsX + GallowsW + 4; // 70
	static constexpr lv_coord_t WordY        = GallowsY + 2;            // 26
	static constexpr lv_coord_t WordW        = 160 - WordX - 2;         // 88

	// T9 pending strip sits below the word reveal.
	static constexpr lv_coord_t PendingY     = WordY + 22;
	static constexpr lv_coord_t PendingH     = 16;

	// Used-letters strip stretches across the bottom of the play area.
	static constexpr lv_coord_t UsedY        = GallowsY + GallowsH + 4;
	static constexpr lv_coord_t UsedW        = 156;

	// Hard cap on word length - keeps the reveal label readable inside
	// WordW (~88 px) using pixelbasic16. 9 letters fits comfortably.
	static constexpr uint8_t MaxWordLen      = 9;

	// Six wrong guesses = full figure = game over. Matches the canonical
	// six-stroke gallows (head, body, two arms, two legs).
	static constexpr uint8_t MaxWrong        = 6;

	// T9 multi-tap window. Same value PhoneT9Input uses (CycleMs = 900)
	// so the muscle-memory feel is identical between the SMS composer
	// and the hangman guess input.
	static constexpr uint16_t kCommitMs      = 900;

private:
	// ---- LVGL node graph ----------------------------------------------
	PhoneSynthwaveBg* wallpaper = nullptr;
	PhoneStatusBar*   statusBar = nullptr;
	PhoneSoftKeyBar*  softKeys  = nullptr;

	lv_obj_t* hudWinsLabel    = nullptr;
	lv_obj_t* hudLossesLabel  = nullptr;
	lv_obj_t* hudMissesLabel  = nullptr;

	// Gallows + figure. Static beams and dynamic body parts are
	// distinct lv_obj rectangles so the body can show / hide piecewise
	// based on `wrongCount`.
	lv_obj_t* gallowsPost     = nullptr;
	lv_obj_t* gallowsBase     = nullptr;
	lv_obj_t* gallowsBeam     = nullptr;
	lv_obj_t* gallowsRope     = nullptr;
	lv_obj_t* figureHead      = nullptr;
	lv_obj_t* figureBody      = nullptr;
	lv_obj_t* figureArmL      = nullptr;
	lv_obj_t* figureArmR      = nullptr;
	lv_obj_t* figureLegL      = nullptr;
	lv_obj_t* figureLegR      = nullptr;

	lv_obj_t* wordLabel       = nullptr;   // big pixelbasic16 reveal
	lv_obj_t* pendingPrompt   = nullptr;   // "GUESS:" caption
	lv_obj_t* pendingLetter   = nullptr;   // big pending letter (pixelbasic16)
	lv_obj_t* pendingRing     = nullptr;   // small "abc" ring with current bracketed
	lv_obj_t* usedLabel       = nullptr;   // "USED: A B C" strip

	lv_obj_t* overlayPanel    = nullptr;   // win / loss panel
	lv_obj_t* overlayLabel    = nullptr;   // overlay text

	// ---- game state ---------------------------------------------------
	enum class GameState : uint8_t {
		Playing,
		Won,
		Lost,
	};

	GameState state           = GameState::Playing;
	char      currentWord[MaxWordLen + 1] = {0};
	uint8_t   wordLen         = 0;
	bool      revealed[MaxWordLen] = {false};
	bool      guessed[26]     = {false};   // tracks every letter ever guessed
	uint8_t   wrongCount      = 0;

	// T9 multi-tap: pending letter not yet committed as a guess.
	int8_t    pendingKey      = -1;        // 0..9 of the digit being cycled, -1 = idle
	int8_t    pendingCharIdx  = -1;        // index into kKeyLetters[pendingKey]
	lv_timer_t* commitTimer   = nullptr;

	// Session tally.
	uint16_t  winsCount       = 0;
	uint16_t  lossesCount     = 0;

	// ---- build helpers ------------------------------------------------
	void buildHud();
	void buildGallows();
	void buildFigure();
	void buildWordReveal();
	void buildPending();
	void buildUsedStrip();
	void buildOverlay();

	// ---- state transitions --------------------------------------------
	void newRound();
	void pickWord();
	void submitGuess(char letter);   // letter is uppercase A-Z
	void afterGuess();                // checks win/loss + advances state

	// T9 multi-tap helpers.
	void onDigitPress(uint8_t digit);
	void cycleDirection(int8_t dir); // +1 forward, -1 backward
	void commitPending();
	void cancelPending();
	void rearmCommitTimer();
	void cancelCommitTimer();

	// Returns true if at least one un-guessed letter exists in the
	// digit's ring. When false, the digit press becomes a no-op.
	bool keyHasUnusedLetter(uint8_t digit) const;
	// Returns -1 if no un-guessed letter found.
	int8_t findNextUnusedInKey(uint8_t digit, int8_t startIdx, int8_t dir) const;

	// ---- rendering ----------------------------------------------------
	void renderAll();
	void renderHud();
	void renderFigure();
	void renderWord();
	void renderUsed();
	void renderPending();
	void renderOverlay();
	void renderSoftKeys();

	// ---- timer callbacks ----------------------------------------------
	static void commitTimerCb(lv_timer_t* timer);

	// ---- input --------------------------------------------------------
	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONEHANGMAN_H
