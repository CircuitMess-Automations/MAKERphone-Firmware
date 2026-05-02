#ifndef MAKERPHONE_PHONESOLITAIRE_H
#define MAKERPHONE_PHONESOLITAIRE_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneSolitaire (S94)
 *
 * Phase-N arcade entry: a dialer-driven Klondike Solitaire. Each player
 * works the classic 4-foundation / 7-tableau Patience layout using the
 * numpad to pick which pile to act on:
 *
 *   +------------------------------------+
 *   | ||||  12:34                  ##### | <- PhoneStatusBar (10px)
 *   | MOVES 042              SCORE 02048 | <- HUD strip      (12px)
 *   |                                    |
 *   | [##] [3H]    [ A] [ A] [ A] [ A]   | <- top row (17px):
 *   |  ST   WS      H    D    C    S     |    stock | waste | 4 foundations
 *   |                                    |
 *   | [##][##][##][##][##][##][##]       | <- 7 tableau columns
 *   | [..][##][##][##][##][##][##]       |    each col is a stack
 *   | [..][..][##][##][##][##][##]       |    of 14x?? card sprites
 *   |  1   2   3   4   5   6   7         |    with overlap
 *   |                                    |
 *   |    DRAW             BACK           | <- PhoneSoftKeyBar (10px)
 *   +------------------------------------+
 *
 * Controls (dialer-driven, per the roadmap title):
 *   - BTN_1..BTN_7  : pick tableau column 1..7. First press selects
 *                     that column as the move source (must hold a
 *                     face-up card); a follow-up dialer press picks
 *                     the destination.
 *   - BTN_8         : draw -- flip the top stock card onto the waste,
 *                     or recycle the entire waste back into the stock
 *                     when the stock is empty.
 *   - BTN_9         : pick the waste pile as the source for the next
 *                     move (its top card is the candidate).
 *   - BTN_0         : pick a foundation as the destination. The suit
 *                     is auto-routed from the selected card -- e.g.
 *                     selecting the top of waste then pressing 0 sends
 *                     it to its matching suit foundation. Pressing 0
 *                     with no source is a "promote any to foundation"
 *                     auto-move that scans every face-up top card and
 *                     plays whichever can advance the foundations.
 *   - BTN_ENTER (A) : same as BTN_0 -- handy "auto-foundation" shortcut
 *                     when a source is already selected.
 *   - BTN_BACK (B)  : if a source is selected, cancel that selection;
 *                     otherwise pop back to PhoneGamesScreen.
 *   - BTN_R         : restart the deal with a fresh shuffle (cheap "I
 *                     give up" reset).
 *   - BTN_L         : same as BTN_8 (alias so the bumper feels useful).
 *
 * Move semantics:
 *   - Tableau -> Tableau moves the entire face-up run of the source
 *     column starting from its first face-up card. Legality is checked
 *     against the run's first card (descending rank, alternating
 *     colour onto the destination's top card; a King onto an empty
 *     column).
 *   - Tableau -> Foundation moves only the topmost card.
 *   - Waste -> Tableau / Foundation moves only the top of waste.
 *   - Foundation -> Tableau moves only the top of that foundation.
 *   - When a tableau move uncovers a face-down card, that card is
 *     auto-flipped face-up and the player gets a small score bump
 *     (5 points), matching the conventional Klondike scoring.
 *   - Win condition: every foundation holds 13 cards (Ace through
 *     King). The Won overlay congratulates the player and BTN_ENTER
 *     deals a fresh game.
 *
 * Implementation notes:
 *   - 100% code-only -- every card is a plain lv_obj rectangle with a
 *     pixelbasic7 label rendered as "RankSuit" (e.g. "AH", "10C",
 *     "KS"). Hearts/Diamonds use a red label, Clubs/Spades use the
 *     deep-purple label colour for the high-contrast "black" cards.
 *     No SPIFFS asset cost.
 *   - The seven tableau columns are dynamic: each column rebuilds its
 *     children on every state change via lv_obj_clean() + per-card
 *     create. With at most ~30 cards on screen at once and moves
 *     measured in seconds, the cost is negligible.
 *   - The shuffle uses a plain Fisher-Yates over the 52-card deck,
 *     seeded from millis() at the start of each round. Most deals are
 *     winnable; that's part of the Klondike charm.
 *   - Score / move count are kept in-memory only -- matches every
 *     other Phone* game in the v1.0 roadmap.
 *   - Multi-card move ergonomics: the run that gets moved when picking
 *     a tableau source is "all face-up cards from the first face-up
 *     card to the top". This is the most common Klondike convention
 *     and avoids a deeper "which card to grab" sub-cursor that would
 *     not fit cleanly on the 16-button keypad.
 */
class PhoneSolitaire : public LVScreen, private InputListener {
public:
	PhoneSolitaire();
	virtual ~PhoneSolitaire() override;

	void onStart() override;
	void onStop() override;

	// Screen layout - mirrors the diagram above for the 160x128 panel.
	static constexpr lv_coord_t StatusBarH    = 10;
	static constexpr lv_coord_t SoftKeyH      = 10;
	static constexpr lv_coord_t HudY          = 10;
	static constexpr lv_coord_t HudH          = 12;

	// Top pile row sits just under the HUD.
	static constexpr lv_coord_t TopPileY      = 23;
	static constexpr lv_coord_t TopPileH      = 14;
	static constexpr lv_coord_t TopPileLabelY = 38;   // tiny caption row

	// Tableau bands sit below the top pile row (gap leaves room for
	// the column-number labels at TableauLabelY).
	static constexpr lv_coord_t TableauY       = 50;
	static constexpr lv_coord_t TableauH       = 14;
	static constexpr lv_coord_t TableauLabelY  = 111;

	static constexpr lv_coord_t CardW          = 20;

	// Logical pile counts.
	static constexpr uint8_t TableauCount    = 7;
	static constexpr uint8_t FoundationCount = 4;
	static constexpr uint8_t MaxColCards     = 19;   // theoretical cap
	static constexpr uint8_t StockCap        = 24;

	// Pile id mapping for selectedSource / selection target.
	// 0..6   = tableau columns 0..6
	// 7      = waste
	// 8..11  = foundations 0..3 (H, D, C, S)
	// (stock is never a "source/target" -- pressing 8 just draws.)
	static constexpr int8_t PileNone           = -1;
	static constexpr int8_t PileWaste          = 7;
	static constexpr int8_t PileFoundationBase = 8;

private:
	// ---- model -------------------------------------------------------
	// Single-byte card encoding so deck/pile arrays stay tiny.
	struct Card {
		uint8_t rank   : 4;   // 1..13 (1=A, 11=J, 12=Q, 13=K). 0 means empty.
		uint8_t suit   : 2;   // 0=H, 1=D, 2=C, 3=S
		uint8_t faceUp : 1;
		uint8_t pad    : 1;   // reserved -- keeps sizeof(Card) == 1
	};

	Card    tableau[TableauCount][MaxColCards];
	uint8_t tableauSize[TableauCount];

	Card    stock[StockCap];
	uint8_t stockSize  = 0;

	Card    waste[StockCap];
	uint8_t wasteSize  = 0;

	Card    foundations[FoundationCount][13];
	uint8_t foundationSize[FoundationCount];

	int8_t   selectedSource = PileNone;
	uint32_t moves          = 0;
	uint32_t score          = 0;

	enum class GameState : uint8_t { Playing, Won };
	GameState state = GameState::Playing;

	// ---- LVGL node graph ---------------------------------------------
	PhoneSynthwaveBg* wallpaper = nullptr;
	PhoneStatusBar*   statusBar = nullptr;
	PhoneSoftKeyBar*  softKeys  = nullptr;

	lv_obj_t* hudMovesLabel = nullptr;
	lv_obj_t* hudScoreLabel = nullptr;
	lv_obj_t* overlayLabel  = nullptr;

	// Top row: stock + waste (left) + 4 foundations (right). Each pile
	// is one container with a single card sprite + label inside it.
	lv_obj_t* stockSprite       = nullptr;
	lv_obj_t* stockLabel        = nullptr;
	lv_obj_t* wasteSprite       = nullptr;
	lv_obj_t* wasteLabel        = nullptr;
	lv_obj_t* foundationSprite[FoundationCount] = { nullptr, nullptr, nullptr, nullptr };
	lv_obj_t* foundationLabel [FoundationCount] = { nullptr, nullptr, nullptr, nullptr };

	// Tableau: one container per column. Cards inside are recreated on
	// every render (lv_obj_clean()).
	lv_obj_t* tableauContainer[TableauCount] = { nullptr, nullptr, nullptr, nullptr,
	                                              nullptr, nullptr, nullptr };

	// ---- build helpers -----------------------------------------------
	void buildHud();
	void buildOverlay();
	void buildTopRow();
	void buildTableau();

	// ---- state transitions -------------------------------------------
	void newGame();
	void shuffleDeck(Card deck[52]);
	bool drawFromStock();   // BTN_8 / BTN_L

	// Try to consume `selectedSource` against `target`. Returns true
	// only on a successful, legal move.
	bool tryMoveOnto(int8_t target);

	// Dialer-pressed-with-no-source semantics: scan every face-up top
	// card on tableau + waste and play the first one that can advance
	// any foundation. Returns true if a card was promoted.
	bool autoPromoteToFoundation();

	void selectSource(int8_t pile);   // figures out what to highlight
	void cancelSelection();

	void checkWin();

	// ---- rendering ---------------------------------------------------
	void renderAll();
	void renderTopRow();
	void renderTableauColumn(uint8_t col);
	void refreshHud();
	void refreshSoftKeys();
	void refreshOverlay();
	void refreshSelectionVisuals();

	// Build a single card sprite + label inside `parent` at (x, y).
	// `c.rank == 0` is treated as "empty placeholder".
	lv_obj_t* spawnCardSprite(lv_obj_t* parent,
	                          lv_coord_t x, lv_coord_t y,
	                          Card c, bool isPlaceholder);

	// Convert a card to its 1- or 2-character rank glyph (A, 2..9, T,
	// J, Q, K). Returns a pointer to a static buffer per call.
	static const char* rankGlyph(uint8_t rank);
	// Suit label is one of H / D / C / S.
	static const char* suitGlyph(uint8_t suit);
	static bool isRedSuit(uint8_t suit) { return suit < 2; }

	// ---- input -------------------------------------------------------
	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONESOLITAIRE_H
