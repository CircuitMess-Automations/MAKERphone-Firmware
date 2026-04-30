#include "PhoneChatBubble.h"
#include "../Fonts/font.h"
#include <string.h>

// MAKERphone retro palette - kept identical to every other Phone* widget
// (PhoneStatusBar, PhoneSoftKeyBar, PhoneClockFace, PhoneSynthwaveBg,
//  PhoneIconTile, PhoneDialerKey, PhoneDialerPad, PhonePixelAvatar). One
// shared palette is the whole reason the MAKERphone reskin reads as a
// single coherent device UI even when widgets from very different
// surfaces sit next to each other on screen.
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple page bg
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange (Sent fill)
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple (Received fill)
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream (Received text)
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)  // dim caption (timestamp)

// ---------------------------------------------------------------------------

PhoneChatBubble::PhoneChatBubble(lv_obj_t* parent, Variant variant,
								 const char* text, const char* timestamp)
		: LVObject(parent), variant(variant){

	buildContainer();
	buildBubble();
	buildTail();
	buildTimestamp();

	setText(text != nullptr ? text : "");
	setTimestamp(timestamp);
}

// ----- structural builders -------------------------------------------------

void PhoneChatBubble::buildContainer(){
	// The widget itself is a transparent full-width row whose only
	// flex-laid-out child is the bubble. Letting LVGL's flex engine pick
	// the bubble's x is much more robust than reading lv_pct(100) widths
	// during construction (when the parent might not yet be fully sized).
	// The tail and timestamp are IGNORE_LAYOUT children we anchor by hand
	// in relayout() once we know the bubble's actual position.
	lv_obj_remove_style_all(obj);
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_width(obj, lv_pct(100));
	lv_obj_set_height(obj, 0);          // grown explicitly in relayout()
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_pad_left(obj, SideMargin, 0);
	lv_obj_set_style_pad_right(obj, SideMargin, 0);
	lv_obj_set_style_pad_gap(obj, 0, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(obj, 0, 0);

	lv_obj_set_layout(obj, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(obj,
		(variant == Variant::Sent) ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
		LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
}

void PhoneChatBubble::buildBubble(){
	const bool sent = (variant == Variant::Sent);

	bubble = lv_obj_create(obj);
	lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_radius(bubble, 3, 0);
	lv_obj_set_style_pad_top(bubble, BubblePadV, 0);
	lv_obj_set_style_pad_bottom(bubble, BubblePadV, 0);
	lv_obj_set_style_pad_left(bubble, BubblePadH, 0);
	lv_obj_set_style_pad_right(bubble, BubblePadH, 0);
	lv_obj_set_style_border_width(bubble, 0, 0);
	lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(bubble, sent ? MP_ACCENT : MP_DIM, 0);
	lv_obj_set_width(bubble, LV_SIZE_CONTENT);
	lv_obj_set_height(bubble, LV_SIZE_CONTENT);

	label = lv_label_create(bubble);
	lv_obj_clear_flag(label, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_text_font(label, &pixelbasic7, 0);
	lv_obj_set_style_text_color(label, sent ? MP_BG_DARK : MP_TEXT, 0);
	lv_obj_set_style_text_line_space(label, 1, 0);
	lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
	lv_obj_set_height(label, LV_SIZE_CONTENT);
	lv_label_set_text(label, "");
}

void PhoneChatBubble::buildTail(){
	// Three 1-px-tall rectangles stack directly under the bubble's bottom
	// corner to form a 3-2-1 px stair-stepped tail. Color must match the
	// bubble fill so the tail visually belongs to the bubble rather than
	// floating as a separate decoration.
	const lv_color_t tailColor = (variant == Variant::Sent) ? MP_ACCENT : MP_DIM;

	for(uint8_t i = 0; i < 3; i++){
		lv_obj_t* r = lv_obj_create(obj);
		lv_obj_remove_style_all(r);
		lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_flag(r, LV_OBJ_FLAG_IGNORE_LAYOUT);
		lv_obj_set_size(r, 3 - i, 1);
		lv_obj_set_style_bg_color(r, tailColor, 0);
		lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
		lv_obj_set_style_radius(r, 0, 0);
		tailRect[i] = r;
	}
}

void PhoneChatBubble::buildTimestamp(){
	// Pre-built but kept hidden until setTimestamp() supplies a string.
	timestampLabel = lv_label_create(obj);
	lv_obj_clear_flag(timestampLabel, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(timestampLabel, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_style_text_font(timestampLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(timestampLabel, MP_LABEL_DIM, 0);
	lv_label_set_long_mode(timestampLabel, LV_LABEL_LONG_CLIP);
	lv_label_set_text(timestampLabel, "");
	lv_obj_add_flag(timestampLabel, LV_OBJ_FLAG_HIDDEN);
}

// ----- public mutators -----------------------------------------------------

void PhoneChatBubble::setText(const char* text){
	if(text == nullptr) text = "";

	// Two-pass sizing so the bubble shrinks to short messages but wraps
	// long ones at MaxBubbleWidth - 2 * BubblePadH:
	//   pass 1 - let the label expand to its natural unwrapped width
	//            (LV_SIZE_CONTENT plus LONG_WRAP doesn't wrap until an
	//            explicit width is supplied, so this gives us the full
	//            single-line width of the text in pixelbasic7).
	//   pass 2 - if that natural width exceeds the cap, pin the label
	//            width to the cap, which kicks LONG_WRAP in and the
	//            label's height grows to whatever the wrapped block
	//            needs.
	// This avoids reaching for lv_txt_get_size (which is technically
	// public in LVGL 8.x but not used anywhere else in this firmware,
	// so the simpler self-measure keeps our surface area smaller).
	const lv_coord_t maxLabelW = MaxBubbleWidth - 2 * BubblePadH;

	lv_obj_set_width(label, LV_SIZE_CONTENT);
	lv_label_set_text(label, text);
	lv_obj_update_layout(label);

	const lv_coord_t natural = lv_obj_get_width(label);
	if(natural > maxLabelW){
		lv_obj_set_width(label, maxLabelW);
	}
	// else leave at SIZE_CONTENT so short messages produce a snug bubble.

	relayout();
}

void PhoneChatBubble::setTimestamp(const char* timestamp){
	const bool show = (timestamp != nullptr && timestamp[0] != '\0');
	hasTimestamp = show;

	if(show){
		lv_label_set_text(timestampLabel, timestamp);
		lv_obj_clear_flag(timestampLabel, LV_OBJ_FLAG_HIDDEN);
	}else{
		lv_label_set_text(timestampLabel, "");
		lv_obj_add_flag(timestampLabel, LV_OBJ_FLAG_HIDDEN);
	}

	relayout();
}

void PhoneChatBubble::setShowTail(bool show){
	if(showTail == show) return;
	showTail = show;
	relayout();
}

// ----- internal layout -----------------------------------------------------

void PhoneChatBubble::relayout(){
	// Force a layout pass so the bubble's width/height/x reflect the new
	// label text before we anchor tail and timestamp to its corners. The
	// flex engine on `obj` picks the bubble's x for us based on the row's
	// align (END for Sent, START for Received).
	lv_obj_update_layout(obj);

	const bool sent = (variant == Variant::Sent);
	const lv_coord_t bubbleX = lv_obj_get_x(bubble);
	const lv_coord_t bubbleW = lv_obj_get_width(bubble);
	const lv_coord_t bubbleH = lv_obj_get_height(bubble);

	// Tail rectangles - anchored to the bubble's bottom corner, stair-
	// stepping inward toward the screen center as they descend. Hidden
	// when showTail is false (used by stacked bubbles from the same
	// sender so only the last in the run displays the tail).
	for(uint8_t i = 0; i < 3; i++){
		if(!showTail){
			lv_obj_add_flag(tailRect[i], LV_OBJ_FLAG_HIDDEN);
			continue;
		}
		lv_obj_clear_flag(tailRect[i], LV_OBJ_FLAG_HIDDEN);
		const lv_coord_t y = bubbleH + i;
		if(sent){
			// Right tail: progressively shorter rows pinned to bubble's
			// right edge so the steps recede inward as they descend.
			lv_obj_set_pos(tailRect[i], bubbleX + bubbleW - (3 - i), y);
		}else{
			// Left tail: progressively shorter rows pinned to bubble's
			// left edge.
			lv_obj_set_pos(tailRect[i], bubbleX, y);
		}
	}

	// Total occupied height = bubble + (tail if shown) + (timestamp gap +
	// timestamp height if shown). Set explicitly so a flex-column parent
	// stacks our row at exactly the right height.
	lv_coord_t totalH = bubbleH;
	if(showTail) totalH += TailHeight;

	if(hasTimestamp){
		// Re-measure timestamp width so we can right-align it for Sent.
		lv_obj_update_layout(timestampLabel);
		const lv_coord_t tsW = lv_obj_get_width(timestampLabel);
		const lv_coord_t tsH = lv_obj_get_height(timestampLabel);
		const lv_coord_t tsY = totalH + TimestampGap;

		lv_coord_t tsX;
		if(sent){
			tsX = bubbleX + bubbleW - tsW;
			if(tsX < SideMargin) tsX = SideMargin;
		}else{
			tsX = bubbleX;
		}
		lv_obj_set_pos(timestampLabel, tsX, tsY);
		totalH = tsY + tsH;
	}

	lv_obj_set_height(obj, totalH);
}
