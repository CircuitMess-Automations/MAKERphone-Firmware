#ifndef MAKERPHONE_CONFIG_H
#define MAKERPHONE_CONFIG_H

/*
 * MAKERphone 2.0 build-time switches.
 *
 * Centralised compile flags that gate "experimental" UI / boot routing so we
 * can promote features behind a flip and fall back to the legacy Chatter
 * behaviour without ripping the new code out. Each flag is a 0/1 macro and
 * defaults to ON (1) so that fresh builds always run with the latest skin.
 *
 * To revert to the legacy boot path during local development, define the
 * corresponding macro to 0 *before* this header is included (e.g. via a
 * build flag in the .ino, or a -D in arduino-cli) — for example:
 *
 *     #define MAKERPHONE_USE_HOMESCREEN 0
 *     #include "src/MAKERphoneConfig.h"
 *
 * Keep the rationale next to each flag so future readers know whether it
 * is safe to drop / promote / rename.
 */

/*
 * MAKERPHONE_USE_HOMESCREEN
 *
 * When 1 (default): the post-LockScreen default screen is the new
 * `PhoneHomeScreen` (synthwave wallpaper + retro clock + soft-key bar).
 * Its right soft-key ("MENU") routes to the legacy `MainMenu` so every
 * existing feature (inbox, friends, profile, games, settings) is still
 * reachable while the new phone-style menu (S19/S20) is being built.
 *
 * When 0: the legacy boot path is used unchanged — `LockScreen` resumes
 * directly into `MainMenu` exactly like the original Chatter firmware.
 *
 * The flag exists so a single-line edit (or a -D MAKERPHONE_USE_HOMESCREEN=0
 * compile flag) restores the original behaviour if the new homescreen ever
 * regresses something on real hardware.
 */
#ifndef MAKERPHONE_USE_HOMESCREEN
#define MAKERPHONE_USE_HOMESCREEN 1
#endif

/*
 * MAKERPHONE_USE_T9_COMPOSER
 *
 * When 1 (default): `ConvoScreen` builds the new `PhoneT9Input` widget
 * (S32) as its text composer - multi-tap entry with letter-cycle timer,
 * pending-letter strip, retro caret, and the same MAKERphone palette
 * as the rest of the phone shell. BTN_0..BTN_9 feed digits into the T9
 * state machine, BTN_L acts as backspace (matches PhoneDialerScreen's
 * '*' mapping which the T9 widget interprets as backspace), and BTN_R
 * cycles the case mode (matches PhoneDialerScreen's '#' mapping which
 * the T9 widget interprets as case toggle).
 *
 * When 0: the legacy LVGL textarea-based `TextEntry` composer is built
 * instead - exactly the original Chatter messaging behaviour. This
 * fallback exists so a single-line edit (or a -D
 * MAKERPHONE_USE_T9_COMPOSER=0 compile flag) restores the proven
 * keyboard if the new composer ever regresses something on real
 * hardware mid-development.
 *
 * The flag intentionally only gates the composer inside ConvoScreen -
 * S38 (PhoneContactEdit name field) will host its own PhoneT9Input
 * unconditionally because that screen is new code with no legacy
 * counterpart to swap against.
 */
#ifndef MAKERPHONE_USE_T9_COMPOSER
#define MAKERPHONE_USE_T9_COMPOSER 1
#endif

#endif // MAKERPHONE_CONFIG_H
