#include "PhoneHangman.h"

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

// MAKERphone retro palette - identical to every other Phone* widget so
// PhoneHangman sits beside PhoneTetris (S71/72), PhoneTicTacToe (S81),
// PhoneSokoban (S83/84), PhonePinball (S85/86) without a visual seam.
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)  // dim purple captions
#define MP_BAD         lv_color_make(240,  90,  90)  // soft red - lost / overlay accent
#define MP_GOOD        lv_color_make(150, 220, 140)  // soft green - win overlay accent

namespace {

// Canonical ITU-T E.161 keymap (2 .. 9 only - 0 / 1 don't carry letters
// in this game so we skip them). Indexed by digit (0..9) so the array
// matches PhoneT9Input::kKeyLetters layout, but only entries 2..9 carry
// real letters. Lower-case so the pending-letter strip reads like the
// classic SMS composer; submitGuess() uppercases on commit.
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

inline uint8_t lettersInKey(uint8_t digit){
	if(digit > 9) return 0;
	return static_cast<uint8_t>(strlen(kKeyLetters[digit]));
}

// Inline word list. 5-7 letters each, all common English nouns / verbs
// so a feature-phone player has a fair shot. ~50 entries keeps the
// firmware footprint trivial and the same-word-twice-in-a-row chance
// near zero. Letters are uppercase here because that's the format the
// reveal label uses.
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
	"PLANET", "RETRO", "SPARK", "SUNNY", "TURTLE",
	"VAPOR", "WONDER", "BRIDGE", "CASTLE", "DOLPHIN"
};

constexpr uint8_t kWordCount = sizeof(kWords) / sizeof(kWords[0]);

// Convert an uppercase A-Z to 0..25, returns -1 otherwise.
inline int8_t letterIndex(char c){
	if(c >= 'A' && c <= 'Z') return static_cast<int8_t>(c - 'A');
	if(c >= 'a' && c <= 'z') return static_cast<int8_t>(c - 'a');
	return -1;
}

} // namespace

// ===========================================================================
// ctor / dtor
// ===========================================================================

PhoneHangman::PhoneHangman()
		: LVScreen() {

	for(uint8_t i = 0; i < MaxWordLen; ++i) revealed[i] = false;
	for(uint8_t i = 0; i < 26; ++i) guessed[i] = false;
	currentWord[0] = '\0';

	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildHud();
	buildGallows();
	buildFigure();
	buildWordReveal();
	buildPending();
	buildUsedStrip();
	buildOverlay();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("GUESS");
	softKeys->setRight("BACK");

	newRound();
}

PhoneHangman::~PhoneHangman() {
	cancelCommitTimer();
	// All children parented to obj; LVScreen frees them.
}

void PhoneHangman::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneHangman::onStop() {
	Input::getInstance()->removeListener(this);
	cancelCommitTimer();
}

// ===========================================================================
// build helpers
// ===========================================================================

void PhoneHangman::buildHud() {
	hudWinsLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudWinsLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudWinsLabel, MP_GOOD, 0);
	lv_label_set_text(hudWinsLabel, "WIN 00");
	lv_obj_set_pos(hudWinsLabel, 4, HudY + 2);

	hudLossesLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudLossesLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudLossesLabel, MP_BAD, 0);
	lv_label_set_text(hudLossesLabel, "LOST 00");
	lv_obj_set_pos(hudLossesLabel, 44, HudY + 2);

	hudMissesLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(hudMissesLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hudMissesLabel, MP_ACCENT, 0);
	lv_label_set_text(hudMissesLabel, "MISS 0/6");
	lv_obj_set_pos(hudMissesLabel, 116, HudY + 2);
}

void PhoneHangman::buildGallows() {
	// Static beams - always visible. Coordinates relative to the panel
	// origin; reasoned from the GallowsX/Y/W/H constants so future
	// layout tweaks only need to touch the header.
	const lv_coord_t baseY = GallowsY + GallowsH - 4;
	const lv_coord_t postX = GallowsX + 4;
	const lv_coord_t postH = GallowsH - 6;
	const lv_coord_t beamY = GallowsY + 2;
	const lv_coord_t beamW = 28;

	auto mkBeam = [&](lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h){
		auto* p = lv_obj_create(obj);
		lv_obj_remove_style_all(p);
		lv_obj_set_size(p, w, h);
		lv_obj_set_pos(p, x, y);
		lv_obj_set_style_bg_color(p, MP_LABEL_DIM, 0);
		lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
		lv_obj_set_style_radius(p, 0, 0);
		lv_obj_set_style_border_width(p, 0, 0);
		lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(p, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(p, LV_OBJ_FLAG_IGNORE_LAYOUT);
		return p;
	};

	gallowsBase = mkBeam(GallowsX,         baseY,        24, 3);
	gallowsPost = mkBeam(postX,            beamY,        2,  postH);
	gallowsBeam = mkBeam(postX,            beamY,        beamW, 2);
	gallowsRope = mkBeam(postX + beamW - 2, beamY + 2,   1,  6);
}

void PhoneHangman::buildFigure() {
	// Body parts hung from the gallows rope. Coordinates form a stick
	// figure ~24 px tall starting just below the rope. All parts begin
	// hidden -- renderFigure() un-hides them per `wrongCount` tier.
	const lv_coord_t centerX = GallowsX + 4 + 28 - 2;     // post + beamW - 2
	const lv_coord_t headY   = GallowsY + 8;
	const lv_coord_t headD   = 7;                          // 7x7 head
	const lv_coord_t bodyY   = headY + headD + 1;
	const lv_coord_t bodyH   = 12;
	const lv_coord_t armY    = bodyY + 2;
	const lv_coord_t legY    = bodyY + bodyH;

	auto mkPart = [&](lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, lv_color_t c){
		auto* p = lv_obj_create(obj);
		lv_obj_remove_style_all(p);
		lv_obj_set_size(p, w, h);
		lv_obj_set_pos(p, x, y);
		lv_obj_set_style_bg_color(p, c, 0);
		lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
		lv_obj_set_style_radius(p, 0, 0);
		lv_obj_set_style_border_width(p, 0, 0);
		lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(p, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(p, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
		return p;
	};

	figureHead = mkPart(centerX - headD / 2, headY,         headD, headD, MP_TEXT);
	// Head has a tiny "neck" of background - we leave the head as a
	// solid square; LVGL doesn't have a cheap circle primitive and the
	// pixel-art aesthetic keeps the figure on-brand with the rest of
	// the synthwave UI.

	figureBody = mkPart(centerX - 1,         bodyY,         2,     bodyH, MP_TEXT);
	figureArmL = mkPart(centerX - 7,         armY,          6,     1,     MP_TEXT);
	figureArmR = mkPart(centerX + 1,         armY,          6,     1,     MP_TEXT);
	figureLegL = mkPart(centerX - 5,         legY,          5,     1,     MP_TEXT);
	figureLegR = mkPart(centerX + 1,         legY,          5,     1,     MP_TEXT);
}

void PhoneHangman::buildWordReveal() {
	wordLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(wordLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(wordLabel, MP_TEXT, 0);
	lv_obj_set_style_text_align(wordLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_long_mode(wordLabel, LV_LABEL_LONG_CLIP);
	lv_obj_set_width(wordLabel, WordW);
	lv_obj_set_pos(wordLabel, WordX, WordY);
	lv_label_set_text(wordLabel, "");
}

void PhoneHangman::buildPending() {
	// "GUESS:" caption.
	pendingPrompt = lv_label_create(obj);
	lv_obj_set_style_text_font(pendingPrompt, &pixelbasic7, 0);
	lv_obj_set_style_text_color(pendingPrompt, MP_LABEL_DIM, 0);
	lv_label_set_text(pendingPrompt, "GUESS:");
	lv_obj_set_pos(pendingPrompt, WordX, PendingY + 4);

	// Big pending letter, drawn slightly to the right of the caption.
	pendingLetter = lv_label_create(obj);
	lv_obj_set_style_text_font(pendingLetter, &pixelbasic16, 0);
	lv_obj_set_style_text_color(pendingLetter, MP_ACCENT, 0);
	lv_label_set_text(pendingLetter, "?");
	lv_obj_set_pos(pendingLetter, WordX + 30, PendingY - 1);

	// Small "abc" ring with the current letter underlined / bracketed,
	// rendered as plain text. We re-render renderPending() each time
	// the cycle advances.
	pendingRing = lv_label_create(obj);
	lv_obj_set_style_text_font(pendingRing, &pixelbasic7, 0);
	lv_obj_set_style_text_color(pendingRing, MP_DIM, 0);
	lv_label_set_text(pendingRing, "");
	lv_obj_set_pos(pendingRing, WordX + 46, PendingY + 4);
}

void PhoneHangman::buildUsedStrip() {
	usedLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(usedLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(usedLabel, MP_LABEL_DIM, 0);
	lv_label_set_long_mode(usedLabel, LV_LABEL_LONG_CLIP);
	lv_obj_set_width(usedLabel, UsedW);
	lv_obj_set_pos(usedLabel, 4, UsedY);
	lv_label_set_text(usedLabel, "USED:");
}

void PhoneHangman::buildOverlay() {
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

void PhoneHangman::newRound() {
	cancelCommitTimer();
	cancelPending();

	for(uint8_t i = 0; i < 26; ++i) guessed[i] = false;
	for(uint8_t i = 0; i < MaxWordLen; ++i) revealed[i] = false;
	wrongCount = 0;
	state = GameState::Playing;

	pickWord();
	renderAll();
}

void PhoneHangman::pickWord() {
	// random() is seeded by the kernel jitter of the boot path; we take
	// it as-is to avoid touching global RNG seed elsewhere in the firmware.
	const uint8_t idx = static_cast<uint8_t>(rand() % kWordCount);
	const char* w = kWords[idx];
	const size_t len = strlen(w);
	wordLen = static_cast<uint8_t>(len > MaxWordLen ? MaxWordLen : len);
	memset(currentWord, 0, sizeof(currentWord));
	for(uint8_t i = 0; i < wordLen; ++i) {
		const char c = w[i];
		currentWord[i] = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c;
	}
	currentWord[wordLen] = '\0';
}

void PhoneHangman::submitGuess(char letter) {
	if(state != GameState::Playing) return;
	const int8_t li = letterIndex(letter);
	if(li < 0) return;
	if(guessed[li]) return;       // already tried -- silently ignore

	const char upper = static_cast<char>('A' + li);
	guessed[li] = true;

	// Reveal every position holding this letter.
	bool hit = false;
	for(uint8_t i = 0; i < wordLen; ++i) {
		if(currentWord[i] == upper) {
			revealed[i] = true;
			hit = true;
		}
	}
	if(!hit) {
		if(wrongCount < MaxWrong) ++wrongCount;
	}

	afterGuess();
}

void PhoneHangman::afterGuess() {
	// Win check: every position revealed.
	bool allRevealed = true;
	for(uint8_t i = 0; i < wordLen; ++i) {
		if(!revealed[i]) { allRevealed = false; break; }
	}
	if(allRevealed) {
		state = GameState::Won;
		++winsCount;
	} else if(wrongCount >= MaxWrong) {
		state = GameState::Lost;
		++lossesCount;
		// Reveal the full word so the player learns it.
		for(uint8_t i = 0; i < wordLen; ++i) revealed[i] = true;
	}
	renderAll();
}

// ===========================================================================
// T9 multi-tap
// ===========================================================================

bool PhoneHangman::keyHasUnusedLetter(uint8_t digit) const {
	const uint8_t n = lettersInKey(digit);
	for(uint8_t i = 0; i < n; ++i) {
		const char c = kKeyLetters[digit][i];
		const int8_t li = letterIndex(c);
		if(li >= 0 && !guessed[li]) return true;
	}
	return false;
}

int8_t PhoneHangman::findNextUnusedInKey(uint8_t digit, int8_t startIdx, int8_t dir) const {
	const int8_t n = static_cast<int8_t>(lettersInKey(digit));
	if(n <= 0) return -1;
	int8_t i = startIdx;
	for(int8_t step = 0; step < n; ++step) {
		i = static_cast<int8_t>((i + dir + n) % n);
		const char c = kKeyLetters[digit][i];
		const int8_t li = letterIndex(c);
		if(li >= 0 && !guessed[li]) return i;
	}
	return -1;
}

void PhoneHangman::onDigitPress(uint8_t digit) {
	if(state != GameState::Playing) return;
	if(digit < 2 || digit > 9) return;       // only letter-bearing keys

	if(pendingKey == static_cast<int8_t>(digit) && pendingCharIdx >= 0) {
		// Same key while pending -- advance the cycle to the next un-guessed
		// letter under this digit.
		const int8_t next = findNextUnusedInKey(digit, pendingCharIdx, +1);
		if(next < 0) {
			// Every letter under this digit has been guessed; cancel.
			cancelPending();
		} else {
			pendingCharIdx = next;
			renderPending();
			rearmCommitTimer();
		}
		return;
	}

	// Different key (or no pending). Commit any in-flight letter first
	// so we don't drop the player's previous selection.
	if(pendingKey >= 0 && pendingCharIdx >= 0) {
		commitPending();
	}

	if(!keyHasUnusedLetter(digit)) {
		// Nothing to cycle through under this digit -- show a brief
		// dim placeholder by leaving pending idle. The player can try
		// another key.
		cancelPending();
		return;
	}

	pendingKey = static_cast<int8_t>(digit);
	pendingCharIdx = findNextUnusedInKey(digit, /*startIdx*/ -1, +1);
	renderPending();
	rearmCommitTimer();
}

void PhoneHangman::cycleDirection(int8_t dir) {
	if(state != GameState::Playing) return;
	if(pendingKey < 0 || pendingCharIdx < 0) return;
	const int8_t next = findNextUnusedInKey(static_cast<uint8_t>(pendingKey),
	                                        pendingCharIdx, dir > 0 ? +1 : -1);
	if(next < 0) return;
	pendingCharIdx = next;
	renderPending();
	rearmCommitTimer();
}

void PhoneHangman::commitPending() {
	if(pendingKey < 0 || pendingCharIdx < 0) return;
	const char c = kKeyLetters[pendingKey][pendingCharIdx];
	cancelCommitTimer();
	pendingKey = -1;
	pendingCharIdx = -1;
	renderPending();
	submitGuess(c);
}

void PhoneHangman::cancelPending() {
	cancelCommitTimer();
	pendingKey = -1;
	pendingCharIdx = -1;
	renderPending();
}

void PhoneHangman::rearmCommitTimer() {
	cancelCommitTimer();
	commitTimer = lv_timer_create(commitTimerCb, kCommitMs, this);
	lv_timer_set_repeat_count(commitTimer, 1);
}

void PhoneHangman::cancelCommitTimer() {
	if(commitTimer != nullptr) {
		lv_timer_del(commitTimer);
		commitTimer = nullptr;
	}
}

void PhoneHangman::commitTimerCb(lv_timer_t* timer) {
	if(timer == nullptr || timer->user_data == nullptr) return;
	auto* self = static_cast<PhoneHangman*>(timer->user_data);
	// The timer auto-deletes itself after firing (repeat count 1) but
	// LVGL does not zero the pointer for us; null it before commit so
	// commit's cancelCommitTimer() does not double-free.
	self->commitTimer = nullptr;
	self->commitPending();
}

// ===========================================================================
// rendering
// ===========================================================================

void PhoneHangman::renderAll() {
	renderHud();
	renderFigure();
	renderWord();
	renderUsed();
	renderPending();
	renderOverlay();
	renderSoftKeys();
}

void PhoneHangman::renderHud() {
	if(hudWinsLabel) {
		char buf[16];
		snprintf(buf, sizeof(buf), "WIN %02u", static_cast<unsigned>(winsCount % 100));
		lv_label_set_text(hudWinsLabel, buf);
	}
	if(hudLossesLabel) {
		char buf[16];
		snprintf(buf, sizeof(buf), "LOST %02u", static_cast<unsigned>(lossesCount % 100));
		lv_label_set_text(hudLossesLabel, buf);
	}
	if(hudMissesLabel) {
		char buf[16];
		snprintf(buf, sizeof(buf), "MISS %u/%u",
		         static_cast<unsigned>(wrongCount),
		         static_cast<unsigned>(MaxWrong));
		lv_label_set_text(hudMissesLabel, buf);
	}
}

void PhoneHangman::renderFigure() {
	auto setShown = [](lv_obj_t* p, bool shown){
		if(p == nullptr) return;
		if(shown) lv_obj_clear_flag(p, LV_OBJ_FLAG_HIDDEN);
		else      lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
	};
	setShown(figureHead, wrongCount >= 1);
	setShown(figureBody, wrongCount >= 2);
	setShown(figureArmL, wrongCount >= 3);
	setShown(figureArmR, wrongCount >= 4);
	setShown(figureLegL, wrongCount >= 5);
	setShown(figureLegR, wrongCount >= 6);

	// Tint everything red on loss so the player gets a clear "you lost"
	// visual cue without us spawning an extra overlay sprite.
	const lv_color_t partColor = (state == GameState::Lost) ? MP_BAD : MP_TEXT;
	auto retint = [&](lv_obj_t* p){
		if(p == nullptr) return;
		lv_obj_set_style_bg_color(p, partColor, 0);
	};
	retint(figureHead);
	retint(figureBody);
	retint(figureArmL);
	retint(figureArmR);
	retint(figureLegL);
	retint(figureLegR);
}

void PhoneHangman::renderWord() {
	if(wordLabel == nullptr) return;
	if(wordLen == 0) {
		lv_label_set_text(wordLabel, "");
		return;
	}
	// pixelbasic16 is monospaced; we space letters so the underscore
	// gap matches the visible letter slot. Each pair = "X " (letter +
	// space) and the final entry has no trailing space.
	char buf[MaxWordLen * 2 + 4];
	uint8_t pos = 0;
	for(uint8_t i = 0; i < wordLen; ++i) {
		const char raw = currentWord[i];
		const char ch = revealed[i] ? raw : '_';
		buf[pos++] = ch;
		if(i + 1 < wordLen) buf[pos++] = ' ';
	}
	buf[pos] = '\0';
	lv_label_set_text(wordLabel, buf);

	// Tint cyan on win, red on loss, cream while playing.
	lv_color_t c = MP_TEXT;
	if(state == GameState::Won)  c = MP_GOOD;
	if(state == GameState::Lost) c = MP_BAD;
	lv_obj_set_style_text_color(wordLabel, c, 0);
}

void PhoneHangman::renderUsed() {
	if(usedLabel == nullptr) return;
	char buf[80];
	uint8_t pos = 0;
	const char* prefix = "USED:";
	while(*prefix && pos < sizeof(buf) - 1) buf[pos++] = *prefix++;
	bool any = false;
	for(int8_t li = 0; li < 26; ++li) {
		if(!guessed[li]) continue;
		any = true;
		if(pos + 2 >= sizeof(buf)) break;
		buf[pos++] = ' ';
		buf[pos++] = static_cast<char>('A' + li);
	}
	if(!any && pos + 4 < sizeof(buf)) {
		const char* none = " --";
		while(*none && pos < sizeof(buf) - 1) buf[pos++] = *none++;
	}
	buf[pos] = '\0';
	lv_label_set_text(usedLabel, buf);
}

void PhoneHangman::renderPending() {
	if(pendingLetter == nullptr || pendingRing == nullptr) return;

	if(state != GameState::Playing) {
		// Replace prompt with a "press R" hint so the player knows what
		// to do next without a separate overlay button.
		if(pendingPrompt) lv_label_set_text(pendingPrompt, "PRESS R FOR");
		lv_label_set_text(pendingLetter, "NEW");
		lv_obj_set_style_text_color(pendingLetter, MP_HIGHLIGHT, 0);
		lv_label_set_text(pendingRing, "ROUND");
		return;
	}
	if(pendingPrompt) lv_label_set_text(pendingPrompt, "GUESS:");

	if(pendingKey < 0 || pendingCharIdx < 0) {
		lv_label_set_text(pendingLetter, "_");
		lv_obj_set_style_text_color(pendingLetter, MP_DIM, 0);
		lv_label_set_text(pendingRing, "press 2-9");
		return;
	}

	const uint8_t key = static_cast<uint8_t>(pendingKey);
	const uint8_t n   = lettersInKey(key);
	const char chosen = kKeyLetters[key][pendingCharIdx];
	const char up = static_cast<char>(toupper(static_cast<unsigned char>(chosen)));
	char letterBuf[2] = { up, '\0' };
	lv_label_set_text(pendingLetter, letterBuf);
	lv_obj_set_style_text_color(pendingLetter, MP_ACCENT, 0);

	// Build a small ring like "[a]bc" highlighting the active letter.
	char ring[16];
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
	lv_label_set_text(pendingRing, ring);
}

void PhoneHangman::renderOverlay() {
	if(overlayPanel == nullptr || overlayLabel == nullptr) return;
	if(state == GameState::Playing) {
		lv_obj_add_flag(overlayPanel, LV_OBJ_FLAG_HIDDEN);
		return;
	}
	lv_obj_clear_flag(overlayPanel, LV_OBJ_FLAG_HIDDEN);
	char buf[64];
	if(state == GameState::Won) {
		snprintf(buf, sizeof(buf), "YOU WIN!\n%s", currentWord);
		lv_obj_set_style_border_color(overlayPanel, MP_GOOD, 0);
	} else {
		snprintf(buf, sizeof(buf), "GAME OVER\nWORD: %s", currentWord);
		lv_obj_set_style_border_color(overlayPanel, MP_BAD, 0);
	}
	lv_label_set_text(overlayLabel, buf);
	lv_obj_move_foreground(overlayPanel);
}

void PhoneHangman::renderSoftKeys() {
	if(softKeys == nullptr) return;
	if(state == GameState::Playing) {
		softKeys->setLeft("GUESS");
	} else {
		softKeys->setLeft("NEW");
	}
	softKeys->setRight("BACK");
}

// ===========================================================================
// input
// ===========================================================================

void PhoneHangman::buttonPressed(uint i) {
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
				// On the win/loss overlay, ENTER also starts a new round
				// so the player doesn't have to remember the R bumper.
				if(softKeys) softKeys->flashLeft();
				newRound();
				break;
			}
			if(pendingKey >= 0 && pendingCharIdx >= 0) {
				if(softKeys) softKeys->flashLeft();
				commitPending();
			}
			break;

		case BTN_LEFT:
			cycleDirection(-1);
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
