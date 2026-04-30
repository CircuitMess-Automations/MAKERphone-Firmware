#include "InboxScreen.h"

#include "../Elements/ListItem.h"
#include "../Elements/PhoneMessageRow.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSynthwaveBg.h"
#include "../Fonts/font.h"
#include "../Storage/Storage.h"
#include "ConvoScreen.h"
#include "PairScreen.h"

// MAKERphone retro palette - inlined per the established pattern in
// every Phone* screen. The caption uses cyan to read as informational,
// the empty-state hint uses dim purple so it does not compete with the
// "Add friend" button when there are no contacts yet.
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)   // cyan caption
#define MP_TEXT         lv_color_make(255, 220, 180)   // warm cream
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)   // dim purple

// ----- list-strip geometry -------------------------------------------------
//
// The list lives between the status bar (y = 0..10) plus a 12-px
// caption strip (y = 10..22) on top, and the soft-key bar (y = 118..128)
// on the bottom. That leaves a 96-px tall, 152-px wide window for the
// message rows themselves - exactly three PhoneMessageRow rows of 32 px
// each, which is the visual sweet spot for a 160x128 panel.
static constexpr lv_coord_t kListX = 4;
static constexpr lv_coord_t kListY = 22;
static constexpr lv_coord_t kListW = 152;
static constexpr lv_coord_t kListH = 96;

InboxScreen::InboxScreen() : LVScreen(), apop(this) {
	// Full-screen, transparent body. Every visible element below is
	// positioned manually inside its own container - this matches the
	// pattern used by PhoneHomeScreen / PhoneMainMenu / PhoneCallHistory
	// and keeps the synthwave wallpaper reading as one continuous bg.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_pad_all(obj, 0, 0);

	buildShell();
	buildListContainer();
	rebuildList();
}

InboxScreen::~InboxScreen() {
	// rowElements / wallpaper / statusBar / softKeys are all parented to
	// `obj`. LVScreen's dtor frees obj recursively, so we do not delete
	// them here. The C++ wrapper objects (LVObject subclasses) leak
	// their `lv_obj_t*` to LVGL and the language objects survive only
	// long enough for their members to be reset by ~LVObject. Since
	// LVScreen destruction implies the screen is no longer running
	// (running flag enforced in LVScreen's dtor), this is safe.
}

void InboxScreen::onStart() {
	apop.start();
}

void InboxScreen::onStarting() {
	// On screen entry, refresh every existing row from storage AND
	// detect newly-paired friends since the last visit (the same case
	// the legacy InboxScreen handled by re-counting Friends.all()).
	const auto frens = Storage.Friends.all();
	const size_t expectedRowCount = (frens.size() > 0) ? (frens.size() - 1) : 0;
	if(expectedRowCount != rowElements.size()) {
		rebuildList();
	} else {
		for(auto* row : rowElements) {
			if(row) row->refresh();
		}
	}
}

void InboxScreen::onStop() {
	apop.stop();
}

// ----- shell builders ------------------------------------------------------

void InboxScreen::buildShell() {
	// 1) Wallpaper FIRST (z-order bottom). Same pattern as every other
	//    phone-style screen - children added later overlay it without
	//    any opacity gymnastics on the parent.
	wallpaper = new PhoneSynthwaveBg(obj);

	// 2) Status bar at the top (it self-anchors to y=0 via IGNORE_LAYOUT).
	statusBar = new PhoneStatusBar(obj);

	// 3) "MESSAGES" caption between the status bar and the list strip.
	//    Cyan keeps it reading as informational rather than alerting -
	//    same colour PhoneCallHistory uses for its "CALL HISTORY" caption.
	caption = lv_label_create(obj);
	lv_obj_add_flag(caption, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_style_text_font(caption, &pixelbasic7, 0);
	lv_obj_set_style_text_color(caption, MP_HIGHLIGHT, 0);
	lv_label_set_text(caption, "MESSAGES");
	lv_obj_set_align(caption, LV_ALIGN_TOP_MID);
	lv_obj_set_y(caption, 12);

	// 4) Soft-key bar (it self-anchors to y=118 via IGNORE_LAYOUT). The
	//    labels are context-sensitive - "OPEN" is what ENTER does on the
	//    focused row, "BACK" pops the screen.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("OPEN");
	softKeys->setRight("BACK");
}

void InboxScreen::buildListContainer() {
	// Plain transparent strip that hosts either the message rows or the
	// "no friends" empty hint. Flex column so rows stack top-down with
	// a small gap; scrollable so a long contact list scrolls when the
	// rows overflow the 96-px window.
	listContainer = lv_obj_create(obj);
	lv_obj_remove_style_all(listContainer);
	lv_obj_add_flag(listContainer, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(listContainer, kListW, kListH);
	lv_obj_set_pos(listContainer, kListX, kListY);
	lv_obj_set_style_bg_opa(listContainer, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(listContainer, 0, 0);
	lv_obj_set_style_pad_gap(listContainer, 1, 0);
	lv_obj_set_style_border_width(listContainer, 0, 0);
	lv_obj_set_layout(listContainer, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(listContainer, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(listContainer,
						   LV_FLEX_ALIGN_START,
						   LV_FLEX_ALIGN_CENTER,
						   LV_FLEX_ALIGN_CENTER);
	lv_obj_set_scrollbar_mode(listContainer, LV_SCROLLBAR_MODE_ACTIVE);
	lv_obj_set_scroll_dir(listContainer, LV_DIR_VER);
}

// ----- list build / rebuild ------------------------------------------------

void InboxScreen::clearList() {
	// Tear down every per-row C++ wrapper before wiping their LVGL
	// children, otherwise the row dtors (which unregister listener
	// callbacks) would run after their backing lv_obj_t was freed by
	// lv_obj_clean() and crash on the next listener fire.
	for(auto* row : rowElements) {
		delete row;
	}
	rowElements.clear();
	params.clear();

	// Now wipe any remaining children of the list strip. This handles
	// the empty-state ListItem + label too.
	if(listContainer != nullptr) {
		lv_obj_clean(listContainer);
	}
	emptyHint = nullptr;
}

void InboxScreen::rebuildList() {
	clearList();

	const auto frens = Storage.Friends.all();
	// `Friends.all()` always includes the device's own efuse MAC as the
	// first entry - that's a self-record, not a contact. The legacy
	// inbox treated `size() == 1` as "no friends added", same here.
	const bool noContacts = (frens.size() <= 1);

	if(noContacts) {
		// Empty state: a single focusable "Add friend" item plus a
		// centred prompt below it. Reusing ListItem keeps the click /
		// focus styling identical to the legacy behaviour - the user
		// can still press ENTER and land on PairScreen exactly as
		// before. A purpose-built phone-style empty pane is the kind
		// of thing a future polish session can swap in.
		auto* listItem = new ListItem(listContainer, "Add friend", 1);
		lv_group_add_obj(inputGroup, listItem->getLvObj());
		lv_obj_add_event_cb(listItem->getLvObj(), [](lv_event_t* event) {
			auto* screen = static_cast<LVScreen*>(lv_event_get_user_data(event));
			screen->push(new PairScreen());
		}, LV_EVENT_PRESSED, this);

		emptyHint = lv_label_create(listContainer);
		lv_label_set_long_mode(emptyHint, LV_LABEL_LONG_WRAP);
		lv_obj_set_width(emptyHint, kListW - 6);
		lv_obj_set_style_text_font(emptyHint, &pixelbasic7, 0);
		lv_obj_set_style_text_color(emptyHint, MP_LABEL_DIM, 0);
		lv_obj_set_style_text_align(emptyHint, LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_set_style_pad_top(emptyHint, 8, 0);
		lv_label_set_text(emptyHint,
				"You don't have any friends yet.\nPress ENTER to pair.");
		return;
	}

	// Reserve once so push_back inside the loop does not reallocate -
	// we hand out pointers to the LaunchParams elements as event
	// user_data, and a vector reallocation would invalidate them.
	params.reserve(frens.size());

	for(UID_t uid : frens) {
		if(uid == ESP.getEfuseMac()) continue;

		Friend fren = Storage.Friends.get(uid);
		if(fren.uid == 0) continue;

		params.push_back({ uid, this });

		auto* row = new PhoneMessageRow(listContainer, fren);
		lv_group_add_obj(inputGroup, row->getLvObj());
		lv_obj_add_flag(row->getLvObj(), LV_OBJ_FLAG_SCROLL_ON_FOCUS);

		lv_obj_add_event_cb(row->getLvObj(), [](lv_event_t* event) {
			auto* p = static_cast<LaunchParams*>(lv_event_get_user_data(event));
			if(p == nullptr) return;
			p->ctx->openConvo(p->uid);
		}, LV_EVENT_CLICKED, &params.back());

		rowElements.push_back(row);
	}
}

// ----- navigation ----------------------------------------------------------

void InboxScreen::openConvo(UID_t uid) {
	push(new ConvoScreen(uid));
}
