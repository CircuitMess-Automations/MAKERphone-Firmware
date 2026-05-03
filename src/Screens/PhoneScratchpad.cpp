#include "PhoneScratchpad.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <nvs.h>
#include <esp_log.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Elements/PhoneT9Input.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - kept identical to every other Phone* widget so
// the scratchpad slots in beside PhoneNotepad (S64), PhoneTodo (S136),
// PhoneHabits (S137) and PhoneMoodLog (S139) without a visual seam.
// Inlined per the established pattern.
#define MP_BG_DARK         lv_color_make( 20,  12,  36)  // deep purple
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan caption
#define MP_DIM             lv_color_make( 70,  56, 100)  // muted purple
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim cream

// ---------- geometry ------------------------------------------------------
//
// 160 x 128 layout. Single edit-mode view -- there is only one buffer.
//   y =  0..  9   PhoneStatusBar (10 px)
//   y = 12        caption "SCRATCHPAD" (pixelbasic7, ~7 px tall)
//   y = 22        top divider rule (1 px)
//   y = 26..  55  PhoneT9Input slab (Width 156, Height 22 + HelpHeight 8)
//   y = 60        char counter "X of 200 chars" (pixelbasic7)
//   y = 72        dirty marker "* UNSAVED" / "SAVED"
//   y = 105       bottom divider rule (1 px)
//   y = 118..127  PhoneSoftKeyBar (10 px)
//
// The T9 input occupies y=26..55. The 4 px gap below the input + the
// 12 px counter line + the 12 px dirty line + a 9 px gutter to the
// bottom divider keep the layout aerated without any glyph collisions
// at pixelbasic7 (~8 px tall).

static constexpr lv_coord_t kCaptionY      = 12;
static constexpr lv_coord_t kTopDividerY   = 22;
static constexpr lv_coord_t kT9Y           = 26;
static constexpr lv_coord_t kCounterY      = 60;
static constexpr lv_coord_t kDirtyY        = 72;
static constexpr lv_coord_t kBotDividerY   = 105;
static constexpr lv_coord_t kRowLeftX      = 6;
static constexpr lv_coord_t kRowWidth      = 148;

// ---------- NVS persistence ----------------------------------------------

namespace {

constexpr const char* kNamespace = "mpscratch";
constexpr const char* kBlobKey   = "s";

constexpr uint8_t  kMagic0    = 'M';
constexpr uint8_t  kMagic1    = 'P';
constexpr uint8_t  kVersion   = 1;
constexpr size_t   kHeaderLen = 6;   // 2 magic + 1 ver + 1 reserved + 2 length

// Single shared NVS handle, lazy-open. Mirrors PhoneTodo / PhoneHabits /
// PhoneMoodLog so we never spam nvs_open() retries when the partition is
// unavailable.
nvs_handle s_handle    = 0;
bool       s_attempted = false;

bool ensureOpen() {
	if(s_handle != 0) return true;
	if(s_attempted)   return false;
	s_attempted = true;
	auto err = nvs_open(kNamespace, NVS_READWRITE, &s_handle);
	if(err != ESP_OK) {
		ESP_LOGW("PhoneScratchpad",
		         "nvs_open(%s) failed: %d -- scratchpad runs without persistence",
		         kNamespace, (int)err);
		s_handle = 0;
		return false;
	}
	return true;
}

} // namespace

// ---------- ctor / dtor ---------------------------------------------------

PhoneScratchpad::PhoneScratchpad()
		: LVScreen() {

	// Full-screen container, blank canvas - same pattern every Phone*
	// screen uses.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px).
	statusBar = new PhoneStatusBar(obj);

	// Bottom soft-key bar. CLEAR / DONE labels for the single edit
	// view this screen owns.
	softKeys = new PhoneSoftKeyBar(obj);

	buildView();

	// Try to load the previous buffer from NVS into the T9 widget. If
	// that fails (no data yet, or partition unavailable), start blank.
	char loaded[MaxLen + 1] = {};
	uint16_t loadedLen = 0;
	if(loadFromNvs(loaded, sizeof(loaded), loadedLen) && loadedLen > 0) {
		if(t9Input != nullptr) {
			t9Input->setText(String(loaded));
		}
	}
	dirty = false;

	// Long-press threshold matches the rest of the MAKERphone shell so
	// the gesture feels identical from any screen.
	setButtonHoldTime(BTN_BACK, BackHoldMs);

	refreshCaption();
	refreshSoftKeys();
	refreshCharCounter();
	refreshDirty();
}

PhoneScratchpad::~PhoneScratchpad() {
	// PhoneT9Input must be deleted explicitly so its caret + commit
	// timers stop before the lv tree is torn down. The base
	// LVScreen / LVObject destructor takes care of the rest of the
	// lv children parented to `obj`.
	if(t9Input != nullptr) {
		delete t9Input;
		t9Input = nullptr;
	}
}

void PhoneScratchpad::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneScratchpad::onStop() {
	Input::getInstance()->removeListener(this);
}

// ---------- public introspection -----------------------------------------

uint16_t PhoneScratchpad::getLength() const {
	if(t9Input == nullptr) return 0;
	return (uint16_t) t9Input->getText().length();
}

const char* PhoneScratchpad::getText() const {
	if(t9Input == nullptr) return "";
	// PhoneT9Input::getText() returns a String by value. Calling
	// .c_str() on a temporary would be UB, so we cache via a static
	// thread_local buffer mirroring the live text. This accessor is
	// host/test-only -- the hot path uses t9Input->getText() directly.
	static char s_textCache[MaxLen + 1];
	String live = t9Input->getText();
	const size_t copyLen = (live.length() < MaxLen)
			? (size_t) live.length() : (size_t) MaxLen;
	memcpy(s_textCache, live.c_str(), copyLen);
	s_textCache[copyLen] = '\0';
	return s_textCache;
}

// ---------- static helpers -----------------------------------------------

void PhoneScratchpad::trimText(const char* in, char* out, size_t outLen) {
	if(out == nullptr || outLen == 0) return;
	if(in == nullptr) {
		out[0] = '\0';
		return;
	}

	// Skip leading whitespace.
	while(*in != '\0' && isspace((unsigned char) *in)) ++in;

	// Find the trailing whitespace boundary so we can copy without it.
	const char* end = in + strlen(in);
	while(end > in && isspace((unsigned char) *(end - 1))) --end;

	const size_t srcLen = (size_t) (end - in);
	const size_t copyLen = (srcLen < outLen - 1) ? srcLen : (outLen - 1);
	memcpy(out, in, copyLen);
	out[copyLen] = '\0';
}

// ---------- builders -----------------------------------------------------

void PhoneScratchpad::buildView() {
	// Caption -- "SCRATCHPAD". Painted in cyan to match the section
	// captions every other Phone* utility app uses.
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, kCaptionY);
	lv_label_set_text(captionLabel, "SCRATCHPAD");

	// Top divider rule below the caption.
	topDivider = lv_obj_create(obj);
	lv_obj_remove_style_all(topDivider);
	lv_obj_set_size(topDivider, kRowWidth, 1);
	lv_obj_set_style_bg_color(topDivider, MP_DIM, 0);
	lv_obj_set_style_bg_opa(topDivider, LV_OPA_COVER, 0);
	lv_obj_set_pos(topDivider, kRowLeftX, kTopDividerY);

	// PhoneT9Input -- canonical S32 multi-tap entry. Caps at MaxLen.
	t9Input = new PhoneT9Input(obj, MaxLen);
	lv_obj_set_pos(t9Input->getLvObj(),
				   (160 - PhoneT9Input::Width) / 2,
				   kT9Y);
	t9Input->setPlaceholder("JOT A THOUGHT");
	t9Input->setCase(PhoneT9Input::Case::First);

	// Char counter strip -- dim hint line under the T9 input.
	charCounter = lv_label_create(obj);
	lv_obj_set_style_text_font(charCounter, &pixelbasic7, 0);
	lv_obj_set_style_text_color(charCounter, MP_LABEL_DIM, 0);
	lv_label_set_long_mode(charCounter, LV_LABEL_LONG_DOT);
	lv_obj_set_width(charCounter, kRowWidth);
	lv_obj_set_style_text_align(charCounter, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_pos(charCounter, kRowLeftX, kCounterY);
	lv_label_set_text(charCounter, "0 of 200 chars");

	// Dirty marker line. Cream-on-orange-flag when unsaved, dim-cream
	// when the buffer matches disk so the user can tell at a glance.
	dirtyLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(dirtyLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(dirtyLabel, MP_LABEL_DIM, 0);
	lv_label_set_long_mode(dirtyLabel, LV_LABEL_LONG_DOT);
	lv_obj_set_width(dirtyLabel, kRowWidth);
	lv_obj_set_style_text_align(dirtyLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_pos(dirtyLabel, kRowLeftX, kDirtyY);
	lv_label_set_text(dirtyLabel, "SAVED");

	// Bottom divider rule above the soft-keys.
	bottomDivider = lv_obj_create(obj);
	lv_obj_remove_style_all(bottomDivider);
	lv_obj_set_size(bottomDivider, kRowWidth, 1);
	lv_obj_set_style_bg_color(bottomDivider, MP_DIM, 0);
	lv_obj_set_style_bg_opa(bottomDivider, LV_OPA_COVER, 0);
	lv_obj_set_pos(bottomDivider, kRowLeftX, kBotDividerY);

	// Wire the live-update callbacks. We capture `this` so the lambdas
	// can re-paint the counter + dirty marker without going through
	// globals. The T9 widget owns the std::function copy so the
	// pointer stays valid for the widget's lifetime, which equals
	// this screen's lifetime.
	auto self = this;
	t9Input->setOnTextChanged([self](const String& text) {
		(void) text;
		self->dirty = true;
		self->refreshCharCounter();
		self->refreshDirty();
	});
}

// ---------- repainters ---------------------------------------------------

void PhoneScratchpad::refreshCaption() {
	if(captionLabel == nullptr) return;
	lv_label_set_text(captionLabel, "SCRATCHPAD");
}

void PhoneScratchpad::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	softKeys->setLeft("CLEAR");
	softKeys->setRight("DONE");
}

void PhoneScratchpad::refreshCharCounter() {
	if(charCounter == nullptr || t9Input == nullptr) return;
	const String text = t9Input->getText();
	char buf[24];
	snprintf(buf, sizeof(buf), "%u of %u chars",
			 (unsigned) text.length(),
			 (unsigned) MaxLen);
	lv_label_set_text(charCounter, buf);
}

void PhoneScratchpad::refreshDirty() {
	if(dirtyLabel == nullptr) return;
	if(dirty) {
		lv_obj_set_style_text_color(dirtyLabel, MP_ACCENT, 0);
		lv_label_set_text(dirtyLabel, "* UNSAVED");
	}else{
		lv_obj_set_style_text_color(dirtyLabel, MP_LABEL_DIM, 0);
		lv_label_set_text(dirtyLabel, "SAVED");
	}
}

// ---------- persistence --------------------------------------------------

bool PhoneScratchpad::loadFromNvs(char* outBuf, uint16_t outBufLen, uint16_t& lengthOut) {
	lengthOut = 0;
	if(outBuf == nullptr || outBufLen == 0) return false;
	outBuf[0] = '\0';

	if(!ensureOpen()) return false;

	size_t blobSize = 0;
	auto err = nvs_get_blob(s_handle, kBlobKey, nullptr, &blobSize);
	if(err != ESP_OK || blobSize < kHeaderLen || blobSize > kHeaderLen + MaxLen) {
		return false;
	}

	// Bounded staging buffer - we never trust an oversized on-disk
	// blob to fit through outBufLen.
	uint8_t blob[kHeaderLen + MaxLen];
	if(blobSize > sizeof(blob)) return false;
	err = nvs_get_blob(s_handle, kBlobKey, blob, &blobSize);
	if(err != ESP_OK) return false;

	if(blob[0] != kMagic0 || blob[1] != kMagic1 || blob[2] != kVersion) {
		return false;
	}

	const uint16_t storedLen = (uint16_t) blob[4] | ((uint16_t) blob[5] << 8);
	if(storedLen > MaxLen) return false;
	if(blobSize < kHeaderLen + storedLen) return false;

	const uint16_t copyLen = (storedLen < outBufLen - 1)
			? storedLen : (uint16_t) (outBufLen - 1);
	memcpy(outBuf, blob + kHeaderLen, copyLen);
	outBuf[copyLen] = '\0';
	lengthOut = copyLen;
	return true;
}

bool PhoneScratchpad::saveToNvs(const char* text, uint16_t length) {
	if(!ensureOpen()) return false;
	if(length > MaxLen) length = MaxLen;

	uint8_t blob[kHeaderLen + MaxLen];
	blob[0] = kMagic0;
	blob[1] = kMagic1;
	blob[2] = kVersion;
	blob[3] = 0; // reserved
	blob[4] = (uint8_t) ( length        & 0xFF);
	blob[5] = (uint8_t) ((length >>  8) & 0xFF);
	if(length > 0 && text != nullptr) {
		memcpy(blob + kHeaderLen, text, length);
	}

	auto err = nvs_set_blob(s_handle, kBlobKey, blob, kHeaderLen + length);
	if(err != ESP_OK) {
		ESP_LOGW("PhoneScratchpad", "nvs_set_blob failed: %d", (int)err);
		return false;
	}
	err = nvs_commit(s_handle);
	if(err != ESP_OK) {
		ESP_LOGW("PhoneScratchpad", "nvs_commit failed: %d", (int)err);
		return false;
	}
	return true;
}

void PhoneScratchpad::persist() {
	if(t9Input == nullptr) return;

	// Force the T9 widget to commit any in-flight pending letter so a
	// user who taps a digit and immediately exits does not lose their
	// last letter. Same gesture PhoneNotepad / PhoneContactEdit use.
	t9Input->commitPending();

	String live = t9Input->getText();
	const uint16_t len = (uint16_t) ((live.length() < MaxLen)
			? live.length() : MaxLen);

	if(saveToNvs(live.c_str(), len)) {
		dirty = false;
		refreshDirty();
	}
}

void PhoneScratchpad::clearBuffer() {
	if(t9Input != nullptr) {
		t9Input->clear();
	}
	// Even if NVS save fails (partition unavailable), we want the live
	// view to read empty and the dirty flag to be flipped on -- the
	// onTextChanged callback wired in buildView() drives both.
	if(saveToNvs("", 0)) {
		dirty = false;
	}
	refreshCharCounter();
	refreshDirty();
}

// ---------- input actions ------------------------------------------------

void PhoneScratchpad::onClearPressed() {
	if(softKeys) softKeys->flashLeft();
	clearBuffer();
}

void PhoneScratchpad::onDonePressed() {
	if(softKeys) softKeys->flashRight();
	persist();
	pop();
}

// ---------- input --------------------------------------------------------

void PhoneScratchpad::buttonPressed(uint i) {
	// Always-edit mode: route digits + bumpers through the T9 funnel
	// the same way PhoneNotepad / PhoneContactEdit do.
	switch(i) {
		case BTN_0: if(t9Input) t9Input->keyPress('0'); break;
		case BTN_1: if(t9Input) t9Input->keyPress('1'); break;
		case BTN_2: if(t9Input) t9Input->keyPress('2'); break;
		case BTN_3: if(t9Input) t9Input->keyPress('3'); break;
		case BTN_4: if(t9Input) t9Input->keyPress('4'); break;
		case BTN_5: if(t9Input) t9Input->keyPress('5'); break;
		case BTN_6: if(t9Input) t9Input->keyPress('6'); break;
		case BTN_7: if(t9Input) t9Input->keyPress('7'); break;
		case BTN_8: if(t9Input) t9Input->keyPress('8'); break;
		case BTN_9: if(t9Input) t9Input->keyPress('9'); break;

		case BTN_L:
			// Bumper L: T9 backspace -- cancels a pending letter or
			// erases the last committed character.
			if(t9Input) t9Input->keyPress('*');
			break;

		case BTN_R:
			// Bumper R: T9 case toggle (Abc -> ABC -> abc).
			if(t9Input) t9Input->keyPress('#');
			break;

		case BTN_ENTER:
			// Lock in the in-flight pending letter without leaving.
			if(t9Input) t9Input->commitPending();
			break;

		case BTN_LEFT:
			onClearPressed();
			break;

		case BTN_RIGHT:
			// Defer to buttonReleased so a long-press on BACK can
			// pre-empt this on the same hold cycle.
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneScratchpad::buttonHeld(uint i) {
	if(i == BTN_BACK) {
		// Hold-BACK = bail to the parent screen. Auto-save on the way
		// out so an instant-jot session never loses content. Same
		// "flush on leave" convention the docs in the header lay out.
		backLongFired = true;
		if(softKeys) softKeys->flashRight();
		persist();
		pop();
	}
}

void PhoneScratchpad::buttonReleased(uint i) {
	if(i == BTN_BACK) {
		// Short-press BACK: auto-save and exit. Long-press path is
		// suppressed via backLongFired so a hold does not double-fire.
		if(backLongFired) {
			backLongFired = false;
			return;
		}
		if(softKeys) softKeys->flashRight();
		persist();
		pop();
		return;
	}

	if(i == BTN_RIGHT) {
		// Right softkey = DONE. Suppressed if a long-press already
		// exited the screen on the same hold cycle.
		if(backLongFired) {
			backLongFired = false;
			return;
		}
		onDonePressed();
		return;
	}
}
