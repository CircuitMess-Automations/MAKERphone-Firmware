#ifndef MAKERPHONE_PHONEEXPENSES_H
#define MAKERPHONE_PHONEEXPENSES_H

#include <Arduino.h>
#include <lvgl.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneExpenses - S141
 *
 * Phase-Q "Pocket Organiser" running expense tally. Sits next to
 * PhoneTodo (S136), PhoneHabits (S137), PhonePomodoro (S138),
 * PhoneMoodLog (S139) and PhoneScratchpad (S140) inside the eventual
 * organiser-apps grid. The user logs an amount + a category tag and
 * the screen aggregates those entries into a per-day-per-category
 * tally over a rolling 7-day window. Today's total and the running
 * 7-day weekly total are shown side-by-side at the top, with a
 * per-category breakdown immediately below.
 *
 *   List view (default):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |             EXPENSES                   | <- caption (cyan, pixelbasic7)
 *   |  ------------------------------------- |
 *   |  TODAY    23.50      WEEK    142.75    | <- totals row
 *   |  ------------------------------------- |
 *   |  > FOOD     12.50          35.00       | <- cursor row, accent
 *   |    TRAVL     5.00          18.00       |
 *   |    BILLS     0.00          25.00       |
 *   |    FUN       3.00           8.00       |
 *   |    SHOP      0.00          12.00       |
 *   |    OTHER     3.00           5.00       |
 *   |  ------------------------------------- |
 *   |  ADD                          CLEAR    | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 *   Add view (after pressing ADD):
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### |
 *   |            ADD EXPENSE                 |
 *   |  ------------------------------------- |
 *   |             AMOUNT                     |
 *   |              12.50                     | <- big pixelbasic16
 *   |             CATEGORY                   |
 *   |          <   FOOD   >                  | <- L/R cycle
 *   |   0-9 digit   L/R cat   <-- back       | <- hint line
 *   |  ------------------------------------- |
 *   |  SAVE                            <--   | <- PhoneSoftKeyBar
 *   +----------------------------------------+
 *
 * Categories (Category enum, six fixed slots):
 *   FOOD   - groceries / restaurants
 *   TRAVL  - transit, fuel, parking
 *   BILLS  - utilities, rent, subscriptions
 *   FUN    - games, movies, hobbies
 *   SHOP   - clothing, gadgets
 *   OTHER  - everything else
 *
 * Money:
 *   Amounts are stored as **uint32 cents** so we never round and so
 *   the on-disk format stays integer-clean. The UI prints them as
 *   "%u.%02u" two-decimal currency strings. The implicit ceiling of
 *   ~ $42.9 million (UINT32_MAX cents) is so far above any plausible
 *   pocket-organiser tally that we never have to worry about it.
 *
 * Day rollover:
 *   On every load() / onStart() the screen shifts the 7x6 tally grid
 *   left by (todayDay - lastSyncDay). tallies[i][cat] is
 *   (HistoryDays - 1 - i) days ago, so tallies[HistoryDays - 1][cat]
 *   is "today" and tallies[0][cat] is "6 days ago". A clock-set-
 *   backward leaves the grid alone (delta <= 0). The day index is
 *   PhoneClock::nowEpoch() / 86400, exactly as PhoneHabits (S137)
 *   and PhoneMoodLog (S139) do it, so all three pocket-organiser
 *   apps share one definition of "day".
 *
 * Stats:
 *   - Today total: sum across all categories of tallies[6].
 *   - Week total : sum across all 7 days and all categories.
 *   - Per-category today / week numbers fall out of the same grid.
 *
 * Persistence:
 *   One 176-byte NVS blob in namespace "mpexp" / key "e". Layout:
 *
 *     [0]    magic 'M'
 *     [1]    magic 'P'
 *     [2]    version (1)
 *     [3]    reserved (0)
 *     [4..7] lastSyncDay  (uint32 LE)
 *     [8..175]  tallies[7][6]  (uint32 LE cents per cell, row-major
 *                               by day; tallies[0] = oldest day)
 *
 *   Read failure (missing blob, bad magic, NVS error) leaves the
 *   in-memory grid all-zero and the screen runs RAM-only. Writes
 *   are best-effort and fail-soft, the same way PhoneMoodLog /
 *   PhoneHabits / PhoneVirtualPet / PhoneAlarmService degrade.
 *
 * Controls (List view):
 *   - BTN_2 / BTN_L            : cursor up   (cycle to bottom on top edge)
 *   - BTN_8 / BTN_R            : cursor down (cycle to top on bottom edge)
 *   - BTN_LEFT softkey ("ADD") : enter Add mode bound to the cursor cat.
 *   - BTN_RIGHT softkey ("CLEAR") : clear today's entry for the cursor
 *                                category (no-op when zero).
 *   - BTN_ENTER                : alias for ADD.
 *   - BTN_BACK                 : pop the screen.
 *
 * Controls (Add view):
 *   - BTN_0..BTN_9   : append digit to the staged amount (max 7
 *                       digits, clamped at $99999.99).
 *   - BTN_L          : prev category (wraps).
 *   - BTN_R          : next category (wraps).
 *   - BTN_LEFT softkey ("SAVE") / BTN_ENTER :
 *                       commit staged amount onto today's bucket for
 *                       the staged category, return to List.
 *   - BTN_RIGHT softkey ("<--") :
 *                       backspace one digit (or wipe the staged
 *                       amount if already zero).
 *   - BTN_BACK short : cancel and return to List view.
 *   - BTN_BACK long  : pop the screen entirely.
 *
 * Implementation notes:
 *   - 100 % code-only - no SPIFFS asset growth. Reuses
 *     PhoneSynthwaveBg / PhoneStatusBar / PhoneSoftKeyBar so the
 *     screen reads as part of the MAKERphone family.
 *   - List rows are pre-allocated lv_label children of `obj`,
 *     repainted in place on cursor move / commit. No per-frame
 *     timer; paint cost is paid only on user input + on entry.
 *   - The Add view's labels are torn down + rebuilt only on mode
 *     transitions, so steady-state edits just re-render text.
 */
class PhoneExpenses : public LVScreen, private InputListener {
public:
	PhoneExpenses();
	virtual ~PhoneExpenses() override;

	void onStart() override;
	void onStop() override;

	/** Number of days kept in the rolling tally window. 7 == one
	 *  week, the size of the visible "WEEK" total and the size of
	 *  the on-disk grid. */
	static constexpr uint8_t  HistoryDays = 7;

	/** Number of category buckets. Six fixed categories, indexed by
	 *  the Category enum below. */
	static constexpr uint8_t  CategoryCount = 6;

	/** Maximum digits the user can enter into the staged amount in
	 *  Add view. 7 digits = $99999.99 ceiling, well above any
	 *  plausible single expense. */
	static constexpr uint8_t  MaxAmountDigits = 7;

	/** Long-press threshold (matches the rest of the MAKERphone shell). */
	static constexpr uint16_t BackHoldMs = 600;

	/** Six fixed categories. The integer values are the column index
	 *  inside the on-disk tallies grid. */
	enum class Category : uint8_t {
		Food   = 0,
		Travel = 1,
		Bills  = 2,
		Fun    = 3,
		Shop   = 4,
		Other  = 5,
	};

	/** View modes. Public so a host / test can introspect state. */
	enum class Mode : uint8_t {
		List = 0,
		Add  = 1,
	};

	/** Read-only accessors for tests / future hosts. */
	Mode    getMode()      const { return mode; }
	uint8_t getCursor()    const { return cursor; }
	uint8_t getStagedDigitCount() const { return stagedDigits; }
	/** Staged amount in cents while Add mode is active; 0 elsewhere. */
	uint32_t getStagedCents() const { return stagedCents; }
	Category getStagedCategory() const { return stagedCategory; }

	/** Tally accessors. daysAgo: 0 == today, HistoryDays-1 == oldest. */
	uint32_t getTallyCents(uint8_t daysAgo, Category cat) const;
	uint32_t getTodayTotalCents() const;
	uint32_t getWeekTotalCents()  const;
	uint32_t getCategoryTodayCents(Category cat) const;
	uint32_t getCategoryWeekCents(Category cat)  const;

	/** All-uppercase short name for a category ("FOOD", "TRAVL", ...).
	 *  Static + side-effect-free so a host (or a test) can format
	 *  without standing up the screen. Returns "?" for invalid. */
	static const char* categoryName(Category c);

	/** Cycle a category forward (wraps Other -> Food). */
	static Category nextCategory(Category c);
	static Category prevCategory(Category c);

	/** Format a uint32 cents value into "%u.%02u" with no thousands
	 *  separator. Always nul-terminates `out` when `outLen > 0`.
	 *  Static so a host / test can reuse it. */
	static void formatCents(uint32_t cents, char* out, size_t outLen);

private:
	PhoneSynthwaveBg* wallpaper = nullptr;
	PhoneStatusBar*   statusBar = nullptr;
	PhoneSoftKeyBar*  softKeys  = nullptr;

	// List-view widgets.
	lv_obj_t* captionLabel  = nullptr;          // "EXPENSES" / "ADD EXPENSE"
	lv_obj_t* topDivider    = nullptr;
	lv_obj_t* totalsLabel   = nullptr;          // "TODAY 23.50    WEEK 142.75"
	lv_obj_t* totalsDivider = nullptr;
	lv_obj_t* rowLabels[CategoryCount] = { nullptr };
	lv_obj_t* bottomDivider = nullptr;

	// Add-view widgets. Created/destroyed on mode transitions so the
	// list-view labels stay clean of currency-entry baggage.
	lv_obj_t* addAmountCaption   = nullptr;     // "AMOUNT"
	lv_obj_t* addAmountValue     = nullptr;     // "12.50" pixelbasic16
	lv_obj_t* addCategoryCaption = nullptr;     // "CATEGORY"
	lv_obj_t* addCategoryValue   = nullptr;     // "<  FOOD  >"
	lv_obj_t* addHintLabel       = nullptr;     // "0-9 digit  L/R cat  <-- back"

	// Underlying tally grid. tallies[i][c] is (HistoryDays-1-i)
	// days ago for category c, in cents. tallies[HistoryDays-1] is
	// "today".
	uint32_t tallies[HistoryDays][CategoryCount] = {};
	uint32_t lastSyncDay = 0;

	// State.
	Mode     mode             = Mode::List;
	uint8_t  cursor           = 0;              // 0..CategoryCount-1
	uint32_t stagedCents      = 0;              // amount being typed in Add mode
	uint8_t  stagedDigits     = 0;              // number of digits typed
	Category stagedCategory   = Category::Food; // category selected in Add mode
	bool     backLongFired    = false;

	// ---- builders / repainters ----
	void buildListView();
	void buildAddView();
	void teardownAddView();

	void refreshAll();          // list-mode repaint (no-op in Add mode)
	void refreshCaption();
	void refreshTotalsRow();
	void refreshRows();
	void refreshSoftKeys();

	void refreshAddView();      // add-mode repaint
	void refreshAddAmount();
	void refreshAddCategory();

	// ---- mode transitions ----
	void enterList();
	void enterAdd();

	// ---- list actions ----
	void moveCursor(int8_t delta);
	void onAddPressed();
	void onClearPressed();

	// ---- add actions ----
	void onAddDigit(uint8_t d);
	void onAddBackspace();
	void onAddSave();
	void onAddCycleCategory(int8_t delta);

	// ---- model helpers ----
	void     applyToToday(Category cat, uint32_t cents);

	// ---- day rollover ----
	void syncToToday();
	static uint32_t todayIndex();

	// ---- persistence ----
	void load();
	void save();

	// ---- input ----
	void buttonPressed(uint i) override;
	void buttonReleased(uint i) override;
	void buttonHeld(uint i) override;
};

#endif // MAKERPHONE_PHONEEXPENSES_H
