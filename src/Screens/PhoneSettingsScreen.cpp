#include "PhoneSettingsScreen.h"

#include <Input/Input.h>
#include <Pins.hpp>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "PhoneAppStubScreen.h"

// MAKERphone retro palette - inlined per the established pattern in this
// codebase (see PhoneCallHistory.cpp / PhoneMainMenu.cpp / PhoneHomeScreen.cpp).
// The settings screen uses cyan for the caption (informational), warm
// cream for the row labels, dim purple for group headers + chevron icons,
// and the muted-purple translucent cursor rect for the focused row -
// the same accent vocabulary every other Phase-D/I screen relies on.
#define MP_BG_DARK      lv_color_make( 20,  12,  36)   // (unused on transparent bg, kept for parity)
#define MP_ACCENT       lv_color_make(255, 140,  30)   // sunset orange (chevron on focused row)
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)   // cyan (caption)
#define MP_TEXT         lv_color_make(255, 220, 180)   // warm cream (row label)
#define MP_DIM          lv_color_make( 70,  56, 100)   // muted purple (cursor highlight)
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)   // dim purple (group header + idle chevron)

// List-area geometry. The container sits at ListY (just below the
// caption) with a 4 px left margin and ListW = 152 px wide so the
// highlight rect has comfortable horizontal breathing room. Each row
// is RowH px tall and each group header is HdrH px tall - both small
// enough that the 5 rows + 3 headers fit cleanly in the 96 px tall
// list area without scrolling (5*12 + 3*10 = 90 px).
static constexpr lv_coord_t kListX     = 4;
static constexpr lv_coord_t kColLabelX = 6;            // label column inside listContainer
static constexpr lv_coord_t kColChevX  = PhoneSettingsScreen::ListW - 10; // chevron right-anchored

// One-line "section" descriptor used while laying out the visible list.
// Headers and rows are interleaved here; the layout pass walks this
// table once, emitting either a dim "DISPLAY"-style header label or a
// row (label + chevron + cursor target). Stored in static const data
// so it is shared between every PhoneSettingsScreen instance and adds
// no per-screen RAM cost.
namespace {
struct Section {
	bool                         isHeader;
	const char*                  text;          // label text for header / row
	const char*                  stubName;      // friendly stub name when used as a row (NULL for headers)
	PhoneSettingsScreen::Item    item;          // ignored for headers
};

// Logical layout: 3 group headers + 5 rows. The order tracks the Item
// enum 1:1 inside each group (Brightness/Wallpaper under DISPLAY, etc.)
// so a host that introspects via getSelectedItem() reads naturally.
const Section kLayout[] = {
	{ true,  "DISPLAY",            nullptr,       PhoneSettingsScreen::Item::Brightness },
	{ false, "Brightness",         "BRIGHTNESS",  PhoneSettingsScreen::Item::Brightness },
	{ false, "Wallpaper",          "WALLPAPER",   PhoneSettingsScreen::Item::Wallpaper  },
	// S101 - Theme picker drills into the per-theme palette + wallpaper
	// pair (Default Synthwave / Nokia 3310 Monochrome today; Phase O
	// grows the list to 10 themes by S119). Sits right after Wallpaper
	// because the two settings co-modulate: a non-default theme
	// overrides the wallpaperStyle byte for as long as it is selected.
	{ false, "Theme",              "THEME",       PhoneSettingsScreen::Item::Theme      },
	{ true,  "SOUND",              nullptr,       PhoneSettingsScreen::Item::Sound      },
	{ false, "Sound & Vibration",  "SOUND",       PhoneSettingsScreen::Item::Sound      },
	// S68 - haptic-style nav-key tick toggle. Listed in the SOUND
	// group because it co-modulates with the sound profile (the tick
	// only fires while the device is in Mute / Vibrate; in Loud the
	// existing 25 ms key tones already cover navigation feedback).
	{ false, "Key clicks",         "KEY CLICKS",  PhoneSettingsScreen::Item::Haptics    },
	{ true,  "SYSTEM",             nullptr,       PhoneSettingsScreen::Item::DateTime   },
	{ false, "Date & Time",        "DATE & TIME", PhoneSettingsScreen::Item::DateTime   },
	// S144 - owner name (lock-screen greeting). Sits between Date & Time
	// and About so the SYSTEM group reads as a natural "phone identity"
	// cluster and the existing About row stays at the bottom of the list
	// where users expect a feature-phone About entry.
	{ false, "Owner name",         "OWNER",       PhoneSettingsScreen::Item::Owner       },
	// S146 - custom power-off message painted over the PhonePowerDown
	// CRT-shrink animation. Sits directly below "Owner name" so the
	// two T9-typed personalisation slots cluster together inside the
	// existing SYSTEM group; About stays anchored at the bottom of
	// the list where feature-phone users expect to find it.
	{ false, "Power-off msg",      "POWER-OFF MSG", PhoneSettingsScreen::Item::PowerOffMsg },
	{ false, "About",              "ABOUT",       PhoneSettingsScreen::Item::About       },
};
constexpr uint8_t kLayoutLen = sizeof(kLayout) / sizeof(kLayout[0]);
} // namespace

PhoneSettingsScreen::PhoneSettingsScreen()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  listContainer(nullptr),
		  highlight(nullptr) {

	// Full-screen container, no scrollbars, no padding - same blank-canvas
	// pattern PhoneMainMenu / PhoneCallHistory use. Children below either
	// pin themselves with IGNORE_LAYOUT or are LVGL primitives we anchor
	// manually on the 160x128 display.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper at the bottom of the z-order so the rest of the screen
	// reads on top of the synthwave gradient like every other Phase-D
	// screen.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Top: standard signal | clock | battery bar (10 px) so the user
	// always knows the device is alive while drilling through settings.
	statusBar = new PhoneStatusBar(obj);

	buildCaption();
	buildListContainer();
	buildList();

	// Bottom: feature-phone soft-keys. OPEN drills into the focused row,
	// BACK pops back to PhoneMainMenu. Both labels are kept short so they
	// fit in the 10 px softkey bar without truncation on either side.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("OPEN");
	softKeys->setRight("BACK");

	// Initial cursor + highlight position. cursor defaults to 0 (the
	// first row, currently Brightness), so the screen opens with the
	// top-most row pre-focused.
	refreshHighlight();
}

PhoneSettingsScreen::~PhoneSettingsScreen() {
	// All children (wallpaper, statusBar, softKeys, captionLabel,
	// listContainer + its children) are parented to obj - LVGL frees
	// them recursively when the screen's obj is destroyed by the
	// LVScreen base destructor. Nothing manual.
}

void PhoneSettingsScreen::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneSettingsScreen::onStop() {
	Input::getInstance()->removeListener(this);
}

// ----- builders ---------------------------------------------------------

void PhoneSettingsScreen::buildCaption() {
	// "SETTINGS" caption in pixelbasic7 cyan, just under the status bar.
	// Centred horizontally at y = 12 - same pattern PhoneCallHistory's
	// "CALL HISTORY" caption uses, so the screen feels visually
	// consistent with the rest of Phase D / I.
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "SETTINGS");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, 12);
}

void PhoneSettingsScreen::buildListContainer() {
	// Compute the total height needed for the layout (5 rows + 3 headers).
	// Done at runtime rather than as a constexpr because kLayout lives in
	// an anonymous namespace and we want a single source of truth - if a
	// future commit adds a section, the container auto-resizes.
	lv_coord_t totalH = 0;
	for(uint8_t i = 0; i < kLayoutLen; ++i) {
		totalH += kLayout[i].isHeader ? HdrH : RowH;
	}

	// Plain transparent container hosting the highlight rect + per-row
	// label children. Geometry pinned manually rather than relying on
	// flex - we want exact row Y positions so the highlight rect can
	// slide to a constant per-row offset (rows[i].y).
	listContainer = lv_obj_create(obj);
	lv_obj_remove_style_all(listContainer);
	lv_obj_set_size(listContainer, ListW, totalH);
	lv_obj_set_pos(listContainer, kListX, ListY);
	lv_obj_set_scrollbar_mode(listContainer, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(listContainer, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_pad_all(listContainer, 0, 0);
	lv_obj_set_style_bg_opa(listContainer, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(listContainer, 0, 0);

	// Cursor highlight - a single rounded rect sitting BEHIND the rows
	// (created first so its z-order is below the row labels). Muted
	// purple at ~70% opacity reads as "selected" without screaming over
	// the wallpaper. Width spans the whole container; height is RowH so
	// it perfectly covers a focused row.
	highlight = lv_obj_create(listContainer);
	lv_obj_remove_style_all(highlight);
	lv_obj_set_size(highlight, ListW, RowH);
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

void PhoneSettingsScreen::buildList() {
	// Walk the static layout once, emitting either a header label
	// (dim purple, no cursor target) or a row (cream label + chevron,
	// recorded in rows[] for cursor / activate dispatch).
	lv_coord_t y       = 0;
	uint8_t    rowIdx  = 0;

	for(uint8_t i = 0; i < kLayoutLen; ++i) {
		const Section& s = kLayout[i];

		if(s.isHeader) {
			// Group header - pixelbasic7, dim purple, slightly indented
			// so it reads as a section title rather than a row. No
			// chevron, no highlight target.
			lv_obj_t* hdr = lv_label_create(listContainer);
			lv_obj_set_style_text_font(hdr, &pixelbasic7, 0);
			lv_obj_set_style_text_color(hdr, MP_LABEL_DIM, 0);
			lv_label_set_text(hdr, s.text);
			lv_obj_set_pos(hdr, kColLabelX - 2, y + 1);
			y += HdrH;
			continue;
		}

		// Selectable row. The label sits in the left column (cream),
		// the chevron in the right column (dim purple by default,
		// recolored to sunset orange on the focused row by
		// refreshHighlight()).
		Row& r       = rows[rowIdx];
		r.item       = s.item;
		r.stubName   = s.stubName;
		r.y          = y;

		r.labelObj = lv_label_create(listContainer);
		lv_obj_set_style_text_font(r.labelObj, &pixelbasic7, 0);
		lv_obj_set_style_text_color(r.labelObj, MP_TEXT, 0);
		lv_label_set_long_mode(r.labelObj, LV_LABEL_LONG_DOT);
		lv_obj_set_width(r.labelObj, ListW - 24); // leave 24 px for chevron column
		lv_label_set_text(r.labelObj, s.text);
		// S146 - row label drops to y+1 to match the trimmed 8 px RowH.
		// pixelbasic7 is 7 px tall so the label fits cleanly inside the
		// row without bleeding into the next one. Header captions still
		// use y+1 below (they remain on a 9 px HdrH so they keep their
		// 2 px top halo unchanged from S144).
		lv_obj_set_pos(r.labelObj, kColLabelX, y + 1);

		r.chevronObj = lv_label_create(listContainer);
		lv_obj_set_style_text_font(r.chevronObj, &pixelbasic7, 0);
		lv_obj_set_style_text_color(r.chevronObj, MP_LABEL_DIM, 0);
		lv_label_set_text(r.chevronObj, ">");
		lv_obj_set_pos(r.chevronObj, kColChevX, y + 1);

		y += RowH;
		++rowIdx;
	}

	// Sanity guard: kLayout is hand-authored, so if a future commit
	// changes the row count without bumping ItemCount the screen would
	// silently mis-render. Rather than asserting (no assert framework
	// in the firmware), we just leave any unfilled rows with NULL
	// labelObj/chevronObj - refreshHighlight() / launchDefault() are
	// defensive against that.
	for(; rowIdx < ItemCount; ++rowIdx) {
		rows[rowIdx].labelObj   = nullptr;
		rows[rowIdx].chevronObj = nullptr;
		rows[rowIdx].y          = 0;
		rows[rowIdx].item       = static_cast<Item>(rowIdx);
		rows[rowIdx].stubName   = "SETTING";
	}
}

// ----- cursor + highlight ----------------------------------------------

void PhoneSettingsScreen::refreshHighlight() {
	if(highlight == nullptr) return;
	if(cursor >= ItemCount) cursor = 0;

	// Slide the highlight rect to the focused row's Y. We update the
	// position via lv_obj_set_y rather than lv_anim_t so the cursor
	// move feels instantaneous - feature-phone navigation should be
	// snappy, not animated. Animations on the LVGL side would also
	// cost frames during fast wrap-around scrubbing.
	lv_obj_set_y(highlight, rows[cursor].y);

	// Recolor every chevron - dim purple by default, sunset orange on
	// the focused row. Cheap (one style call per row) and keeps the
	// chevron column reading as part of the cursor instead of a
	// static decoration.
	for(uint8_t i = 0; i < ItemCount; ++i) {
		if(rows[i].chevronObj == nullptr) continue;
		lv_obj_set_style_text_color(rows[i].chevronObj,
									(i == cursor) ? MP_ACCENT : MP_LABEL_DIM,
									0);
	}
}

void PhoneSettingsScreen::moveCursorBy(int8_t delta) {
	if(ItemCount == 0) return;
	int16_t next = static_cast<int16_t>(cursor) + delta;
	while(next < 0)                          next += ItemCount;
	while(next >= static_cast<int16_t>(ItemCount)) next -= ItemCount;
	cursor = static_cast<uint8_t>(next);
	refreshHighlight();
}

// ----- public setters / getters ----------------------------------------

void PhoneSettingsScreen::setOnActivate(ActivateHandler cb) { activateCb = cb; }
void PhoneSettingsScreen::setOnBack(BackHandler cb)         { backCb     = cb; }

void PhoneSettingsScreen::setLeftLabel(const char* label) {
	if(softKeys) softKeys->setLeft(label);
}
void PhoneSettingsScreen::setRightLabel(const char* label) {
	if(softKeys) softKeys->setRight(label);
}

PhoneSettingsScreen::Item PhoneSettingsScreen::getSelectedItem() const {
	if(cursor >= ItemCount) return Item::Brightness;
	return rows[cursor].item;
}

void PhoneSettingsScreen::setSelectedItem(Item item) {
	for(uint8_t i = 0; i < ItemCount; ++i) {
		if(rows[i].item == item) {
			cursor = i;
			refreshHighlight();
			return;
		}
	}
}

void PhoneSettingsScreen::flashLeftSoftKey() {
	if(softKeys) softKeys->flashLeft();
}
void PhoneSettingsScreen::flashRightSoftKey() {
	if(softKeys) softKeys->flashRight();
}

// ----- input -----------------------------------------------------------

void PhoneSettingsScreen::buttonPressed(uint i) {
	switch(i) {
		case BTN_2:
		case BTN_LEFT:
			moveCursorBy(-1);
			break;

		case BTN_8:
		case BTN_RIGHT:
			moveCursorBy(+1);
			break;

		case BTN_ENTER: {
			// Flash the OPEN softkey for tactile feedback, then dispatch.
			// Hosts can override the dispatch via setOnActivate; with no
			// callback wired we fall through to the built-in default,
			// which pushes a PhoneAppStubScreen named after the row.
			if(softKeys) softKeys->flashLeft();
			const Item it = getSelectedItem();
			if(activateCb) {
				activateCb(this, it);
			} else {
				launchDefault(it);
			}
			break;
		}

		case BTN_BACK:
			// Flash the BACK softkey (same press-feedback pattern as
			// every other Phase-D/I screen) and pop. Hosts can override
			// the dispatch via setOnBack; with no callback wired we
			// pop() so the user lands back on PhoneMainMenu.
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

// ----- defaults --------------------------------------------------------

void PhoneSettingsScreen::launchDefault(Item item) {
	// Find the row's stubName for the friendly placeholder caption. The
	// stubName list above keeps these uppercase to match the placeholder
	// title style (PhoneAppStubScreen renders the title in pixelbasic16).
	const char* name = "SETTING";
	for(uint8_t i = 0; i < ItemCount; ++i) {
		if(rows[i].item == item && rows[i].stubName != nullptr) {
			name = rows[i].stubName;
			break;
		}
	}
	// PhoneAppStubScreen takes ownership when push()ed - same lifetime
	// contract every other Phase-D/I drill-down uses. The stub's BACK
	// pops back here so the cursor / highlight position is preserved.
	push(new PhoneAppStubScreen(name));
}
