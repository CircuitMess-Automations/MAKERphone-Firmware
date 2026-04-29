#ifndef MAKERPHONE_PHONESOFTKEYBAR_H
#define MAKERPHONE_PHONESOFTKEYBAR_H

#include <Arduino.h>
#include <lvgl.h>
#include "../Interface/LVObject.h"

/**
 * PhoneSoftKeyBar
 *
 * Reusable retro feature-phone softkey bar (160x10) intended to sit at the
 * bottom of MAKERphone screens. It is the visual partner of PhoneStatusBar
 * and gives every screen the unmistakable Sony-Ericsson silhouette:
 *
 *   [<-CALL]                                       [MENU->]
 *   left softkey               (center hint)         right softkey
 *
 * Both labels are context-sensitive - the bar exposes setLeft/setRight/
 * setCenter so any screen can adapt the labels to whatever its left
 * (BTN_LEFT) and right (BTN_RIGHT) buttons currently do. The optional
 * center hint is meant for transient prompts ("HOLD R" on the lock screen,
 * "OK" on a confirmation dialog, etc.); leave it empty for the classic
 * two-label look.
 *
 * Implementation notes:
 *  - Code-only (no SPIFFS assets) so it adds zero data partition cost.
 *  - Uses LV_OBJ_FLAG_IGNORE_LAYOUT so it cooperates with parents that
 *    already use flex/grid layouts - same pattern as PhoneStatusBar.
 *  - Anchored to the bottom edge automatically (y = 128 - 10 = 118).
 *  - Tiny triangular pixel arrows are drawn next to each label to mimic
 *    the classic softkey indicator; they can be hidden by passing
 *    showArrows=false.
 *  - Labels default to "" and are hidden when empty so a single-softkey
 *    screen does not show a stray arrow on the unused side.
 */
class PhoneSoftKeyBar : public LVObject {
public:
	PhoneSoftKeyBar(lv_obj_t* parent);
	virtual ~PhoneSoftKeyBar() = default;

	/** Set the left softkey label (BTN_LEFT). Empty string hides it. */
	void setLeft(const char* label);

	/** Set the right softkey label (BTN_RIGHT). Empty string hides it. */
	void setRight(const char* label);

	/** Set the optional center hint label. Empty string hides it. */
	void setCenter(const char* label);

	/** Hide the small triangular arrows next to each label. */
	void setShowArrows(bool show);

	static constexpr uint16_t BarHeight = 10;
	static constexpr uint16_t ScreenWidth = 160;
	static constexpr uint16_t ScreenHeight = 128;

private:
	lv_obj_t* leftLabel;
	lv_obj_t* rightLabel;
	lv_obj_t* centerLabel;

	lv_obj_t* leftArrow;   // small triangle pointing left
	lv_obj_t* rightArrow;  // small triangle pointing right

	bool showArrows = true;

	void buildBackground();
	void buildLabels();
	void buildArrows();
	void refreshArrows();
};

#endif //MAKERPHONE_PHONESOFTKEYBAR_H
