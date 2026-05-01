#include "PhoneBantumi.h"

#include <Input/Input.h>
#include <Pins.hpp>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../Elements/PhoneSynthwaveBg.h"
#include "../Elements/PhoneStatusBar.h"
#include "../Elements/PhoneSoftKeyBar.h"
#include "../Fonts/font.h"

// MAKERphone retro palette - identical to every other Phone* widget so the
// arcade screen slots in beside PhoneBrickBreaker (S75), PhoneBounce
// (S73/S74) and PhoneTetris (S71/S72) without a visual seam.
#define MP_BG_DARK     lv_color_make(20, 12, 36)     // deep purple
#define MP_ACCENT      lv_color_make(255, 140, 30)   // sunset orange
#define MP_HIGHLIGHT   lv_color_make(122, 232, 255)  // cyan
#define MP_DIM         lv_color_make(70, 56, 100)    // muted purple
#define MP_TEXT        lv_color_make(255, 220, 180)  // warm cream
#define MP_LABEL_DIM   lv_color_make(170, 140, 200)  // dim purple captions

namespace {

// pixel coords for pit i. Player pits 0..5 are bottom-row left-to-right;
// CPU pits 7..12 are top-row right-to-left so the visual top-row column
// `j` corresponds to pit `12 - j`. Stores live at i == 6 (right) and
// i == 13 (left).
inline lv_coord_t pitX(uint8_t i) {
	if(i <= 5) {
		return PhoneBantumi::PitsStartX
		     + i * (PhoneBantumi::PitW + PhoneBantumi::PitGapX);
	}
	if(i == PhoneBantumi::PlayerStore) {
		return PhoneBantumi::PlayerStoreX;
	}
	if(i >= 7 && i <= 12) {
		// CPU pit i corresponds to top-row column (12 - i) (0..5).
		const uint8_t col = static_cast<uint8_t>(12 - i);
		return PhoneBantumi::PitsStartX
		     + col * (PhoneBantumi::PitW + PhoneBantumi::PitGapX);
	}
	// CpuStore (13).
	return PhoneBantumi::CpuStoreX;
}

inline lv_coord_t pitY(uint8_t i) {
	if(i <= 5) return PhoneBantumi::PlayerRowY;
	if(i == PhoneBantumi::PlayerStore) return PhoneBantumi::StoreYTop;
	if(i >= 7 && i <= 12) return PhoneBantumi::CpuRowY;
	return PhoneBantumi::StoreYTop;   // CpuStore
}

inline lv_coord_t pitW(uint8_t i) {
	if(i == PhoneBantumi::PlayerStore || i == PhoneBantumi::CpuStore) {
		return PhoneBantumi::StoreW;
	}
	return PhoneBantumi::PitW;
}

inline lv_coord_t pitH(uint8_t i) {
	if(i == PhoneBantumi::PlayerStore || i == PhoneBantumi::CpuStore) {
		return PhoneBantumi::StoreH;
	}
	return PhoneBantumi::PitH;
}

} // namespace

// ===========================================================================
// ctor / dtor
// ===========================================================================

PhoneBantumi::PhoneBantumi()
		: LVScreen(),
		  wallpaper(nullptr),
		  statusBar(nullptr),
		  softKeys(nullptr) {

	for(uint8_t i = 0; i < PitCount; ++i) {
		pits[i] = 0;
		pitSprites[i] = nullptr;
		pitCountLabels[i] = nullptr;
	}

	lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
	lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_style_pad_all(obj, 0, 0);

	wallpaper = new PhoneSynthwaveBg(obj);
	statusBar = new PhoneStatusBar(obj);

	buildHud();
	buildBoard();
	buildOverlay();

	softKeys = new PhoneSoftKeyBar(obj);
	softKeys->setLeft("START");
	softKeys->setRight("BACK");

	enterIdle();
}

PhoneBantumi::~PhoneBantumi() {
	stopSowTimer();
	stopCpuTimer();
	// All children parented to obj; LVScreen's destructor frees them.
}

void PhoneBantumi::onStart() {
	Input::getInstance()->addListener(this);
}

void PhoneBantumi::onStop() {
	Input::getInstance()->removeListener(this);
	stopSowTimer();
	stopCpuTimer();
}

// ===========================================================================
// build helpers
// ===========================================================================

void PhoneBantumi::buildHud() {
	titleLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(titleLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(titleLabel, MP_ACCENT, 0);
	lv_label_set_text(titleLabel, "MANCALA");
	lv_obj_set_align(titleLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(titleLabel, 12);

	// Centre status text sits in the gap between the two pit rows so the
	// player always sees whose turn it is without crowding the score.
	statusLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(statusLabel, &pixelbasic7, 0);
	lv_obj_set_style_text_color(statusLabel, MP_HIGHLIGHT, 0);
	lv_obj_set_style_text_align(statusLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(statusLabel, "YOUR TURN");
	lv_obj_set_align(statusLabel, LV_ALIGN_TOP_MID);
	lv_obj_set_y(statusLabel, 60);
}

void PhoneBantumi::buildBoard() {
	for(uint8_t i = 0; i < PitCount; ++i) {
		auto* p = lv_obj_create(obj);
		lv_obj_remove_style_all(p);
		lv_obj_set_size(p, pitW(i), pitH(i));
		lv_obj_set_pos(p, pitX(i), pitY(i));
		lv_obj_set_style_bg_color(p, MP_BG_DARK, 0);
		lv_obj_set_style_bg_opa(p, LV_OPA_70, 0);
		lv_obj_set_style_border_color(p, MP_DIM, 0);
		lv_obj_set_style_border_width(p, 1, 0);
		lv_obj_set_style_radius(p, 2, 0);
		lv_obj_set_style_pad_all(p, 0, 0);
		lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(p, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(p, LV_OBJ_FLAG_IGNORE_LAYOUT);
		pitSprites[i] = p;

		// Numeric stone count label centred in the pit. We pick a colour
		// per role: cyan for player pits + store, dim purple for the
		// CPU pits and store. The player's own colour spike makes the
		// "this is your side" read trivially.
		auto* lbl = lv_label_create(p);
		lv_obj_set_style_text_font(lbl, &pixelbasic7, 0);
		lv_obj_set_style_text_color(
				lbl,
				(i <= 6) ? MP_HIGHLIGHT : MP_LABEL_DIM,
				0);
		lv_label_set_text(lbl, "0");
		lv_obj_set_align(lbl, LV_ALIGN_CENTER);
		pitCountLabels[i] = lbl;
	}
}

void PhoneBantumi::buildOverlay() {
	overlayLabel = lv_label_create(obj);
	lv_obj_set_style_text_font(overlayLabel, &pixelbasic16, 0);
	lv_obj_set_style_text_color(overlayLabel, MP_TEXT, 0);
	lv_obj_set_style_text_align(overlayLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_text(overlayLabel, "");
	lv_obj_set_align(overlayLabel, LV_ALIGN_CENTER);
	lv_obj_set_y(overlayLabel, 4);
}

// ===========================================================================
// state transitions
// ===========================================================================

void PhoneBantumi::enterIdle() {
	state = GameState::Idle;
	stopSowTimer();
	stopCpuTimer();

	// Reset board to canonical 4-stones-per-pit. Stores stay at zero.
	for(uint8_t i = 0; i < PitCount; ++i) {
		pits[i] = (i == PlayerStore || i == CpuStore) ? 0 : StonesPerPit;
	}
	cursor = 0;
	lastResult = WinResult::None;

	render();
	refreshSoftKeys();
	refreshStatusLine();
	refreshOverlay();
}

void PhoneBantumi::startMatch() {
	// Same as enterIdle's board reset but immediately hand control to
	// the player so they can sow.
	for(uint8_t i = 0; i < PitCount; ++i) {
		pits[i] = (i == PlayerStore || i == CpuStore) ? 0 : StonesPerPit;
	}
	cursor = 0;
	lastResult = WinResult::None;
	beginPlayerTurn();
}

void PhoneBantumi::beginPlayerTurn() {
	state = GameState::PlayerTurn;

	// Snap the cursor to the first non-empty pit on the player's side
	// so the player never points at an unsowable cell. If every pit is
	// empty the side-empty check will end the match anyway.
	if(pits[cursor] == 0) {
		bool found = false;
		for(uint8_t i = 0; i < 6; ++i) {
			if(pits[i] > 0) { cursor = i; found = true; break; }
		}
		if(!found) {
			endMatch();
			return;
		}
	}

	render();
	refreshSoftKeys();
	refreshStatusLine();
	refreshOverlay();
}

void PhoneBantumi::beginCpuTurn() {
	state = GameState::CpuThink;
	render();
	refreshSoftKeys();
	refreshStatusLine();
	refreshOverlay();
	startCpuTimer();
}

void PhoneBantumi::beginSow(uint8_t fromPit, bool ownerIsPlayer) {
	// Pick up all stones from `fromPit`. The pit goes to zero; the next
	// pit to receive is fromPit+1 (skipping opponent's store).
	if(pits[fromPit] == 0) return;
	sowHand          = pits[fromPit];
	pits[fromPit]    = 0;
	sowOwnerIsPlayer = ownerIsPlayer;
	sowNext          = advanceIndex(fromPit, ownerIsPlayer);
	sowLastIdx       = fromPit;     // updated as stones drop
	sowExtraTurn     = false;

	state = GameState::Sowing;
	render();
	refreshSoftKeys();
	refreshStatusLine();
	refreshOverlay();
	startSowTimer();
}

void PhoneBantumi::finishSow() {
	stopSowTimer();

	// Capture rule: last stone landed in *your* empty pit (it has 1
	// stone, the one we just dropped) AND the opposite pit has stones.
	const bool ownerSide = sowOwnerIsPlayer;
	const uint8_t last   = sowLastIdx;
	const bool lastOnOwnSide =
			(ownerSide && isPlayerPit(last))
			|| (!ownerSide && isCpuPit(last));

	if(lastOnOwnSide && pits[last] == 1 && pits[opposite(last)] > 0) {
		const uint8_t store = ownerSide ? PlayerStore : CpuStore;
		pits[store] += pits[opposite(last)] + 1;
		pits[opposite(last)] = 0;
		pits[last]           = 0;
	}

	// Game-end check: if either side is empty the match is over.
	if(sideEmpty(/*playerSide=*/true) || sideEmpty(/*playerSide=*/false)) {
		sweepRemaining();
		endMatch();
		return;
	}

	// Free turn: last stone in own store -> same player goes again.
	if(sowExtraTurn) {
		if(ownerSide) beginPlayerTurn();
		else          beginCpuTurn();
		return;
	}

	// Otherwise hand over.
	if(ownerSide) beginCpuTurn();
	else          beginPlayerTurn();
}

void PhoneBantumi::endMatch() {
	state = GameState::GameOver;
	stopSowTimer();
	stopCpuTimer();

	if(pits[PlayerStore] > pits[CpuStore])      lastResult = WinResult::Player;
	else if(pits[PlayerStore] < pits[CpuStore]) lastResult = WinResult::Cpu;
	else                                        lastResult = WinResult::Draw;

	render();
	refreshSoftKeys();
	refreshStatusLine();
	refreshOverlay();
}

// ===========================================================================
// core game ops
// ===========================================================================

uint8_t PhoneBantumi::advanceIndex(uint8_t idx, bool ownerIsPlayer) const {
	uint8_t next = static_cast<uint8_t>((idx + 1) % PitCount);
	// Skip the opponent's store.
	if(ownerIsPlayer && next == CpuStore) {
		next = 0;   // wrap past CpuStore (13) -> 0 (player pit 0)
	}
	if(!ownerIsPlayer && next == PlayerStore) {
		next = 7;   // wrap past PlayerStore (6) -> 7 (CPU pit 0)
	}
	return next;
}

bool PhoneBantumi::sideEmpty(bool playerSide) const {
	const uint8_t lo = playerSide ? 0 : 7;
	const uint8_t hi = playerSide ? 6 : 13;   // exclusive
	for(uint8_t i = lo; i < hi; ++i) {
		if(pits[i] > 0) return false;
	}
	return true;
}

void PhoneBantumi::sweepRemaining() {
	for(uint8_t i = 0; i < 6; ++i) {
		pits[PlayerStore] += pits[i];
		pits[i] = 0;
	}
	for(uint8_t i = 7; i < 13; ++i) {
		pits[CpuStore] += pits[i];
		pits[i] = 0;
	}
}

bool PhoneBantumi::pickCpuMove(uint8_t& out) const {
	// Greedy one-ply heuristic. Score every legal CPU pit:
	//   +100  if the move grants an extra turn (last stone lands in
	//         CpuStore)
	//   +(2 * captureGain) if the move captures opponent stones
	//   +stoneIntoStore if the move would deposit any stones into
	//         CpuStore on its way through
	//   tiebreak: prefer the rightmost-from-CPU pit (pit 7), which
	//         tends to keep options open.
	int  bestScore = -1;
	int  bestIdx   = -1;
	for(int8_t i = 7; i <= 12; ++i) {
		const uint8_t stones = pits[i];
		if(stones == 0) continue;

		// Simulate sowing into a scratch board. We don't apply capture
		// here -- we score the *outcome* candidates instead.
		uint8_t scratch[PitCount];
		memcpy(scratch, pits, sizeof(scratch));
		scratch[i] = 0;
		uint8_t cursorIdx = static_cast<uint8_t>(i);
		uint8_t lastDrop  = cursorIdx;
		uint8_t storeAdds = 0;
		uint8_t handLeft  = stones;
		while(handLeft > 0) {
			cursorIdx = advanceIndex(cursorIdx, /*ownerIsPlayer=*/false);
			scratch[cursorIdx]++;
			lastDrop = cursorIdx;
			if(cursorIdx == CpuStore) ++storeAdds;
			--handLeft;
		}

		int score = static_cast<int>(storeAdds);

		// Extra-turn bonus.
		if(lastDrop == CpuStore) score += 100;

		// Capture bonus -- last drop lands in own empty CPU pit and the
		// opposite player pit has stones.
		if(lastDrop >= 7 && lastDrop <= 12 && scratch[lastDrop] == 1) {
			const uint8_t opp = static_cast<uint8_t>(12 - lastDrop);
			if(scratch[opp] > 0) {
				score += 2 * static_cast<int>(scratch[opp] + 1);
			}
		}

		// Tiebreaker: lower pit index wins (closer to CPU's "1" cell).
		// Multiply current score by 10 to keep the tiebreaker subdominant.
		const int weighted = score * 10 + static_cast<int>(12 - i);

		if(weighted > bestScore) {
			bestScore = weighted;
			bestIdx   = i;
		}
	}

	if(bestIdx < 0) return false;
	out = static_cast<uint8_t>(bestIdx);
	return true;
}

// ===========================================================================
// rendering
// ===========================================================================

void PhoneBantumi::render() {
	refreshPits();
	refreshSelection();
}

void PhoneBantumi::refreshPits() {
	for(uint8_t i = 0; i < PitCount; ++i) {
		auto* lbl = pitCountLabels[i];
		if(lbl == nullptr) continue;
		char buf[6];
		snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(pits[i]));
		lv_label_set_text(lbl, buf);
	}

	// Stores and pits get a baseline border colour. The CPU side stays
	// dim so the player knows where they are at a glance.
	for(uint8_t i = 0; i < PitCount; ++i) {
		auto* p = pitSprites[i];
		if(p == nullptr) continue;
		lv_color_t border;
		if(i == PlayerStore) {
			border = MP_HIGHLIGHT;    // player's bank pops cyan
		} else if(i == CpuStore) {
			border = MP_DIM;          // CPU's bank stays muted
		} else if(isPlayerPit(i)) {
			border = MP_LABEL_DIM;    // player pits ride a light dim
		} else {
			border = MP_DIM;          // CPU pits stay dim
		}
		lv_obj_set_style_border_color(p, border, 0);
	}

	// Highlight the last sown pit so the player can read which cell
	// just received a stone -- a simple but legible "sow trail".
	if((state == GameState::Sowing || state == GameState::PlayerTurn
	    || state == GameState::CpuThink || state == GameState::GameOver)
	   && pitSprites[sowLastIdx] != nullptr
	   && state != GameState::Idle) {
		// Only honour the trail mid-sow + briefly after; in PlayerTurn
		// the selection cursor takes over (see refreshSelection).
		if(state == GameState::Sowing) {
			lv_obj_set_style_border_color(pitSprites[sowLastIdx],
			                              MP_ACCENT, 0);
		}
	}
}

void PhoneBantumi::refreshSelection() {
	// Cursor only matters during PlayerTurn -- in every other state
	// the focus is dictated by the game (sow trail / overlay).
	if(state != GameState::PlayerTurn) return;
	if(cursor > 5) return;
	auto* p = pitSprites[cursor];
	if(p == nullptr) return;
	lv_obj_set_style_border_color(p, MP_ACCENT, 0);
	lv_obj_move_foreground(p);
}

void PhoneBantumi::refreshStatusLine() {
	if(statusLabel == nullptr) return;
	char buf[20];
	switch(state) {
		case GameState::Idle:
			snprintf(buf, sizeof(buf), "PRESS START");
			break;
		case GameState::PlayerTurn:
			snprintf(buf, sizeof(buf), "YOUR TURN  %u-%u",
			         static_cast<unsigned>(pits[PlayerStore]),
			         static_cast<unsigned>(pits[CpuStore]));
			break;
		case GameState::CpuThink:
			snprintf(buf, sizeof(buf), "CPU...     %u-%u",
			         static_cast<unsigned>(pits[PlayerStore]),
			         static_cast<unsigned>(pits[CpuStore]));
			break;
		case GameState::Sowing:
			snprintf(buf, sizeof(buf), "%s     %u-%u",
			         sowOwnerIsPlayer ? "SOWING..." : "CPU SOWS.",
			         static_cast<unsigned>(pits[PlayerStore]),
			         static_cast<unsigned>(pits[CpuStore]));
			break;
		case GameState::GameOver:
			snprintf(buf, sizeof(buf), "FINAL  %u-%u",
			         static_cast<unsigned>(pits[PlayerStore]),
			         static_cast<unsigned>(pits[CpuStore]));
			break;
	}
	lv_label_set_text(statusLabel, buf);

	// Tint the status line so each state reads at a glance.
	lv_color_t c;
	switch(state) {
		case GameState::PlayerTurn: c = MP_HIGHLIGHT; break;
		case GameState::CpuThink:
		case GameState::Sowing:     c = MP_ACCENT;    break;
		case GameState::GameOver:   c = MP_TEXT;      break;
		default:                    c = MP_LABEL_DIM; break;
	}
	lv_obj_set_style_text_color(statusLabel, c, 0);
}

void PhoneBantumi::refreshSoftKeys() {
	if(softKeys == nullptr) return;
	switch(state) {
		case GameState::Idle:
			softKeys->setLeft("START");
			softKeys->setRight("BACK");
			break;
		case GameState::PlayerTurn:
			softKeys->setLeft("SOW");
			softKeys->setRight("BACK");
			break;
		case GameState::CpuThink:
			softKeys->setLeft("");
			softKeys->setRight("BACK");
			break;
		case GameState::Sowing:
			softKeys->setLeft("SKIP");
			softKeys->setRight("BACK");
			break;
		case GameState::GameOver:
			softKeys->setLeft("AGAIN");
			softKeys->setRight("BACK");
			break;
	}
}

void PhoneBantumi::refreshOverlay() {
	if(overlayLabel == nullptr) return;
	switch(state) {
		case GameState::Idle:
			lv_label_set_text(overlayLabel, "PRESS\nSTART");
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
		case GameState::PlayerTurn:
		case GameState::CpuThink:
		case GameState::Sowing:
			lv_obj_add_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
		case GameState::GameOver:
			switch(lastResult) {
				case WinResult::Player:
					lv_label_set_text(overlayLabel, "YOU\nWIN!");
					break;
				case WinResult::Cpu:
					lv_label_set_text(overlayLabel, "CPU\nWINS");
					break;
				case WinResult::Draw:
				default:
					lv_label_set_text(overlayLabel, "DRAW");
					break;
			}
			lv_obj_clear_flag(overlayLabel, LV_OBJ_FLAG_HIDDEN);
			break;
	}
}

// ===========================================================================
// timers
// ===========================================================================

void PhoneBantumi::startSowTimer() {
	if(sowTimer != nullptr) return;
	sowTimer = lv_timer_create(&PhoneBantumi::onSowTickStatic,
	                           SowTickMs, this);
}

void PhoneBantumi::stopSowTimer() {
	if(sowTimer != nullptr) {
		lv_timer_del(sowTimer);
		sowTimer = nullptr;
	}
}

void PhoneBantumi::startCpuTimer() {
	if(cpuTimer != nullptr) return;
	cpuTimer = lv_timer_create(&PhoneBantumi::onCpuTickStatic,
	                           CpuThinkMs, this);
	lv_timer_set_repeat_count(cpuTimer, 1);
}

void PhoneBantumi::stopCpuTimer() {
	if(cpuTimer != nullptr) {
		lv_timer_del(cpuTimer);
		cpuTimer = nullptr;
	}
}

void PhoneBantumi::onSowTickStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneBantumi*>(timer->user_data);
	if(self == nullptr) return;
	if(self->state != GameState::Sowing) return;

	// Drop one stone in `sowNext` and either advance or finish.
	if(self->sowHand == 0) {
		// Defensive: sowHand should never be 0 here -- we'd have already
		// finishSow()'d -- but guard against logic drift.
		self->finishSow();
		return;
	}

	self->pits[self->sowNext] += 1;
	self->sowLastIdx = self->sowNext;
	self->sowHand   -= 1;

	if(self->sowHand == 0) {
		// Last stone -- mark extra-turn flag if it landed in the owner's
		// store, then run finishSow which handles capture / handover /
		// game-end.
		const uint8_t store = self->sowOwnerIsPlayer ? PlayerStore : CpuStore;
		self->sowExtraTurn = (self->sowLastIdx == store);
		self->render();
		self->refreshStatusLine();
		self->finishSow();
		return;
	}

	// More stones to drop -- advance the cursor.
	self->sowNext = self->advanceIndex(self->sowNext,
	                                   self->sowOwnerIsPlayer);
	self->render();
	self->refreshStatusLine();
}

void PhoneBantumi::onCpuTickStatic(lv_timer_t* timer) {
	auto* self = static_cast<PhoneBantumi*>(timer->user_data);
	if(self == nullptr) return;
	self->stopCpuTimer();
	if(self->state != GameState::CpuThink) return;

	uint8_t move = 7;
	if(!self->pickCpuMove(move)) {
		// CPU has no legal move -- end the match.
		self->sweepRemaining();
		self->endMatch();
		return;
	}
	self->beginSow(move, /*ownerIsPlayer=*/false);
}

// ===========================================================================
// input
// ===========================================================================

void PhoneBantumi::buttonPressed(uint i) {
	switch(state) {
		case GameState::Idle:
			if(i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				startMatch();
			} else if(i == BTN_BACK) {
				if(softKeys) softKeys->flashRight();
				pop();
			}
			return;

		case GameState::PlayerTurn:
			if(i == BTN_LEFT || i == BTN_4) {
				// Find next non-empty player pit to the left, wrapping.
				for(uint8_t step = 1; step <= 6; ++step) {
					const uint8_t cand =
							static_cast<uint8_t>((cursor + 6 - step) % 6);
					if(pits[cand] > 0) {
						cursor = cand;
						break;
					}
				}
				render();
				return;
			}
			if(i == BTN_RIGHT || i == BTN_6) {
				for(uint8_t step = 1; step <= 6; ++step) {
					const uint8_t cand =
							static_cast<uint8_t>((cursor + step) % 6);
					if(pits[cand] > 0) {
						cursor = cand;
						break;
					}
				}
				render();
				return;
			}
			if(i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				if(pits[cursor] == 0) return;   // illegal move; ignore
				beginSow(cursor, /*ownerIsPlayer=*/true);
				return;
			}
			if(i == BTN_BACK) {
				if(softKeys) softKeys->flashRight();
				pop();
			}
			return;

		case GameState::CpuThink:
			if(i == BTN_BACK) {
				if(softKeys) softKeys->flashRight();
				pop();
			}
			// Fast-forward the CPU's "thinking" beat for the impatient.
			if(i == BTN_ENTER) {
				stopCpuTimer();
				uint8_t move = 7;
				if(!pickCpuMove(move)) {
					sweepRemaining();
					endMatch();
					return;
				}
				beginSow(move, /*ownerIsPlayer=*/false);
			}
			return;

		case GameState::Sowing:
			if(i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				// Speed-finish: drain remaining stones in one go.
				while(sowHand > 0) {
					pits[sowNext] += 1;
					sowLastIdx = sowNext;
					--sowHand;
					if(sowHand == 0) {
						const uint8_t store =
								sowOwnerIsPlayer ? PlayerStore : CpuStore;
						sowExtraTurn = (sowLastIdx == store);
						break;
					}
					sowNext = advanceIndex(sowNext, sowOwnerIsPlayer);
				}
				render();
				refreshStatusLine();
				finishSow();
				return;
			}
			if(i == BTN_BACK) {
				if(softKeys) softKeys->flashRight();
				pop();
			}
			return;

		case GameState::GameOver:
			if(i == BTN_ENTER) {
				if(softKeys) softKeys->flashLeft();
				enterIdle();
				startMatch();
			} else if(i == BTN_BACK) {
				if(softKeys) softKeys->flashRight();
				pop();
			}
			return;
	}
}
