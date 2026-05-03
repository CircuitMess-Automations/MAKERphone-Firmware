#include "PhoneExpenses.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>

#include <nvs.h>
#include <esp_log.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../Services/PhoneClock.h"

// MAKERphone retro palette - kept identical to every other Phone*
// widget so the expense tally slots in beside PhoneTodo (S136),
// PhoneHabits (S137), PhonePomodoro (S138), PhoneMoodLog (S139) and
// PhoneScratchpad (S140) without a visual seam. Same inline-#define
// convention every other Phone* screen .cpp uses.
#define MP_BG_DARK         lv_color_make( 20,  12,  36)  // deep purple
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan
#define MP_DIM             lv_color_make( 70,  56, 100)  // muted purple
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim cream

// ---------- geometry -----------------------------------------------------
//
// 160 x 128 layout:
//   y =  0..  9     PhoneStatusBar (10 px)
//   y = 12          caption "EXPENSES" / "ADD EXPENSE"
//   y = 22          top divider rule
//
//   List view:
//     y = 26        totals row "TODAY 23.50    WEEK 142.75" (pixelbasic7)
//     y = 38        totals divider rule
//     y = 42..92    six category rows at 10 px stride
//     y =105        bottom divider rule
//
//   Add view:
//     y = 26        small caption "AMOUNT"
//     y = 36        big amount (pixelbasic16) centred
//     y = 60        small caption "CATEGORY"
//     y = 70        big category (pixelbasic16) centred with "<  X  >"
//     y = 92        small hint "0-9 digit  L/R cat  <-- back"
//     y =105        bottom divider rule
//
//   y = 118..127    PhoneSoftKeyBar

static constexpr lv_coord_t kCaptionY     = 12;
static constexpr lv_coord_t kTopDividerY  = 22;
static constexpr lv_coord_t kTotalsY      = 26;
static constexpr lv_coord_t kTotalsDivY   = 38;
static constexpr lv_coord_t kRowsY        = 42;
static constexpr lv_coord_t kRowStride    = 10;
static constexpr lv_coord_t kBotDividerY  = 105;

static constexpr lv_coord_t kRowLeftX     = 4;
static constexpr lv_coord_t kRowWidth     = 152;

static constexpr lv_coord_t kAddAmountCapY     = 26;
static constexpr lv_coord_t kAddAmountValueY   = 36;
static constexpr lv_coord_t kAddCategoryCapY   = 60;
static constexpr lv_coord_t kAddCategoryValueY = 70;
static constexpr lv_coord_t kAddHintY          = 92;

// ---------- NVS persistence ----------------------------------------------

namespace {

constexpr const char* kNamespace  = "mpexp";
constexpr const char* kBlobKey    = "e";

constexpr uint8_t  kMagic0   = 'M';
constexpr uint8_t  kMagic1   = 'P';
constexpr uint8_t  kVersion  = 1;

// 8-byte header + 7 days * 6 categories * 4 bytes = 168 = 176 total.
constexpr size_t   kBlobBytes =
    8 + (size_t) PhoneExpenses::HistoryDays
            * (size_t) PhoneExpenses::CategoryCount
            * sizeof(uint32_t);

// Single shared NVS handle, lazy-open. Mirrors PhoneMoodLog /
// PhoneHabits / PhoneVirtualPet so we never spam nvs_open() retries
// when the flash partition is unavailable.
nvs_handle s_handle    = 0;
bool       s_attempted = false;

bool ensureOpen() {
	if(s_handle != 0) return true;
	if(s_attempted)   return false;
	s_attempted = true;
	auto err = nvs_open(kNamespace, NVS_READWRITE, &s_handle);
	if(err != ESP_OK) {
		ESP_LOGW("PhoneExpenses",
		         "nvs_open(%s) failed: %d -- expenses runs without persistence",
		         kNamespace, (int)err);
		s_handle = 0;
		return false;
	}
	return true;
}

void writeU32LE(uint8_t* p, uint32_t v) {
	p[0] = (uint8_t)( v        & 0xFF);
	p[1] = (uint8_t)((v >>  8) & 0xFF);
	p[2] = (uint8_t)((v >> 16) & 0xFF);
	p[3] = (uint8_t)((v >> 24) & 0xFF);
}

uint32_t readU32LE(const uint8_t* p) {
	return  (uint32_t) p[0]
	     | ((uint32_t) p[1] <<  8)
	     | ((uint32_t) p[2] << 16)
	     | ((uint32_t) p[3] << 24);
}

// Hard ceiling on the staged amount during Add mode: 9_999_999 cents
// == $99,999.99. Anything past that is silently clamped so the
// label never overflows the 14 px digit cell.
constexpr uint32_t kStagedCentsCeiling = 9999999u;

} // namespace

// ---------- public statics -----------------------------------------------

const char* PhoneExpenses::categoryName(Category c) {
	switch(c) {
		case Category::Food:   return "FOOD";
		case Category::Travel: return "TRAVL";
		case Category::Bills:  return "BILLS";
		case Category::Fun:    return "FUN";
		case Category::Shop:   return "SHOP";
		case Category::Other:  return "OTHER";
	}
	return "?";
}

PhoneExpenses::Category PhoneExpenses::nextCategory(Category c) {
	uint8_t n = (uint8_t) c;
	n = (uint8_t)((n + 1u) % CategoryCount);
	return (Category) n;
}

PhoneExpenses::Category PhoneExpenses::prevCategory(Category c) {
	uint8_t n = (uint8_t) c;
	n = (uint8_t)((n + CategoryCount - 1u) % CategoryCount);
	return (Category) n;
}

void PhoneExpenses::formatCents(uint32_t cents, char* out, size_t outLen) {
	if(out == nullptr || outLen == 0) return;
	uint32_t whole = cents / 100u;
	uint32_t frac  = cents % 100u;
	snprintf(out, outLen, "%u.%02u",
	         (unsigned) whole, (unsigned) frac);
}

uint32_t PhoneExpenses::todayIndex() {
	return PhoneClock::nowEpoch() / 86400u;
}

// ---------- ctor / dtor --------------------------------------------------

PhoneExpenses::PhoneExpenses()
		: LVScreen() {
	for(uint8_t i = 0; i < HistoryDays; ++i) {
		for(uint8_t c = 0; c < CategoryCount; ++c) {
			tallies[i][c] = 0;
		}
	}
	lastSyncDay = todayIndex();

	// Pull persisted entries (if any). syncToToday() then shifts the
	// 7-row history left so "today" lives at tallies[HistoryDays-1]
	// before the first refresh paints anything. Empty / failed read
	// degrades to RAM-only.
	load();
	syncToToday();

	cursor = 0;

	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);
	softKeys  = new PhoneSoftKeyBar(obj);

	buildListView();

	setButtonHoldTime(BTN_BACK, BackHoldMs);

	refreshAll();
}

PhoneExpenses::~PhoneExpenses() {
	// LVGL children parented to obj are freed by the LVScreen base.
}

void PhoneExpenses::onStart() {
	Input::getInstance()->addListener(this);
	// Re-sync when the screen comes back to the foreground so a long
	// background sit-out still slides the grid into the right slots.
	syncToToday();
	if(mode == Mode::List) {
		refreshAll();
	} else {
		refreshAddView();
	}
}

void PhoneExpenses::onStop() {
	Input::getInstance()->removeListener(this);
}

// ---------- builders -----------------------------------------------------

void PhoneExpenses::buildListView() {
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, kCaptionY);
	lv_label_set_text(captionLabel, "EXPENSES");

	topDivider = lv_obj_create(obj);
	lv_obj_remove_style_all(topDivider);
	lv_obj_set_size(topDivider, kRowWidth, 1);
	lv_obj_set_style_bg_color(topDivider, MP_DIM, 0);
	lv_obj_set_style_bg_opa(topDivider, LV_OPA_COVER, 0);
	lv_obj_set_pos(topDivider, kRowLeftX, kTopDividerY);

	totalsLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(totalsLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(totalsLabel, MP_TEXT, 0);
	lv_obj_set_pos(totalsLabel, kRowLeftX, kTotalsY);
	lv_label_set_text(totalsLabel, "");

	totalsDivider = lv_obj_create(obj);
	lv_obj_remove_style_all(totalsDivider);
	lv_obj_set_size(totalsDivider, kRowWidth, 1);
	lv_obj_set_style_bg_color(totalsDivider, MP_DIM, 0);
	lv_obj_set_style_bg_opa(totalsDivider, LV_OPA_COVER, 0);
	lv_obj_set_pos(totalsDivider, kRowLeftX, kTotalsDivY);

	for(uint8_t i = 0; i < CategoryCount; ++i) {
		lv_obj_t* lbl = lv_label_create(obj);
		lv_obj_set_style_text_font(lbl, &pixelbasic7, 0);
		lv_obj_set_style_text_color(lbl, MP_TEXT, 0);
		lv_obj_set_pos(lbl, kRowLeftX,
		               (lv_coord_t)(kRowsY + i * kRowStride));
		lv_label_set_text(lbl, "");
		rowLabels[i] = lbl;
	}

	bottomDivider = lv_obj_create(obj);
	lv_obj_remove_style_all(bottomDivider);
	lv_obj_set_size(bottomDivider, kRowWidth, 1);
	lv_obj_set_style_bg_color(bottomDivider, MP_DIM, 0);
	lv_obj_set_style_bg_opa(bottomDivider, LV_OPA_COVER, 0);
	lv_obj_set_pos(bottomDivider, kRowLeftX, kBotDividerY);
}

void PhoneExpenses::buildAddView() {
	addAmountCaption = lv_label_create(obj);
	lv_obj_set_style_text_font(addAmountCaption, &pixelbasic7, 0);
	lv_obj_set_style_text_color(addAmountCaption, MP_LABEL_DIM, 0);
	lv_obj_set_align(addAmountCaption, LV_ALIGN_TOP_MID);
	lv_obj_set_y(addAmountCaption, kAddAmountCapY);
	lv_label_set_text(addAmountCaption, "AMOUNT");

	addAmountValue = lv_label_create(obj);
	lv_obj_set_style_text_font(addAmountValue, &pixelbasic16, 0);
	lv_obj_set_style_text_color(addAmountValue, MP_TEXT, 0);
	lv_obj_set_align(addAmountValue, LV_ALIGN_TOP_MID);
	lv_obj_set_y(addAmountValue, kAddAmountValueY);
	lv_label_set_text(addAmountValue, "0.00");

	addCategoryCaption = lv_label_create(obj);
	lv_obj_set_style_text_font(addCategoryCaption, &pixelbasic7, 0);
	lv_obj_set_style_text_color(addCategoryCaption, MP_LABEL_DIM, 0);
	lv_obj_set_align(addCategoryCaption, LV_ALIGN_TOP_MID);
	lv_obj_set_y(addCategoryCaption, kAddCategoryCapY);
	lv_label_set_text(addCategoryCaption, "CATEGORY");

	addCategoryValue = lv_label_create(obj);
	lv_obj_set_style_text_font(addCategoryValue, &pixelbasic16, 0);
	lv_obj_set_style_text_color(addCategoryValue, MP_HIGHLIGHT, 0);
	lv_obj_set_align(addCategoryValue, LV_ALIGN_TOP_MID);
	lv_obj_set_y(addCategoryValue, kAddCategoryValueY);
	lv_label_set_text(addCategoryValue, "FOOD");

	addHintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(addHintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(addHintLabel, MP_DIM, 0);
	lv_obj_set_align(addHintLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(addHintLabel, kAddHintY);
	lv_label_set_text(addHintLabel, "0-9 ENTER  L/R CAT");
}

void PhoneExpenses::teardownAddView() {
	if(addAmountCaption)   { lv_obj_del(addAmountCaption);   addAmountCaption = nullptr; }
	if(addAmountValue)     { lv_obj_del(addAmountValue);     addAmountValue = nullptr; }
	if(addCategoryCaption) { lv_obj_del(addCategoryCaption); addCategoryCaption = nullptr; }
	if(addCategoryValue)   { lv_obj_del(addCategoryValue);   addCategoryValue = nullptr; }
	if(addHintLabel)       { lv_obj_del(addHintLabel);       addHintLabel = nullptr; }
}

// ---------- public introspection -----------------------------------------

uint32_t PhoneExpenses::getTallyCents(uint8_t daysAgo, Category cat) const {
	if(daysAgo >= HistoryDays) return 0;
	uint8_t c = (uint8_t) cat;
	if(c >= CategoryCount) return 0;
	return tallies[HistoryDays - 1 - daysAgo][c];
}

uint32_t PhoneExpenses::getTodayTotalCents() const {
	uint32_t sum = 0;
	for(uint8_t c = 0; c < CategoryCount; ++c) {
		sum += tallies[HistoryDays - 1][c];
	}
	return sum;
}

uint32_t PhoneExpenses::getWeekTotalCents() const {
	uint32_t sum = 0;
	for(uint8_t i = 0; i < HistoryDays; ++i) {
		for(uint8_t c = 0; c < CategoryCount; ++c) {
			sum += tallies[i][c];
		}
	}
	return sum;
}

uint32_t PhoneExpenses::getCategoryTodayCents(Category cat) const {
	uint8_t c = (uint8_t) cat;
	if(c >= CategoryCount) return 0;
	return tallies[HistoryDays - 1][c];
}

uint32_t PhoneExpenses::getCategoryWeekCents(Category cat) const {
	uint8_t c = (uint8_t) cat;
	if(c >= CategoryCount) return 0;
	uint32_t sum = 0;
	for(uint8_t i = 0; i < HistoryDays; ++i) {
		sum += tallies[i][c];
	}
	return sum;
}

// ---------- repainters (List view) ---------------------------------------

void PhoneExpenses::refreshAll() {
	refreshCaption();
	refreshTotalsRow();
	refreshRows();
	refreshSoftKeys();
}

void PhoneExpenses::refreshCaption() {
	if(captionLabel == nullptr) return;
	lv_label_set_text(captionLabel,
	                  (mode == Mode::Add) ? "ADD EXPENSE" : "EXPENSES");
}

void PhoneExpenses::refreshTotalsRow() {
	if(totalsLabel == nullptr) return;
	char today[16];
	char week[16];
	formatCents(getTodayTotalCents(), today, sizeof(today));
	formatCents(getWeekTotalCents(),  week,  sizeof(week));

	char buf[48];
	snprintf(buf, sizeof(buf), "TODAY %s   WEEK %s", today, week);
	lv_label_set_text(totalsLabel, buf);
}

void PhoneExpenses::refreshRows() {
	for(uint8_t i = 0; i < CategoryCount; ++i) {
		if(rowLabels[i] == nullptr) continue;
		Category cat = (Category) i;
		uint32_t today = getCategoryTodayCents(cat);
		uint32_t week  = getCategoryWeekCents(cat);

		char todayBuf[16];
		char weekBuf[16];
		formatCents(today, todayBuf, sizeof(todayBuf));
		formatCents(week,  weekBuf,  sizeof(weekBuf));

		const char* prefix = (i == cursor) ? ">" : " ";
		char buf[48];
		snprintf(buf, sizeof(buf), "%s %-5s %8s    %8s",
		         prefix, categoryName(cat), todayBuf, weekBuf);
		lv_label_set_text(rowLabels[i], buf);

		// Cursor row gets the accent colour, others stay cream-on-purple.
		// Categories with no today entry fade to dim so the eye locks
		// on the rows that actually moved today.
		lv_color_t col;
		if(i == cursor) {
			col = MP_ACCENT;
		} else if(today == 0 && week == 0) {
			col = MP_DIM;
		} else if(today == 0) {
			col = MP_LABEL_DIM;
		} else {
			col = MP_TEXT;
		}
		lv_obj_set_style_text_color(rowLabels[i], col, 0);
	}
}

void PhoneExpenses::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	if(mode == Mode::Add) {
		softKeys->setLeft("SAVE");
		softKeys->setRight("<--");
	} else {
		softKeys->setLeft("ADD");
		// Only offer CLEAR when the cursor row has something to clear,
		// matching PhoneMoodLog's pattern of hiding inert softkeys.
		Category cat = (Category) cursor;
		softKeys->setRight((getCategoryTodayCents(cat) != 0) ? "CLEAR" : "");
	}
}

// ---------- repainters (Add view) ----------------------------------------

void PhoneExpenses::refreshAddView() {
	refreshCaption();
	refreshAddAmount();
	refreshAddCategory();
	refreshSoftKeys();
}

void PhoneExpenses::refreshAddAmount() {
	if(addAmountValue == nullptr) return;
	char buf[16];
	formatCents(stagedCents, buf, sizeof(buf));
	lv_label_set_text(addAmountValue, buf);
	// Cream while typing, accent once a non-zero amount is parked.
	lv_obj_set_style_text_color(addAmountValue,
	                            (stagedCents == 0) ? MP_LABEL_DIM : MP_TEXT, 0);
}

void PhoneExpenses::refreshAddCategory() {
	if(addCategoryValue == nullptr) return;
	char buf[24];
	snprintf(buf, sizeof(buf), "<  %s  >",
	         categoryName(stagedCategory));
	lv_label_set_text(addCategoryValue, buf);
}

// ---------- mode transitions ---------------------------------------------

void PhoneExpenses::enterList() {
	if(mode == Mode::List) return;
	mode = Mode::List;
	teardownAddView();
	stagedCents   = 0;
	stagedDigits  = 0;
	refreshAll();
}

void PhoneExpenses::enterAdd() {
	if(mode == Mode::Add) return;
	mode = Mode::Add;
	stagedCents    = 0;
	stagedDigits   = 0;
	stagedCategory = (Category) cursor;
	buildAddView();
	refreshAddView();
}

// ---------- list actions -------------------------------------------------

void PhoneExpenses::moveCursor(int8_t delta) {
	if(delta == 0) return;
	int16_t n = (int16_t) cursor + (int16_t) delta;
	while(n < 0)                          n += CategoryCount;
	while(n >= (int16_t) CategoryCount)   n -= CategoryCount;
	cursor = (uint8_t) n;
	refreshRows();
	refreshSoftKeys();
}

void PhoneExpenses::onAddPressed() {
	if(softKeys) softKeys->flashLeft();
	enterAdd();
}

void PhoneExpenses::onClearPressed() {
	Category cat = (Category) cursor;
	if(getCategoryTodayCents(cat) == 0) return;   // softkey hidden anyway
	if(softKeys) softKeys->flashRight();
	tallies[HistoryDays - 1][(uint8_t) cat] = 0;
	save();
	refreshAll();
}

// ---------- add actions --------------------------------------------------

void PhoneExpenses::onAddDigit(uint8_t d) {
	if(d > 9) return;
	if(stagedDigits >= MaxAmountDigits) return;
	uint64_t next = (uint64_t) stagedCents * 10u + (uint64_t) d;
	if(next > (uint64_t) kStagedCentsCeiling) return;
	stagedCents  = (uint32_t) next;
	if(stagedCents != 0 || stagedDigits != 0) {
		// Don't count "leading zeros" that don't move the displayed
		// amount. Only the first meaningful digit starts the count.
		if(stagedCents != 0) ++stagedDigits;
	}
	refreshAddAmount();
}

void PhoneExpenses::onAddBackspace() {
	if(softKeys) softKeys->flashRight();
	if(stagedCents == 0) {
		stagedDigits = 0;
		refreshAddAmount();
		return;
	}
	stagedCents /= 10u;
	if(stagedDigits > 0) --stagedDigits;
	refreshAddAmount();
}

void PhoneExpenses::onAddSave() {
	if(softKeys) softKeys->flashLeft();
	if(stagedCents == 0) {
		// Nothing to commit; treat as a soft cancel.
		enterList();
		return;
	}
	applyToToday(stagedCategory, stagedCents);
	enterList();
}

void PhoneExpenses::onAddCycleCategory(int8_t delta) {
	if(delta == 0) return;
	if(delta > 0) stagedCategory = nextCategory(stagedCategory);
	else          stagedCategory = prevCategory(stagedCategory);
	refreshAddCategory();
}

// ---------- model helpers ------------------------------------------------

void PhoneExpenses::applyToToday(Category cat, uint32_t cents) {
	uint8_t c = (uint8_t) cat;
	if(c >= CategoryCount) return;
	uint64_t next = (uint64_t) tallies[HistoryDays - 1][c] + (uint64_t) cents;
	if(next > (uint64_t) UINT32_MAX) next = UINT32_MAX;
	tallies[HistoryDays - 1][c] = (uint32_t) next;
	save();
}

// ---------- day rollover -------------------------------------------------

void PhoneExpenses::syncToToday() {
	const uint32_t today = todayIndex();
	if(today == lastSyncDay) return;
	if(today < lastSyncDay) {
		// Clock moved backward - adopt the new anchor without rotating
		// any history. Edge case (user time-traveled). The next
		// forward advance only shifts by the new delta.
		lastSyncDay = today;
		return;
	}
	const uint32_t delta = today - lastSyncDay;
	if(delta >= HistoryDays) {
		for(uint8_t i = 0; i < HistoryDays; ++i) {
			for(uint8_t c = 0; c < CategoryCount; ++c) tallies[i][c] = 0;
		}
	} else {
		const uint8_t shift = (uint8_t) delta;
		for(uint8_t i = 0; i + shift < HistoryDays; ++i) {
			for(uint8_t c = 0; c < CategoryCount; ++c) {
				tallies[i][c] = tallies[i + shift][c];
			}
		}
		for(uint8_t i = (uint8_t)(HistoryDays - shift); i < HistoryDays; ++i) {
			for(uint8_t c = 0; c < CategoryCount; ++c) tallies[i][c] = 0;
		}
	}
	lastSyncDay = today;
}

// ---------- persistence --------------------------------------------------

void PhoneExpenses::load() {
	if(!ensureOpen()) return;

	size_t blobSize = 0;
	auto err = nvs_get_blob(s_handle, kBlobKey, nullptr, &blobSize);
	if(err != ESP_OK)            return;
	if(blobSize != kBlobBytes)   return;

	uint8_t buf[kBlobBytes] = {};
	size_t  readLen = blobSize;
	err = nvs_get_blob(s_handle, kBlobKey, buf, &readLen);
	if(err != ESP_OK)            return;
	if(readLen != kBlobBytes)    return;
	if(buf[0] != kMagic0)        return;
	if(buf[1] != kMagic1)        return;
	if(buf[2] != kVersion)       return;

	lastSyncDay = readU32LE(&buf[4]);
	size_t off = 8;
	for(uint8_t i = 0; i < HistoryDays; ++i) {
		for(uint8_t c = 0; c < CategoryCount; ++c) {
			tallies[i][c] = readU32LE(&buf[off]);
			off += 4;
		}
	}
}

void PhoneExpenses::save() {
	if(!ensureOpen()) return;

	uint8_t buf[kBlobBytes] = {};
	buf[0] = kMagic0;
	buf[1] = kMagic1;
	buf[2] = kVersion;
	buf[3] = 0;
	writeU32LE(&buf[4], lastSyncDay);
	size_t off = 8;
	for(uint8_t i = 0; i < HistoryDays; ++i) {
		for(uint8_t c = 0; c < CategoryCount; ++c) {
			writeU32LE(&buf[off], tallies[i][c]);
			off += 4;
		}
	}

	auto err = nvs_set_blob(s_handle, kBlobKey, buf, kBlobBytes);
	if(err != ESP_OK) {
		ESP_LOGW("PhoneExpenses", "nvs_set_blob failed: %d", (int)err);
		return;
	}
	err = nvs_commit(s_handle);
	if(err != ESP_OK) {
		ESP_LOGW("PhoneExpenses", "nvs_commit failed: %d", (int)err);
	}
}

// ---------- input --------------------------------------------------------

void PhoneExpenses::buttonPressed(uint i) {
	if(mode == Mode::List) {
		switch(i) {
			case BTN_2: case BTN_L:
				moveCursor(-1);
				break;
			case BTN_8: case BTN_R:
				moveCursor(+1);
				break;
			case BTN_LEFT:
				onAddPressed();
				break;
			case BTN_ENTER:
				onAddPressed();
				break;
			case BTN_RIGHT: {
				Category cat = (Category) cursor;
				if(getCategoryTodayCents(cat) != 0) onClearPressed();
				break;
			}
			default:
				break;
		}
	} else {
		// Add mode.
		switch(i) {
			case BTN_0: onAddDigit(0); break;
			case BTN_1: onAddDigit(1); break;
			case BTN_2: onAddDigit(2); break;
			case BTN_3: onAddDigit(3); break;
			case BTN_4: onAddDigit(4); break;
			case BTN_5: onAddDigit(5); break;
			case BTN_6: onAddDigit(6); break;
			case BTN_7: onAddDigit(7); break;
			case BTN_8: onAddDigit(8); break;
			case BTN_9: onAddDigit(9); break;

			case BTN_L:
				onAddCycleCategory(-1);
				break;
			case BTN_R:
				onAddCycleCategory(+1);
				break;

			case BTN_LEFT:
			case BTN_ENTER:
				onAddSave();
				break;

			case BTN_RIGHT:
				onAddBackspace();
				break;

			default:
				break;
		}
	}
}

void PhoneExpenses::buttonHeld(uint i) {
	if(i == BTN_BACK) {
		backLongFired = true;
		// Long-back always pops the screen entirely, regardless of mode.
		pop();
	}
}

void PhoneExpenses::buttonReleased(uint i) {
	if(i == BTN_BACK) {
		if(backLongFired) {
			backLongFired = false;
			return;
		}
		// Short-back: if we're inside Add, fall back to the list view
		// so the user keeps their tally on screen. From the list view,
		// it pops the screen.
		if(mode == Mode::Add) {
			enterList();
		} else {
			pop();
		}
	}
}
