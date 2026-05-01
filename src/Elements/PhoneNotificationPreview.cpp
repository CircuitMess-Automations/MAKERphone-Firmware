#include "PhoneNotificationPreview.h"
#include "../Fonts/font.h"
#include "../Storage/Storage.h"
#include "../Model/Friend.hpp"
#include "../Model/Convo.hpp"
#include <string.h>
#include <stdio.h>

// MAKERphone retro palette - kept identical across the Phone* family.
#define MP_TEXT       lv_color_make(255, 220, 180)   // warm cream caption
#define MP_HIGHLIGHT  lv_color_make(122, 232, 255)   // cyan accent
#define MP_ACCENT     lv_color_make(255, 140, 30)    // sunset orange accent
#define MP_LABEL_DIM  lv_color_make(170, 140, 200)   // dim caption purple

PhoneNotificationPreview::PhoneNotificationPreview(lv_obj_t* parent)
		: LVObject(parent) {
	// Anchor independently of any flex/column layout the host screen uses.
	lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_size(obj, PreviewWidth, PreviewHeight);

	// Transparent slab - the synthwave wallpaper shows through.
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_border_width(obj, 0, 0);
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);

	// Two stacked rows; each is 12 px tall with a 2 px gap between
	// them. Hand-positioned because LV_LAYOUT_FLEX would re-flow when
	// either row hides and the rectangular slot would visibly shift.
	buildRow(messagesRow, messagesGlyph, messagesCount, messagesPeer,
	         ">", MP_HIGHLIGHT, /*y=*/ 0);
	buildRow(callsRow,    callsGlyph,    callsCount,    callsPeer,
	         "x", MP_ACCENT,    /*y=*/ 14);

	// Subscribe to both notification streams. Listener registration is
	// idempotent so the dtor can blindly remove without checking.
	Messages.addUnreadListener(this);
	MissedCallLog::instance().addListener(this);

	refresh();
}

PhoneNotificationPreview::~PhoneNotificationPreview() {
	Messages.removeUnreadListener(this);
	MissedCallLog::instance().removeListener(this);
}

void PhoneNotificationPreview::buildRow(lv_obj_t*& row,
                                        lv_obj_t*& glyph,
                                        lv_obj_t*& countLabel,
                                        lv_obj_t*& peerLabel,
                                        const char* glyphText,
                                        lv_color_t glyphColor,
                                        int16_t y) {
	row = lv_obj_create(obj);
	lv_obj_set_size(row, PreviewWidth, 12);
	lv_obj_set_pos(row, 0, y);
	lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
	lv_obj_set_style_radius(row, 0, 0);
	lv_obj_set_style_pad_all(row, 0, 0);
	lv_obj_set_style_border_width(row, 0, 0);
	lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);

	// Left-side category glyph. ASCII so we don't pull any extra glyphs
	// into the bitmap font.
	glyph = lv_label_create(row);
	lv_label_set_text(glyph, glyphText);
	lv_obj_set_style_text_font(glyph, &pixelbasic7, 0);
	lv_obj_set_style_text_color(glyph, glyphColor, 0);
	lv_obj_align(glyph, LV_ALIGN_LEFT_MID, 2, 0);

	// Count + category caption (e.g. "3 NEW MESSAGES"). Same color as
	// the glyph so the row reads as a single coloured strip.
	countLabel = lv_label_create(row);
	lv_label_set_text(countLabel, "");
	lv_obj_set_style_text_font(countLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(countLabel, glyphColor, 0);
	lv_obj_align(countLabel, LV_ALIGN_LEFT_MID, 12, 0);

	// Right-aligned peer name (latest sender / caller). Dimmed so the
	// count stays the eye-catcher while the peer is informational.
	peerLabel = lv_label_create(row);
	lv_label_set_text(peerLabel, "");
	lv_obj_set_style_text_font(peerLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(peerLabel, MP_LABEL_DIM, 0);
	lv_label_set_long_mode(peerLabel, LV_LABEL_LONG_DOT);
	lv_obj_set_width(peerLabel, 70);
	lv_obj_set_style_text_align(peerLabel, LV_TEXT_ALIGN_RIGHT, 0);
	lv_obj_align(peerLabel, LV_ALIGN_RIGHT_MID, -2, 0);
}

uint8_t PhoneNotificationPreview::computeUnreadCount() const {
	uint8_t n = 0;
	auto convoUIDs = Storage.Convos.all();
	for(auto uid : convoUIDs) {
		Convo c = Storage.Convos.get(uid);
		if(c.unread) {
			if(n < 255) n++;
		}
	}
	return n;
}

void PhoneNotificationPreview::copyLatestUnreadPeerName(char* dst,
                                                       size_t dstSize) const {
	if(dst == nullptr || dstSize == 0) return;
	dst[0] = '\0';

	// Walk newest -> oldest (reversed) so the first hit is the most
	// recent unread conversation - the peer the user most wants to see.
	auto convoUIDs = Storage.Convos.all();
	for(auto it = convoUIDs.rbegin(); it != convoUIDs.rend(); ++it) {
		Convo c = Storage.Convos.get(*it);
		if(!c.unread) continue;

		Friend f = Storage.Friends.get(*it);
		// f.profile.nickname is a fixed buffer ([21]) and may be empty
		// for unknown peers; in that case fall through to "UNKNOWN" so
		// the right-hand label is never empty when a count is shown.
		if(f.profile.nickname[0] != '\0') {
			strncpy(dst, f.profile.nickname, dstSize - 1);
			dst[dstSize - 1] = '\0';
		} else {
			strncpy(dst, "UNKNOWN", dstSize - 1);
			dst[dstSize - 1] = '\0';
		}
		return;
	}
}

const char* PhoneNotificationPreview::pluralise(uint8_t n,
                                                const char* singular,
                                                const char* plural) {
	return (n == 1) ? singular : plural;
}

void PhoneNotificationPreview::renderMessagesRow(uint8_t unread) {
	if(unread == 0) {
		lv_obj_add_flag(messagesRow, LV_OBJ_FLAG_HIDDEN);
		return;
	}
	lv_obj_clear_flag(messagesRow, LV_OBJ_FLAG_HIDDEN);

	char buf[28];
	snprintf(buf, sizeof(buf), "%u NEW %s",
	         (unsigned) unread,
	         pluralise(unread, "MESSAGE", "MESSAGES"));
	lv_label_set_text(messagesCount, buf);

	char peer[24];
	copyLatestUnreadPeerName(peer, sizeof(peer));
	lv_label_set_text(messagesPeer, peer);
}

void PhoneNotificationPreview::renderCallsRow(uint8_t missed) {
	if(missed == 0) {
		lv_obj_add_flag(callsRow, LV_OBJ_FLAG_HIDDEN);
		return;
	}
	lv_obj_clear_flag(callsRow, LV_OBJ_FLAG_HIDDEN);

	char buf[28];
	snprintf(buf, sizeof(buf), "%u MISSED %s",
	         (unsigned) missed,
	         pluralise(missed, "CALL", "CALLS"));
	lv_label_set_text(callsCount, buf);

	const char* peer = MissedCallLog::instance().latestName();
	// Empty peer name (anonymous caller) collapses to a generic label so
	// the right column is never visually empty when a count is showing.
	lv_label_set_text(callsPeer, (peer && peer[0] != '\0') ? peer : "UNKNOWN");
}

void PhoneNotificationPreview::refresh() {
	const uint8_t unread = computeUnreadCount();
	const uint8_t missed = MissedCallLog::instance().count();

	renderMessagesRow(unread);
	renderCallsRow(missed);

	emptyState = (unread == 0) && (missed == 0);

	// Hide the wrapping slab entirely when there is nothing to show.
	// The host (LockScreen) renders its own "Nothing new" centre label
	// in that case, so we don't double up.
	if(emptyState) {
		lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
	}
}

void PhoneNotificationPreview::onUnread(bool /*unread*/) {
	refresh();
}

void PhoneNotificationPreview::onMissedCallsChanged() {
	refresh();
}
