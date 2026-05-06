#include "PhoneMemoryAudit.h"

#include <Input/Input.h>
#include <Pins.hpp>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"
#include "PhoneAppStubScreen.h"

// MAKERphone retro palette - inlined per the established pattern in
// this codebase (see PhoneAboutScreen.cpp, PhoneFirmwareInfoScreen.cpp).
// Cyan for the caption (informational), warm cream for the values
// (the payload the developer is actually here to read), dim purple
// for the small section labels above each value -- exactly the
// layout PhoneAboutScreen uses, deliberately so the diagnostics
// pages read as a family.
#define MP_HIGHLIGHT    lv_color_make(122, 232, 255)   // cyan caption
#define MP_TEXT         lv_color_make(255, 220, 180)   // warm cream value
#define MP_LABEL_DIM    lv_color_make(170, 140, 200)   // dim purple section label
#define MP_ACCENT       lv_color_make(255, 140,  30)   // sunset orange (PASS / DRIFT verdict)

// Body geometry. Tighter than PhoneAboutScreen because we have six
// label/value pairs to fit in the 96 px content window between the
// status bar (y = 0..10) and the soft-key bar (y = 118..128). Each
// row is 8 px tall: 7 px for the pixelbasic7 glyph + 1 px halo. The
// label is rendered at column 4, the value at column 56 (just past
// the longest label "BEFORE:" + 1 px) so the two columns line up
// like a labelled report rather than two free-floating columns.
static constexpr lv_coord_t kBodyX           = 4;
static constexpr lv_coord_t kValueX          = 56;
static constexpr lv_coord_t kBodyW           = 152;
static constexpr lv_coord_t kBodyTopY        = 22;
static constexpr lv_coord_t kRowH            = 14;
static constexpr lv_coord_t kRowCount        = 6;

// Audit-iteration kinds. Indices map onto runIteration() and the
// "CYCLE" row label. Kept as an enum so the dispatch reads
// kind-by-kind rather than as bare integers, which makes it easy
// to grow the audit set in a future session without renumbering.
namespace {

enum AuditKind : uint8_t {
	KindWallpaper      = 0,   // PhoneSynthwaveBg under a transient parent
	KindChromeBars     = 1,   // PhoneStatusBar + PhoneSoftKeyBar + labels
	KindStubScreen     = 2,   // Full PhoneAppStubScreen new/delete
};

constexpr const char* kCycleNames[PhoneMemoryAudit::kKindCount] = {
	"PhoneSynthwaveBg",
	"Chrome+Labels",
	"PhoneAppStub",
};

} // namespace

PhoneMemoryAudit::PhoneMemoryAudit()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr),
		  captionLabel(nullptr),
		  beforeLabel(nullptr),
		  beforeValue(nullptr),
		  nowLabel(nullptr),
		  nowValue(nullptr),
		  deltaLabel(nullptr),
		  deltaValue(nullptr),
		  iterLabel(nullptr),
		  iterValue(nullptr),
		  cycleLabel(nullptr),
		  cycleValue(nullptr),
		  statusLabel(nullptr),
		  statusValue(nullptr),
		  tickTimer(nullptr),
		  startHeap(0),
		  iter(0),
		  lastKind(0),
		  state(State::Running) {

	// Full-screen container, no scrollbars, no inner padding -- same
	// blank-canvas pattern PhoneAboutScreen / PhoneFirmwareInfoScreen
	// use. Children below either pin themselves with IGNORE_LAYOUT or
	// are LVGL primitives we anchor manually on the 160x128 display.
	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	// Wallpaper at the bottom of the z-order so the labels overlay it
	// cleanly. The audit page still feels like part of the MAKERphone
	// family rather than a debug terminal.
	wallpaper = new PhoneSynthwaveBg(obj);

	// Standard signal | clock | battery bar -- developers tend to leave
	// this page running while they wander away from the bench, so the
	// time/battery readout is genuinely useful here.
	statusBar = new PhoneStatusBar(obj);

	buildCaption();
	buildBody();

	// Two-action softkey bar: PAUSE on the left (toggles to RUN when
	// the user pauses the audit), BACK on the right. refreshSoftKeys()
	// keeps the labels in sync with the State enum.
	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("PAUSE");
	softKeys->setRight("BACK");
}

PhoneMemoryAudit::~PhoneMemoryAudit() {
	// Clean up the work timer if onStop() did not (e.g. the screen is
	// destroyed without ever being started -- this happens for the
	// memory-audit page itself when, ironically, this audit's tested
	// against). All other children are parented to obj and freed by
	// the LVScreen base destructor.
	if(tickTimer != nullptr) {
		lv_timer_del(tickTimer);
		tickTimer = nullptr;
	}
}

void PhoneMemoryAudit::onStart() {
	Input::getInstance()->addListener(this);

	// Seed the baseline now (not in the ctor) so any allocations the
	// audit page itself caused are already accounted for. The on-screen
	// "DELTA" then reflects only the audit-loop churn.
	startHeap = (uint32_t) ESP.getFreeHeap();
	iter      = 0;
	lastKind  = 0;
	state     = State::Running;

	// One-line console banner so a tethered developer can timestamp
	// the run from the USB serial monitor without having to squint at
	// the on-screen labels. Matches the [tag] pattern other Phase-N
	// audit-style services use (see PhoneRingtoneEngine logging).
	Serial.printf("[mem-audit] start  baseline=%u B\n", (unsigned) startHeap);

	// Initial paint so the user sees real numbers immediately rather
	// than blanks until the first tick fires.
	refreshNowFields();
	refreshIterFields(0);
	refreshStatusField();
	refreshSoftKeys();

	// Idempotent: if a previous onStart() left a timer running we
	// reuse it rather than stacking a second one. lv_timer_create
	// pins user_data to `this` so the static callback can route
	// back to the correct instance.
	if(tickTimer == nullptr) {
		tickTimer = lv_timer_create(onTickTimer, kTickMs, this);
	}
}

void PhoneMemoryAudit::onStop() {
	Input::getInstance()->removeListener(this);
	if(tickTimer != nullptr) {
		lv_timer_del(tickTimer);
		tickTimer = nullptr;
	}
}

// ----- builders --------------------------------------------------------

void PhoneMemoryAudit::buildCaption() {
	// "MEM AUDIT" caption in pixelbasic7 cyan, just under the status
	// bar -- same anchor pattern PhoneAboutScreen uses for "ABOUT" and
	// PhoneFirmwareInfoScreen uses for "FW INFO". Kept short so it
	// does not crowd the centred title strip on the 160 px display.
	captionLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(captionLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(captionLabel, MP_HIGHLIGHT, 0);
	lv_label_set_text(captionLabel, "MEM AUDIT");
	lv_obj_set_align(captionLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(captionLabel, 12);
}

void PhoneMemoryAudit::buildBody() {
	// Inline helper that builds a single label/value pair at a fixed
	// row index. Same pattern PhoneAboutScreen / PhoneFirmwareInfoScreen
	// use; kept inline rather than as a method because this is the only
	// place that needs it.
	auto makePair = [this](lv_obj_t** outLabel, lv_obj_t** outValue,
						   const char* labelText, const char* initialValue,
						   lv_color_t valueColor, lv_coord_t rowIdx) {
		const lv_coord_t y = kBodyTopY + rowIdx * kRowH;

		lv_obj_t* lab = lv_label_create(obj);
		lv_obj_set_style_text_font(lab, &pixelbasic7, 0);
		lv_obj_set_style_text_color(lab, MP_LABEL_DIM, 0);
		lv_label_set_text(lab, labelText);
		lv_obj_set_pos(lab, kBodyX, y);

		lv_obj_t* val = lv_label_create(obj);
		lv_obj_set_style_text_font(val, &pixelbasic7, 0);
		lv_obj_set_style_text_color(val, valueColor, 0);
		// LV_LABEL_LONG_DOT mirrors the rest of the Phase-J / Phase-S
		// diagnostics screens: if a future build accidentally feeds an
		// over-long string into one of these slots the label truncates
		// with an ellipsis rather than wrapping into the next row.
		lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
		lv_obj_set_width(val, kBodyW - kValueX);
		lv_label_set_text(val, initialValue);
		lv_obj_set_pos(val, kValueX, y);

		*outLabel = lab;
		*outValue = val;
	};

	makePair(&beforeLabel, &beforeValue, "BEFORE",  "...",          MP_TEXT,    0);
	makePair(&nowLabel,    &nowValue,    "NOW",     "...",          MP_TEXT,    1);
	makePair(&deltaLabel,  &deltaValue,  "DELTA",   "+0 B",         MP_TEXT,    2);
	makePair(&iterLabel,   &iterValue,   "ITER",    "0 / 1000",     MP_TEXT,    3);
	makePair(&cycleLabel,  &cycleValue,  "CYCLE",   "-",            MP_TEXT,    4);
	// Status uses the accent (sunset orange) on the value column so
	// the verdict (RUNNING / PAUSED / DONE PASS / DONE DRIFT N B)
	// pops against the otherwise warm-cream report.
	makePair(&statusLabel, &statusValue, "STATUS",  "RUNNING",      MP_ACCENT,  5);
}

// ----- refresh helpers -------------------------------------------------

void PhoneMemoryAudit::refreshNowFields() {
	char buf[32];

	const uint32_t freeNow = (uint32_t) ESP.getFreeHeap();

	snprintf(buf, sizeof(buf), "%u B", (unsigned) startHeap);
	if(beforeValue) lv_label_set_text(beforeValue, buf);

	snprintf(buf, sizeof(buf), "%u B", (unsigned) freeNow);
	if(nowValue) lv_label_set_text(nowValue, buf);

	// Compute the delta as a *signed* integer so the on-screen sign is
	// honest about whether the heap shrank (negative -> a leak) or
	// grew (positive -> we are recovering memory the page came in
	// holding). A free heap that GREW between start and now is fine
	// from a leak-audit perspective so we render it with a leading '+'
	// to make that intent obvious.
	const int32_t delta = (int32_t) freeNow - (int32_t) startHeap;
	if(delta >= 0) {
		snprintf(buf, sizeof(buf), "+%ld B", (long) delta);
	} else {
		snprintf(buf, sizeof(buf), "%ld B", (long) delta);
	}
	if(deltaValue) lv_label_set_text(deltaValue, buf);
}

void PhoneMemoryAudit::refreshIterFields(uint8_t kindUsed) {
	char buf[32];
	snprintf(buf, sizeof(buf), "%lu / %lu",
			 (unsigned long) iter, (unsigned long) kTotalIterations);
	if(iterValue) lv_label_set_text(iterValue, buf);

	const char* name = (kindUsed < kKindCount) ? kCycleNames[kindUsed] : "-";
	if(cycleValue) lv_label_set_text(cycleValue, name);
}

void PhoneMemoryAudit::refreshStatusField() {
	if(statusValue == nullptr) return;

	switch(state) {
		case State::Running:
			lv_label_set_text(statusValue, "RUNNING");
			break;
		case State::Paused:
			lv_label_set_text(statusValue, "PAUSED");
			break;
		case State::Done: {
			// Recompute the absolute delta now (rather than caching it
			// from refreshNowFields) so the verdict text is always
			// derived from the most recent heap reading. ESP.getFreeHeap
			// returns size_t on Arduino; we coerce to int32_t for the
			// signed comparison and to uint32_t for the threshold.
			const uint32_t freeNow = (uint32_t) ESP.getFreeHeap();
			const int32_t  delta   = (int32_t) freeNow - (int32_t) startHeap;
			const uint32_t adelta  = (delta < 0) ? (uint32_t) -delta : (uint32_t) delta;

			char buf[32];
			if(adelta < kPassThresholdBytes) {
				snprintf(buf, sizeof(buf), "DONE PASS");
			} else {
				snprintf(buf, sizeof(buf), "DONE DRIFT %lu B",
						 (unsigned long) adelta);
			}
			lv_label_set_text(statusValue, buf);
			break;
		}
	}
}

void PhoneMemoryAudit::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	switch(state) {
		case State::Running: softKeys->setLeft("PAUSE"); break;
		case State::Paused:  softKeys->setLeft("RUN");   break;
		case State::Done:    softKeys->setLeft("");      break;
	}
	softKeys->setRight("BACK");
}

// ----- audit core ------------------------------------------------------

void PhoneMemoryAudit::runIteration(uint8_t kind) {
	switch(kind) {
		case KindWallpaper: {
			// Build a transient host container as a child of obj. We
			// position it well off-screen so the LVGL renderer culls
			// it without ever rasterising it onto the framebuffer --
			// the audit must not flash the user every 50 ms.
			lv_obj_t* host = lv_obj_create(obj);
			lv_obj_add_flag(host, LV_OBJ_FLAG_IGNORE_LAYOUT);
			lv_obj_set_size(host, 160, 128);
			lv_obj_set_pos(host, -300, -300);
			lv_obj_set_scrollbar_mode(host, LV_SCROLLBAR_MODE_OFF);
			lv_obj_set_style_pad_all(host, 0, 0);
			lv_obj_set_style_bg_opa(host, LV_OPA_TRANSP, 0);
			lv_obj_clear_flag(host, LV_OBJ_FLAG_SCROLLABLE);

			// PhoneSynthwaveBg is the single heaviest Phone* widget --
			// it allocates a sky, a ground plane, a sun, scan lines, a
			// horizontal grid, sun rays, and a star field, so cycling
			// it stresses the LVGL primitive allocator hard.
			new PhoneSynthwaveBg(host);

			lv_obj_del(host);
			break;
		}

		case KindChromeBars: {
			// Same transient host pattern, but populated with the
			// "chrome" widgets every Phone* screen carries -- a status
			// bar, a softkey bar, and a couple of labels with the same
			// font + colour styling the body labels use. This pattern
			// mirrors the typical screen drop more closely than the
			// wallpaper-only kind above.
			lv_obj_t* host = lv_obj_create(obj);
			lv_obj_add_flag(host, LV_OBJ_FLAG_IGNORE_LAYOUT);
			lv_obj_set_size(host, 160, 128);
			lv_obj_set_pos(host, -300, -300);
			lv_obj_set_scrollbar_mode(host, LV_SCROLLBAR_MODE_OFF);
			lv_obj_set_style_pad_all(host, 0, 0);
			lv_obj_set_style_bg_opa(host, LV_OPA_TRANSP, 0);
			lv_obj_clear_flag(host, LV_OBJ_FLAG_SCROLLABLE);

			new PhoneStatusBar(host);
			new PhoneSoftKeyBar(host);

			// A trio of labels stands in for the typical body text on
			// a real Phone* screen -- caption, label, value -- so the
			// LVGL label allocator is exercised on the same lifetime
			// edge as the body labels of this audit page.
			for(uint8_t k = 0; k < 3; ++k) {
				lv_obj_t* lbl = lv_label_create(host);
				lv_obj_set_style_text_font(lbl, &pixelbasic7, 0);
				lv_obj_set_style_text_color(lbl, MP_TEXT, 0);
				lv_label_set_text(lbl, "AUDIT");
				lv_obj_set_pos(lbl, 4, 24 + k * 10);
			}

			lv_obj_del(host);
			break;
		}

		case KindStubScreen: {
			// Full LVScreen lifecycle: lv_group_create from the
			// LVScreen ctor, the LV_EVENT_SCREEN_LOADED callback wiring,
			// the children-of-obj cascade on destruct. We never call
			// start() on the stub so it never becomes the current
			// screen (and therefore never reroutes input or animates)
			// -- the audit is interested only in the new/delete pair,
			// not in actually displaying the stub.
			PhoneAppStubScreen* stub = new PhoneAppStubScreen("AUDIT");
			delete stub;
			break;
		}

		default:
			// Defensive: an out-of-range kind is a programming error,
			// so we count the iteration but otherwise do nothing
			// rather than scribble random allocations.
			break;
	}
}

void PhoneMemoryAudit::runBatch() {
	// Drain kBatchSize iterations per timer fire so the page completes
	// kTotalIterations in a few seconds. We rotate kinds round-robin
	// over the batch so the cycle label visibly changes between ticks
	// (it would otherwise stay on a single kind for the entire batch).
	uint8_t kindUsed = lastKind;
	for(uint32_t b = 0; b < kBatchSize && iter < kTotalIterations; ++b) {
		kindUsed = (uint8_t) (iter % (uint32_t) kKindCount);
		runIteration(kindUsed);
		++iter;
	}
	lastKind = kindUsed;

	refreshNowFields();
	refreshIterFields(lastKind);

	if(iter >= kTotalIterations) {
		finish();
	}
}

void PhoneMemoryAudit::finish() {
	// Stop the timer first so a tick that fires between the state
	// transition and the soft-key relabel cannot try to run another
	// batch on top of a Done audit.
	if(tickTimer != nullptr) {
		lv_timer_del(tickTimer);
		tickTimer = nullptr;
	}

	state = State::Done;
	refreshNowFields();
	refreshStatusField();
	refreshSoftKeys();

	// Tag the result over the USB serial console so a tethered
	// developer can copy/paste the verdict into a bug ticket without
	// retyping the on-screen numbers. The signed delta is rendered
	// with the same '+' / '-' convention as the "DELTA" row above.
	const uint32_t freeNow = (uint32_t) ESP.getFreeHeap();
	const int32_t  delta   = (int32_t) freeNow - (int32_t) startHeap;
	const uint32_t adelta  = (delta < 0) ? (uint32_t) -delta : (uint32_t) delta;
	const char*    verdict = (adelta < kPassThresholdBytes) ? "PASS" : "DRIFT";
	Serial.printf("[mem-audit] done   iter=%lu before=%u now=%u delta=%+ld %s\n",
				  (unsigned long) iter,
				  (unsigned) startHeap,
				  (unsigned) freeNow,
				  (long) delta,
				  verdict);
}

// ----- timer callback --------------------------------------------------

void PhoneMemoryAudit::onTickTimer(lv_timer_t* timer) {
	if(timer == nullptr || timer->user_data == nullptr) return;
	auto* self = static_cast<PhoneMemoryAudit*>(timer->user_data);
	if(self->state != State::Running) return;
	self->runBatch();
}

// ----- input -----------------------------------------------------------

void PhoneMemoryAudit::buttonPressed(uint i) {
	switch(i) {
		case BTN_ENTER: {
			// Toggle RUN <-> PAUSE while the audit is in progress;
			// once finished, ENTER is a no-op (the audit can be
			// re-run by popping back to the dialer and typing the
			// service code again -- there is no point repeating a
			// finished verdict in place).
			if(softKeys) softKeys->flashLeft();
			if(state == State::Running) {
				state = State::Paused;
			} else if(state == State::Paused) {
				state = State::Running;
			}
			refreshStatusField();
			refreshSoftKeys();
			break;
		}

		case BTN_BACK:
			// Flash the BACK softkey for tactile feedback then pop. An
			// abort mid-run is fine: the audit's transient hosts have
			// already been deleted at the end of each runIteration()
			// call, and the screen's own children are cleaned up by
			// the LVScreen base cascade.
			if(softKeys) softKeys->flashRight();
			pop();
			break;

		default:
			break;
	}
}
