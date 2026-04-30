#include "PhoneContactsScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <string.h>
#include <ctype.h>
#include <algorithm>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "../Storage/Storage.h"
#include "../Storage/PhoneContacts.h"
#include "../Model/Friend.hpp"

// Path-only marker used by IntroScreen.cpp to confirm the include exists.

// MAKERphone retro palette - kept identical to every other Phone* widget so
// the contacts screen reads visually as part of the same family. Inlined
// here per the established pattern (see PhoneCallHistory.cpp etc.).
#define MP_BG_DARK      lv_color_make( 20,  12,  36)
#define MP_ACCENT       lv_color_make(255, 140,  30)
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)
#define MP_TEXT         lv_color_make(255, 220, 180)
#define MP_DIM          lv_color_make( 70,  56, 100)
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)

// Visible-list geometry. The strip on the right takes 12 px so the row
// area gets 160 - 12 = 148 px (with a 4 px left gutter, the rows occupy
// 144 px). 12 px row height fits VisibleRows=8 rows in a 96 px tall list
// container (y = 22..117), leaving a comfortable 1 px halo around each
// row's pixelbasic7 baseline so the highlight rect doesn't clip.
static constexpr lv_coord_t kListX     = 4;
static constexpr lv_coord_t kListY     = 22;
static constexpr lv_coord_t kStripW    = 12;
static constexpr lv_coord_t kListW     = 160 - kListX - kStripW;   // 144
static constexpr lv_coord_t kRowH      = 12;
static constexpr lv_coord_t kListH     = kRowH * 8;                // 96 px

// Inside-row column geometry. Badge sits flush-left, name follows after a
// 2 px gutter, favorite mark is right-anchored with a 2 px right padding.
static constexpr lv_coord_t kBadgeX    = 2;
static constexpr lv_coord_t kBadgeY    = 1;
static constexpr lv_coord_t kBadgeSize = 10;
static constexpr lv_coord_t kNameX     = kBadgeX + kBadgeSize + 4;  // 16
static constexpr lv_coord_t kNameW     = kListW - kNameX - 8;       // ~120

// Right-edge A-Z scrubber column geometry (pinned to obj, not the list
// container, so it can be styled independently of the row strip).
static constexpr lv_coord_t kStripX    = 160 - kStripW;             // 148
static constexpr lv_coord_t kStripY    = kListY;
static constexpr lv_coord_t kStripH    = kListH;

// Hue table for the per-row letter badge. 8 distinct shades drawn from
// the MAKERphone palette neighbours so the badges read as part of the
// same visual family while still differentiating contacts. Indexed by
// avatarSeed % 8.
static const lv_color_t kBadgeHues[8] = {
	lv_color_make(255, 140,  30),  // sunset orange
	lv_color_make(122, 232, 255),  // cyan
	lv_color_make(255, 100, 160),  // pink
	lv_color_make(180, 240, 120),  // green
	lv_color_make(255, 220,  90),  // gold
	lv_color_make(160, 130, 240),  // lavender
	lv_color_make( 90, 200, 230),  // teal
	lv_color_make(240, 170, 100),  // peach
};

PhoneContactsScreen::PhoneContactsScreen()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  listContainer(nullptr),
		  highlight(nullptr),
		  emptyLabel(nullptr),
		  stripBar(nullptr),
		  stripLetter(nullptr),
		  stripThumb(nullptr) {

	// Zero per-row pointer arrays so a partial constructor abort leaves
	// nullptrs rather than dangling reads in refreshVisibleRows().
	for(uint8_t i = 0; i < VisibleRows; ++i) {
		rowBadgeBg[i]     = nullptr;
		rowBadgeLetter[i] = nullptr;
		rowName[i]        = nullptr;
		rowFav[i]         = nullptr;
	}

	// Full-screen container, blank canvas - same pattern every Phone*
	// screen uses.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper at the bottom of LVGL's z-order so every other element
	// overlays it cleanly. Same z-order as PhoneCallHistory.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery (10 px tall).
	statusBar = new PhoneStatusBar(obj);

	buildCaption();
	buildListContainer();
	buildRows();
	buildAZStrip();
	buildEmptyLabel();

	// Bottom: feature-phone soft-keys.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("OPEN");
	softKeys->setRight("BACK");

	// Populate from the live Friends repo, falling back to a small set
	// of representative sample entries when no contacts exist yet so the
	// screen reads as a real phone-book on first boot.
	seedFromStorageOrSamples();

	// Initial paint.
	sortEntries();
	refreshVisibleRows();
	refreshHighlight();
	refreshAZStrip();
	refreshEmptyState();
}

PhoneContactsScreen::~PhoneContactsScreen() {
	// All children parented to obj; LVScreen's destructor frees obj and
	// LVGL recursively frees the lv_obj_t backing storage.
}

void PhoneContactsScreen::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneContactsScreen::onStop() {
	Input::getInstance()->removeListener(this);
}

// ----- builders -----

void PhoneContactsScreen::buildCaption() {
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "CONTACTS");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, 12);
}

void PhoneContactsScreen::buildListContainer() {
	listContainer = lv_obj_create(obj);
	lv_obj_remove_style_all(listContainer);
	lv_obj_set_size(listContainer, kListW, kListH);
	lv_obj_set_pos(listContainer, kListX, kListY);
	lv_obj_set_scrollbar_mode(listContainer, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(listContainer, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_pad_all(listContainer, 0, 0);
	lv_obj_set_style_bg_opa(listContainer, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(listContainer, 0, 0);

	// Cursor highlight - single rounded rect sitting BEHIND the rows
	// (created first so its z-order is below the row labels).
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

void PhoneContactsScreen::buildRows() {
	// Build VisibleRows worth of (badge, name, fav-mark) widget triples.
	// They are positioned once and never moved again - rebinding text +
	// badge background colour on cursor / window scroll is cheap.
	for(uint8_t i = 0; i < VisibleRows; ++i) {
		const lv_coord_t y = i * kRowH;

		// Letter badge background - 10x10 coloured square with a 1 px
		// rounded corner so it reads as a chip rather than a plain box.
		rowBadgeBg[i] = lv_obj_create(listContainer);
		lv_obj_remove_style_all(rowBadgeBg[i]);
		lv_obj_set_size(rowBadgeBg[i], kBadgeSize, kBadgeSize);
		lv_obj_set_pos(rowBadgeBg[i], kBadgeX, y + kBadgeY);
		lv_obj_set_scrollbar_mode(rowBadgeBg[i], LV_SCROLLBAR_MODE_OFF);
		lv_obj_clear_flag(rowBadgeBg[i], LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_style_radius(rowBadgeBg[i], 2, 0);
		lv_obj_set_style_bg_color(rowBadgeBg[i], MP_DIM, 0);
		lv_obj_set_style_bg_opa(rowBadgeBg[i], LV_OPA_COVER, 0);
		lv_obj_set_style_border_width(rowBadgeBg[i], 0, 0);

		// Letter glyph centred inside the badge. pixelbasic7 single
		// character; colour is dark purple so it contrasts against the
		// bright badge hue.
		rowBadgeLetter[i] = lv_label_create(rowBadgeBg[i]);
		lv_obj_set_style_text_font(rowBadgeLetter[i], &pixelbasic7, 0);
		lv_obj_set_style_text_color(rowBadgeLetter[i], MP_BG_DARK, 0);
		lv_label_set_text(rowBadgeLetter[i], "");
		lv_obj_set_align(rowBadgeLetter[i], LV_ALIGN_CENTER);

		// Display name - middle column, LABEL_LONG_DOT to truncate a long
		// name with an ellipsis instead of overflowing into the favorite
		// mark or the A-Z strip.
		rowName[i] = lv_label_create(listContainer);
		lv_obj_set_style_text_font(rowName[i], &pixelbasic7, 0);
		lv_obj_set_style_text_color(rowName[i], MP_TEXT, 0);
		lv_label_set_long_mode(rowName[i], LV_LABEL_LONG_DOT);
		lv_obj_set_width(rowName[i], kNameW);
		lv_label_set_text(rowName[i], "");
		lv_obj_set_pos(rowName[i], kNameX, y + 2);

		// Favorite "*" mark - right-anchored to the row strip, hidden by
		// default; refreshVisibleRows toggles visibility per-entry.
		rowFav[i] = lv_label_create(listContainer);
		lv_obj_set_style_text_font(rowFav[i], &pixelbasic7, 0);
		lv_obj_set_style_text_color(rowFav[i], MP_ACCENT, 0);
		lv_label_set_text(rowFav[i], "*");
		lv_obj_align(rowFav[i], LV_ALIGN_TOP_RIGHT, -2, y + 2);
		lv_obj_add_flag(rowFav[i], LV_OBJ_FLAG_HIDDEN);
	}
}

void PhoneContactsScreen::buildAZStrip() {
	// Thin vertical scrubber bar - 1 px wide, full list height, dim
	// purple so it reads as the "track" the thumb slides along.
	stripBar = lv_obj_create(obj);
	lv_obj_remove_style_all(stripBar);
	lv_obj_set_size(stripBar, 1, kStripH);
	lv_obj_set_pos(stripBar, kStripX + (kStripW / 2), kStripY);
	lv_obj_set_scrollbar_mode(stripBar, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(stripBar, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(stripBar, MP_DIM, 0);
	lv_obj_set_style_bg_opa(stripBar, LV_OPA_70, 0);
	lv_obj_set_style_border_width(stripBar, 0, 0);

	// Thumb - 3-px-tall sunset-orange box that slides along the track.
	stripThumb = lv_obj_create(obj);
	lv_obj_remove_style_all(stripThumb);
	lv_obj_set_size(stripThumb, kStripW - 4, 3);
	lv_obj_set_pos(stripThumb, kStripX + 2, kStripY);
	lv_obj_set_scrollbar_mode(stripThumb, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(stripThumb, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_radius(stripThumb, 1, 0);
	lv_obj_set_style_bg_color(stripThumb, MP_ACCENT, 0);
	lv_obj_set_style_bg_opa(stripThumb, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(stripThumb, 0, 0);
	lv_obj_add_flag(stripThumb, LV_OBJ_FLAG_HIDDEN);

	// Letter glyph - cyan pixelbasic7 letter showing the current row's
	// letter bucket. Sits on the strip at the same Y as the thumb.
	stripLetter = lv_label_create(obj);
	lv_obj_set_style_text_font(stripLetter, &pixelbasic7, 0);
	lv_obj_set_style_text_color(stripLetter, MP_HIGHLIGHT, 0);
	lv_label_set_text(stripLetter, "");
	lv_obj_set_pos(stripLetter, kStripX + 3, kStripY);
	lv_obj_add_flag(stripLetter, LV_OBJ_FLAG_HIDDEN);
}

void PhoneContactsScreen::buildEmptyLabel() {
	emptyLabel = lv_label_create(listContainer);
	lv_obj_set_style_text_font(emptyLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(emptyLabel, MP_LABEL_DIM, 0);
	lv_label_set_text(emptyLabel, "no contacts yet");
	lv_obj_set_align(emptyLabel, LV_ALIGN_CENTER);
	lv_obj_add_flag(emptyLabel, LV_OBJ_FLAG_HIDDEN);
}

// ----- population -----

void PhoneContactsScreen::seedFromStorageOrSamples() {
	// Walk every paired peer in the Friends repo, skipping the device's
	// own efuse-mac id (legacy convention - the user never wants their
	// own profile in the contacts list). PhoneContacts::displayNameOf()
	// transparently picks the user-edited override if present, else the
	// peer's broadcast nickname, else "Contact".
	const UID_t selfUid = (UID_t) ESP.getEfuseMac();
	for(UID_t uid : Storage.Friends.all()){
		if(uid == selfUid) continue;
		const char* name  = PhoneContacts::displayNameOf(uid);
		const uint8_t seed = PhoneContacts::avatarSeedOf(uid);
		const bool fav     = PhoneContacts::isFavorite(uid);
		addEntry(uid, name, seed, fav);
	}

	// Fallback: a representative sample so the screen reads as a phone
	// book on first boot, before the user has paired any peer. UIDs are
	// 0 so an opener can detect the placeholder rows and no-op on them
	// if it wants to.
	if(entries.empty()){
		addEntry(0, "ALEX KIM",     1, false);
		addEntry(0, "ALICE",        2, false);
		addEntry(0, "BRIAN COOPER", 3, true);
		addEntry(0, "CARL",         4, true);
		addEntry(0, "DAD",          5, true);
		addEntry(0, "ELLA",         6, false);
		addEntry(0, "GEORGE",       7, false);
		addEntry(0, "HANNAH",       0, false);
		addEntry(0, "JOHN DOE",     1, false);
		addEntry(0, "MOM",          2, true);
		addEntry(0, "PIZZA SHOP",   3, false);
		addEntry(0, "SAM",          4, false);
		addEntry(0, "WORK",         5, false);
	}
}

// ----- public API -----

void PhoneContactsScreen::setOnOpen(EntryHandler cb) { openCb = cb; }
void PhoneContactsScreen::setOnBack(BackHandler  cb) { backCb = cb; }

void PhoneContactsScreen::setLeftLabel(const char* label) {
	if(softKeys) softKeys->setLeft(label);
}

void PhoneContactsScreen::setRightLabel(const char* label) {
	if(softKeys) softKeys->setRight(label);
}

void PhoneContactsScreen::setCaption(const char* text) {
	if(captionLabel) lv_label_set_text(captionLabel, text != nullptr ? text : "");
}

uint8_t PhoneContactsScreen::addEntry(UID_t uid, const char* name, uint8_t avatarSeed, bool favorite) {
	// Ring-buffer behaviour - drop the oldest entry when we'd exceed
	// MaxEntries so a long-running session can't exhaust the per-screen
	// heap. The cursor stays put.
	if(entries.size() >= MaxEntries) {
		entries.erase(entries.begin());
		if(cursor > 0) cursor--;
		if(windowTop > 0) windowTop--;
	}

	Entry e{};
	e.uid        = uid;
	if(name == nullptr || name[0] == '\0'){
		strncpy(e.name, "Contact", MaxNameLen);
	}else{
		strncpy(e.name, name, MaxNameLen);
	}
	e.name[MaxNameLen] = '\0';
	e.avatarSeed = avatarSeed;
	e.favorite   = favorite ? 1 : 0;
	entries.push_back(e);

	sortEntries();
	refreshVisibleRows();
	refreshHighlight();
	refreshAZStrip();
	refreshEmptyState();

	return (uint8_t)(entries.size() - 1);
}

void PhoneContactsScreen::clearEntries() {
	entries.clear();
	cursor    = 0;
	windowTop = 0;
	refreshVisibleRows();
	refreshHighlight();
	refreshAZStrip();
	refreshEmptyState();
}

void PhoneContactsScreen::setCursor(uint8_t idx) {
	if(idx >= entries.size()) return;
	cursor = idx;
	refreshHighlight();
	refreshAZStrip();
}

void PhoneContactsScreen::flashLeftSoftKey() {
	if(softKeys) softKeys->flashLeft();
}

void PhoneContactsScreen::flashRightSoftKey() {
	if(softKeys) softKeys->flashRight();
}

// ----- sorting -----

char PhoneContactsScreen::firstLetterOf(const char* name) {
	if(name == nullptr || name[0] == '\0') return '#';
	const char c = (char) toupper((unsigned char) name[0]);
	if(c >= 'A' && c <= 'Z') return c;
	return '#';
}

int PhoneContactsScreen::cmpName(const Entry& a, const Entry& b) {
	// Favorites first. Within each bucket, case-insensitive lexicographic
	// compare on the display name. Stable enough that the same user state
	// always produces the same row order across reboots.
	if(a.favorite != b.favorite) return a.favorite ? -1 : 1;
	const unsigned char* pa = (const unsigned char*) a.name;
	const unsigned char* pb = (const unsigned char*) b.name;
	while(*pa && *pb) {
		const int ca = toupper(*pa);
		const int cb = toupper(*pb);
		if(ca != cb) return ca < cb ? -1 : 1;
		++pa; ++pb;
	}
	if(*pa == *pb) return 0;
	return *pa ? 1 : -1;
}

void PhoneContactsScreen::sortEntries() {
	std::sort(entries.begin(), entries.end(),
		[](const Entry& a, const Entry& b){ return cmpName(a, b) < 0; });
}

// ----- rendering -----

void PhoneContactsScreen::refreshVisibleRows() {
	for(uint8_t i = 0; i < VisibleRows; ++i) {
		if(rowBadgeBg[i] == nullptr || rowBadgeLetter[i] == nullptr ||
		   rowName[i] == nullptr || rowFav[i] == nullptr) continue;

		const uint16_t entryIdx = (uint16_t) windowTop + (uint16_t) i;
		const bool inRange = entryIdx < entries.size();

		if(!inRange) {
			lv_obj_add_flag(rowBadgeBg[i], LV_OBJ_FLAG_HIDDEN);
			lv_label_set_text(rowName[i], "");
			lv_obj_add_flag(rowFav[i], LV_OBJ_FLAG_HIDDEN);
			continue;
		}

		const Entry& e = entries[entryIdx];

		// Letter badge - hue from the seed table, single-letter glyph.
		lv_obj_clear_flag(rowBadgeBg[i], LV_OBJ_FLAG_HIDDEN);
		lv_obj_set_style_bg_color(rowBadgeBg[i], kBadgeHues[e.avatarSeed % 8], 0);
		const char letter[2] = { firstLetterOf(e.name), '\0' };
		lv_label_set_text(rowBadgeLetter[i], letter);

		// Name in warm cream (favorites in sunset orange so the user can
		// pick them out at a glance even with the "*" mark on the right).
		lv_label_set_text(rowName[i], e.name);
		lv_obj_set_style_text_color(rowName[i],
									e.favorite ? MP_ACCENT : MP_TEXT, 0);

		if(e.favorite) {
			lv_obj_clear_flag(rowFav[i], LV_OBJ_FLAG_HIDDEN);
		}else{
			lv_obj_add_flag(rowFav[i], LV_OBJ_FLAG_HIDDEN);
		}
	}
}

void PhoneContactsScreen::refreshHighlight() {
	if(entries.empty() || highlight == nullptr) {
		if(highlight) lv_obj_add_flag(highlight, LV_OBJ_FLAG_HIDDEN);
		return;
	}

	// Slide windowTop so the cursor stays inside the visible window.
	if(cursor < windowTop) {
		windowTop = cursor;
	}else if(cursor >= windowTop + VisibleRows) {
		windowTop = (uint8_t)(cursor + 1 - VisibleRows);
	}
	refreshVisibleRows();

	const uint8_t row = (uint8_t)(cursor - windowTop);
	lv_obj_clear_flag(highlight, LV_OBJ_FLAG_HIDDEN);
	lv_obj_set_pos(highlight, 0, row * kRowH);
}

void PhoneContactsScreen::refreshAZStrip() {
	if(stripLetter == nullptr || stripThumb == nullptr) return;

	if(entries.empty() || cursor >= entries.size()) {
		lv_obj_add_flag(stripLetter, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(stripThumb,  LV_OBJ_FLAG_HIDDEN);
		return;
	}

	const char letter = firstLetterOf(entries[cursor].name);
	const char buf[2] = { letter, '\0' };
	lv_label_set_text(stripLetter, buf);

	// Map the letter into a 0..1 position in the alphabet bar. '#' (any
	// non-A..Z first char) lands at the very top.
	uint8_t idx = 0;
	if(letter >= 'A' && letter <= 'Z') idx = (uint8_t)(letter - 'A');
	const lv_coord_t maxY = kStripH - 7;   // leave room for the 7 px glyph
	const lv_coord_t y    = (lv_coord_t)((int32_t) idx * (int32_t) maxY / 25);

	lv_obj_clear_flag(stripLetter, LV_OBJ_FLAG_HIDDEN);
	lv_obj_set_pos(stripLetter, kStripX + 3, kStripY + y);

	// Thumb sits next to the letter - same Y, anchored to the alphabet
	// position. With the thumb pinned to obj (not the strip bar) we can
	// position it independently of the bar's 1 px width.
	lv_obj_clear_flag(stripThumb, LV_OBJ_FLAG_HIDDEN);
	lv_obj_set_pos(stripThumb, kStripX + 2, kStripY + y + 2);
}

void PhoneContactsScreen::refreshEmptyState() {
	if(emptyLabel == nullptr) return;
	if(entries.empty()) {
		lv_obj_clear_flag(emptyLabel, LV_OBJ_FLAG_HIDDEN);
	}else{
		lv_obj_add_flag(emptyLabel, LV_OBJ_FLAG_HIDDEN);
	}
}

// ----- navigation -----

void PhoneContactsScreen::moveCursorBy(int8_t delta) {
	if(entries.empty()) return;
	const int16_t n = (int16_t) entries.size();
	int16_t next = (int16_t) cursor + (int16_t) delta;
	while(next < 0)  next += n;
	while(next >= n) next -= n;
	cursor = (uint8_t) next;
	refreshHighlight();
	refreshAZStrip();
}

void PhoneContactsScreen::jumpLetter(int8_t direction) {
	// Jump to the first contact whose first letter differs from the
	// current row's letter, walking in `direction` (`+1` next, `-1`
	// previous). With every entry sharing a letter the jump degenerates
	// to a single-row move, which is the right "feels like the alphabet
	// is exhausted" feedback.
	if(entries.empty()) return;
	const int16_t n = (int16_t) entries.size();
	const char curLetter = firstLetterOf(entries[cursor].name);

	int16_t i = (int16_t) cursor;
	for(int16_t step = 0; step < n; ++step) {
		i += direction;
		if(i < 0)  i += n;
		if(i >= n) i -= n;
		const char l = firstLetterOf(entries[i].name);
		if(l != curLetter) {
			// Walk back to the first entry of this letter bucket when
			// jumping forward (so the user lands on the bucket head),
			// or stay on the last entry of the previous bucket when
			// jumping backwards (so a second L jumps the bucket above).
			if(direction > 0) {
				cursor = (uint8_t) i;
			}else{
				// Walk left until the previous entry's letter changes,
				// landing the cursor at the head of the previous bucket
				// rather than its tail.
				int16_t k = i;
				while(true) {
					int16_t prev = k - 1;
					if(prev < 0) prev += n;
					if(firstLetterOf(entries[prev].name) != l) break;
					k = prev;
					if(k == i) break;  // full wrap - bail out
				}
				cursor = (uint8_t) k;
			}
			refreshHighlight();
			refreshAZStrip();
			return;
		}
	}

	// Single-bucket fallback - just hop one row in the requested direction.
	moveCursorBy(direction);
}

// ----- input -----

void PhoneContactsScreen::buttonPressed(uint i) {
	switch(i) {
		case BTN_LEFT:
		case BTN_2:
			moveCursorBy(-1);
			break;

		case BTN_RIGHT:
		case BTN_8:
			moveCursorBy(+1);
			break;

		case BTN_L:
			// L bumper - jump to the head of the previous letter bucket.
			jumpLetter(-1);
			break;

		case BTN_R:
			// R bumper - jump to the head of the next letter bucket.
			jumpLetter(+1);
			break;

		case BTN_ENTER: {
			// OPEN - flash the left softkey and dispatch.
			if(softKeys) softKeys->flashLeft();
			if(openCb && !entries.empty() && cursor < entries.size()) {
				openCb(this, entries[cursor]);
			}
			break;
		}

		case BTN_BACK:
			// BACK - flash the right softkey, then handler-or-pop.
			if(softKeys) softKeys->flashRight();
			if(backCb) {
				backCb(this);
			}else{
				pop();
			}
			break;

		default:
			break;
	}
}
