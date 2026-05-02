#include "PhoneComposer.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../Services/PhoneComposerStorage.h"
#include "../Services/PhoneComposerPlayback.h"

// MAKERphone retro palette - kept identical to every other Phone* widget so
// the composer slots in beside PhoneCalculator (S60) / PhoneStopwatch (S61) /
// PhoneTimer (S62) / PhoneCalendar (S63) / PhoneNotepad (S64) without a
// visual seam. Inlined per the established pattern.
#define MP_BG_DARK         lv_color_make( 20,  12,  36)  // deep purple
#define MP_ACCENT          lv_color_make(255, 140,  30)  // sunset orange
#define MP_HIGHLIGHT       lv_color_make(122, 232, 255)  // cyan caption
#define MP_DIM             lv_color_make( 70,  56, 100)  // muted purple
#define MP_TEXT            lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM       lv_color_make(170, 140, 200)  // dim cream

// ---------- geometry ------------------------------------------------------
//
// 160 x 128 layout:
//   y =   0..  9   PhoneStatusBar (10 px)
//   y =  12        caption  "COMPOSER  N/64"
//   y =  21        top divider (1 px)
//   y =  24..  37  big preview "C#4 1/4"  (pixelbasic16, ~13 px tall)
//   y =  40        stamp summary (pixelbasic7)
//   y =  49        mid divider (1 px)
//   y =  52..  91  ribbon rows (5 rows of stride 8, ~40 px)
//   y =  94        bottom divider (1 px)
//   y =  98..1 04  hint line 1 (pixelbasic7)
//   y = 107..113   hint line 2 (pixelbasic7)
//   y = 118..127   PhoneSoftKeyBar (10 px)

static constexpr lv_coord_t kCaptionY      = 12;
static constexpr lv_coord_t kTopDividerY   = 21;
static constexpr lv_coord_t kPreviewY      = 24;
static constexpr lv_coord_t kStampY        = 40;
static constexpr lv_coord_t kMidDividerY   = 49;
static constexpr lv_coord_t kRibbonTopY    = 52;
static constexpr lv_coord_t kRibbonStride  = 8;
static constexpr lv_coord_t kBotDividerY   = 94;
static constexpr lv_coord_t kHint1Y        = 98;
static constexpr lv_coord_t kHint2Y        = 107;

static constexpr lv_coord_t kRowLeftX      = 6;
static constexpr lv_coord_t kRowWidth      = 148;

static constexpr lv_coord_t kDividerX      = 4;
static constexpr lv_coord_t kDividerW      = 152;

// RTTTL durations cycled by BTN_0.
static const uint8_t kLengthOptions[PhoneComposer::LengthCount] = {
		1, 2, 4, 8, 16, 32
};

// Tone -> uppercase canonical character. Only used internally; the public
// labelForTone() wraps this so a caller can also accept 'P' (rest).
static bool isValidTone(char c) {
	return c == 'C' || c == 'D' || c == 'E' || c == 'F' ||
	       c == 'G' || c == 'A' || c == 'B' || c == 'P';
}

// ---------- ctor / dtor ---------------------------------------------------

PhoneComposer::PhoneComposer()
		: LVScreen() {

	// Zero the buffer up front so an early refresh sees stable empty
	// rows. The first ribbon paint will draw "(empty)" for every slot
	// past noteCount.
	for(uint8_t i = 0; i < MaxNotes; ++i) {
		buffer[i].tone   = 'P';
		buffer[i].sharp  = false;
		buffer[i].octave = OctaveDef;
		buffer[i].length = 4;
		buffer[i].dotted = false;
	}

	// Full-screen container, blank canvas - same pattern every Phone*
	// screen uses.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px).
	statusBar = new PhoneStatusBar(obj);

	buildHeader();
	buildPreview();
	buildRibbon();
	buildHints();

	// Bottom soft-key bar. "CLR" on the left wipes the buffer; "BACK"
	// on the right is dirty-aware -- it deletes the cursor's note when
	// the buffer is non-empty and pops the screen otherwise.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->set("CLR", "BACK");

	// Long-press detection on BTN_BACK so a hold pops the screen even
	// when the buffer is non-empty (a short press otherwise deletes
	// the cursor's note rather than exiting). 600 ms threshold matches
	// the rest of the MAKERphone shell so the gesture feels identical
	// from any screen.
	setButtonHoldTime(BTN_BACK, BackHoldMs);

	// S123 -- long-press gestures wired alongside the short-press
	// behaviour the screen already had. The press handler stamps the
	// "*LongFired" flag so the matching released-handler can suppress
	// the short-press fall-through, mirroring the BTN_BACK pattern.
	//   BTN_9        : preview play / stop
	//   BTN_0        : save current buffer to active slot
	//   BTN_ENTER    : load active slot into buffer
	//   BTN_LEFT     : cycle active slot (0..SaveSlotCount-1)
	setButtonHoldTime(BTN_9,     SlotHoldMs);
	setButtonHoldTime(BTN_0,     SlotHoldMs);
	setButtonHoldTime(BTN_ENTER, SlotHoldMs);
	setButtonHoldTime(BTN_LEFT,  SlotHoldMs);

	// Lazily warm up the NVS handle so the first save / load doesn't
	// pay the open cost on the input path. Idempotent if already open.
	PhoneComposerStorage::begin();

	// Initial paint -- everything renders against an empty buffer so the
	// preview reads "REST 1/4 o4" and every ribbon row reads "(empty)".
	refreshCaption();
	refreshPreview();
	refreshStamp();
	refreshRibbon();
	refreshSoftKeys();
	refreshHints();
}

PhoneComposer::~PhoneComposer() {
	// All children (wallpaper, status bar, soft-keys, labels) are
	// parented to obj and freed by the LVScreen base destructor.
}

void PhoneComposer::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneComposer::onStop() {
	Input::getInstance()->removeListener(this);
	// Hush any in-flight preview so the buzzer does not bleed past
	// the screen's lifetime. Safe to call when nothing is playing.
	PhoneComposerPlayback::stop();
}

// ---------- builders ------------------------------------------------------

void PhoneComposer::buildHeader() {
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_style_text_align(captionLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(captionLabel, kRowWidth);
	lv_label_set_text(captionLabel, "COMPOSER  0/64");
	lv_obj_set_pos(captionLabel, kRowLeftX, kCaptionY);

	topDivider = lv_obj_create(obj);
	lv_obj_remove_style_all(topDivider);
	lv_obj_clear_flag(topDivider, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(topDivider, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(topDivider, kDividerW, 1);
	lv_obj_set_pos(topDivider, kDividerX, kTopDividerY);
	lv_obj_set_style_bg_color(topDivider, MP_DIM, 0);
	lv_obj_set_style_bg_opa(topDivider, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(topDivider, 0, 0);
}

void PhoneComposer::buildPreview() {
	previewLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(previewLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(previewLabel, MP_TEXT, 0);
	lv_obj_set_style_text_align(previewLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(previewLabel, kRowWidth);
	lv_label_set_long_mode(previewLabel, LV_LABEL_LONG_DOT);
	lv_label_set_text(previewLabel, "REST  1/4");
	lv_obj_set_pos(previewLabel, kRowLeftX, kPreviewY);

	stampLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(stampLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(stampLabel, MP_LABEL_DIM, 0);
	lv_obj_set_style_text_align(stampLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(stampLabel, kRowWidth);
	lv_label_set_text(stampLabel, "OCT 4   L=1/4");
	lv_obj_set_pos(stampLabel, kRowLeftX, kStampY);

	midDivider = lv_obj_create(obj);
	lv_obj_remove_style_all(midDivider);
	lv_obj_clear_flag(midDivider, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(midDivider, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(midDivider, kDividerW, 1);
	lv_obj_set_pos(midDivider, kDividerX, kMidDividerY);
	lv_obj_set_style_bg_color(midDivider, MP_DIM, 0);
	lv_obj_set_style_bg_opa(midDivider, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(midDivider, 0, 0);
}

void PhoneComposer::buildRibbon() {
	for(uint8_t i = 0; i < RibbonRows; ++i) {
		lv_obj_t* row = lv_label_create(obj);
		lv_obj_set_style_text_font(row, &pixelbasic7, 0);
		lv_obj_set_style_text_color(row, MP_LABEL_DIM, 0);
		lv_obj_set_style_text_align(row, LV_TEXT_ALIGN_LEFT, 0);
		lv_obj_set_width(row, kRowWidth);
		lv_label_set_long_mode(row, LV_LABEL_LONG_DOT);
		lv_label_set_text(row, "");
		lv_obj_set_pos(row, kRowLeftX, kRibbonTopY + i * kRibbonStride);
		ribbonLabels[i] = row;
	}

	botDivider = lv_obj_create(obj);
	lv_obj_remove_style_all(botDivider);
	lv_obj_clear_flag(botDivider, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(botDivider, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(botDivider, kDividerW, 1);
	lv_obj_set_pos(botDivider, kDividerX, kBotDividerY);
	lv_obj_set_style_bg_color(botDivider, MP_DIM, 0);
	lv_obj_set_style_bg_opa(botDivider, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(botDivider, 0, 0);
}

void PhoneComposer::buildHints() {
	// Hint line 1 keeps the short-press summary the user already
	// learned from S121.
	hintLine1 = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLine1, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLine1, MP_LABEL_DIM, 0);
	lv_obj_set_style_text_align(hintLine1, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(hintLine1, kRowWidth);
	lv_label_set_text(hintLine1, "1=REST  2-8=NOTES  9=DUP");
	lv_obj_set_pos(hintLine1, kRowLeftX, kHint1Y);

	// Hint line 2 is rewritten by S123 -- it doubles as a status
	// indicator for the active slot AND a discoverability cue for the
	// long-press save/load/play gestures. The literal text is filled
	// in by refreshHints() so it can react to slot changes.
	hintLine2 = lv_label_create(obj);
	lv_obj_set_style_text_font(hintLine2, &pixelbasic7, 0);
	lv_obj_set_style_text_color(hintLine2, MP_LABEL_DIM, 0);
	lv_obj_set_style_text_align(hintLine2, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(hintLine2, kRowWidth);
	lv_label_set_text(hintLine2, "");
	lv_obj_set_pos(hintLine2, kRowLeftX, kHint2Y);
}

// ---------- public API ----------------------------------------------------

const PhoneComposer::Note& PhoneComposer::noteAt(uint8_t i) const {
	// Out-of-range returns a stable reference to the last buffer slot
	// rather than UB; this matches PhoneNotepad::getNoteText's
	// "always safe" contract.
	if(i >= MaxNotes) i = MaxNotes - 1;
	return buffer[i];
}

bool PhoneComposer::appendNote(const Note& n) {
	if(noteCount >= MaxNotes) return false;
	if(!isValidTone(n.tone)) return false;
	buffer[noteCount] = n;
	noteCount++;
	cursor = (uint8_t)(noteCount - 1);
	refreshCaption();
	refreshRibbon();
	refreshPreview();
	refreshSoftKeys();
	return true;
}

bool PhoneComposer::insertNoteAt(uint8_t i, const Note& n) {
	if(noteCount >= MaxNotes) return false;
	if(!isValidTone(n.tone)) return false;
	if(i > noteCount) i = noteCount;
	for(int8_t k = (int8_t)noteCount; k > (int8_t)i; --k) {
		buffer[k] = buffer[k - 1];
	}
	buffer[i] = n;
	noteCount++;
	cursor = i;
	refreshCaption();
	refreshRibbon();
	refreshPreview();
	refreshSoftKeys();
	return true;
}

bool PhoneComposer::deleteNoteAt(uint8_t i) {
	if(noteCount == 0) return false;
	if(i >= noteCount) return false;
	for(uint8_t k = i; k < (uint8_t)(noteCount - 1); ++k) {
		buffer[k] = buffer[k + 1];
	}
	noteCount--;
	if(cursor >= noteCount) {
		cursor = (noteCount == 0) ? 0 : (uint8_t)(noteCount - 1);
	}
	refreshCaption();
	refreshRibbon();
	refreshPreview();
	refreshSoftKeys();
	return true;
}

void PhoneComposer::clearAll() {
	noteCount = 0;
	cursor = 0;
	refreshCaption();
	refreshRibbon();
	refreshPreview();
	refreshSoftKeys();
}

char PhoneComposer::labelForTone(char tone) {
	switch(tone) {
		case 'C': case 'D': case 'E': case 'F':
		case 'G': case 'A': case 'B':
			return tone;
		case 'P':
			return '-';
		default:
			return '?';
	}
}

void PhoneComposer::formatNote(const Note& n, char* out, size_t outLen) {
	if(out == nullptr || outLen == 0) return;
	if(n.tone == 'P') {
		// "REST 1/4" -- octave is meaningless for a rest. Add the dot
		// suffix so a dotted-rest still round-trips through the
		// preview consistently.
		snprintf(out, outLen, n.dotted ? "REST 1/%u." : "REST 1/%u",
				 (unsigned)n.length);
		return;
	}
	const char tone = labelForTone(n.tone);
	const char* sharpStr = n.sharp ? "#" : "";
	if(n.dotted) {
		snprintf(out, outLen, "%c%s%u  1/%u.",
				 tone, sharpStr, (unsigned)n.octave, (unsigned)n.length);
	} else {
		snprintf(out, outLen, "%c%s%u  1/%u",
				 tone, sharpStr, (unsigned)n.octave, (unsigned)n.length);
	}
}

// ---------- repainters ----------------------------------------------------

void PhoneComposer::refreshCaption() {
	if(captionLabel == nullptr) return;
	char buf[32] = {};
	// S123: caption now shows the active save slot index in addition
	// to the buffer fill ratio. The "S0" prefix matches the storage
	// layer's slot-key naming so a power-user reading the NVS dump
	// recognises the binding immediately.
	snprintf(buf, sizeof(buf), "COMPOSER  %u/%u  S%u",
			 (unsigned)noteCount, (unsigned)MaxNotes,
			 (unsigned)activeSlot);
	lv_label_set_text(captionLabel, buf);
}

void PhoneComposer::refreshPreview() {
	if(previewLabel == nullptr) return;
	if(noteCount == 0) {
		lv_label_set_text(previewLabel, "( empty )");
		lv_obj_set_style_text_color(previewLabel, MP_LABEL_DIM, 0);
		return;
	}
	const Note& n = buffer[(cursor < noteCount) ? cursor : 0];
	char buf[24] = {};
	formatNote(n, buf, sizeof(buf));
	lv_label_set_text(previewLabel, buf);
	lv_obj_set_style_text_color(previewLabel,
			n.tone == 'P' ? MP_LABEL_DIM : MP_TEXT, 0);
}

void PhoneComposer::refreshStamp() {
	if(stampLabel == nullptr) return;
	char buf[40] = {};
	const char* sharpStr  = stampSharp  ? "#"  : "";
	const char* dottedStr = stampDotted ? "."  : "";
	snprintf(buf, sizeof(buf), "OCT %u%s   L=1/%u%s",
			 (unsigned)stampOctave, sharpStr,
			 (unsigned)stampLength, dottedStr);
	lv_label_set_text(stampLabel, buf);
}

void PhoneComposer::refreshRibbon() {
	if(ribbonLabels[0] == nullptr) return;

	// Window the cursor into a centred page when possible. When the
	// buffer is shorter than RibbonRows we just paint everything from
	// the top and pad with "(empty)".
	int8_t firstIdx;
	if(noteCount <= RibbonRows) {
		firstIdx = 0;
	} else {
		const int8_t centre = (int8_t)(RibbonRows / 2);
		firstIdx = (int8_t)cursor - centre;
		const int8_t maxFirst = (int8_t)(noteCount - RibbonRows);
		if(firstIdx < 0) firstIdx = 0;
		if(firstIdx > maxFirst) firstIdx = maxFirst;
	}

	for(uint8_t row = 0; row < RibbonRows; ++row) {
		const int16_t idx = (int16_t)firstIdx + row;
		lv_obj_t* lab = ribbonLabels[row];
		if(idx < (int16_t)noteCount) {
			const Note& n = buffer[idx];
			char body[24] = {};
			toneRowLabel(n, body, sizeof(body));
			char buf[40] = {};
			const bool isCursor = ((uint8_t)idx == cursor);
			if(isCursor) {
				snprintf(buf, sizeof(buf), "> %02u  %s <",
						 (unsigned)(idx + 1), body);
				lv_obj_set_style_text_color(lab, MP_HIGHLIGHT, 0);
			} else {
				snprintf(buf, sizeof(buf), "  %02u  %s",
						 (unsigned)(idx + 1), body);
				lv_obj_set_style_text_color(lab, MP_TEXT, 0);
			}
			lv_label_set_text(lab, buf);
		} else {
			// Empty pad row -- show a faint hint so the user can see
			// the buffer's tail and the maximum capacity at a glance.
			lv_obj_set_style_text_color(lab, MP_LABEL_DIM, 0);
			lv_label_set_text(lab, "  --  ( empty )");
		}
	}
}

void PhoneComposer::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	// LEFT softkey is always "CLR" -- regardless of buffer state, so
	// the label stays predictable while the user is typing.
	// RIGHT softkey is "DEL" while there is a cursor row to delete and
	// "BACK" once the buffer is empty (so a short press exits cleanly
	// instead of trying to delete a non-existent note). Same dirty-
	// aware pattern PhoneCalculator's right softkey uses.
	softKeys->set("CLR", noteCount > 0 ? "DEL" : "BACK");
}

// ---------- key actions ---------------------------------------------------

void PhoneComposer::onToneKey(char tone) {
	if(!isValidTone(tone)) return;
	if(noteCount >= MaxNotes) {
		if(softKeys) softKeys->flashLeft();
		return;
	}
	Note n;
	n.tone   = tone;
	n.sharp  = (tone == 'P') ? false : stampSharp;
	n.octave = stampOctave;
	n.length = stampLength;
	n.dotted = stampDotted;

	// New note is appended at the END of the buffer. The cursor follows
	// the new note so subsequent edits operate on it. This matches the
	// Sony-Ericsson Composer's "type-and-grow" behaviour rather than
	// inserting in the middle (BTN_ENTER explicitly inserts a copy).
	buffer[noteCount] = n;
	noteCount++;
	cursor = (uint8_t)(noteCount - 1);

	refreshCaption();
	refreshRibbon();
	refreshPreview();
	refreshSoftKeys();
}

void PhoneComposer::onDuplicateKey() {
	if(noteCount == 0) {
		if(softKeys) softKeys->flashLeft();
		return;
	}
	if(noteCount >= MaxNotes) {
		if(softKeys) softKeys->flashLeft();
		return;
	}
	const Note& src = buffer[noteCount - 1];
	buffer[noteCount] = src;
	noteCount++;
	cursor = (uint8_t)(noteCount - 1);
	refreshCaption();
	refreshRibbon();
	refreshPreview();
	refreshSoftKeys();
}

void PhoneComposer::onCycleLength() {
	stampLength = advanceLength(stampLength);
	refreshStamp();
}

void PhoneComposer::onOctaveDelta(int8_t delta) {
	int16_t next = (int16_t)stampOctave + delta;
	if(next < (int16_t)OctaveMin) next = OctaveMin;
	if(next > (int16_t)OctaveMax) next = OctaveMax;
	stampOctave = (uint8_t)next;
	refreshStamp();
}

void PhoneComposer::onInsertCopy() {
	if(noteCount == 0) {
		// Nothing to insert -- seed a default rest so the user has
		// something to chew on rather than getting silent feedback.
		Note n;
		n.tone   = 'P';
		n.sharp  = false;
		n.octave = stampOctave;
		n.length = stampLength;
		n.dotted = stampDotted;
		appendNote(n);
		return;
	}
	if(noteCount >= MaxNotes) {
		if(softKeys) softKeys->flashLeft();
		return;
	}
	const Note src = buffer[cursor];
	insertNoteAt(cursor, src);
}

void PhoneComposer::onDeleteCursor() {
	if(noteCount == 0) {
		// Empty buffer -- the right softkey behaves like "BACK" and
		// pops the screen. The caller (buttonReleased) routes through
		// here for both BTN_BACK short and BTN_RIGHT short presses.
		if(softKeys) softKeys->flashRight();
		pop();
		return;
	}
	deleteNoteAt(cursor);
}

// ---------- helpers -------------------------------------------------------

uint8_t PhoneComposer::lengthIndexOf(uint8_t length) {
	for(uint8_t i = 0; i < LengthCount; ++i) {
		if(kLengthOptions[i] == length) return i;
	}
	return 2; // default to "4" if a stray value sneaks in
}

uint8_t PhoneComposer::advanceLength(uint8_t length) {
	const uint8_t idx = lengthIndexOf(length);
	return kLengthOptions[(idx + 1) % LengthCount];
}

void PhoneComposer::toneRowLabel(const Note& n, char* out, size_t outLen) const {
	if(out == nullptr || outLen == 0) return;
	if(n.tone == 'P') {
		snprintf(out, outLen, n.dotted ? "-     1/%u." : "-     1/%u",
				 (unsigned)n.length);
		return;
	}
	const char tone = labelForTone(n.tone);
	const char* sharpStr = n.sharp ? "#" : " ";
	if(n.dotted) {
		snprintf(out, outLen, "%c%s%u   1/%u.",
				 tone, sharpStr, (unsigned)n.octave, (unsigned)n.length);
	} else {
		snprintf(out, outLen, "%c%s%u   1/%u",
				 tone, sharpStr, (unsigned)n.octave, (unsigned)n.length);
	}
}

// ---------- input ---------------------------------------------------------

void PhoneComposer::buttonPressed(uint i) {
	switch(i) {
		case BTN_1: onToneKey('P'); break;     // rest
		case BTN_2: onToneKey('C'); break;
		case BTN_3: onToneKey('D'); break;
		case BTN_4: onToneKey('E'); break;
		case BTN_5: onToneKey('F'); break;
		case BTN_6: onToneKey('G'); break;
		case BTN_7: onToneKey('A'); break;
		case BTN_8: onToneKey('B'); break;

		// S123: BTN_9 / BTN_0 / BTN_ENTER / BTN_LEFT short-press
		// behaviour is unchanged, but the long-press handlers now
		// fire too. The "*LongFired" flags are reset on press and
		// the matching released-handler suppresses the short-press
		// fall-through when a hold has already triggered.
		case BTN_9:
			dupLongFired   = false;
			break;
		case BTN_0:
			cycleLongFired = false;
			break;
		case BTN_ENTER:
			enterLongFired = false;
			break;

		case BTN_L: onOctaveDelta(-1); break;
		case BTN_R: onOctaveDelta(+1); break;

		case BTN_LEFT:
			// "CLR" / cycle-slot split: clearing the buffer is a
			// destructive operation, so we wait until release to
			// commit. A long-press cycles the active slot and
			// suppresses the CLR fall-through.
			leftLongFired = false;
			break;

		case BTN_RIGHT:
			// Defer to buttonReleased so a long-press can pre-empt the
			// short-press behaviour (delete vs pop). Same pattern
			// PhoneNotepad uses for its softkey-RIGHT split.
			backLongFired = false;
			break;

		default:
			break;
	}
}

void PhoneComposer::buttonHeld(uint i) {
	if(i == BTN_BACK) {
		// Hold-BACK = bail to the parent screen, regardless of buffer
		// state. Same convention every Phase-D / Phase-F / Phase-L
		// screen uses, so muscle memory transfers cleanly.
		backLongFired = true;
		if(softKeys) softKeys->flashRight();
		pop();
		return;
	}

	// S123 long-press gestures.
	switch(i) {
		case BTN_9:
			// Hold-9: toggle preview playback. Stays inside the
			// screen so the user can keep editing while a loop runs.
			dupLongFired = true;
			togglePreview();
			break;
		case BTN_0:
			// Hold-0: save the current buffer to the active slot.
			cycleLongFired = true;
			(void)saveToActiveSlot();
			break;
		case BTN_ENTER:
			// Hold-A: load the active slot into the buffer. If the
			// slot is empty the load is a no-op and the screen just
			// flashes a hint update so the user knows nothing was
			// loaded.
			enterLongFired = true;
			(void)loadFromActiveSlot();
			break;
		case BTN_LEFT: {
			// Hold-LEFT: cycle the active slot (0..SaveSlotCount-1).
			// Suppresses the CLR fall-through that would have fired
			// on a short press of the same key.
			leftLongFired = true;
			activeSlot = (uint8_t)((activeSlot + 1) % SaveSlotCount);
			if(softKeys) softKeys->flashLeft();
			refreshCaption();
			refreshHints();
			break;
		}
		default:
			break;
	}
}

void PhoneComposer::buttonReleased(uint i) {
	if(i == BTN_BACK) {
		// Short-press BACK: delete the cursor's note when the buffer
		// is non-empty, otherwise pop. The long-press path is
		// suppressed via backLongFired so a hold does not double-fire
		// on release.
		if(backLongFired) {
			backLongFired = false;
			return;
		}
		if(softKeys) softKeys->flashRight();
		onDeleteCursor();
		return;
	}
	if(i == BTN_RIGHT) {
		// Softkey RIGHT mirrors the BACK short-press semantics so the
		// user has two keys for the same action.
		if(softKeys) softKeys->flashRight();
		onDeleteCursor();
		return;
	}

	// S123 short-press fall-throughs. Each branch checks the matching
	// "*LongFired" flag so a hold doesn't double-fire its short-press
	// behaviour on release.
	if(i == BTN_9) {
		if(dupLongFired) {
			dupLongFired = false;
			return;
		}
		onDuplicateKey();
		return;
	}
	if(i == BTN_0) {
		if(cycleLongFired) {
			cycleLongFired = false;
			return;
		}
		onCycleLength();
		return;
	}
	if(i == BTN_ENTER) {
		if(enterLongFired) {
			enterLongFired = false;
			return;
		}
		onInsertCopy();
		return;
	}
	if(i == BTN_LEFT) {
		if(leftLongFired) {
			leftLongFired = false;
			return;
		}
		// Short-press: keep the legacy "CLR" wipe.
		if(softKeys) softKeys->flashLeft();
		clearAll();
		return;
	}
}


// =====================================================================
// S123 — slot persistence + Ringtone preview wiring.
// =====================================================================

void PhoneComposer::refreshHints() {
	if(hintLine2 == nullptr) return;

	// Tell the user what the long-press gestures do AND surface the
	// active slot's current state. The line is intentionally compact
	// (4 tokens, ~28 chars) so it fits at 7-pixel font on the 160 px
	// screen.
	const bool occupied = PhoneComposerStorage::hasSlot(activeSlot);
	const bool playing  = isPreviewing();

	char buf[40] = {};
	// 21-ish glyphs at pixelbasic7 6 px stride keeps us inside the
	// 148 px row width with margin to spare. The "*" suffix on the
	// slot tag flags whether the slot already has saved data.
	snprintf(buf, sizeof(buf),
			 "S%u%s 9=%s 0=SV A=LD",
			 (unsigned)activeSlot,
			 occupied ? "*" : " ",
			 playing  ? "STP" : "PLY");
	lv_label_set_text(hintLine2, buf);
}

bool PhoneComposer::isPreviewing() const {
	return PhoneComposerPlayback::isPlaying();
}

void PhoneComposer::togglePreview() {
	if(PhoneComposerPlayback::isPlaying()) {
		PhoneComposerPlayback::stop();
		refreshHints();
		return;
	}
	if(noteCount == 0) {
		// Nothing to preview -- give the user a visible cue rather
		// than a silent buzzer-no-op.
		if(softKeys) softKeys->flashLeft();
		return;
	}
	const bool ok = PhoneComposerPlayback::play(buffer, noteCount,
	                                            DefaultBpm,
	                                            /*loopForever=*/false,
	                                            /*name=*/"COMPOSER");
	if(!ok) {
		if(softKeys) softKeys->flashLeft();
	}
	refreshHints();
}

bool PhoneComposer::saveToActiveSlot() {
	const bool ok = PhoneComposerStorage::saveSlot(activeSlot,
	                                               buffer, noteCount,
	                                               /*name=*/"COMPOSER",
	                                               DefaultBpm);
	if(!ok) {
		if(softKeys) softKeys->flashRight();
		return false;
	}
	if(softKeys) softKeys->flashLeft();
	refreshHints();
	return true;
}

bool PhoneComposer::loadFromActiveSlot() {
	if(!PhoneComposerStorage::hasSlot(activeSlot)) {
		// Empty slot -- flash and bail without touching the buffer.
		if(softKeys) softKeys->flashRight();
		return false;
	}

	Note loaded[MaxNotes] = {};
	uint8_t loadedCount = 0;
	uint16_t bpm = 0;
	const bool ok = PhoneComposerStorage::loadSlot(activeSlot,
	                                               loaded, MaxNotes,
	                                               &loadedCount,
	                                               /*outName=*/nullptr,
	                                               /*outNameLen=*/0,
	                                               &bpm);
	if(!ok) {
		if(softKeys) softKeys->flashRight();
		return false;
	}

	// Stop any in-flight preview so the engine doesn't keep playing
	// the stale buffer past the load.
	PhoneComposerPlayback::stop();

	clearAll();
	for(uint8_t i = 0; i < loadedCount; ++i) {
		appendNote(loaded[i]);
	}

	// If the load came back empty (saved-empty slot) the screen still
	// updates so the user sees the slot indicator's "*" mark react to
	// future saves.
	cursor = (noteCount > 0) ? (uint8_t)(noteCount - 1) : 0;

	refreshCaption();
	refreshPreview();
	refreshRibbon();
	refreshSoftKeys();
	refreshHints();
	if(softKeys) softKeys->flashLeft();
	return true;
}
