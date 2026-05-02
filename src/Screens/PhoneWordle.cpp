#include "PhoneWordle.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette -- identical to every other Phone* widget so
// PhoneWordle sits beside PhoneHangman (S87), PhoneSudoku (S95), etc.
// without a visual seam.
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)  // dim purple captions
#define MP_BAD         lv_color_make(240,  90,  90)  // soft red - lost overlay
#define MP_GOOD        lv_color_make(120, 200, 110)  // soft green - hit
#define MP_YELLOW      lv_color_make(230, 200,  70)  // mustard yellow - present

namespace {

// Canonical ITU-T E.161 keymap (2 .. 9 only). Indexed by digit so the
// array matches PhoneT9Input::kKeyLetters / PhoneHangman::kKeyLetters.
// Lower-case so the pending-letter strip reads like the SMS composer;
// commitPending() uppercases when stamping the letter into rowBuf.
static const char* kKeyLetters[10] = {
	"",        // 0
	"",        // 1
	"abc",     // 2
	"def",     // 3
	"ghi",     // 4
	"jkl",     // 5
	"mno",     // 6
	"pqrs",    // 7
	"tuv",     // 8
	"wxyz"     // 9
};

inline uint8_t lettersInKey(uint8_t digit) {
	if(digit > 9) return 0;
	return static_cast<uint8_t>(strlen(kKeyLetters[digit]));
}

// Inline word list. All five-letter common English words, uppercase A-Z.
// The list is kept tight (well under 1 KB of RODATA) so a couple of
// rounds in a row almost never repeat. The target is picked uniformly
// at random by pickWord().
static const char* kWords[] = {
	"APPLE", "BREAD", "CHAIR", "DREAM", "EAGLE",
	"FROST", "GIANT", "HONEY", "IGLOO", "JOLLY",
	"KNIFE", "LEMON", "MUSIC", "NIGHT", "OCEAN",
	"PIANO", "QUIET", "RIVER", "SUGAR", "TIGER",
	"UNCLE", "VOICE", "WATER", "YOUTH", "ZEBRA",
	"BRAVE", "CLOUD", "DANCE", "EMBER", "FLAME",
	"GLOBE", "HORSE", "INDEX", "KAYAK", "LIGHT",
	"MAGIC", "NORTH", "OASIS", "PIXEL", "QUEEN",
	"ROBOT", "SMILE", "TRAIN", "ULTRA", "VIVID",
	"WHEEL", "YACHT", "BEACH", "CANDY", "DEPTH",
	"RETRO", "SPARK", "SUNNY", "VAPOR", "BRICK",
	"CLOCK", "DRIVE", "FRESH", "GLARE", "JEWEL",
	"MINER", "PAINT", "QUEST", "SHINE", "TOWER",
	"WORLD", "ZESTY", "AMBER", "BERRY", "CABIN",
	"DIZZY", "ECHOY", "FANCY", "GLOOM", "HOTEL",
	"IVORY", "JOKER", "LATCH", "MERRY", "NOVEL",
	"OLIVE", "PEARL", "RAVEN", "SOLAR", "TONIC",
	"WINDY", "BLAZE", "CRAFT", "DAILY", "FLOAT"
};

constexpr uint8_t kWordCount = sizeof(kWords) / sizeof(kWords[0]);

inline char toUpperAZ(char c) {
	if(c >= 'a' && c <= 'z') return static_cast<char>(c - 32);
	return c;
}

} // namespace

// ===========================================================================
// ctor / dtor
// ===========================================================================

PhoneWordle::PhoneWordle()
		: LVScreen() {

	// Zero the row buffers and per-cell scores (ranges-based init in the
	// header is value-initialised, but we explicitly zero rowBuf so the
	// trailing nulls are guaranteed inside ASAN-style bounds checks).
	for(uint8_t r = 0; r < kRows; ++r) {
		for(uint8_t c = 0; c < kCols; ++c) {
			rowBuf[r][c]   = ' ';
			rowScore[r][c] = CellScore::Empty;
		}
		rowBuf[r][kCols] = '\0';
		rowSubmitted[r] = false;
	}
	target[0] = '\0';

	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildTitle();
	buildGrid();
	buildHint();
	buildOverlay();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("ENTER");
	softKeys->setRight("BACK");

	newRound();
}

PhoneWordle::~PhoneWordle() {
	cancelCommitTimer();
	// All children parented to obj; LVScreen frees them.
}

void PhoneWordle::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneWordle::onStop() {
	Input::getInstance()->removeListener(this);
	cancelCommitTimer();
}

// ===========================================================================
// build helpers
// ===========================================================================

void PhoneWordle::buildTitle() {
	titleLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(titleLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(titleLabel, MP_ACCENT, 0);
	lv_label_set_text(titleLabel, "WORDLE");
	lv_obj_set_pos(titleLabel, 6, TitleY);

	statsLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(statsLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(statsLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(statsLabel, "WIN 00 LOST 00");
	lv_obj_set_pos(statsLabel, 50, TitleY);
}

void PhoneWordle::buildGrid() {
	for(uint8_t r = 0; r < kRows; ++r) {
		for(uint8_t c = 0; c < kCols; ++c) {
			auto* bg = lv_obj_create(obj);
			lv_obj_remove_style_all(bg);
			lv_obj_set_size(bg, CellW, CellH);
			lv_obj_set_pos(bg,
			               GridX + c * (CellW + CellGap),
			               GridY + r * (CellH + CellGap));
			lv_obj_set_style_bg_color(bg, MP_BG_DARK, 0);
			lv_obj_set_style_bg_opa(bg, LV_OPA_70, 0);
			lv_obj_set_style_border_color(bg, MP_DIM, 0);
			lv_obj_set_style_border_width(bg, 1, 0);
			lv_obj_set_style_radius(bg, 0, 0);
			lv_obj_set_style_pad_all(bg, 0, 0);
			lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_clear_flag(bg, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_flag(bg, LV_OBJ_FLAG_IGNORE_LAYOUT);

			auto* lbl = lv_label_create(bg);
			lv_obj_set_style_text_font(lbl, &pixelbasic7, 0);
			lv_obj_set_style_text_color(lbl, MP_TEXT, 0);
			lv_obj_set_align(lbl, LV_ALIGN_CENTER);
			lv_label_set_text(lbl, "");

			cells[r][c].bg    = bg;
			cells[r][c].label = lbl;
		}
	}
}

void PhoneWordle::buildHint() {
	hintLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLabel, MP_LABEL_DIM, 0);
	lv_label_set_long_mode(hintLabel, LV_LABEL_LONG_CLIP);
	lv_obj_set_width(hintLabel, 156);
	lv_obj_set_pos(hintLabel, 2, HintY);
	lv_label_set_text(hintLabel, "PRESS 2-9 TO TYPE");
}

void PhoneWordle::buildOverlay() {
	overlayPanel = lv_obj_create(obj);
	lv_obj_remove_style_all(overlayPanel);
	lv_obj_set_size(overlayPanel, 140, 36);
	lv_obj_set_align(overlayPanel, LV_ALIGN_CENTER);
	lv_obj_set_style_bg_color(overlayPanel, MP_BG_DARK, 0);
	lv_obj_set_style_bg_opa(overlayPanel, LV_OPA_90, 0);
	lv_obj_set_style_border_color(overlayPanel, MP_HIGHLIGHT, 0);
	lv_obj_set_style_border_width(overlayPanel, 1, 0);
	lv_obj_set_style_radius(overlayPanel, 3, 0);
	lv_obj_set_style_pad_all(overlayPanel, 0, 0);
	lv_obj_clear_flag(overlayPanel, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(overlayPanel, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(overlayPanel, LV_OBJ_FLAG_HIDDEN);

	overlayLabel = lv_label_create(overlayPanel);
	lv_obj_set_style_text_font(overlayLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(overlayLabel, MP_TEXT, 0);
	lv_obj_set_style_text_align(overlayLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_align(overlayLabel, LV_ALIGN_CENTER);
	lv_label_set_text(overlayLabel, "");
}

// ===========================================================================
// state transitions
// ===========================================================================

void PhoneWordle::newRound() {
	cancelPending();
	for(uint8_t r = 0; r < kRows; ++r) {
		for(uint8_t c = 0; c < kCols; ++c) {
			rowBuf[r][c]   = ' ';
			rowScore[r][c] = CellScore::Empty;
		}
		rowBuf[r][kCols] = '\0';
		rowSubmitted[r] = false;
	}
	rowIdx = 0;
	colIdx = 0;
	state  = GameState::Playing;
	pickWord();
	renderAll();
}

void PhoneWordle::pickWord() {
	const uint8_t idx = static_cast<uint8_t>(rand() % kWordCount);
	const char* w = kWords[idx];
	for(uint8_t i = 0; i < kCols; ++i) {
		const char c = (w[i] != '\0') ? w[i] : 'A';
		target[i] = toUpperAZ(c);
	}
	target[kCols] = '\0';
}

void PhoneWordle::submitRow() {
	if(state != GameState::Playing) return;
	if(rowIdx >= kRows) return;
	// Need a fully-typed five-letter row to submit. If pending exists,
	// commit it first so the player does not have to ENTER twice.
	if(pendingKey >= 0 && pendingCharIdx >= 0) {
		commitPending();
	}
	// Verify all five cells are letters.
	for(uint8_t c = 0; c < kCols; ++c) {
		const char ch = rowBuf[rowIdx][c];
		if(ch < 'A' || ch > 'Z') {
			// Not enough letters; render a hint via renderHint() and
			// keep the row open.
			renderHint();
			return;
		}
	}

	// Wordle's classic two-pass scoring. First pass: greens. We track a
	// per-letter "remaining" count so duplicates are scored correctly.
	uint8_t remaining[26] = {0};
	for(uint8_t c = 0; c < kCols; ++c) {
		const int idx = target[c] - 'A';
		if(idx >= 0 && idx < 26) ++remaining[idx];
	}
	for(uint8_t c = 0; c < kCols; ++c) {
		if(rowBuf[rowIdx][c] == target[c]) {
			rowScore[rowIdx][c] = CellScore::Hit;
			const int idx = rowBuf[rowIdx][c] - 'A';
			if(idx >= 0 && idx < 26 && remaining[idx] > 0) --remaining[idx];
		}
	}
	// Second pass: yellows and greys.
	for(uint8_t c = 0; c < kCols; ++c) {
		if(rowScore[rowIdx][c] == CellScore::Hit) continue;
		const int idx = rowBuf[rowIdx][c] - 'A';
		if(idx >= 0 && idx < 26 && remaining[idx] > 0) {
			rowScore[rowIdx][c] = CellScore::Present;
			--remaining[idx];
		} else {
			rowScore[rowIdx][c] = CellScore::Miss;
		}
	}
	rowSubmitted[rowIdx] = true;

	// Win check.
	bool win = true;
	for(uint8_t c = 0; c < kCols; ++c) {
		if(rowScore[rowIdx][c] != CellScore::Hit) { win = false; break; }
	}
	if(win) {
		state = GameState::Won;
		++winsCount;
	} else if(rowIdx + 1 >= kRows) {
		state = GameState::Lost;
		++lossesCount;
	} else {
		++rowIdx;
		colIdx = 0;
	}
	afterSubmit();
}

void PhoneWordle::afterSubmit() {
	renderAll();
}

// ===========================================================================
// T9 multi-tap
// ===========================================================================

void PhoneWordle::onDigitPress(uint8_t digit) {
	if(state != GameState::Playing) return;
	if(digit < 2 || digit > 9) return;
	if(rowIdx >= kRows) return;

	if(pendingKey == static_cast<int8_t>(digit) && pendingCharIdx >= 0) {
		// Same digit -> cycle within the key's letter ring at the
		// current column.
		const uint8_t n = lettersInKey(digit);
		if(n == 0) return;
		pendingCharIdx = static_cast<int8_t>((pendingCharIdx + 1) % n);
		renderGrid();
		renderHint();
		rearmCommitTimer();
		return;
	}

	// Different digit (or no pending). Commit any in-flight letter so
	// it advances the cursor, then start a fresh cycle on the new key.
	if(pendingKey >= 0 && pendingCharIdx >= 0) {
		commitPending();
	}
	if(colIdx >= kCols) {
		// Row is already full of committed letters. Player needs to
		// either backspace or press ENTER; refuse to start a new
		// pending letter that would have nowhere to land.
		return;
	}
	pendingKey = static_cast<int8_t>(digit);
	pendingCharIdx = 0;
	renderGrid();
	renderHint();
	rearmCommitTimer();
}

void PhoneWordle::cycleDirection(int8_t dir) {
	if(state != GameState::Playing) return;
	if(pendingKey < 0 || pendingCharIdx < 0) return;
	const uint8_t n = lettersInKey(static_cast<uint8_t>(pendingKey));
	if(n == 0) return;
	pendingCharIdx = static_cast<int8_t>(
		(pendingCharIdx + (dir > 0 ? 1 : -1) + n) % n);
	renderGrid();
	renderHint();
	rearmCommitTimer();
}

void PhoneWordle::commitPending() {
	if(pendingKey < 0 || pendingCharIdx < 0) return;
	if(rowIdx >= kRows || colIdx >= kCols) {
		// Should never happen, but be defensive.
		cancelPending();
		return;
	}
	const char c = kKeyLetters[pendingKey][pendingCharIdx];
	const char up = toUpperAZ(c);
	rowBuf[rowIdx][colIdx] = up;
	rowScore[rowIdx][colIdx] = CellScore::Typed;
	if(colIdx < kCols) ++colIdx;
	cancelCommitTimer();
	pendingKey = -1;
	pendingCharIdx = -1;
	renderGrid();
	renderHint();
}

void PhoneWordle::cancelPending() {
	cancelCommitTimer();
	pendingKey = -1;
	pendingCharIdx = -1;
}

void PhoneWordle::rearmCommitTimer() {
	cancelCommitTimer();
	commitTimer = lv_timer_create(commitTimerCb, kCommitMs, this);
	lv_timer_set_repeat_count(commitTimer, 1);
}

void PhoneWordle::cancelCommitTimer() {
	if(commitTimer != nullptr) {
		lv_timer_del(commitTimer);
		commitTimer = nullptr;
	}
}

void PhoneWordle::commitTimerCb(lv_timer_t* timer) {
	if(timer == nullptr || timer->user_data == nullptr) return;
	auto* self = static_cast<PhoneWordle*>(timer->user_data);
	// Timer auto-deletes after firing (repeat count 1) but LVGL does
	// not zero our pointer; null it before commit so cancelCommitTimer()
	// from inside commitPending() does not double-free.
	self->commitTimer = nullptr;
	self->commitPending();
	// Re-render the hint strip explicitly -- commitPending has already
	// done it, but the player's eyes follow the cursor so it's worth
	// the redundant refresh.
	self->renderHint();
}

void PhoneWordle::backspace() {
	if(state != GameState::Playing) return;
	if(pendingKey >= 0 && pendingCharIdx >= 0) {
		// Cancel pending -- the player typed a letter, then realised
		// they meant to undo before committing.
		cancelPending();
		renderGrid();
		renderHint();
		return;
	}
	if(colIdx == 0) return;     // nothing to delete in this row
	--colIdx;
	rowBuf[rowIdx][colIdx] = ' ';
	rowScore[rowIdx][colIdx] = CellScore::Empty;
	renderGrid();
	renderHint();
}

// ===========================================================================
// rendering
// ===========================================================================

void PhoneWordle::renderAll() {
	renderTitleStats();
	renderGrid();
	renderHint();
	renderOverlay();
	renderSoftKeys();
}

void PhoneWordle::renderTitleStats() {
	if(statsLabel == nullptr) return;
	char buf[24];
	snprintf(buf, sizeof(buf), "WIN %02u LOST %02u",
	         static_cast<unsigned>(winsCount % 100),
	         static_cast<unsigned>(lossesCount % 100));
	lv_label_set_text(statsLabel, buf);
}

void PhoneWordle::renderGrid() {
	for(uint8_t r = 0; r < kRows; ++r) {
		for(uint8_t c = 0; c < kCols; ++c) {
			Cell& cell = cells[r][c];
			if(cell.bg == nullptr || cell.label == nullptr) continue;

			// Decide what character to draw + its colour styling.
			char drawChar = ' ';
			lv_color_t bgColor    = MP_BG_DARK;
			lv_color_t borderCol  = MP_DIM;
			lv_color_t textCol    = MP_TEXT;
			lv_opa_t   bgOpa      = LV_OPA_70;

			const bool isCurrentCell =
				(state == GameState::Playing) &&
				(r == rowIdx) &&
				(c == colIdx) &&
				(pendingKey >= 0 && pendingCharIdx >= 0);

			if(rowSubmitted[r]) {
				// Submitted row: use the score colours.
				drawChar = rowBuf[r][c];
				switch(rowScore[r][c]) {
					case CellScore::Hit:
						bgColor = MP_GOOD; borderCol = MP_GOOD;
						textCol = MP_BG_DARK; bgOpa = LV_OPA_COVER;
						break;
					case CellScore::Present:
						bgColor = MP_YELLOW; borderCol = MP_YELLOW;
						textCol = MP_BG_DARK; bgOpa = LV_OPA_COVER;
						break;
					case CellScore::Miss:
					default:
						bgColor = MP_DIM; borderCol = MP_DIM;
						textCol = MP_LABEL_DIM; bgOpa = LV_OPA_COVER;
						break;
				}
			} else if(isCurrentCell) {
				// Currently being edited: pending letter from the T9 ring.
				drawChar = toUpperAZ(kKeyLetters[pendingKey][pendingCharIdx]);
				bgColor = MP_BG_DARK; borderCol = MP_ACCENT;
				textCol = MP_ACCENT; bgOpa = LV_OPA_70;
			} else if(rowScore[r][c] == CellScore::Typed) {
				// Committed but not yet submitted.
				drawChar = rowBuf[r][c];
				bgColor = MP_BG_DARK; borderCol = MP_HIGHLIGHT;
				textCol = MP_TEXT; bgOpa = LV_OPA_70;
			} else if(state == GameState::Playing && r == rowIdx && c == colIdx) {
				// Empty current cell -- give it the focus border so the
				// player can see where the next letter will land.
				bgColor = MP_BG_DARK; borderCol = MP_ACCENT;
				textCol = MP_TEXT; bgOpa = LV_OPA_70;
			}

			lv_obj_set_style_bg_color(cell.bg, bgColor, 0);
			lv_obj_set_style_bg_opa(cell.bg, bgOpa, 0);
			lv_obj_set_style_border_color(cell.bg, borderCol, 0);

			char buf[2] = { drawChar, '\0' };
			if(drawChar == ' ' || drawChar == '\0') buf[0] = '\0';
			lv_label_set_text(cell.label, buf);
			lv_obj_set_style_text_color(cell.label, textCol, 0);
		}
	}
}

void PhoneWordle::renderHint() {
	if(hintLabel == nullptr) return;

	char buf[40];
	if(state == GameState::Won) {
		snprintf(buf, sizeof(buf), "PRESS R FOR NEW ROUND");
	} else if(state == GameState::Lost) {
		snprintf(buf, sizeof(buf), "WORD: %s  R=NEW", target);
	} else if(pendingKey >= 0 && pendingCharIdx >= 0) {
		const uint8_t key = static_cast<uint8_t>(pendingKey);
		const uint8_t n   = lettersInKey(key);
		// Build a small ring like "[a]bc" highlighting the active letter.
		char ring[12];
		uint8_t pos = 0;
		for(uint8_t i = 0; i < n && pos + 4 < sizeof(ring); ++i) {
			if(static_cast<int8_t>(i) == pendingCharIdx) {
				ring[pos++] = '[';
				ring[pos++] = kKeyLetters[key][i];
				ring[pos++] = ']';
			} else {
				ring[pos++] = kKeyLetters[key][i];
			}
		}
		ring[pos] = '\0';
		const char up = toUpperAZ(kKeyLetters[key][pendingCharIdx]);
		snprintf(buf, sizeof(buf), "TYPING: %c    %s", up, ring);
	} else if(colIdx >= kCols) {
		snprintf(buf, sizeof(buf), "PRESS ENTER TO SUBMIT");
	} else if(colIdx == 0) {
		snprintf(buf, sizeof(buf), "PRESS 2-9 TO TYPE");
	} else {
		snprintf(buf, sizeof(buf), "%u/%u  LEFT=DEL  ENT=NEXT",
		         static_cast<unsigned>(colIdx),
		         static_cast<unsigned>(kCols));
	}
	lv_label_set_text(hintLabel, buf);
}

void PhoneWordle::renderOverlay() {
	if(overlayPanel == nullptr || overlayLabel == nullptr) return;
	if(state == GameState::Playing) {
		lv_obj_add_flag(overlayPanel, LV_OBJ_FLAG_HIDDEN);
		return;
	}
	lv_obj_clear_flag(overlayPanel, LV_OBJ_FLAG_HIDDEN);
	char buf[64];
	if(state == GameState::Won) {
		snprintf(buf, sizeof(buf), "YOU WIN!\n%s", target);
		lv_obj_set_style_border_color(overlayPanel, MP_GOOD, 0);
	} else {
		snprintf(buf, sizeof(buf), "GAME OVER\nWORD: %s", target);
		lv_obj_set_style_border_color(overlayPanel, MP_BAD, 0);
	}
	lv_label_set_text(overlayLabel, buf);
	lv_obj_move_foreground(overlayPanel);
}

void PhoneWordle::renderSoftKeys() {
	if(softKeys == nullptr) return;
	if(state == GameState::Playing) {
		softKeys->setLeft("ENTER");
	} else {
		softKeys->setLeft("NEW");
	}
	softKeys->setRight("BACK");
}

// ===========================================================================
// input
// ===========================================================================

void PhoneWordle::buttonPressed(uint i) {
	switch(i) {
		case BTN_BACK:
			if(softKeys) softKeys->flashRight();
			cancelPending();
			pop();
			break;

		case BTN_R:
			if(softKeys) softKeys->flashLeft();
			newRound();
			break;

		case BTN_ENTER:
			if(state != GameState::Playing) {
				if(softKeys) softKeys->flashLeft();
				newRound();
				break;
			}
			if(pendingKey >= 0 && pendingCharIdx >= 0) {
				// Commit pending letter and advance. Player can then
				// press ENTER again on a full row to submit.
				if(softKeys) softKeys->flashLeft();
				commitPending();
				break;
			}
			if(colIdx >= kCols) {
				if(softKeys) softKeys->flashLeft();
				submitRow();
			}
			break;

		case BTN_LEFT:
		case BTN_L:
			backspace();
			break;

		case BTN_RIGHT:
			cycleDirection(+1);
			break;

		case BTN_2: onDigitPress(2); break;
		case BTN_3: onDigitPress(3); break;
		case BTN_4: onDigitPress(4); break;
		case BTN_5: onDigitPress(5); break;
		case BTN_6: onDigitPress(6); break;
		case BTN_7: onDigitPress(7); break;
		case BTN_8: onDigitPress(8); break;
		case BTN_9: onDigitPress(9); break;

		default:
			break;
	}
}
