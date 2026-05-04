#include "PhoneConfettiOverlay.h"

// MAKERphone retro palette - kept identical with every other Phone*
// widget (PhoneStatusBar, PhoneSoftKeyBar, PhoneClockFace, PhoneIconTile,
// PhoneSynthwaveBg, PhoneMenuGrid, PhoneDialerKey, PhoneDialerPad,
// PhonePixelAvatar, PhoneChatBubble, PhoneSignalIcon, PhoneBatteryIcon,
// PhoneNotificationToast) so the confetti reads as a flourish that
// belongs to the same device. Duplicated rather than centralised at
// this small scale - if the palette ever moves out of the widgets,
// every widget moves together.
#define MP_ACCENT      lv_color_make(255, 140,  30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)   // cyan
#define MP_TEXT        lv_color_make(255, 220, 180)   // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)   // dim purple

// Per-piece x-offset is precomputed to spread 14 pieces across the
// 160 px width without two pieces stacking. The first piece sits at
// x=8, every subsequent piece advances by (~152 / PieceCount) px.
// A small per-piece jitter (idx * 7 % 5) keeps the spread from
// reading as a perfect grid.
static lv_coord_t pieceX(uint8_t idx) {
	const lv_coord_t step = static_cast<lv_coord_t>(
		(PhoneConfettiOverlay::OverlayWidth - 16)
		/ PhoneConfettiOverlay::PieceCount);
	const lv_coord_t jitter = static_cast<lv_coord_t>((idx * 7u) % 5u);
	const lv_coord_t x = 8 + idx * step + jitter;
	return x;
}

// Stamp four palette tints down the pool in sequence so every
// neighbouring trio of pieces reads as a varied colour mix.
static lv_color_t pieceColor(uint8_t idx) {
	switch(idx & 0x03u) {
		case 0:  return MP_ACCENT;
		case 1:  return MP_HIGHLIGHT;
		case 2:  return MP_TEXT;
		default: return MP_LABEL_DIM;
	}
}

// Per-piece fall duration ramps linearly between FallSlowMs (idx 0)
// and FallFastMs (idx PieceCount-1) so the slowest and fastest
// pieces are visually distinguishable. Values outside the bracket
// are clamped on the static range above.
static uint32_t pieceDuration(uint8_t idx) {
	const uint32_t span = PhoneConfettiOverlay::FallSlowMs
	                    - PhoneConfettiOverlay::FallFastMs;
	const uint32_t denom = (PhoneConfettiOverlay::PieceCount > 1u)
	                     ? (PhoneConfettiOverlay::PieceCount - 1u) : 1u;
	const uint32_t step = (span * idx) / denom;
	return PhoneConfettiOverlay::FallSlowMs - step;
}

// Per-piece start delay - spread pieces over [0..StaggerMaxMs] so
// the field reads as natural confetti rather than a synchronised
// curtain. The 'reverse-stride' (PieceCount - idx) keeps the first
// few pieces visible at frame 0 instead of leaving the screen empty
// for the first second.
static uint32_t pieceDelay(uint8_t idx) {
	const uint32_t denom = (PhoneConfettiOverlay::PieceCount > 1u)
	                     ? (PhoneConfettiOverlay::PieceCount - 1u) : 1u;
	const uint32_t span = PhoneConfettiOverlay::StaggerMaxMs;
	return (span * idx) / denom;
}

// ----- ctor / dtor -------------------------------------------------------

PhoneConfettiOverlay::PhoneConfettiOverlay(lv_obj_t* parent) : LVObject(parent) {
	for(uint8_t i = 0; i < PieceCount; ++i) pieces[i] = nullptr;

	// Full-screen transparent container. Sits on top of every other
	// widget so the falling pieces are visible above the clock face,
	// status bar and soft-key bar without an opacity dance on the
	// host. Hit-testing is disabled so the host's input listeners
	// keep receiving every key press while the overlay is on screen.
	lv_obj_remove_style_all(obj);
	lv_obj_set_size(obj, OverlayWidth, OverlayHeight);
	lv_obj_set_pos(obj, 0, 0);
	lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(obj, 0, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_outline_width(obj, 0, 0);

	buildPieces();

	// Hidden until start() is called - matches the PhoneNotificationToast
	// pattern (built off-screen, surfaces on first show()).
	lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

PhoneConfettiOverlay::~PhoneConfettiOverlay() {
	// Cancel any in-flight animation before LVGL frees the piece
	// objects from underneath us. Without this, an animation that
	// fires on the same tick as our parent's destructor could
	// dereference a freed lv_obj.
	for(uint8_t i = 0; i < PieceCount; ++i) {
		if(pieces[i] != nullptr) lv_anim_del(pieces[i], fallExec);
	}
}

// ----- builders ----------------------------------------------------------

void PhoneConfettiOverlay::buildPieces() {
	for(uint8_t i = 0; i < PieceCount; ++i) {
		lv_obj_t* p = lv_obj_create(obj);
		lv_obj_remove_style_all(p);
		lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(p, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_set_size(p, PieceSize, PieceSize);
		lv_obj_set_style_radius(p, 0, 0);
		lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
		lv_obj_set_style_bg_color(p, pieceColor(i), 0);
		lv_obj_set_style_border_width(p, 0, 0);
		// Park each piece just above the visible area so the very
		// first frame of any animation pops the piece in cleanly.
		lv_obj_set_pos(p, pieceX(i), -static_cast<lv_coord_t>(PieceSize));
		pieces[i] = p;
	}
}

// ----- public api --------------------------------------------------------

void PhoneConfettiOverlay::start() {
	// Idempotent restart - cancel any in-flight animation, surface
	// the overlay, then relaunch every piece from frame 0.
	for(uint8_t i = 0; i < PieceCount; ++i) {
		if(pieces[i] != nullptr) lv_anim_del(pieces[i], fallExec);
	}
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
	for(uint8_t i = 0; i < PieceCount; ++i) launchPiece(i);
	active = true;
}

void PhoneConfettiOverlay::stop() {
	for(uint8_t i = 0; i < PieceCount; ++i) {
		if(pieces[i] != nullptr) lv_anim_del(pieces[i], fallExec);
	}
	lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
	active = false;
}

// ----- private helpers ---------------------------------------------------

void PhoneConfettiOverlay::launchPiece(uint8_t idx) {
	if(idx >= PieceCount || pieces[idx] == nullptr) return;

	lv_anim_t a;
	lv_anim_init(&a);
	lv_anim_set_var(&a, pieces[idx]);
	// Range: spawn a touch above the screen, exit just below so the
	// piece visibly reaches the bottom edge before resetting.
	lv_anim_set_values(&a,
		-static_cast<int32_t>(PieceSize),
		static_cast<int32_t>(OverlayHeight + PieceSize));
	lv_anim_set_time(&a, pieceDuration(idx));
	lv_anim_set_delay(&a, pieceDelay(idx));
	lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
	lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
	lv_anim_set_exec_cb(&a, fallExec);
	lv_anim_start(&a);
}

// ----- animation callback ------------------------------------------------

void PhoneConfettiOverlay::fallExec(void* var, int32_t v) {
	// v is in [-PieceSize .. OverlayHeight + PieceSize]. We bind it
	// straight to the piece's y so the animation engine handles the
	// timing, easing and reset.
	lv_obj_set_y(static_cast<lv_obj_t*>(var), static_cast<lv_coord_t>(v));
}
