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
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan (delivered double tick)

// ---------------------------------------------------------------------------

PhoneChatBubble::PhoneChatBubble(lv_obj_t* parent, Variant variant,
								 const char* text, const char* timestamp)
		: LVObject(parent), variant(variant){

	buildContainer();
	buildBubble();
	buildTail();
	buildTimestamp();
	buildStatus();

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

void PhoneChatBubble::buildStatus(){
	// S34 - Pre-built host for the delivery-status glyph (clock /
	// single tick / double tick). Same layout philosophy as the tail
	// and timestamp: an IGNORE_LAYOUT child of `obj` whose children
	// are 1-px "pixel" rectangles. Hidden by default; renderStatus()
	// shows it once a non-None status is assigned.
	statusBox = lv_obj_create(obj);
	lv_obj_remove_style_all(statusBox);
	lv_obj_clear_flag(statusBox, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(statusBox, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_set_style_bg_opa(statusBox, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(statusBox, 0, 0);
	lv_obj_set_style_border_width(statusBox, 0, 0);
	lv_obj_set_size(statusBox, StatusBoxW, StatusBoxH);
	lv_obj_add_flag(statusBox, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t* PhoneChatBubble::statusPixel(lv_obj_t* parent, int16_t x, int16_t y,
									   lv_color_t color){
	// Single 1x1 colored "pixel" - same primitive used to draw the
	// stair-stepped tail. Cheaper than a canvas and renders crisply at
	// the 160x128 native resolution because LVGL never anti-aliases a
	// 1-px square.
	lv_obj_t* px = lv_obj_create(parent);
	lv_obj_remove_style_all(px);
	lv_obj_clear_flag(px, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(px, 1, 1);
	lv_obj_set_pos(px, x, y);
	lv_obj_set_style_bg_color(px, color, 0);
	lv_obj_set_style_bg_opa(px, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(px, 0, 0);
	return px;
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

void PhoneChatBubble::setStatus(Status next){
	// Received bubbles always store None - the indicator is a
	// Sent-only feature and we never want a stray Delivered call to
	// tag a peer's incoming message with our local read state.
	if(variant == Variant::Received){
		next = Status::None;
	}
	if(status == next){
		// Even on a no-op we re-anchor the box (cheap) so callers can
		// nudge it to the right place after they manually move the
		// bubble. Keeps the API forgiving.
		relayout();
		return;
	}
	status = next;
	renderStatus();
	relayout();
}

void PhoneChatBubble::renderStatus(){
	if(statusBox == nullptr) return;

	// Clear any previously-drawn glyph children. The container itself
	// stays alive so relayout() can keep its (x, y) anchored even
	// across status changes.
	lv_obj_clean(statusBox);

	const bool show = (variant == Variant::Sent) && (status != Status::None);
	if(!show){
		lv_obj_add_flag(statusBox, LV_OBJ_FLAG_HIDDEN);
		return;
	}
	lv_obj_clear_flag(statusBox, LV_OBJ_FLAG_HIDDEN);

	// Pending / Sent draw in the same dim caption color as the
	// timestamp so both feel like the same "metadata" tier. Delivered
	// flips to MP_HIGHLIGHT (cyan) so the user gets a clear visual
	// "it landed" cue in their peripheral vision while typing.
	const lv_color_t dimC  = MP_LABEL_DIM;
	const lv_color_t goodC = MP_HIGHLIGHT;

	if(status == Status::Pending){
		// 5x5 hollow ring (radius=99 + 1px border, transparent fill)
		// + two short hands inside. Placed centered in the 7x5 box.
		const int16_t ox = (StatusBoxW - 5) / 2;
		const int16_t oy = 0;
		lv_obj_t* ring = lv_obj_create(statusBox);
		lv_obj_remove_style_all(ring);
		lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_size(ring, 5, 5);
		lv_obj_set_pos(ring, ox, oy);
		lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
		lv_obj_set_style_radius(ring, 99, 0);
		lv_obj_set_style_border_width(ring, 1, 0);
		lv_obj_set_style_border_color(ring, dimC, 0);
		lv_obj_set_style_border_opa(ring, LV_OPA_COVER, 0);
		// Two minute/hour hand "pixels" inside the ring. The center
		// of the 5x5 ring (with 1px border) is at (2, 2); we put the
		// 12 o'clock pixel at (2, 1) and the 3 o'clock pixel at
		// (3, 2) for a recognizable analog-clock silhouette.
		statusPixel(statusBox, ox + 2, oy + 1, dimC);
		statusPixel(statusBox, ox + 3, oy + 2, dimC);
		return;
	}

	// Tick glyph (5x4) used by both Sent and Delivered. The shape:
	//   col: 0 1 2 3 4
	//   r=0: .  .  .  .  X
	//   r=1: .  .  .  X  .
	//   r=2: X  .  X  .  .
	//   r=3: .  X  .  .  .
	// Vertex of the V is at (1, 3); the right wing climbs to (4, 0).
	auto drawTick = [this](int16_t ox, int16_t oy, lv_color_t c){
		statusPixel(statusBox, ox + 4, oy + 0, c);
		statusPixel(statusBox, ox + 3, oy + 1, c);
		statusPixel(statusBox, ox + 0, oy + 2, c);
		statusPixel(statusBox, ox + 2, oy + 2, c);
		statusPixel(statusBox, ox + 1, oy + 3, c);
	};

	if(status == Status::Sent){
		// Single tick - draw left-aligned in the 7x5 box. The right
		// 2 px column is empty padding so a Sent and Delivered icon
		// share the same right edge for clean stacking.
		drawTick(0, 1, dimC);
		return;
	}

	if(status == Status::Delivered){
		// Double tick - second tick shifted +2 px so the two V's
		// overlap by 3 columns (col 2..4 of the first tick are also
		// col 0..2 of the second). Total width = 7 px, exactly the
		// status-box width.
		drawTick(0, 1, goodC);
		drawTick(2, 1, goodC);
		return;
	}
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
	// max(timestamp, status) line if either is shown). Set explicitly so
	// a flex-column parent stacks our row at exactly the right height.
	lv_coord_t totalH = bubbleH;
	if(showTail) totalH += TailHeight;

	const bool showStatus = (variant == Variant::Sent)
							&& (status != Status::None);

	// Pre-measure the bottom-line components so we can right-align the
	// timestamp + status pair with a single shared baseline (matters
	// when one is taller than the other - the timestamp label uses
	// pixelbasic7 which is 7 px tall while the statusBox is 5 px).
	lv_coord_t tsW = 0;
	lv_coord_t tsH = 0;
	if(hasTimestamp){
		lv_obj_update_layout(timestampLabel);
		tsW = lv_obj_get_width(timestampLabel);
		tsH = lv_obj_get_height(timestampLabel);
	}
	const lv_coord_t stW = showStatus ? StatusBoxW : 0;
	const lv_coord_t stH = showStatus ? StatusBoxH : 0;

	const lv_coord_t lineY = totalH + TimestampGap;
	const lv_coord_t lineH = (tsH > stH) ? tsH : stH;

	if(hasTimestamp){
		// Sent: status icon sits to the RIGHT of the timestamp, both
		//       anchored to the bubble's right edge (matches the
		//       "12:34 ✓✓" reading order most retro phones used).
		// Received: timestamp left-aligned to the bubble's left edge;
		//           no status icon is ever shown for Received variants.
		lv_coord_t tsX;
		const lv_coord_t tsY = lineY + (lineH - tsH);

		if(sent){
			lv_coord_t rightEdge = bubbleX + bubbleW;
			if(showStatus) rightEdge -= stW + StatusGap;
			tsX = rightEdge - tsW;
			if(tsX < SideMargin) tsX = SideMargin;
		}else{
			tsX = bubbleX;
		}
		lv_obj_set_pos(timestampLabel, tsX, tsY);
	}

	if(showStatus){
		// Anchor the status box's right edge to the bubble's right
		// edge so the indicator hugs the same screen-edge column the
		// bubble does. When a timestamp is shown it appears to the
		// status box's left (handled above).
		lv_coord_t stX = (bubbleX + bubbleW) - stW;
		if(stX < SideMargin) stX = SideMargin;
		const lv_coord_t stY = lineY + (lineH - stH);
		lv_obj_set_pos(statusBox, stX, stY);
	}

	if(hasTimestamp || showStatus){
		totalH = lineY + lineH;
	}

	lv_obj_set_height(obj, totalH);
}
