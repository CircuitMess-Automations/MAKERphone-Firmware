#ifndef CHATTER_FIRMWARE_INBOXSCREEN_H
#define CHATTER_FIRMWARE_INBOXSCREEN_H

#include <vector>

#include "../Interface/LVScreen.h"
#include "../AutoPop.h"
#include "../Types.hpp"

class PhoneMessageRow;
class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * InboxScreen
 *
 * S31 — restyled as a phone-style message list. Each conversation is
 * rendered with `PhoneMessageRow` (avatar + name + 1-line preview +
 * "NEW" / "..." badge), wrapped in a synthwave wallpaper + status bar +
 * soft-key bar shell consistent with every other Phone* screen.
 *
 * The screen still:
 *  - registers each row with `inputGroup` so the LVGL D-pad cursor
 *    drives selection across the list,
 *  - uses `LV_EVENT_CLICKED` on each row to open the matching
 *    `ConvoScreen` for the friend's UID,
 *  - keeps the legacy auto-pop on idle behaviour via `AutoPop`,
 *  - shows a centred "no friends" prompt that pushes `PairScreen` when
 *    the only known UID is the device's own efuse MAC.
 *
 * Visual layout (160x128 px):
 *
 *      0 +---------------------------------+
 *        |   signal | 12:42 | battery       |   <- PhoneStatusBar (10 px)
 *     11 +---------------------------------+
 *        |          MESSAGES                |   <- caption (cyan)
 *     22 +---------------------------------+
 *        |                                  |
 *        |  [AV]  ALEX KIM            NEW   |
 *        |        hey, you up?        ...   |
 *        |                                  |
 *        |  [AV]  MOM                 ...   |   <- list (96 px, 3 rows)
 *        |        on my way            ...  |
 *        |                                  |
 *    118 +---------------------------------+
 *        | OPEN                       BACK  |   <- PhoneSoftKeyBar (10 px)
 *    128 +---------------------------------+
 */
class InboxScreen : public LVScreen {
public:
	InboxScreen();
	~InboxScreen() override;

	void onStart()    override;
	void onStop()     override;
	void onStarting() override;

protected:
	AutoPop apop;

	struct LaunchParams {
		UID_t uid;
		InboxScreen* ctx;
	};

	std::vector<LaunchParams> params;
	std::vector<PhoneMessageRow*> rowElements;

	PhoneSynthwaveBg* wallpaper   = nullptr;
	PhoneStatusBar*   statusBar   = nullptr;
	PhoneSoftKeyBar*  softKeys    = nullptr;
	lv_obj_t*         caption     = nullptr;
	lv_obj_t*         listContainer = nullptr;
	lv_obj_t*         emptyHint   = nullptr;

	void openConvo(UID_t uid);

	/**
	 * (Re)build the rows from `Storage.Friends.all()`. Called from the
	 * constructor (initial paint) and from `onStarting()` when a freshly
	 * paired friend has appeared since the screen was last shown.
	 */
	void rebuildList();

private:
	void buildShell();          // wallpaper + status bar + caption + softkeys
	void buildListContainer();  // the scrollable middle strip
	void clearList();           // wipe rowElements + emptyHint without disturbing the shell
};

#endif //CHATTER_FIRMWARE_INBOXSCREEN_H
