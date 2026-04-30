#include "PhoneCallHistory.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <string.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - inlined per the established pattern in this
// codebase (see PhoneCallEnded.cpp / PhoneActiveCall.cpp /
// PhoneIncomingCall.cpp). The call-history screen uses cyan for the
// caption (matches PhoneActiveCall's "calm / informational" reading)
// and uses the type-specific row glyph colours below as the only
// diagonally-coloured elements - the row name + timestamp stay warm
// cream so a dense list still reads cleanly. The cursor highlight is a
// translucent muted-purple rect, the same shade the existing menu /
// dialer screens use for non-focal accents.
#define MP_BG_DARK      lv_color_make( 20,  12,  36)   // deep purple background fill
#define MP_ACCENT       lv_color_make(255, 140,  30)   // sunset orange (missed glyph)
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)   // cyan (caption + incoming glyph)
#define MP_TEXT         lv_color_make(255, 220, 180)   // warm cream (name / time / outgoing)
#define MP_DIM          lv_color_make( 70,  56, 100)   // muted purple (cursor highlight)
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)   // dim purple (empty-log placeholder)

// Visible-list geometry. The container sits at y = 22 (just below the
// caption strip) and stretches across the whole 160 px width minus a
// 4 px margin on each side so the highlight rect has a comfortable
// border. Each row is RowH px tall - 13 px gives pixelbasic7 (~7 px
// glyph height) a 3-px halo of breathing room above and below, which
// keeps the cursor highlight from clipping descender pixels.
static constexpr lv_coord_t kListX     = 4;
static constexpr lv_coord_t kListY     = 22;
static constexpr lv_coord_t kListW     = 152;            // 160 - 2*4
static constexpr lv_coord_t kRowH      = 13;
static constexpr lv_coord_t kListH     = kRowH * 7;      // 91 px - sub of kListY..118

// Inside-row column geometry. Glyph sits flush left, name takes the
// middle 96 px, timestamp is right-anchored. Numbers chosen so the
// most common timestamp shapes ("12:42", "YDAY", "MON") fit without
// overlapping a 12-char name.
static constexpr lv_coord_t kColGlyphX = 4;
static constexpr lv_coord_t kColNameX  = 16;
static constexpr lv_coord_t kColNameW  = 96;
static constexpr lv_coord_t kColTimeX  = kListW - 4;     // right-anchored

PhoneCallHistory::PhoneCallHistory()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  listContainer(nullptr),
		  highlight(nullptr),
		  emptyLabel(nullptr) {

	// Zero the per-row pointer arrays so a partial constructor abort
	// (an LVGL alloc failure during buildRows()) leaves nullptrs rather
	// than dangling reads in refreshVisibleRows().
	for(uint8_t i = 0; i < VisibleRows; ++i) {
		rowGlyphs[i] = nullptr;
		rowNames[i]  = nullptr;
		rowTimes[i]  = nullptr;
	}

	// Full-screen container, no scrollbars, no inner padding - same
	// blank-canvas pattern every other Phone* screen uses. Children
	// below are anchored manually on the 160x128 display.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper FIRST so it sits at the bottom of LVGL's z-order. Every
	// other element overlays it without any opacity gymnastics on the
	// parent. Same z-order pattern as PhoneActiveCall / PhoneCallEnded.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px tall) so the user
	// keeps device-state context while browsing history.
	statusBar = new PhoneStatusBar(obj);

	buildCaption();
	buildListContainer();
	buildRows();
	buildEmptyLabel();

	// Seed the visible log with a small set of representative entries
	// covering all three Type values, so the screen reads as a real
	// call log out of the box. S28 will swap these for live LoRa data
	// via clearEntries() + addEntry().
	seedSampleEntries();

	// Bottom: feature-phone soft-keys. CALL on the left (BTN_LEFT/ENTER)
	// and BACK on the right (BTN_RIGHT/BACK), matching the rest of the
	// phone-style screens.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("CALL");
	softKeys->setRight("BACK");

	// Initial paint - render the first window of rows and slide the
	// highlight to entry 0 (or hide it if the log somehow ended up
	// empty after seedSampleEntries). refreshEmptyState handles the
	// "no recent calls" placeholder visibility too.
	refreshVisibleRows();
	refreshHighlight();
	refreshEmptyState();
}

PhoneCallHistory::~PhoneCallHistory() {
	// Children (wallpaper, statusBar, softKeys, captionLabel, list +
	// rows, highlight, emptyLabel) are all parented to obj - LVScreen's
	// destructor frees obj and LVGL recursively frees their lv_obj_t
	// backing storage. Nothing manual.
}

void PhoneCallHistory::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneCallHistory::onStop() {
	Input::getInstance()->removeListener(this);
}

// ----- builders -----

void PhoneCallHistory::buildCaption() {
	// "CALL HISTORY" caption in pixelbasic7 cyan, just under the status
	// bar. Cyan (rather than the call-ended screen's orange) keeps it
	// reading as informational rather than alerting. Centred horizontally
	// at y = 12 so it sits cleanly between the 10 px status bar and the
	// list container at y = 22.
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "CALL HISTORY");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, 12);
}

void PhoneCallHistory::buildListContainer() {
	// Plain transparent container hosting the highlight rect + per-row
	// label children. Parent is obj (the screen body) and the geometry
	// is pinned manually rather than relying on flex - we want exact
	// row Y positions so the highlight rect can slide to a constant
	// per-row offset (kRowH * (cursor - windowTop)).
	listContainer = lv_obj_create(obj);
	lv_obj_remove_style_all(listContainer);
	lv_obj_set_size(listContainer, kListW, kListH);
	lv_obj_set_pos(listContainer, kListX, kListY);
	lv_obj_set_scrollbar_mode(listContainer, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(listContainer, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_pad_all(listContainer, 0, 0);
	lv_obj_set_style_bg_opa(listContainer, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(listContainer, 0, 0);

	// Cursor highlight - a single rounded rect sitting BEHIND the rows
	// (created first so its z-order is below the row labels). Muted
	// purple at ~70% opacity reads as "selected" without screaming
	// over the wallpaper. The rect's Y position is updated by
	// refreshHighlight; its size is fixed.
	highlight = lv_obj_create(listContainer);
	lv_obj_remove_style_all(highlight);
	lv_obj_set_size(highlight, kListW, kRowH);
	lv_obj_set_pos(highlight, 0, 0);
	lv_obj_set_scrollbar_mode(highlight, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(highlight, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_radius(highlight, 2, 0);
	lv_obj_set_style_bg_color(highlight, MP_DIM, 0);
	lv_obj_set_style_bg_opa(highlight, LV_OPA_70, 0);
	lv_obj_set_style_border_color(highlight, MP_HIGHLIGHT, 0);
	lv_obj_set_style_border_opa(highlight, LV_OPA_50, 0);
	lv_obj_set_style_border_width(highlight, 1, 0);
}

void PhoneCallHistory::buildRows() {
	// Build VisibleRows worth of (glyph, name, timestamp) label triples.
	// They are positioned once and never moved again - on cursor /
	// scroll motion we just rewrite their text via lv_label_set_text.
	for(uint8_t i = 0; i < VisibleRows; ++i) {
		const lv_coord_t y = i * kRowH + 2;   // +2 so glyph baseline lines up with row centre

		// Type glyph - pixelbasic7 single-char label, leftmost column.
		rowGlyphs[i] = lv_label_create(listContainer);
		lv_obj_set_style_text_font(rowGlyphs[i], &pixelbasic7, 0);
		lv_obj_set_style_text_color(rowGlyphs[i], MP_TEXT, 0);
		lv_label_set_text(rowGlyphs[i], "");
		lv_obj_set_pos(rowGlyphs[i], kColGlyphX, y);

		// Name - pixelbasic7, middle column, LABEL_LONG_DOT to truncate
		// a long name with an ellipsis instead of pushing the timestamp
		// off-screen.
		rowNames[i] = lv_label_create(listContainer);
		lv_obj_set_style_text_font(rowNames[i], &pixelbasic7, 0);
		lv_obj_set_style_text_color(rowNames[i], MP_TEXT, 0);
		lv_label_set_long_mode(rowNames[i], LV_LABEL_LONG_DOT);
		lv_obj_set_width(rowNames[i], kColNameW);
		lv_label_set_text(rowNames[i], "");
		lv_obj_set_pos(rowNames[i], kColNameX, y);

		// Timestamp - pixelbasic7, right column. We anchor by setting
		// the alignment to TOP_RIGHT inside the parent so the right
		// edge stays pinned at kColTimeX regardless of label width.
		rowTimes[i] = lv_label_create(listContainer);
		lv_obj_set_style_text_font(rowTimes[i], &pixelbasic7, 0);
		lv_obj_set_style_text_color(rowTimes[i], MP_LABEL_DIM, 0);
		lv_label_set_text(rowTimes[i], "");
		lv_obj_set_style_text_align(rowTimes[i], LV_TEXT_ALIGN_RIGHT, 0);
		// Use TOP_RIGHT alignment within the listContainer so the right
		// edge stays at kListW - 4 even when text length changes.
		lv_obj_align(rowTimes[i], LV_ALIGN_TOP_RIGHT, -4, y);
	}
}

void PhoneCallHistory::buildEmptyLabel() {
	// Centre-mounted "no recent calls" placeholder. Visible only when
	// entries.empty(); refreshEmptyState toggles its hidden flag. Sits
	// inside listContainer so it shares the same coordinate space as
	// the row strip and disappears cleanly when rows take over.
	emptyLabel = lv_label_create(listContainer);
	lv_obj_set_style_text_font(emptyLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(emptyLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(emptyLabel, "no recent calls");
	lv_obj_set_align(emptyLabel, LV_ALIGN_CENTER);
	lv_obj_add_flag(emptyLabel, LV_OBJ_FLAG_HIDDEN);
}

// ----- sample data -----

void PhoneCallHistory::seedSampleEntries() {
	// A representative spread of types so the screen reads as a real
	// call log on first boot. Names and timestamps deliberately mix
	// short ("MOM"), medium ("ALEX KIM") and long ("+1 555 0123") so
	// the list visually exercises every column-truncation case at
	// design-time. Order is freshest -> oldest, the same way every
	// feature-phone call log reads.
	addEntry(Type::Incoming, "ALEX KIM",     "12:42", 42, 1);
	addEntry(Type::Outgoing, "MOM",          "11:30", 318, 2);
	addEntry(Type::Missed,   "JOHN DOE",     "09:15",   0, 3);
	addEntry(Type::Incoming, "+1 555 0123",  "YDAY",   85, 4);
	addEntry(Type::Outgoing, "UNKNOWN",      "MON",     7, 0);
	addEntry(Type::Missed,   "PIZZA SHOP",   "MON",     0, 5);
	addEntry(Type::Outgoing, "DAD",          "SUN",   620, 6);
	addEntry(Type::Incoming, "WORK",         "SUN",   190, 7);
}

// ----- public API -----

void PhoneCallHistory::setOnCall(EntryHandler cb) { callCb = cb; }
void PhoneCallHistory::setOnBack(BackHandler  cb) { backCb = cb; }

void PhoneCallHistory::setLeftLabel(const char* label) {
	if(softKeys) softKeys->setLeft(label);
}

void PhoneCallHistory::setRightLabel(const char* label) {
	if(softKeys) softKeys->setRight(label);
}

void PhoneCallHistory::setCaption(const char* text) {
	if(captionLabel) lv_label_set_text(captionLabel, text != nullptr ? text : "");
}

uint8_t PhoneCallHistory::addEntry(Type        type,
								   const char* name,
								   const char* timestamp,
								   uint32_t    durationSeconds,
								   uint8_t     avatarSeed) {
	// Ring-buffer behaviour: drop the oldest entry once we'd exceed
	// MaxEntries so a long-running session can't exhaust the
	// per-screen heap. The cursor stays put when this happens (the
	// freshly-added entry is what the user typically wanted to see
	// next, and a stable cursor avoids surprising scroll jumps).
	if(entries.size() >= MaxEntries) {
		entries.erase(entries.begin());
		if(cursor > 0) cursor--;
		if(windowTop > 0) windowTop--;
	}

	Entry e{};
	e.type = type;
	copyName(e, name);
	copyTimestamp(e, timestamp);
	e.durationSeconds = durationSeconds;
	e.avatarSeed      = avatarSeed;
	entries.push_back(e);

	// Repaint - addEntry can be called both at ctor time (sample data)
	// and at runtime (live LoRa traffic from S28), and either path
	// needs the on-screen window to reflect the new state.
	refreshVisibleRows();
	refreshHighlight();
	refreshEmptyState();

	return (uint8_t)(entries.size() - 1);
}

void PhoneCallHistory::clearEntries() {
	entries.clear();
	cursor    = 0;
	windowTop = 0;
	refreshVisibleRows();
	refreshHighlight();
	refreshEmptyState();
}

void PhoneCallHistory::setCursor(uint8_t idx) {
	if(idx >= entries.size()) return;
	cursor = idx;
	refreshHighlight();
}

void PhoneCallHistory::flashLeftSoftKey() {
	if(softKeys) softKeys->flashLeft();
}

void PhoneCallHistory::flashRightSoftKey() {
	if(softKeys) softKeys->flashRight();
}

// ----- private helpers -----

void PhoneCallHistory::copyName(Entry& e, const char* src) {
	if(src == nullptr || src[0] == '\0') {
		strncpy(e.name, "UNKNOWN", MaxNameLen);
		e.name[MaxNameLen] = '\0';
		return;
	}
	strncpy(e.name, src, MaxNameLen);
	e.name[MaxNameLen] = '\0';
}

void PhoneCallHistory::copyTimestamp(Entry& e, const char* src) {
	if(src == nullptr) {
		e.timestamp[0] = '\0';
		return;
	}
	strncpy(e.timestamp, src, MaxTsLen);
	e.timestamp[MaxTsLen] = '\0';
}

const char* PhoneCallHistory::glyphFor(Type t) {
	switch(t) {
		case Type::Incoming: return "<";
		case Type::Outgoing: return ">";
		case Type::Missed:   return "x";
	}
	return "?";
}

lv_color_t PhoneCallHistory::colorFor(Type t) {
	switch(t) {
		case Type::Incoming: return MP_HIGHLIGHT;  // cyan
		case Type::Outgoing: return MP_TEXT;       // warm cream
		case Type::Missed:   return MP_ACCENT;     // sunset orange
	}
	return MP_TEXT;
}

void PhoneCallHistory::refreshVisibleRows() {
	// Walk the VisibleRows-deep window starting at windowTop. For each
	// slot either render the matching entries[windowTop + i] or hide
	// the row entirely when we have run past the end of the log.
	for(uint8_t i = 0; i < VisibleRows; ++i) {
		const uint16_t entryIdx = (uint16_t) windowTop + (uint16_t) i;
		const bool inRange = entryIdx < entries.size();

		if(rowGlyphs[i] == nullptr || rowNames[i] == nullptr || rowTimes[i] == nullptr) continue;

		if(!inRange) {
			lv_label_set_text(rowGlyphs[i], "");
			lv_label_set_text(rowNames[i],  "");
			lv_label_set_text(rowTimes[i],  "");
			continue;
		}

		const Entry& e   = entries[entryIdx];
		const lv_color_t c = colorFor(e.type);

		lv_label_set_text(rowGlyphs[i], glyphFor(e.type));
		lv_obj_set_style_text_color(rowGlyphs[i], c, 0);

		// Name stays warm-cream for everyone (the glyph carries the
		// type cue); colouring the name too would make the dense list
		// noisy. The single exception is missed calls - we tint the
		// name sunset orange to underscore the unread state, matching
		// what every feature-phone call log does.
		lv_label_set_text(rowNames[i], e.name);
		lv_obj_set_style_text_color(rowNames[i],
									e.type == Type::Missed ? MP_ACCENT : MP_TEXT,
									0);

		lv_label_set_text(rowTimes[i], e.timestamp);
	}
}

void PhoneCallHistory::refreshHighlight() {
	if(entries.empty() || highlight == nullptr) {
		// Hide the cursor rect when there is nothing to focus.
		if(highlight) lv_obj_add_flag(highlight, LV_OBJ_FLAG_HIDDEN);
		return;
	}

	// If the cursor walked outside the visible window, slide windowTop
	// to bring it back in. Two cases: cursor < windowTop (scrolled up
	// past the top edge) and cursor >= windowTop + VisibleRows
	// (scrolled down past the bottom edge). Either way clamp windowTop
	// so the bottom of the window lands at the last entry on overflow.
	if(cursor < windowTop) {
		windowTop = cursor;
	} else if(cursor >= windowTop + VisibleRows) {
		windowTop = (uint8_t)(cursor + 1 - VisibleRows);
	}
	refreshVisibleRows();

	const uint8_t row = (uint8_t)(cursor - windowTop);
	lv_obj_clear_flag(highlight, LV_OBJ_FLAG_HIDDEN);
	lv_obj_set_pos(highlight, 0, row * kRowH);
}

void PhoneCallHistory::refreshEmptyState() {
	if(emptyLabel == nullptr) return;
	if(entries.empty()) {
		lv_obj_clear_flag(emptyLabel, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_add_flag(emptyLabel, LV_OBJ_FLAG_HIDDEN);
	}
}

void PhoneCallHistory::moveCursorBy(int8_t delta) {
	if(entries.empty()) return;

	const int16_t n = (int16_t) entries.size();
	int16_t next = (int16_t) cursor + (int16_t) delta;

	// Wrap-around: a feature-phone call log lets the user flick from
	// the bottom back to the top without thinking about edges. Same
	// affordance PhoneMenuGrid offers on the main menu.
	while(next < 0)  next += n;
	while(next >= n) next -= n;

	cursor = (uint8_t) next;
	refreshHighlight();
}

// ----- input -----

void PhoneCallHistory::buttonPressed(uint i) {
	switch(i) {
		case BTN_LEFT:
		case BTN_2:
			// UP - move the cursor one row toward the top, scrolling
			// the window if we walk past its top edge.
			moveCursorBy(-1);
			break;

		case BTN_RIGHT:
		case BTN_8:
			// DOWN - move the cursor one row toward the bottom.
			moveCursorBy(+1);
			break;

		case BTN_ENTER: {
			// CALL - flash the left softkey for tactile feedback, then
			// dispatch to the host-supplied handler with the focused
			// entry. The screen does NOT push a PhoneActiveCall here -
			// that wiring is S28's job; for S27 we just hand the
			// selected entry to whoever set the callback. With no
			// callback wired the press still feels (the flash fires)
			// but nothing else happens, so the screen stays driveable
			// for visual / hardware testing.
			if(softKeys) softKeys->flashLeft();
			if(callCb && !entries.empty() && cursor < entries.size()) {
				callCb(this, entries[cursor]);
			}
			break;
		}

		case BTN_BACK:
			// BACK - flash the right softkey, then either fire the
			// host-supplied handler or fall back to pop(). Default
			// pop() targets whoever pushed us (typically PhoneMainMenu
			// once S20's "PHONE history" tile is wired to land here in
			// a future session, or the dialer screen for now).
			if(softKeys) softKeys->flashRight();
			if(backCb) {
				backCb(this);
			} else {
				pop();
			}
			break;

		default:
			break;
	}
}
