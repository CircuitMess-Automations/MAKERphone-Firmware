#include "PhoneMoodLog.h"

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
// widget so the mood journal slots in beside PhoneTodo (S136),
// PhoneHabits (S137), PhonePomodoro (S138) and the rest of the
// Phase-Q family without a visual seam. Same inline-#define
// convention every other Phone* screen .cpp uses.
#define MP_BG_DARK         lv_color_make( 20,  12,  36)  // deep purple
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan
#define MP_DIM             lv_color_make( 70,  56, 100)  // muted purple
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim cream

// Per-mood swatch colours. Picked so a glance at the strip reads
// "warm = good, dark = bad" without relying on any non-ASCII glyph.
#define MP_MOOD_AWFUL      lv_color_make(190,  60,  80)  // muted red
#define MP_MOOD_BAD        lv_color_make(255, 140,  30)  // sunset orange
#define MP_MOOD_OKAY       lv_color_make(170, 140, 200)  // soft purple
#define MP_MOOD_GOOD       lv_color_make(122, 232, 255)  // cyan
#define MP_MOOD_GREAT      lv_color_make(255, 220, 100)  // soft yellow

// ---------- geometry -----------------------------------------------------
//
// 160 x 128 layout:
//   y =  0..  9     PhoneStatusBar (10 px)
//   y = 12          caption "MOOD LOG"
//   y = 22          top divider rule
//   y = 26          30-day strip (4 x 8 px cells, 5 px stride, 150 wide)
//   y = 38          cursor day caption "TODAY WED 03 MAY" (pixelbasic7)
//   y = 48..61      cursor swatch (16 x 14 px)
//   y = 51          mood label "GREAT" / "--" right of swatch (pixelbasic16)
//   y = 70          stats line "AVG 3.4/5    LOGGED 21/30" (pixelbasic7)
//   y = 80          streak line "STREAK 5d"
//   y = 92          key hint "1=AWFUL ... 5=GREAT"
//   y =105          bottom divider rule
//   y =118..127     PhoneSoftKeyBar
//
// The 30 cells are pre-allocated at construction. refreshAll() repaints
// colours in place; cursor navigation just moves the cursorRing.

static constexpr lv_coord_t kCaptionY     = 12;
static constexpr lv_coord_t kTopDividerY  = 22;
static constexpr lv_coord_t kStripY       = 26;
static constexpr lv_coord_t kStripCellW   = 4;
static constexpr lv_coord_t kStripCellH   = 8;
static constexpr lv_coord_t kStripStride  = 5;
static constexpr lv_coord_t kStripLeftX   = 5;

static constexpr lv_coord_t kDayY         = 38;
static constexpr lv_coord_t kSwatchX      = 6;
static constexpr lv_coord_t kSwatchY      = 48;
static constexpr lv_coord_t kSwatchW      = 16;
static constexpr lv_coord_t kSwatchH      = 14;
static constexpr lv_coord_t kMoodLabelX   = 28;
static constexpr lv_coord_t kMoodLabelY   = 51;

static constexpr lv_coord_t kStatsY       = 70;
static constexpr lv_coord_t kStreakY      = 80;
static constexpr lv_coord_t kHintY        = 92;
static constexpr lv_coord_t kBotDividerY  = 105;

static constexpr lv_coord_t kRowLeftX     = 4;
static constexpr lv_coord_t kRowWidth     = 152;

// ---------- NVS persistence ----------------------------------------------

namespace {

constexpr const char* kNamespace  = "mpmood";
constexpr const char* kBlobKey    = "m";

constexpr uint8_t  kMagic0   = 'M';
constexpr uint8_t  kMagic1   = 'P';
constexpr uint8_t  kVersion  = 1;

constexpr size_t   kBlobBytes = 8 + (size_t) PhoneMoodLog::HistoryDays;

// Single shared NVS handle, lazy-open. Mirrors PhoneHabits /
// PhoneVirtualPet so we never spam nvs_open() retries when the flash
// partition is unavailable.
nvs_handle s_handle    = 0;
bool       s_attempted = false;

bool ensureOpen() {
	if(s_handle != 0) return true;
	if(s_attempted)   return false;
	s_attempted = true;
	auto err = nvs_open(kNamespace, NVS_READWRITE, &s_handle);
	if(err != ESP_OK) {
		ESP_LOGW("PhoneMoodLog",
		         "nvs_open(%s) failed: %d -- moodlog runs without persistence",
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

lv_color_t moodColor(uint8_t m) {
	switch(m) {
		case 1: return MP_MOOD_AWFUL;
		case 2: return MP_MOOD_BAD;
		case 3: return MP_MOOD_OKAY;
		case 4: return MP_MOOD_GOOD;
		case 5: return MP_MOOD_GREAT;
		default: return MP_DIM;
	}
}

// 0 = SUN .. 6 = SAT, matches PhoneClock's leap-free anchor calendar
// where Thu 2026-01-01 has weekday index 4. Same constants
// PhoneClockFace::updateDate uses, kept inline so we don't take a
// dependency on PhoneClock's private day-of-week helper.
const char* dayName(uint8_t dow) {
	static const char* kDow[7] = {
		"SUN","MON","TUE","WED","THU","FRI","SAT"
	};
	return kDow[dow % 7];
}

const char* monthName(uint8_t m) {
	static const char* kMonths[12] = {
		"JAN","FEB","MAR","APR","MAY","JUN",
		"JUL","AUG","SEP","OCT","NOV","DEC"
	};
	if(m == 0 || m > 12) return "???";
	return kMonths[m - 1];
}

// Synthetic civil-time conversion that matches PhoneClock's leap-free
// calendar: every year is exactly 365 days, every February is 28. We
// only ever use this for caption-formatting historical days, so we
// happily inherit PhoneClock's 1.4-day-per-decade drift in exchange
// for not pulling in <time.h>.
void epochToCivil(uint32_t epoch, uint16_t& year, uint8_t& month, uint8_t& day, uint8_t& dow) {
	constexpr uint32_t kAnchorEpoch   = 1766016000UL; // Thu 2026-01-01 00:00
	constexpr uint8_t  kAnchorWeekday = 4;            // 0=SUN .. 4=THU
	constexpr uint8_t  kMonthDays[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };

	if(epoch < kAnchorEpoch) epoch = kAnchorEpoch;
	uint32_t daysSinceAnchor = (epoch - kAnchorEpoch) / 86400u;
	uint32_t years = daysSinceAnchor / 365u;
	uint32_t doy   = daysSinceAnchor % 365u;
	year = (uint16_t)(2026u + years);
	uint8_t m = 0;
	while(m < 12 && doy >= (uint32_t) kMonthDays[m]) {
		doy -= kMonthDays[m];
		++m;
	}
	month = (uint8_t)(m + 1);
	day   = (uint8_t)(doy + 1);
	dow   = (uint8_t)((kAnchorWeekday + daysSinceAnchor) % 7u);
}

} // namespace

// ---------- public statics -----------------------------------------------

const char* PhoneMoodLog::moodName(Mood m) {
	switch(m) {
		case Mood::Awful: return "AWFUL";
		case Mood::Bad:   return "BAD";
		case Mood::Okay:  return "OKAY";
		case Mood::Good:  return "GOOD";
		case Mood::Great: return "GREAT";
		default:          return "--";
	}
}

uint32_t PhoneMoodLog::todayIndex() {
	return PhoneClock::nowEpoch() / 86400u;
}

// ---------- ctor / dtor --------------------------------------------------

PhoneMoodLog::PhoneMoodLog()
		: LVScreen() {
	for(uint8_t i = 0; i < HistoryDays; ++i) entries[i] = 0;
	lastSyncDay = todayIndex();

	// Pull persisted entries (if any). syncToToday() then shifts the
	// 30-byte history left so "today" lives at entries[HistoryDays-1]
	// before the first refresh paints anything. Empty / failed read
	// degrades to RAM-only.
	load();
	syncToToday();

	cursor = HistoryDays - 1; // start the cursor on today

	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);
	softKeys  = new PhoneSoftKeyBar(obj);

	buildView();

	setButtonHoldTime(BTN_BACK, BackHoldMs);

	refreshAll();
}

PhoneMoodLog::~PhoneMoodLog() {
	// LVGL children parented to obj are freed by the LVScreen base.
}

void PhoneMoodLog::onStart() {
	Input::getInstance()->addListener(this);
	// Re-sync when the screen comes back to the foreground so a long
	// background sit-out still slides the strip into the right slots.
	syncToToday();
	refreshAll();
}

void PhoneMoodLog::onStop() {
	Input::getInstance()->removeListener(this);
}

// ---------- builders -----------------------------------------------------

void PhoneMoodLog::buildView() {
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, kCaptionY);
	lv_label_set_text(captionLabel, "MOOD LOG");

	topDivider = lv_obj_create(obj);
	lv_obj_remove_style_all(topDivider);
	lv_obj_set_size(topDivider, kRowWidth, 1);
	lv_obj_set_style_bg_color(topDivider, MP_DIM, 0);
	lv_obj_set_style_bg_opa(topDivider, LV_OPA_COVER, 0);
	lv_obj_set_pos(topDivider, kRowLeftX, kTopDividerY);

	for(uint8_t i = 0; i < HistoryDays; ++i) {
		lv_obj_t* cell = lv_obj_create(obj);
		lv_obj_remove_style_all(cell);
		lv_obj_set_size(cell, kStripCellW, kStripCellH);
		lv_obj_set_style_bg_color(cell, MP_DIM, 0);
		lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
		lv_obj_set_pos(cell,
		               kStripLeftX + (lv_coord_t)(i * kStripStride),
		               kStripY);
		cells[i] = cell;
	}

	cursorRing = lv_obj_create(obj);
	lv_obj_remove_style_all(cursorRing);
	lv_obj_set_size(cursorRing, kStripCellW + 2, kStripCellH + 2);
	lv_obj_set_style_bg_opa(cursorRing, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_color(cursorRing, MP_HIGHLIGHT, 0);
	lv_obj_set_style_border_width(cursorRing, 1, 0);
	lv_obj_set_style_border_opa(cursorRing, LV_OPA_COVER, 0);
	lv_obj_set_pos(cursorRing,
	               kStripLeftX + (lv_coord_t)(cursor * kStripStride) - 1,
	               kStripY - 1);

	dayLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(dayLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(dayLabel, MP_LABEL_DIM, 0);
	lv_obj_set_pos(dayLabel, kRowLeftX, kDayY);
	lv_label_set_text(dayLabel, "");

	swatch = lv_obj_create(obj);
	lv_obj_remove_style_all(swatch);
	lv_obj_set_size(swatch, kSwatchW, kSwatchH);
	lv_obj_set_style_bg_color(swatch, MP_DIM, 0);
	lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(swatch, MP_DIM, 0);
	lv_obj_set_style_border_width(swatch, 1, 0);
	lv_obj_set_style_border_opa(swatch, LV_OPA_COVER, 0);
	lv_obj_set_pos(swatch, kSwatchX, kSwatchY);

	moodLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(moodLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(moodLabel, MP_TEXT, 0);
	lv_obj_set_pos(moodLabel, kMoodLabelX, kMoodLabelY);
	lv_label_set_text(moodLabel, "--");

	statsLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(statsLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(statsLabel, MP_LABEL_DIM, 0);
	lv_obj_set_pos(statsLabel, kRowLeftX, kStatsY);
	lv_label_set_text(statsLabel, "");

	streakLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(streakLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(streakLabel, MP_LABEL_DIM, 0);
	lv_obj_set_pos(streakLabel, kRowLeftX, kStreakY);
	lv_label_set_text(streakLabel, "");

	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_DIM, 0);
	lv_obj_set_pos(hintLabel, kRowLeftX, kHintY);
	lv_label_set_text(hintLabel, "1=AWFUL 2=BAD 3=OK 4=GOOD 5=GREAT");

	bottomDivider = lv_obj_create(obj);
	lv_obj_remove_style_all(bottomDivider);
	lv_obj_set_size(bottomDivider, kRowWidth, 1);
	lv_obj_set_style_bg_color(bottomDivider, MP_DIM, 0);
	lv_obj_set_style_bg_opa(bottomDivider, LV_OPA_COVER, 0);
	lv_obj_set_pos(bottomDivider, kRowLeftX, kBotDividerY);
}

// ---------- public introspection -----------------------------------------

PhoneMoodLog::Mood PhoneMoodLog::getMoodForDay(uint8_t daysAgo) const {
	if(daysAgo >= HistoryDays) return Mood::None;
	uint8_t v = entries[HistoryDays - 1 - daysAgo];
	if(v > MoodLevels) v = 0;
	return (Mood) v;
}

uint8_t PhoneMoodLog::getLoggedCount() const {
	uint8_t n = 0;
	for(uint8_t i = 0; i < HistoryDays; ++i) {
		if(entries[i] != 0) ++n;
	}
	return n;
}

uint8_t PhoneMoodLog::getStreak() const {
	uint8_t s = 0;
	for(int16_t i = (int16_t) HistoryDays - 1; i >= 0; --i) {
		if(entries[i] != 0) ++s;
		else break;
	}
	return s;
}

uint16_t PhoneMoodLog::getAverageX10() const {
	uint16_t sum = 0;
	uint8_t  n   = 0;
	for(uint8_t i = 0; i < HistoryDays; ++i) {
		if(entries[i] != 0) {
			sum = (uint16_t)(sum + entries[i]);
			++n;
		}
	}
	if(n == 0) return 0;
	return (uint16_t)((sum * 10u) / n);
}

// ---------- repainters ---------------------------------------------------

void PhoneMoodLog::refreshAll() {
	refreshStrip();
	refreshDayCaption();
	refreshSwatch();
	refreshStats();
	refreshSoftKeys();
}

void PhoneMoodLog::refreshStrip() {
	for(uint8_t i = 0; i < HistoryDays; ++i) {
		if(cells[i] == nullptr) continue;
		lv_obj_set_style_bg_color(cells[i], moodColor(entries[i]), 0);
	}
	if(cursorRing != nullptr) {
		lv_obj_set_pos(cursorRing,
		               kStripLeftX + (lv_coord_t)(cursor * kStripStride) - 1,
		               kStripY - 1);
	}
}

void PhoneMoodLog::refreshDayCaption() {
	if(dayLabel == nullptr) return;
	const uint32_t today    = todayIndex();
	const uint32_t daysAgo  = (uint32_t)(HistoryDays - 1 - cursor);
	const uint32_t targetDay   = (today >= daysAgo) ? (today - daysAgo) : 0u;
	const uint32_t targetEpoch = targetDay * 86400u;
	uint16_t y = 0;
	uint8_t  mo = 0, d = 0, dw = 0;
	epochToCivil(targetEpoch, y, mo, d, dw);

	char buf[40];
	if(daysAgo == 0) {
		snprintf(buf, sizeof(buf), "TODAY %s %02u %s",
		         dayName(dw), (unsigned) d, monthName(mo));
	} else if(daysAgo == 1) {
		snprintf(buf, sizeof(buf), "YESTERDAY %s %02u %s",
		         dayName(dw), (unsigned) d, monthName(mo));
	} else {
		snprintf(buf, sizeof(buf), "%s %02u %s   -%ud",
		         dayName(dw), (unsigned) d, monthName(mo),
		         (unsigned) daysAgo);
	}
	lv_label_set_text(dayLabel, buf);
}

void PhoneMoodLog::refreshSwatch() {
	if(swatch == nullptr || moodLabel == nullptr) return;
	const uint8_t v = entries[cursor];
	lv_obj_set_style_bg_color(swatch, moodColor(v), 0);
	lv_obj_set_style_border_color(swatch,
	                              (v == 0) ? MP_DIM : MP_HIGHLIGHT, 0);

	const Mood m = (Mood)((v <= MoodLevels) ? v : 0);
	lv_label_set_text(moodLabel, moodName(m));
	lv_obj_set_style_text_color(moodLabel,
	                            (v == 0) ? MP_LABEL_DIM : MP_TEXT, 0);
}

void PhoneMoodLog::refreshStats() {
	if(statsLabel == nullptr || streakLabel == nullptr) return;

	const uint8_t  logged = getLoggedCount();
	const uint16_t avgx10 = getAverageX10();
	char buf[48];
	if(logged == 0) {
		snprintf(buf, sizeof(buf), "AVG --/5      LOGGED 0/%u",
		         (unsigned) HistoryDays);
	} else {
		snprintf(buf, sizeof(buf), "AVG %u.%u/5    LOGGED %u/%u",
		         (unsigned)(avgx10 / 10),
		         (unsigned)(avgx10 % 10),
		         (unsigned) logged,
		         (unsigned) HistoryDays);
	}
	lv_label_set_text(statsLabel, buf);

	const uint8_t streak = getStreak();
	char sbuf[20];
	snprintf(sbuf, sizeof(sbuf), "STREAK %ud", (unsigned) streak);
	lv_label_set_text(streakLabel, sbuf);
}

void PhoneMoodLog::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	// Left softkey: only shown when the cursor is parked off "today",
	// because the gesture is "TODAY = jump back to today" and a no-op
	// when already there feels noisy.
	softKeys->setLeft((cursor == HistoryDays - 1) ? "" : "TODAY");
	// Right softkey: only meaningful when the cursor day actually has
	// a mood logged. PhoneSoftKeyBar hides empty labels so the unused
	// side stays visually quiet.
	softKeys->setRight((entries[cursor] != 0) ? "CLEAR" : "");
}

// ---------- model helpers ------------------------------------------------

void PhoneMoodLog::setCursorMood(Mood m) {
	uint8_t v = (uint8_t) m;
	if(v > MoodLevels) v = 0;
	entries[cursor] = v;
	save();
	refreshAll();
}

void PhoneMoodLog::clearCursor() {
	if(softKeys) softKeys->flashRight();
	entries[cursor] = 0;
	save();
	refreshAll();
}

void PhoneMoodLog::jumpToToday() {
	if(softKeys) softKeys->flashLeft();
	cursor = HistoryDays - 1;
	refreshAll();
}

void PhoneMoodLog::moveCursor(int8_t delta) {
	if(delta == 0) return;
	int16_t next = (int16_t) cursor + (int16_t) delta;
	if(next < 0)                            next = 0;
	if(next >= (int16_t) HistoryDays)       next = (int16_t)(HistoryDays - 1);
	cursor = (uint8_t) next;
	refreshAll();
}

// ---------- day rollover -------------------------------------------------

void PhoneMoodLog::syncToToday() {
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
		for(uint8_t i = 0; i < HistoryDays; ++i) entries[i] = 0;
	} else {
		const uint8_t shift = (uint8_t) delta;
		for(uint8_t i = 0; i + shift < HistoryDays; ++i) {
			entries[i] = entries[i + shift];
		}
		for(uint8_t i = (uint8_t)(HistoryDays - shift); i < HistoryDays; ++i) {
			entries[i] = 0;
		}
	}
	lastSyncDay = today;
}

// ---------- persistence --------------------------------------------------

void PhoneMoodLog::load() {
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
	for(uint8_t i = 0; i < HistoryDays; ++i) {
		uint8_t v = buf[8 + i];
		if(v > MoodLevels) v = 0;
		entries[i] = v;
	}
}

void PhoneMoodLog::save() {
	if(!ensureOpen()) return;

	uint8_t buf[kBlobBytes] = {};
	buf[0] = kMagic0;
	buf[1] = kMagic1;
	buf[2] = kVersion;
	buf[3] = 0;
	writeU32LE(&buf[4], lastSyncDay);
	for(uint8_t i = 0; i < HistoryDays; ++i) {
		buf[8 + i] = (entries[i] <= MoodLevels) ? entries[i] : 0;
	}

	auto err = nvs_set_blob(s_handle, kBlobKey, buf, kBlobBytes);
	if(err != ESP_OK) {
		ESP_LOGW("PhoneMoodLog", "nvs_set_blob failed: %d", (int)err);
		return;
	}
	err = nvs_commit(s_handle);
	if(err != ESP_OK) {
		ESP_LOGW("PhoneMoodLog", "nvs_commit failed: %d", (int)err);
	}
}

// ---------- input --------------------------------------------------------

void PhoneMoodLog::buttonPressed(uint i) {
	switch(i) {
		case BTN_1: setCursorMood(Mood::Awful); break;
		case BTN_2: setCursorMood(Mood::Bad);   break;
		case BTN_3: setCursorMood(Mood::Okay);  break;
		case BTN_4: setCursorMood(Mood::Good);  break;
		case BTN_5: setCursorMood(Mood::Great); break;

		case BTN_L:
			moveCursor(-1);
			break;
		case BTN_R:
			moveCursor(+1);
			break;

		case BTN_LEFT:
			jumpToToday();
			break;

		case BTN_RIGHT:
			// Only honour CLEAR when the cursor day has something to
			// clear, matching the soft-key visibility rule. Otherwise
			// the press is a no-op so a stray right-press does not
			// silently reshape state.
			if(entries[cursor] != 0) clearCursor();
			break;

		case BTN_ENTER:
			jumpToToday();
			break;

		default:
			break;
	}
}

void PhoneMoodLog::buttonHeld(uint i) {
	if(i == BTN_BACK) {
		backLongFired = true;
		pop();
	}
}

void PhoneMoodLog::buttonReleased(uint i) {
	if(i == BTN_BACK) {
		if(backLongFired) {
			backLongFired = false;
			return;
		}
		pop();
	}
}
