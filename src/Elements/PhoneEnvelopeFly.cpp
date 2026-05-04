#include "PhoneEnvelopeFly.h"

// MAKERphone retro palette - kept identical with every other Phone*
// widget (PhoneStatusBar, PhoneSoftKeyBar, PhoneClockFace, PhoneIconTile,
// PhoneSynthwaveBg, PhoneMenuGrid, PhoneDialerKey, PhoneDialerPad,
// PhonePixelAvatar, PhoneChatBubble, PhoneSignalIcon, PhoneBatteryIcon,
// PhoneNotificationToast, PhoneConfettiOverlay) so the envelope reads
// as a flourish that belongs to the same device. Duplicated rather
// than centralised at this small scale - if the palette ever moves
// out of the widgets, every widget moves together.
#define MP_ACCENT      lv_color_make(255, 140,  30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)   // cyan
#define MP_DIM         lv_color_make( 70,  56, 100)   // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)   // warm cream

// Flight start anchor - bottom-left of the screen, just above the
// soft-key bar, so the envelope feels like it's leaving the message
// composer. Anchor is the carrier's top-left corner.
static constexpr lv_coord_t StartX =   8;
static constexpr lv_coord_t StartY =  92;

// Flight end anchor - top-right corner, just under the status bar's
// signal icon so the envelope reads as "headed up to the antenna".
static constexpr lv_coord_t EndX   = static_cast<lv_coord_t>(
	PhoneEnvelopeFly::OverlayWidth - PhoneEnvelopeFly::SpriteWidth - 4);
static constexpr lv_coord_t EndY   =  10;

// Sparkle anchor - centred on the envelope's destination so the
// "puff" emanates from where the envelope vanished.
static constexpr lv_coord_t SparkleCX = static_cast<lv_coord_t>(
	EndX + PhoneEnvelopeFly::SpriteWidth / 2);
static constexpr lv_coord_t SparkleCY = static_cast<lv_coord_t>(
	EndY + PhoneEnvelopeFly::SpriteHeight / 2);

// ----- ctor / dtor -------------------------------------------------------

PhoneEnvelopeFly::PhoneEnvelopeFly(lv_obj_t* parent) : LVObject(parent) {
	// Full-screen transparent container. Sits on top of every other
	// widget so the envelope is visible above the convo bubbles, the
	// status bar and the soft-key bar without an opacity dance on
	// the host. Hit-testing is disabled so input listeners keep
	// receiving every key press while the flight is on screen.
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

	buildSprite();

	// Hidden until start() is called - matches the PhoneConfettiOverlay
	// pattern (built off-screen, surfaces on first start()).
	lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

PhoneEnvelopeFly::~PhoneEnvelopeFly() {
	// Cancel every in-flight animation before LVGL frees the carrier
	// and sparkle from underneath us. Without this, an animation that
	// fires on the same tick as our parent's destructor could
	// dereference a freed lv_obj.
	if(carrier != nullptr) {
		lv_anim_del(carrier, flyXExec);
		lv_anim_del(carrier, flyYExec);
		lv_anim_del(carrier, flyOpaExec);
	}
	if(sparkle != nullptr) {
		lv_anim_del(sparkle, sparkleSizeExec);
		lv_anim_del(sparkle, sparkleOpaExec);
	}
}

// ----- builders ----------------------------------------------------------

void PhoneEnvelopeFly::buildSprite() {
	// Carrier: invisible 18x12 box that owns the envelope rectangles
	// and translates as one piece. We park it just below the visible
	// area so the very first frame of the flight pops it in cleanly.
	carrier = lv_obj_create(obj);
	lv_obj_remove_style_all(carrier);
	lv_obj_clear_flag(carrier, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(carrier, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(carrier, SpriteWidth, SpriteHeight);
	lv_obj_set_style_bg_opa(carrier, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(carrier, 0, 0);
	lv_obj_set_style_pad_all(carrier, 0, 0);
	lv_obj_set_pos(carrier, StartX, StartY);

	// Shadow: a 1-px MP_DIM sliver under the body, offset 1 px down
	// and 1 px right so the envelope reads as floating above the
	// background rather than pasted on. Drawn first so the body
	// renders on top of it.
	shadow = lv_obj_create(carrier);
	lv_obj_remove_style_all(shadow);
	lv_obj_clear_flag(shadow, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(shadow, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(shadow, SpriteWidth, SpriteHeight);
	lv_obj_set_style_radius(shadow, 0, 0);
	lv_obj_set_style_bg_opa(shadow, LV_OPA_60, 0);
	lv_obj_set_style_bg_color(shadow, MP_DIM, 0);
	lv_obj_set_style_border_width(shadow, 0, 0);
	lv_obj_set_pos(shadow, 1, 1);

	// Body: warm cream rectangle, slightly inset so the shadow
	// bleeds through at the bottom edge.
	body = lv_obj_create(carrier);
	lv_obj_remove_style_all(body);
	lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(body, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(body, SpriteWidth - 1, SpriteHeight - 1);
	lv_obj_set_style_radius(body, 0, 0);
	lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(body, MP_TEXT, 0);
	lv_obj_set_style_border_width(body, 1, 0);
	lv_obj_set_style_border_color(body, MP_DIM, 0);
	lv_obj_set_pos(body, 0, 0);

	// Flap: two thin diagonal rectangles approximating the V seam of
	// an opened envelope flap. Pixel-art on a 160x128 display can't
	// render true diagonals so we cheat with two short bars - the
	// eye fills in the V at this scale.
	flapL = lv_obj_create(carrier);
	lv_obj_remove_style_all(flapL);
	lv_obj_clear_flag(flapL, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(flapL, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(flapL, (SpriteWidth / 2) - 1, 1);
	lv_obj_set_style_radius(flapL, 0, 0);
	lv_obj_set_style_bg_opa(flapL, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(flapL, MP_DIM, 0);
	lv_obj_set_style_border_width(flapL, 0, 0);
	lv_obj_set_pos(flapL, 1, 2);

	flapR = lv_obj_create(carrier);
	lv_obj_remove_style_all(flapR);
	lv_obj_clear_flag(flapR, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(flapR, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(flapR, (SpriteWidth / 2) - 1, 1);
	lv_obj_set_style_radius(flapR, 0, 0);
	lv_obj_set_style_bg_opa(flapR, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(flapR, MP_DIM, 0);
	lv_obj_set_style_border_width(flapR, 0, 0);
	lv_obj_set_pos(flapR, SpriteWidth / 2, 2);

	// Seam: a single MP_ACCENT orange pixel at the V vertex - the
	// distinctive "wax-seal-ish" highlight that makes the envelope
	// feel like a sent letter rather than a generic rectangle.
	seam = lv_obj_create(carrier);
	lv_obj_remove_style_all(seam);
	lv_obj_clear_flag(seam, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(seam, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(seam, 2, 2);
	lv_obj_set_style_radius(seam, 0, 0);
	lv_obj_set_style_bg_opa(seam, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(seam, MP_ACCENT, 0);
	lv_obj_set_style_border_width(seam, 0, 0);
	lv_obj_set_pos(seam, (SpriteWidth / 2) - 1, 4);

	// Sparkle: parented to the overlay (not the carrier) so it
	// stays anchored at the destination after the envelope dissolves.
	// A circular MP_HIGHLIGHT cyan ring that expands from 0 to
	// SparkleMaxR radius while fading out.
	sparkle = lv_obj_create(obj);
	lv_obj_remove_style_all(sparkle);
	lv_obj_clear_flag(sparkle, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(sparkle, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(sparkle, 1, 1);
	lv_obj_set_style_radius(sparkle, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_opa(sparkle, LV_OPA_TRANSP, 0);
	lv_obj_set_style_bg_color(sparkle, MP_HIGHLIGHT, 0);
	lv_obj_set_style_border_width(sparkle, 1, 0);
	lv_obj_set_style_border_color(sparkle, MP_HIGHLIGHT, 0);
	lv_obj_set_style_border_opa(sparkle, LV_OPA_COVER, 0);
	lv_obj_set_pos(sparkle, SparkleCX, SparkleCY);
}

// ----- public api --------------------------------------------------------

void PhoneEnvelopeFly::start() {
	if(carrier == nullptr || sparkle == nullptr) return;

	// Idempotent restart - cancel any in-flight animation, surface
	// the overlay, then relaunch every channel from frame 0.
	lv_anim_del(carrier, flyXExec);
	lv_anim_del(carrier, flyYExec);
	lv_anim_del(carrier, flyOpaExec);
	lv_anim_del(sparkle, sparkleSizeExec);
	lv_anim_del(sparkle, sparkleOpaExec);

	// Re-park the carrier at the start anchor and full opacity so a
	// re-trigger doesn't show the residue of the previous flight.
	lv_obj_set_pos(carrier, StartX, StartY);
	lv_obj_set_style_opa(carrier, LV_OPA_COVER, 0);

	// Reset sparkle to invisible 1x1 dot at the destination - the
	// expansion animation grows it from there.
	lv_obj_set_size(sparkle, 1, 1);
	lv_obj_set_pos(sparkle, SparkleCX, SparkleCY);
	lv_obj_set_style_border_opa(sparkle, LV_OPA_TRANSP, 0);

	lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);

	// Channel 1: x-sweep (StartX -> EndX), ease-out so the envelope
	// leaves the composer area quickly and decelerates as it docks
	// at the top-right corner.
	lv_anim_t ax;
	lv_anim_init(&ax);
	lv_anim_set_var(&ax, carrier);
	lv_anim_set_values(&ax, StartX, EndX);
	lv_anim_set_time(&ax, FlyMs);
	lv_anim_set_path_cb(&ax, lv_anim_path_ease_out);
	lv_anim_set_exec_cb(&ax, flyXExec);
	lv_anim_start(&ax);

	// Channel 2: y-climb (StartY -> EndY), ease-in so the envelope
	// "lifts off" slowly before accelerating up. Combined with the
	// ease-out x-sweep this reads as a parabolic arc, no trig needed.
	lv_anim_t ay;
	lv_anim_init(&ay);
	lv_anim_set_var(&ay, carrier);
	lv_anim_set_values(&ay, StartY, EndY);
	lv_anim_set_time(&ay, FlyMs);
	lv_anim_set_path_cb(&ay, lv_anim_path_ease_in);
	lv_anim_set_exec_cb(&ay, flyYExec);
	lv_anim_start(&ay);

	// Channel 3: dissolve. The envelope holds full opacity for the
	// first ~80 % of the flight, then fades to transparent over the
	// last ~20 % so it visibly disappears as the sparkle pops. We
	// implement this with one animation over the full duration that
	// keeps opa pinned high until late, using lv_anim_path_step.
	lv_anim_t ao;
	lv_anim_init(&ao);
	lv_anim_set_var(&ao, carrier);
	lv_anim_set_values(&ao, LV_OPA_COVER, LV_OPA_TRANSP);
	lv_anim_set_time(&ao, FlyMs);
	lv_anim_set_path_cb(&ao, lv_anim_path_step);
	lv_anim_set_exec_cb(&ao, flyOpaExec);
	lv_anim_start(&ao);

	// Channel 4: sparkle radial expansion (1 -> SparkleMaxR*2 px
	// diameter). Delayed by FlyMs so it fires exactly as the envelope
	// dissolves. Uses ease-out so the ring snaps out fast and
	// decelerates - reads as a "delivered" pop.
	lv_anim_t as;
	lv_anim_init(&as);
	lv_anim_set_var(&as, sparkle);
	lv_anim_set_values(&as, 1, SparkleMaxR * 2);
	lv_anim_set_time(&as, SparkleMs);
	lv_anim_set_delay(&as, FlyMs);
	lv_anim_set_path_cb(&as, lv_anim_path_ease_out);
	lv_anim_set_exec_cb(&as, sparkleSizeExec);
	// The size animation runs slightly longer than the opacity one
	// so its ready_cb is the natural place to hide the overlay.
	lv_anim_set_ready_cb(&as, onAllDone);
	lv_anim_start(&as);

	// Channel 5: sparkle opacity. Fades from full to zero over the
	// same window so the ring dissolves as it hits max radius.
	lv_anim_t aso;
	lv_anim_init(&aso);
	lv_anim_set_var(&aso, sparkle);
	lv_anim_set_values(&aso, LV_OPA_COVER, LV_OPA_TRANSP);
	lv_anim_set_time(&aso, SparkleMs);
	lv_anim_set_delay(&aso, FlyMs);
	lv_anim_set_path_cb(&aso, lv_anim_path_linear);
	lv_anim_set_exec_cb(&aso, sparkleOpaExec);
	lv_anim_start(&aso);

	active = true;
}

void PhoneEnvelopeFly::stop() {
	if(carrier != nullptr) {
		lv_anim_del(carrier, flyXExec);
		lv_anim_del(carrier, flyYExec);
		lv_anim_del(carrier, flyOpaExec);
	}
	if(sparkle != nullptr) {
		lv_anim_del(sparkle, sparkleSizeExec);
		lv_anim_del(sparkle, sparkleOpaExec);
	}
	lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
	active = false;
}

// ----- animation callbacks ----------------------------------------------

void PhoneEnvelopeFly::flyXExec(void* var, int32_t v) {
	auto c = static_cast<lv_obj_t*>(var);
	lv_obj_set_x(c, static_cast<lv_coord_t>(v));
}

void PhoneEnvelopeFly::flyYExec(void* var, int32_t v) {
	auto c = static_cast<lv_obj_t*>(var);
	lv_obj_set_y(c, static_cast<lv_coord_t>(v));
}

void PhoneEnvelopeFly::flyOpaExec(void* var, int32_t v) {
	auto c = static_cast<lv_obj_t*>(var);
	lv_obj_set_style_opa(c, static_cast<lv_opa_t>(v), 0);
}

void PhoneEnvelopeFly::sparkleSizeExec(void* var, int32_t v) {
	auto c = static_cast<lv_obj_t*>(var);
	lv_coord_t d = static_cast<lv_coord_t>(v);
	if(d < 1) d = 1;
	// Re-centre on every frame so growth radiates from the
	// destination point rather than expanding out of the top-left.
	lv_obj_set_size(c, d, d);
	lv_obj_set_pos(c,
		static_cast<lv_coord_t>(SparkleCX - d / 2),
		static_cast<lv_coord_t>(SparkleCY - d / 2));
}

void PhoneEnvelopeFly::sparkleOpaExec(void* var, int32_t v) {
	auto c = static_cast<lv_obj_t*>(var);
	lv_obj_set_style_border_opa(c, static_cast<lv_opa_t>(v), 0);
}

void PhoneEnvelopeFly::onAllDone(lv_anim_t* a) {
	// `a->var` is the sparkle lv_obj. Its parent is our overlay
	// container - hop one level up and hide it so the next start()
	// call has a clean canvas.
	auto sp = static_cast<lv_obj_t*>(a->var);
	if(sp == nullptr) return;
	lv_obj_t* overlay = lv_obj_get_parent(sp);
	if(overlay != nullptr) lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
}
