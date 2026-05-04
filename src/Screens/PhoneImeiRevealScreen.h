#ifndef MAKERPHONE_PHONEIMEIREVEALSCREEN_H
#define MAKERPHONE_PHONEIMEIREVEALSCREEN_H

#include <Arduino.h>
#include <Input/InputListener.h>
#include "../Interface/LVScreen.h"

class PhoneSynthwaveBg;
class PhoneStatusBar;
class PhoneSoftKeyBar;

/**
 * PhoneImeiRevealScreen
 *
 * Phase-S Easter-egg screen (S164). When the user types the classic
 * GSM check code `*#06#` on PhoneDialerScreen the dialer detects the
 * sequence, clears the buffer and pushes this screen. The page mimics
 * the unmistakable Sony-Ericsson IMEI reveal silhouette: a single
 * 15-digit identifier in big pixelbasic16 cyan, with a small "IMEI"
 * caption above and an even smaller "(serial number)" subtitle below
 * to sell the joke.
 *
 *   +----------------------------------------+
 *   |  ||||      12:34                ##### | <- PhoneStatusBar (10 px)
 *   |               IMEI                     | <- pixelbasic7 cyan caption
 *   |                                        |
 *   |       351827 29 083746 5              | <- pixelbasic16 cream
 *   |                                        |    (15 digits, TAC/FAC/SNR/CD)
 *   |          serial number                 | <- pixelbasic7 dim
 *   |                                        |
 *   |                                        |
 *   |                                BACK    | <- PhoneSoftKeyBar (10 px)
 *   +----------------------------------------+
 *
 * The "IMEI" is **fake** -- the Chatter has no GSM modem, so this is
 * deliberately a decorative reveal. The 15 digits are derived from
 * the device's efuse MAC so the same prototype always shows the same
 * number (a customer can read it off the screen, type it again the
 * next day and see the exact same string -- which is what sells the
 * joke; a random number every push would feel broken). The first 14
 * digits are pulled from the lower 56 bits of the MAC mod 10^14 and
 * the 15th digit is a Luhn check digit so the value is even
 * superficially valid as an IMEI to anyone who knows the format.
 *
 * Behavior:
 *  - BTN_BACK / BTN_ENTER / BTN_RIGHT pop back to the dialer. ENTER
 *    is accepted as a friendly second way out so the user does not
 *    have to hunt for the BACK key after the reveal "wow" moment.
 *    There is no other input.
 *  - The screen is self-contained -- it owns no state beyond the
 *    one-shot IMEI string formatted in the constructor.
 *
 * Implementation notes:
 *  - Code-only, zero SPIFFS. Reuses PhoneSynthwaveBg / PhoneStatusBar /
 *    PhoneSoftKeyBar so the screen feels visually part of the rest of
 *    the MAKERphone family.
 *  - 160x128 budget: 10 px status bar at top, 10 px softkey bar at the
 *    bottom, the IMEI caption at y=14, the big 15-digit value centred
 *    vertically around y=58, the subtitle just below at y=82. There is
 *    deliberate breathing room above and below the value so the reveal
 *    reads like a "result" rather than a debug dump.
 *  - The 15-digit string is rendered with the TAC/FAC/SNR/CD
 *    grouping ("AAAAAA BB CCCCCC D" -- 6/2/6/1) the original
 *    Ericsson / Sony-Ericsson handsets used for *#06#. pixelbasic16
 *    is narrow enough that the 18-char layout (15 digits + 3
 *    spaces) still fits the 160 px width with ~2 px margin on
 *    each side.
 *  - formatImei() is exposed static so unit tests / hosts can sanity-
 *    check the digit math without standing the screen up. The Luhn
 *    helper is private (luhnCheckDigit) since nothing else in the
 *    codebase needs it today.
 */
class PhoneImeiRevealScreen : public LVScreen, private InputListener {
public:
	PhoneImeiRevealScreen();
	virtual ~PhoneImeiRevealScreen() override;

	void onStart() override;
	void onStop() override;

	/**
	 * Format a 15-digit IMEI string derived from `mac` into `out`. The
	 * first 14 digits are `(mac & 0x00FFFFFFFFFFFFFFULL) mod 10^14`
	 * (zero-padded), and the 15th digit is a Luhn check digit so the
	 * resulting value is superficially valid.
	 *
	 * The output is grouped "AAAAAA BB CCCCCC D" with single spaces,
	 * which is 18 chars + NUL. `out` must be at least 20 bytes.
	 *
	 * Static so a host (e.g. a future test) can introspect the digit
	 * math without standing up the screen.
	 */
	static void formatImei(uint64_t mac, char* out, size_t outLen);

private:
	PhoneSynthwaveBg* wallpaper;
	PhoneStatusBar*   statusBar;
	PhoneSoftKeyBar*  softKeys;

	lv_obj_t* captionLabel;
	lv_obj_t* imeiLabel;
	lv_obj_t* subtitleLabel;

	void buildLabels();

	/** Compute the Luhn check digit for the 14-digit prefix `digits`. */
	static uint8_t luhnCheckDigit(const char* digits14);

	void buttonPressed(uint i) override;
};

#endif // MAKERPHONE_PHONEIMEIREVEALSCREEN_H
