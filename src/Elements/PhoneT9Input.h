#ifndef MAKERPHONE_PHONET9INPUT_H
#define MAKERPHONE_PHONET9INPUT_H

#include <Arduino.h>
#include <lvgl.h>
#include <functional>
#include <utility>
#include "../Interface/LVObject.h"

/**
 * PhoneT9Input
 *
 * Reusable retro feature-phone multi-tap text-entry widget for
 * MAKERphone 2.0. It is the Phase-E atom (S32) that the future
 * `ConvoScreen` composer (S33) and `PhoneContactEdit` name-field (S38)
 * will host as their default input surface. Mirrors the classic
 * Sony-Ericsson SMS composer:
 *
 *      +----------------------------------+
 *      | hello wo|                        |   <- text + cursor caret (blinks)
 *      +----------------------------------+
 *      | abc -> [a]bc                  Abc|   <- pending-letter strip (right: case)
 *      +----------------------------------+
 *
 * Each tap on a number key cycles through the letters bound to that
 * key under the canonical T9 / ITU-T E.161 keymap:
 *
 *      2 -> A B C 2
 *      3 -> D E F 3
 *      4 -> G H I 4
 *      5 -> J K L 5
 *      6 -> M N O 6
 *      7 -> P Q R S 7
 *      8 -> T U V 8
 *      9 -> W X Y Z 9
 *      0 -> ' ' 0
 *      1 -> . , ? ! 1
 *      # -> case toggle (Abc -> ABC -> abc)
 *      * -> backspace
 *
 * Implementation notes:
 *  - 100 % code-only - one slab background, two `lv_label`s (text and
 *    caret), and one optional pending-letter underline frame. No SPIFFS
 *    assets, no canvas backing buffer. Same family as `PhoneDialerKey`,
 *    `PhoneChatBubble`, `PhoneNotificationToast`.
 *  - Multi-tap is driven by a single `lv_timer_t` ("commit timer"). A
 *    second tap on the SAME key within `CycleMs` advances through the
 *    key's letter list IN PLACE - the latest pending letter overwrites
 *    the previous one rather than appending. A tap on a DIFFERENT key,
 *    or the timer firing, "commits" the pending letter and starts a new
 *    pending letter on the new key. There's never more than one
 *    in-flight pending letter at a time.
 *  - The caret blinks via a second `lv_timer_t` running at `CaretMs`.
 *    It is suppressed (forced visible) while a pending letter is in
 *    progress - matches the behaviour every Sony-Ericsson handset of
 *    the era used so the user could clearly see "this letter isn't
 *    committed yet".
 *  - Input is fed through `keyPress(char glyph)` rather than wiring
 *    directly to `InputListener` - this keeps the widget testable, lets
 *    the host route both arrow-key + enter and direct numpad presses
 *    through one funnel, and matches `PhoneDialerPad`'s `pressGlyph()`
 *    contract so a screen can simply forward `pad.onPress(...)` ->
 *    `t9.keyPress(...)`. There is also a `backspace()` helper for
 *    softkey callers and a `clear()` helper.
 *  - Two callbacks fire as the buffer mutates:
 *      onTextChanged(newText)  - whenever the visible text changes
 *                                (including pending-letter overwrites).
 *      onCommit(newText)       - whenever a pending letter is locked in
 *                                (timer fired, different key tapped, or
 *                                explicit `commitPending()`).
 *    Hosts that only care about the final string can just use
 *    `getText()` after receiving the screen-level "send" softkey.
 *  - Lifecycle: the destructor cancels both timers so a screen
 *    tear-down mid-cycle never leaves a stale callback pointing into
 *    freed memory. Same pattern `PhoneNotificationToast` uses.
 */
class PhoneT9Input : public LVObject {
public:
	using TextChangedCb = std::function<void(const String& text)>;
	using CommitCb      = std::function<void(const String& text)>;

	enum class Case : uint8_t {
		Lower = 0,   // abc - default
		First = 1,   // Abc - first letter of a fresh pending sequence is upper
		Upper = 2    // ABC - everything upper
	};

	/**
	 * Build a T9 entry inside `parent`.
	 *
	 * @param parent    LVGL parent.
	 * @param maxLength Hard cap on total text length (default 160 - the
	 *                  classic SMS payload). Further keystrokes after
	 *                  the cap silently no-op (caller can show a hint).
	 */
	PhoneT9Input(lv_obj_t* parent, uint16_t maxLength = 160);
	virtual ~PhoneT9Input();

	/**
	 * Route one numpad press through the T9 state machine.
	 *
	 * Recognised glyphs: '0'..'9', '*' (backspace), '#' (case toggle).
	 * Anything else is silently ignored so a host can blindly forward
	 * `PhoneDialerPad::onPress(glyph, ...)` without filtering.
	 */
	void keyPress(char glyph);

	/** Erase the last committed character (or cancel a pending letter). */
	void backspace();

	/** Reset the buffer, pending letter, and cursor to empty. */
	void clear();

	/**
	 * Force the pending letter (if any) to lock in and re-arm a fresh
	 * pending-letter cycle for the next key. Hosts can call this from
	 * the "Send" softkey to make sure the in-flight letter ends up in
	 * `getText()` before the message goes out.
	 */
	void commitPending();

	/** The full visible text including any pending letter. */
	String getText() const { return text; }

	/** Replace the buffer outright (e.g. when reopening a draft). */
	void setText(const String& s);

	/** Empty hint shown in MP_LABEL_DIM when the buffer is empty. */
	void setPlaceholder(const String& s);

	Case getCase() const { return caseMode; }
	void setCase(Case c);
	void cycleCase();

	bool hasPending() const { return pendingActive; }

	void setOnTextChanged(TextChangedCb cb) { onTextChanged = std::move(cb); }
	void setOnCommit(CommitCb cb) { onCommit = std::move(cb); }

	// ----- layout constants -----
	static constexpr uint16_t Width        = 156;  // fits 160 px with 2 px margin
	static constexpr uint16_t Height       = 22;
	static constexpr uint16_t HelpHeight   = 8;    // pending-letter strip height

	// ----- timing constants (ms) -----
	// Window during which a repeat tap on the same key advances through
	// its letter ring. Matches the classic feature-phone feel - long
	// enough for thoughtful typing, short enough that committing feels
	// snappy. Tunable in one place if user testing wants it different.
	static constexpr uint16_t CycleMs      = 900;

	// Caret blink period (ms). Both halves of the duty cycle are equal,
	// so the caret toggles every CaretMs and a full blink is 2*CaretMs.
	static constexpr uint16_t CaretMs      = 450;

private:
	uint16_t maxLength;

	// Committed text excludes the pending letter; `text` is the full
	// visible buffer (committed + pending letter when active).
	String text;
	String committed;
	String placeholder;

	// Pending-letter state. Only meaningful while `pendingActive` is true.
	bool   pendingActive = false;
	int8_t pendingKeyIndex = -1; // 0..9 for digits, -1 for none
	uint8_t pendingCharIndex = 0; // index into the active key's letter ring

	// Whether the next pending letter starts uppercase (used by the
	// `First` case mode after a sentence-terminator commit).
	bool nextStartsUpper = true;

	Case caseMode = Case::First;

	// LVGL children
	lv_obj_t* textLabel    = nullptr;
	lv_obj_t* caret        = nullptr;
	lv_obj_t* pendingStrip = nullptr;
	lv_obj_t* pendingLabel = nullptr;
	lv_obj_t* caseLabel    = nullptr;
	lv_obj_t* placeholderLabel = nullptr;

	// Timers
	lv_timer_t* commitTimer = nullptr;
	lv_timer_t* caretTimer  = nullptr;
	bool caretVisible = true;

	TextChangedCb onTextChanged;
	CommitCb      onCommit;

	void buildBackground();
	void buildTextLabel();
	void buildCaret();
	void buildPendingStrip();
	void buildPlaceholder();

	void refreshDisplay();
	void refreshCaseLabel();
	void refreshPlaceholder();

	void startPendingCycle(int8_t keyIndex);
	void advancePendingCycle();
	void commitPendingInternal(bool fireCallback);
	void cancelPending();

	void armCommitTimer();
	void cancelCommitTimer();

	void armCaretTimer();
	void cancelCaretTimer();

	char applyCase(char base) const;

	static const char* lettersForKey(uint8_t keyIndex);

	static void commitTimerCb(lv_timer_t* timer);
	static void caretTimerCb(lv_timer_t* timer);
};

#endif //MAKERPHONE_PHONET9INPUT_H
