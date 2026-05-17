# mp24/components/chatter_app/CMakeLists.txt
#
# First component to compile actual upstream phone-firmware code
# from src/. Starts tiny — just the Interface/ base layer plus our
# InputLVGL shim — so we can verify the build path works before
# growing the SRCS list one feature at a time.
#
# Design choices:
#
#   - We compile src/*.cpp at their absolute paths instead of adding
#     src/ to INCLUDE_DIRS. This matters because if src/ were on the
#     include path, the BASE-LAYER headers like LVObject.h would
#     leak into other components, and worse, any other Pins.hpp /
#     InputLVGL.h that lives under src/ would compete with our
#     circuitos_shim versions. The relative #includes inside src/
#     files (e.g. "../InputLVGL.h", "LVObject.h") work locally
#     without any -I tweaking because C/C++ relative includes
#     resolve from the source file's directory, not the build dir.
#
#   - src/ IS on PRIV_INCLUDE_DIRS so the shim file shim/InputLVGL.cpp
#     can `#include "InputLVGL.h"` to pull in the class declaration
#     we're implementing. PRIV keeps it scoped to this component.
#
#   - The circuitos_shim component comes FIRST in REQUIRES so any
#     '<Pins.hpp>' from inside src/ files finds our MP2.4 override
#     before any upstream Chatter copy. (chatter_library — which has
#     the upstream Pins.hpp at libraries/Chatter-Library/src/Pins.hpp
#     — is intentionally NOT in REQUIRES here. We don't need its
#     contents for the Interface/ base layer; if a later screen
#     port pulls something from Chatter-Library we add it then with
#     care about ordering.)
#
#   - SRCS deliberately exclude src/InputLVGL.cpp. The shim provides
#     the symbols InputLVGL::{InputLVGL, getInstance, getIndev}.
#     If we ever accidentally compile both, the linker catches the
#     duplicate definition immediately.

# Absolute path to repo-root src/. CMAKE_CURRENT_LIST_DIR is this
# CMakeLists's directory: mp24/components/chatter_app/. Go up three
# levels to repo root, then into src/.
set(SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../src")

idf_component_register(
    SRCS
        # Interface/ base classes — every LVScreen subclass inherits
        # from these, so they're the first dependency to satisfy.
        "${SRC_DIR}/Interface/LVObject.cpp"
        "${SRC_DIR}/Interface/LVScreen.cpp"

        # S-MP17b: more Interface/ infrastructure. Each pulls
        # additional LVGL 8 + CircuitOS surface through the
        # compiler. ConvoView.cpp deliberately omitted — it pulls
        # Model/Storage/MessageService which we haven't shimmed
        # yet.
        "${SRC_DIR}/Interface/LVSelectable.cpp"
        "${SRC_DIR}/Interface/LVModal.cpp"
        "${SRC_DIR}/Interface/Pics.cpp"
        "${SRC_DIR}/Interface/PhoneTransitions.cpp"

        # S-MP17d: first Element/ widget. BatteryElement is the
        # leanest one — 24 LOC, depends only on LVObject (already
        # compiled), Battery singleton (MP24Battery shim), and
        # LoopListener (CircuitOS). Good runtime smoke test for
        # the LoopManager listener pattern: it registers itself
        # and ticks every LoopManager pump cycle, calling
        # Battery.getLevel() and updating an lv_img if changed.
        "${SRC_DIR}/Elements/BatteryElement.cpp"

        # S-MP18a: theme + fonts + foundational service + the
        # background widget every "real-looking" screen uses, plus
        # the first lean screen that consumes them.
        #
        # The whole stack is dependency-clean: MakerphoneTheme is
        # ~3500 LOC of pure data tables with no service or model
        # deps. The fonts are LVGL binary font data in .c files,
        # also zero deps. PhoneClock is a ~150 LOC RAM-only wall-
        # clock service (S54 backing for the Date & Time screen),
        # no SPIFFS / NVS / RTC. PhoneSynthwaveBg builds on those
        # three; PhoneWelcomeScreen builds on PhoneSynthwaveBg.
        #
        # No instantiation in TestScreen yet — this commit just
        # proves the compile path; runtime use of PhoneWelcomeScreen
        # lands when we wire it as a real boot destination.
        "${SRC_DIR}/MakerphoneTheme.cpp"
        "${SRC_DIR}/Fonts/pixelbasic7.c"
        "${SRC_DIR}/Fonts/pixelbasic16.c"
        "${SRC_DIR}/Services/PhoneClock.cpp"
        "${SRC_DIR}/Elements/PhoneSynthwaveBg.cpp"
        "${SRC_DIR}/Screens/PhoneWelcomeScreen.cpp"

        # S-MP18c: universal-UI Elements + the service one of them
        # depends on. PhoneStatusBar (top strip with time, battery,
        # signal indicators) and PhoneSoftKeyBar (bottom strip with
        # softkey labels) appear in 105 callers each — once these
        # compile most screens are unblocked.
        #
        # PhoneIdleDim is a service PhoneSoftKeyBar pulls for the
        # input-activity timestamp it uses to dim the soft keys
        # after N seconds of no input. Tiny (~260 LOC), zero new
        # external deps (just LoopListener + InputListener +
        # Settings, all of which are already in our build).
        "${SRC_DIR}/Services/PhoneIdleDim.cpp"
        "${SRC_DIR}/Elements/PhoneStatusBar.cpp"
        "${SRC_DIR}/Elements/PhoneSoftKeyBar.cpp"

        # S-MP18e: bulk Element compile expansion. Every file
        # below has clean deps (only Fonts + MakerphoneTheme +
        # LoopListener + Chatter + LVObject — all already in the
        # build). Linker will drop them until something
        # instantiates them, but compile success on the full set
        # is broad validation that the LVGL 8.4 + arduino-esp32 +
        # shim toolchain is stable across the upstream Element/
        # surface.
        #
        # NotificationElement deliberately omitted — pulls
        # Services/MessageService.h which we haven't shimmed.
        "${SRC_DIR}/Elements/PhoneSignalIcon.cpp"
        "${SRC_DIR}/Elements/PhoneBatteryIcon.cpp"
        "${SRC_DIR}/Elements/PhoneClockFace.cpp"
        "${SRC_DIR}/Elements/PhoneIconTile.cpp"
        "${SRC_DIR}/Elements/PhoneDialerKey.cpp"
        "${SRC_DIR}/Elements/PhoneNotificationToast.cpp"
        "${SRC_DIR}/Elements/PhoneTipBanner.cpp"
        "${SRC_DIR}/Elements/PhoneChatBubble.cpp"
        "${SRC_DIR}/Elements/PhoneIdleHint.cpp"
        "${SRC_DIR}/Elements/PhoneLockHint.cpp"
        "${SRC_DIR}/Elements/PhoneChargingOverlay.cpp"
        "${SRC_DIR}/Elements/PhoneMissedCallFlash.cpp"

        # S-MP18f: ten utility / info / game screens, all with
        # dependency closure inside what chatter_app already
        # links (status bar + soft-key bar + universal Elements +
        # Settings + PhoneClock). Each ~150-700 LOC; ~4500 LOC
        # total of new compile coverage.
        #
        # No instantiation in any factory yet — the linker drops
        # them until referenced. This is breadth compile-validation,
        # not runtime work. Once we want to switch the boot screen
        # to one of these, the factory is a 5-line addition.
        "${SRC_DIR}/Screens/PhoneAccentScreen.cpp"
        "${SRC_DIR}/Screens/PhoneBrightnessScreen.cpp"
        "${SRC_DIR}/Screens/PhoneDateTimeScreen.cpp"
        "${SRC_DIR}/Screens/PhoneFirmwareInfoScreen.cpp"
        "${SRC_DIR}/Screens/PhoneDemoSpeedScreen.cpp"
        "${SRC_DIR}/Screens/PhoneDemoModeScreen.cpp"
        "${SRC_DIR}/Screens/PhoneFlashlight.cpp"
        "${SRC_DIR}/Screens/PhoneFortuneCookie.cpp"
        "${SRC_DIR}/Screens/PhoneCalculator.cpp"
        "${SRC_DIR}/Screens/PhoneCalendar.cpp"

        # S-MP18g: games + utilities batch — 14 more screens that
        # pass the same dep audit. These are mini-apps (puzzle
        # games, calculators, utility tools) deliberately
        # self-contained for their tile-launcher pattern in the
        # upstream firmware.
        "${SRC_DIR}/Screens/Phone2048.cpp"
        "${SRC_DIR}/Screens/PhoneAirHockey.cpp"
        "${SRC_DIR}/Screens/PhoneBantumi.cpp"
        "${SRC_DIR}/Screens/PhoneBounce.cpp"
        "${SRC_DIR}/Screens/PhoneBrickBreaker.cpp"
        "${SRC_DIR}/Screens/PhoneBubbleSmile.cpp"
        "${SRC_DIR}/Screens/PhoneCoinFlip.cpp"
        "${SRC_DIR}/Screens/PhoneConnectFour.cpp"
        "${SRC_DIR}/Screens/PhoneCurrencyConverter.cpp"
        "${SRC_DIR}/Screens/PhoneDiceRoller.cpp"
        "${SRC_DIR}/Screens/PhoneAsciiArt.cpp"
        "${SRC_DIR}/Screens/PhoneAvatarEditor.cpp"
        "${SRC_DIR}/Screens/PhoneDrumKitScreen.cpp"
        "${SRC_DIR}/Screens/PhoneExpenses.cpp"

        # S-MP18k attempted to bulk-add 13 screens that the
        # Storage + MessageService stubs unblocked, but the dep
        # audit was too permissive — didn't check Modals/,
        # MAKERphoneConfig.h, or the full Element/ + Service/
        # closure for each screen. CI found ConvoScreen,
        # ProfileScreen, and possibly others pulling
        # LoRaService.h → RadioLib transitively.
        # Reverting the batch. Adding screens one-at-a-time with
        # careful audit is the better pace going forward.

        # S-MP18h: Model layer + LoRaPacket struct data + the
        # MessageService stub. This is the gating step for every
        # message-related screen and any Element that pulls in
        # the Notification chain.
        #
        # Model classes — small, self-contained, no external deps:
        "${SRC_DIR}/Model/Message.cpp"
        # LoRaPacket.cpp provides the .pack/.unpack vtable
        # definitions for MessagePacket / TextMessage / PicMessage
        # / AckPacket / PairPacket. The header is included by
        # MessageService.h so any consumer transitively needs the
        # struct definitions. Easier to compile the upstream .cpp
        # (no external deps) than to stub a dozen vtable entries.
        "${SRC_DIR}/Services/LoRaPacket.cpp"

        # Stub for MessageService — replaces upstream's .cpp
        # which would pull in LoRaService → RadioLib → Util/Task
        # chain we don't want. Same pattern as shim/InputLVGL.cpp:
        # upstream header unchanged, upstream .cpp excluded from
        # the build (just not listed in SRCS), our .cpp here
        # provides the public ABI as no-ops.
        "shim/MessageServiceStub.cpp"

        # S-MP18o: real (not stubbed) services with clean deps.
        # All compile against Audio/Piezo (CircuitOS) + Notes.h
        # (chatter_library) + Settings.h (chatter_library) +
        # LoopListener.h + MessageService.h (stubbed).
        #
        # PhoneOwnerEmoji        — emoji picker state. Self-contained.
        # PhoneLvglCost          — LVGL frame-cost telemetry.
        # PhoneSoftKeyTone       — softkey click-tone library.
        # PhoneRingtoneEngine    — base ringtone player.
        # PhoneSystemTones       — melody data table for ringtones
        #                          (2159 LOC, mostly note arrays).
        # PhoneMusicLibrary      — music asset index.
        # PhoneMusicPlaylists    — playlist editor backing service.
        # BuzzerService          — soft-key + msg-received beeps.
        #
        # These unblock five previously-deferred screens
        # (PhoneOwnerEmojiScreen, PhoneSoftKeyToneScreen,
        # PhonePlaylistsScreen, PhoneDiagScreen, ConvoScreen) plus
        # any code that uses BuzzerService for haptic feedback.
        "${SRC_DIR}/Services/PhoneOwnerEmoji.cpp"
        "${SRC_DIR}/Services/PhoneLvglCost.cpp"
        "${SRC_DIR}/Services/PhoneSoftKeyTone.cpp"
        "${SRC_DIR}/Services/PhoneRingtoneEngine.cpp"
        "${SRC_DIR}/Services/PhoneSystemTones.cpp"
        "${SRC_DIR}/Services/PhoneMusicLibrary.cpp"
        "${SRC_DIR}/Services/PhoneMusicPlaylists.cpp"
        "${SRC_DIR}/Services/BuzzerService.cpp"

        # S-MP18w: 13 more services. All real upstream code with
        # clean closure against current SRCS + transitive
        # header-only references into Screens/ (PhoneComposer.h,
        # PhoneCallHistory.h, PhoneBeatMaker.h — all three pull
        # only LVScreen + InputListener which we already have).
        #
        # Composer family (RTTTL + playback + storage):
        "${SRC_DIR}/Services/PhoneComposerRtttl.cpp"
        "${SRC_DIR}/Services/PhoneComposerPlayback.cpp"
        "${SRC_DIR}/Services/PhoneComposerStorage.cpp"
        # Ringtone family:
        "${SRC_DIR}/Services/PhoneRingtoneLibrary.cpp"
        "${SRC_DIR}/Services/PhoneAlarmTone.cpp"
        "${SRC_DIR}/Services/PhoneContactRingtone.cpp"
        # Vibration family:
        "${SRC_DIR}/Services/PhoneVibrationEngine.cpp"
        "${SRC_DIR}/Services/PhoneVibrationLibrary.cpp"
        # Storage-backed services (NVS):
        "${SRC_DIR}/Services/PhoneCallHistoryStorage.cpp"
        "${SRC_DIR}/Services/PhoneBeatMakerStorage.cpp"
        # Stand-alone services:
        "${SRC_DIR}/Services/MissedCallLog.cpp"
        "${SRC_DIR}/Services/PhoneVirtualPet.cpp"
        "${SRC_DIR}/Services/PhoneAlarmService.cpp"

        # S-MP19/3: Services/Pair state machine. Pulled into the
        # link graph by FriendsScreen now that PhoneMainMenu's
        # Contacts tile dispatches to FriendsScreen. All five
        # files compile against the stubbed LoRaService /
        # ProfileService / Storage; the pair-flow operates on
        # empty repos so the state machine fires but never
        # produces a match.
        "${SRC_DIR}/Services/Pair/State.cpp"
        "${SRC_DIR}/Services/Pair/BroadcastState.cpp"
        "${SRC_DIR}/Services/Pair/RequestState.cpp"
        "${SRC_DIR}/Services/Pair/AcknowledgeState.cpp"
        "${SRC_DIR}/Services/Pair/PairService.cpp"

        # S-MP18w: 5 independent services + 8 Elements with
        # clean deps. Unblocks the alarm/call/composer/dialer/
        # home-screen cluster of screens in subsequent commits.
        "${SRC_DIR}/Services/MissedCallLog.cpp"
        "${SRC_DIR}/Services/PhoneRingtoneLibrary.cpp"
        "${SRC_DIR}/Services/PhoneVibrationEngine.cpp"
        "${SRC_DIR}/Services/PhoneVibrationLibrary.cpp"
        "${SRC_DIR}/Services/PhoneVirtualPet.cpp"

        # S-MP18m: services + modals + elements that compile
        # cleanly now that LoRaService is stubbed. ProfileService
        # is the canary — its .cpp does `#include "LoRaService.h"`
        # which was the previous blocker.
        "${SRC_DIR}/Services/ProfileService.cpp"

        # Modals/ContextMenu + Prompt: 30+ screens import these
        # for confirm/cancel flows. Both pull only Interface/ +
        # already-compiled infrastructure.
        "${SRC_DIR}/Modals/ContextMenu.cpp"
        "${SRC_DIR}/Modals/Prompt.cpp"

        # Elements unblocked by services-now-stubbed:
        #   Avatar         — base for EditableAvatar; minimal deps
        #   EditableAvatar — Avatar + InputLVGL
        #   TextEntry      — InputLVGL + LVObject + Fonts; common
        #                    on settings / profile / compose screens
        #   ColorBox       — LVObject + InputLVGL; theme picker
        "${SRC_DIR}/Elements/Avatar.cpp"
        "${SRC_DIR}/Elements/EditableAvatar.cpp"
        "${SRC_DIR}/Elements/TextEntry.cpp"
        "${SRC_DIR}/Elements/ColorBox.cpp"

        # S-MP18q: six more Elements that unblock messaging /
        # friends / call screens. All clean against current SRCS.
        #   User             — friend avatar + nickname tile;
        #                      needs ProfileService (in S-MP18m)
        #                      + Avatar.
        #   ListItem         — generic list row.
        #   PhonePixelAvatar — procedural pixel avatar; matches
        #                      PhoneContacts::deriveSeed output.
        #   PhoneMessageRow  — InboxScreen row with last message
        #                      preview. Pulls MessageService +
        #                      ProfileService + Storage (all
        #                      stubbed or compiled).
        #   PhoneEnvelopeFly — outbound-message animation.
        #   UserWithMessage  — User + most-recent message preview.
        "${SRC_DIR}/Elements/User.cpp"
        "${SRC_DIR}/Elements/ListItem.cpp"
        "${SRC_DIR}/Elements/PhonePixelAvatar.cpp"
        "${SRC_DIR}/Elements/PhoneMessageRow.cpp"
        "${SRC_DIR}/Elements/PhoneEnvelopeFly.cpp"
        "${SRC_DIR}/Elements/UserWithMessage.cpp"

        # S-MP18w: Elements unblocked by Service additions and
        # base-Element availability. Each has clean deps against
        # current SRCS.
        #
        #   PhoneChargeBars       cell-style charge-bar widget
        #                         (used by PhoneHomeScreen)
        #   PhoneConfettiOverlay  birthday/celebrate confetti
        #   PhoneOperatorBanner   carrier name banner (SIM info)
        #   PhoneYawnOverlay      idle "yawn" animation
        #   PhoneMenuGrid         3x3 icon-tile grid for menus
        #   PhoneDialerPad        12-key dialer pad
        "${SRC_DIR}/Elements/PhoneChargeBars.cpp"
        "${SRC_DIR}/Elements/PhoneConfettiOverlay.cpp"
        "${SRC_DIR}/Elements/PhoneOperatorBanner.cpp"
        "${SRC_DIR}/Elements/PhoneYawnOverlay.cpp"
        "${SRC_DIR}/Elements/PhoneMenuGrid.cpp"
        "${SRC_DIR}/Elements/PhoneDialerPad.cpp"

        # PhoneVirtualPet — single virtual-pet state service.
        # Self-contained (LoopListener + PhoneClock + nvs only).
        # Unblocks SettingsScreen which references it.
        "${SRC_DIR}/Services/PhoneVirtualPet.cpp"

        # S-MP18s: composition Elements unblocked by what's
        # already compiled.
        #
        # ConvoView         MessageService -> visual bridge for
        #                   one conversation. ConvoView.cpp had
        #                   two max(int, int32_t) calls that
        #                   failed under modern C++ STL template
        #                   deduction; S-MP18t patches both call
        #                   sites to cast 0 → int32_t and gates
        #                   them back in.
        # ConvoMessage      pixel-bubble line in a conversation
        # ConvoBox          scrolling list of ConvoMessages.
        #                   Constructs a ConvoView; needs the
        #                   S-MP18t patch.
        # PicMenu           sticker / canned-pic picker modal
        # PhoneT9Input      T9-keypad text composer (~730 LOC)
        # EmojiMenu         emoji picker
        # NotificationElement  unread badge over status bar
        "${SRC_DIR}/Interface/ConvoView.cpp"
        "${SRC_DIR}/Elements/ConvoMessage.cpp"
        "${SRC_DIR}/Elements/ConvoBox.cpp"
        "${SRC_DIR}/Elements/PicMenu.cpp"
        "${SRC_DIR}/Elements/PhoneT9Input.cpp"
        "${SRC_DIR}/Elements/EmojiMenu.cpp"
        "${SRC_DIR}/Elements/NotificationElement.cpp"

        # S-MP18w: 11 more Elements with clean closure.
        #   PhoneT9Vocab            — T9-input vocabulary tries
        #   GameItem                — tile in GamesScreen menu
        #   PhoneEqualizerVisualiser  bars from ringtone playback
        #   PhoneChargeBars         — animated battery during charge
        #   PhoneNotificationPreview  inbox / missed-call preview
        #   UnlockSlide             — lock-screen swipe gesture
        #   PhoneConfettiOverlay    — birthday / level-up confetti
        #   PhoneYawnOverlay        — idle-mood yawn animation
        #   PhoneOperatorBanner     — carrier name strip
        #   PhoneMenuGrid           — icon grid for PhoneMainMenu
        #   PhoneDialerPad          — keypad for PhoneDialerScreen
        "${SRC_DIR}/Elements/PhoneT9Vocab.cpp"
        "${SRC_DIR}/Elements/GameItem.cpp"
        "${SRC_DIR}/Elements/PhoneEqualizerVisualiser.cpp"
        "${SRC_DIR}/Elements/PhoneChargeBars.cpp"
        "${SRC_DIR}/Elements/PhoneNotificationPreview.cpp"
        "${SRC_DIR}/Elements/UnlockSlide.cpp"
        "${SRC_DIR}/Elements/PhoneConfettiOverlay.cpp"
        "${SRC_DIR}/Elements/PhoneYawnOverlay.cpp"
        "${SRC_DIR}/Elements/PhoneOperatorBanner.cpp"
        "${SRC_DIR}/Elements/PhoneMenuGrid.cpp"
        "${SRC_DIR}/Elements/PhoneDialerPad.cpp"

        # S-MP18w (continued): 8 Elements with clean deps. Unblock
        # PhoneHangman / PhoneWordle (T9Vocab), PhoneMainMenu
        # (MenuGrid), PhoneDialerScreen (DialerPad — though that
        # screen is still gated on Games), LockScreen + PhoneHomeScreen
        # (ChargeBars, ConfettiOverlay, OperatorBanner, YawnOverlay,
        # UnlockSlide).
        "${SRC_DIR}/Elements/PhoneT9Vocab.cpp"
        "${SRC_DIR}/Elements/PhoneMenuGrid.cpp"
        "${SRC_DIR}/Elements/PhoneDialerPad.cpp"
        "${SRC_DIR}/Elements/PhoneChargeBars.cpp"
        "${SRC_DIR}/Elements/UnlockSlide.cpp"
        "${SRC_DIR}/Elements/PhoneOperatorBanner.cpp"
        "${SRC_DIR}/Elements/PhoneConfettiOverlay.cpp"
        "${SRC_DIR}/Elements/PhoneYawnOverlay.cpp"

        # The original ProfileScreen target — compiles after all
        # the above land. Pulls ProfileService (just added) +
        # Modals (just added) + Elements (just added) + Storage
        # stub + MessageService stub. Should now be clean.
        "${SRC_DIR}/Screens/ProfileScreen.cpp"

        # S-MP18n: four more screens with strict-clean deps —
        # each only pulls Status/SoftKey/Wallpaper + already-
        # compiled services. Audited via grep over the explicit
        # include lines (not the broad regex that mis-passed
        # ConvoScreen earlier).
        "${SRC_DIR}/Screens/PhoneHomeLayoutScreen.cpp"
        "${SRC_DIR}/Screens/PhoneLockWidgetScreen.cpp"
        "${SRC_DIR}/Screens/PhoneHapticsScreen.cpp"
        "${SRC_DIR}/Screens/PhoneImeiRevealScreen.cpp"

        # S-MP18p: four screens whose service deps are now
        # available after S-MP18o.
        "${SRC_DIR}/Screens/PhoneOwnerEmojiScreen.cpp"
        "${SRC_DIR}/Screens/PhoneSoftKeyToneScreen.cpp"
        "${SRC_DIR}/Screens/PhonePlaylistsScreen.cpp"
        "${SRC_DIR}/Screens/PhoneDiagScreen.cpp"

        # S-MP18r: three core messaging screens, unblocked by
        # the S-MP18q Element batch.
        "${SRC_DIR}/Screens/InboxScreen.cpp"
        "${SRC_DIR}/Screens/FriendsScreen.cpp"
        "${SRC_DIR}/Screens/PhoneCallEnded.cpp"

        # S-MP18t: ConvoScreen — pulls ConvoBox + ConvoView,
        # both gated by the source patch in this session.
        "${SRC_DIR}/Screens/ConvoScreen.cpp"

        # S-MP18w: three core flow screens unblocked by the
        # six new Elements + PhoneVirtualPet + SleepService stub
        # added above. PhoneDialerScreen was originally a fourth
        # but its .cpp includes Games/Snake/Snake.h (cheat-code
        # for a hidden snake game when dialing *#GAME), which
        # pulls Games/GameEngine → glm.h. Held back for the
        # games-engine session.
        "${SRC_DIR}/Screens/PhoneHomeScreen.cpp"
        "${SRC_DIR}/Screens/PhoneMainMenu.cpp"
        "${SRC_DIR}/Screens/SettingsScreen.cpp"

        # S-MP18u: bulk batch of 19 screens that the strict audit
        # script (/tmp/audit.sh) shows have full include closure
        # in the current chatter_app SRCS. Top-level Element /
        # Service / Modal includes all resolve. Transitive issues
        # surface in CI if any.
        #
        # MainMenu was originally in this batch but pulls
        # Screens/GamesScreen.h → Games/GameEngine/Rendering/
        # PixelDim.hpp → glm.h (not vendored). The legacy Chatter
        # main-menu would need either glm-port or a stubbed
        # GamesScreen header. Held back for the games-engine
        # session.
        "${SRC_DIR}/Screens/PhoneAboutScreen.cpp"
        "${SRC_DIR}/Screens/PhoneActiveCall.cpp"
        "${SRC_DIR}/Screens/PhoneBirthdayReminders.cpp"
        "${SRC_DIR}/Screens/PhoneBootSplash.cpp"
        "${SRC_DIR}/Screens/PhoneCameraScreen.cpp"
        "${SRC_DIR}/Screens/PhoneContactDetail.cpp"
        "${SRC_DIR}/Screens/PhoneContactEdit.cpp"
        "${SRC_DIR}/Screens/PhoneContactWallpaperPicker.cpp"
        "${SRC_DIR}/Screens/PhoneContactsScreen.cpp"
        "${SRC_DIR}/Screens/PhoneCountdown.cpp"
        "${SRC_DIR}/Screens/PhoneFindFriendsRadar.cpp"
        "${SRC_DIR}/Screens/PhoneGalleryScreen.cpp"
        # PhoneGamesScreen.cpp also pulls Games/GameEngine →
        # glm.h, same reason as MainMenu. Held back.
        "${SRC_DIR}/Screens/PhoneHabits.cpp"
        "${SRC_DIR}/Screens/PhoneHelicopter.cpp"
        "${SRC_DIR}/Screens/PhoneKaraokeScreen.cpp"
        "${SRC_DIR}/Screens/PhoneLunarLander.cpp"
        "${SRC_DIR}/Screens/PhoneMagic8Ball.cpp"
        "${SRC_DIR}/Screens/PhoneMemoryAudit.cpp"

        # S-MP18v: 39 more screens passing the strict-and-
        # transitive audit (script /tmp/audit_screens.sh +
        # transitive-includes-screen check). Every Element/Modal/
        # Service include resolves to something already in SRCS,
        # AND every #include of a sibling Screens/X.h points at
        # something also being added in this batch.
        #
        # The bulk is utility/game screens that have entirely
        # self-contained logic.
        "${SRC_DIR}/Screens/PhoneMemoryMatch.cpp"
        "${SRC_DIR}/Screens/PhoneMinesweeper.cpp"
        "${SRC_DIR}/Screens/PhoneMoodLog.cpp"
        "${SRC_DIR}/Screens/PhoneNotepad.cpp"
        "${SRC_DIR}/Screens/PhoneOperatorScreen.cpp"
        "${SRC_DIR}/Screens/PhoneOwnerNameScreen.cpp"
        "${SRC_DIR}/Screens/PhonePinball.cpp"
        "${SRC_DIR}/Screens/PhonePomodoro.cpp"
        "${SRC_DIR}/Screens/PhonePowerDown.cpp"
        "${SRC_DIR}/Screens/PhonePowerOffMessageScreen.cpp"
        "${SRC_DIR}/Screens/PhoneProfileScreen.cpp"
        "${SRC_DIR}/Screens/PhoneRadio.cpp"
        "${SRC_DIR}/Screens/PhoneReversi.cpp"
        "${SRC_DIR}/Screens/PhoneScratchpad.cpp"
        "${SRC_DIR}/Screens/PhoneSettingsScreen.cpp"
        "${SRC_DIR}/Screens/PhoneSimPinScreen.cpp"
        "${SRC_DIR}/Screens/PhoneSimon.cpp"
        "${SRC_DIR}/Screens/PhoneSlidingPuzzle.cpp"
        "${SRC_DIR}/Screens/PhoneSnakesLadders.cpp"
        "${SRC_DIR}/Screens/PhoneSokoban.cpp"
        "${SRC_DIR}/Screens/PhoneSolitaire.cpp"
        "${SRC_DIR}/Screens/PhoneSoundScreen.cpp"
        "${SRC_DIR}/Screens/PhoneSpeedDialScreen.cpp"
        "${SRC_DIR}/Screens/PhoneSteps.cpp"
        "${SRC_DIR}/Screens/PhoneStopwatch.cpp"
        "${SRC_DIR}/Screens/PhoneStressReliever.cpp"
        "${SRC_DIR}/Screens/PhoneSudoku.cpp"
        "${SRC_DIR}/Screens/PhoneTetris.cpp"
        "${SRC_DIR}/Screens/PhoneThemeScreen.cpp"
        "${SRC_DIR}/Screens/PhoneTicTacToe.cpp"
        "${SRC_DIR}/Screens/PhoneTimer.cpp"
        "${SRC_DIR}/Screens/PhoneTimers.cpp"
        "${SRC_DIR}/Screens/PhoneTodo.cpp"
        "${SRC_DIR}/Screens/PhoneTowerDefence.cpp"
        "${SRC_DIR}/Screens/PhoneUnitConverter.cpp"
        "${SRC_DIR}/Screens/PhoneWallpaperScreen.cpp"
        "${SRC_DIR}/Screens/PhoneWhackAMole.cpp"
        "${SRC_DIR}/Screens/PhoneWorldClock.cpp"
        "${SRC_DIR}/Screens/UserHWTest.cpp"

        # S-MP18x: 13 more screens whose service / Element deps
        # are all now in the chatter_app SRCS. Strict audit clean
        # against current state (S-MP18w batch landed all
        # required services + Elements).
        #
        # S-MP18x/1: MainMenu was originally in this batch but
        # pulls GamesScreen.h which transitively pulls
        # Games/GameEngine/Rendering/PixelDim.hpp -> <glm.h>.
        # That's deeper than 'just header includes' — the header
        # cascade can't be satisfied without vendoring glm or
        # patching upstream. Deferred to a games-engine session.
        "${SRC_DIR}/Screens/PhoneAlarmClock.cpp"
        "${SRC_DIR}/Screens/PhoneAlarmTonePicker.cpp"
        "${SRC_DIR}/Screens/PhoneBeatMaker.cpp"
        "${SRC_DIR}/Screens/PhoneCallHistory.cpp"
        "${SRC_DIR}/Screens/PhoneComposer.cpp"
        "${SRC_DIR}/Screens/PhoneContactRingtonePicker.cpp"
        "${SRC_DIR}/Screens/PhoneHangman.cpp"
        "${SRC_DIR}/Screens/PhoneIncomingCall.cpp"
        "${SRC_DIR}/Screens/PhoneMusicPlayer.cpp"
        "${SRC_DIR}/Screens/PhoneProfileRingtoneScreen.cpp"
        "${SRC_DIR}/Screens/PhoneVirtualPet.cpp"
        "${SRC_DIR}/Screens/PhoneWordle.cpp"

        # S-MP20/2: smallest first user of the glm vendor landed
        # at S-MP20/1. GameObject.cpp is a leaf -- its only
        # non-stdlib includes are GameObject.h + RenderComponent.h
        # + PixelDim.hpp + CollisionComponent.h. PixelDim.hpp
        # pulls <glm.h>, which our Arduino-GLM shim forwards to
        # <glm/glm.hpp> + <glm/ext.hpp>. Its body only move-
        # constructs shared_ptrs and assigns glm::vec2 / float --
        # no constructor calls into RenderSystem/CollisionSystem,
        # so the link closure stays small: nothing new is
        # required at link time.
        #
        # If this builds + links, the glm include path is
        # validated under real compilation and the rest of the
        # GameEngine .cpp files can be added one (or two) at a
        # time without further surprises in the glm path itself.
        "${SRC_DIR}/Games/GameEngine/GameObject.cpp"

        # S-MP20/3: Collision subsystem -- 5 leaf files that
        # together form the geometric-collision side of the
        # GameEngine. CollisionComponent + CircleCC + RectCC +
        # PolygonCC are pure value types over glm::vec2; the
        # only external includes are Arduino.h (for PolygonCC's
        # math) and the vendored <glm.h>. CollisionSystem
        # inherits from GameSystem and owns four Walls
        # (RectCC's). Nothing in the GameObject anchor reaches
        # into CollisionSystem at link time, so as long as no
        # screen instantiates a CollisionSystem these TUs are
        # gc'd by -ffunction-sections + --gc-sections. The point
        # of this commit is to validate the .cpp's compile
        # cleanly under our toolchain + glm shim, building up
        # the engine surface before the games themselves land.
        "${SRC_DIR}/Games/GameEngine/Collision/CollisionComponent.cpp"
        "${SRC_DIR}/Games/GameEngine/Collision/CircleCC.cpp"
        "${SRC_DIR}/Games/GameEngine/Collision/RectCC.cpp"
        "${SRC_DIR}/Games/GameEngine/Collision/PolygonCC.cpp"
        # S-MP20/4: CollisionSystem.cpp re-enabled. The three
        # toolchain diffs flagged by S-MP20/3/1 are now addressed:
        #   (1) GLM_ENABLE_EXPERIMENTAL is defined at the top of
        #       shim_includes/arduino_glm_compat.h, which is
        #       force-included via -include on this component.
        #       That unblocks <glm/gtx/matrix_transform_2d.hpp>
        #       and <glm/gtx/vector_angle.hpp>.
        #   (2) TFT_GREEN / TFT_RED (and the full canonical
        #       Bodmer/TFT_eSPI 565 palette) are now defined in
        #       circuitos_shim/include/TFT_eSPI.h.
        #   (3) Sprite gained a 2-arg drawPixel(x, y) overload
        #       (forwards to the 3-arg form with TFT_WHITE).
        # CollisionSystem inherits from GameSystem and owns four
        # Walls (RectCCs). No screen instantiates a CollisionSystem
        # yet, so the TU is still gc'd at link time -- this commit
        # just validates it compiles cleanly under our toolchain.
        "${SRC_DIR}/Games/GameEngine/Collision/CollisionSystem.cpp"

        # S-MP20/4b: Rendering subsystem -- starting safe with
        # RenderComponent.cpp, the 4-method getter/setter leaf at
        # the base of the Rendering inheritance hierarchy
        # (SpriteRC, StaticRC, AnimRC all derive from it). The
        # only external includes are <Display/Sprite.h> (already
        # shimmed and exercised by every chatter_app TU that
        # uses Sprite directly) and PixelDim.hpp (already
        # validated under glm via S-MP20/2's GameObject.cpp).
        # RenderComponent itself is an abstract base -- the pure
        # virtual push(Sprite*, PixelDim, float) ensures no one
        # can instantiate it, and no screen in chatter_app
        # references a RenderComponent yet, so this TU is still
        # gc'd at link time. Compile-only validation, prepping
        # the path for Sprite/Static/Anim RC + RenderSystem in
        # subsequent fires.
        "${SRC_DIR}/Games/GameEngine/Rendering/RenderComponent.cpp"

        # S-MP20/4c: RenderSystem.cpp -- the second Rendering leaf
        # (ctor + update() only; 34 lines). Inherits from
        # GameSystem; update() iterates getObjects() and per-layer
        # dispatches RenderComponent::push() onto a Sprite canvas.
        # External deps:
        #   - <set>, <map> -- stdlib, already exercised by
        #     CollisionSystem.cpp (S-MP20/4).
        #   - "RenderSystem.h" -> <Display/Sprite.h> + GameSystem.h
        #     + GameObject.h (already pulled by RenderComponent.cpp
        #     and the Collision/* leaves under SRCS).
        # The gc-sections gambit: no file in SRCS references
        # RenderSystem (verified by grep across the 212-entry SRCS
        # list -- only Game.h pulls it, and Game.h is not in SRCS),
        # so --gc-sections should drop the entire TU at link time.
        # If, despite that, the linker retains RenderSystem::update,
        # it will fail to resolve GameSystem::getObjects() (defined
        # in GameSystem.cpp, NOT in SRCS) and GameSystem::GameSystem
        # (same). In that case revert this commit and move to
        # S-MP20/4d (StaticRC.cpp) per the prior fire's checkpoint.
        # Compile-only validation, same pattern as the prior leaves.
        "${SRC_DIR}/Games/GameEngine/Rendering/RenderSystem.cpp"

        # S-MP20/4d: StaticRC.cpp -- third Rendering leaf. Wraps a
        # File-backed RGB565 icon at fixed dimensions; push()
        # branches on rot == 0 to either drawIcon-direct or
        # drawIcon-into-a-temp-Sprite-then-pushRotateZoomWithAA.
        # External deps (each addressed by the shim diff in this
        # commit):
        #   - File overload of Sprite::drawIcon (added to Sprite.h
        #     + MP24Sprite.cpp, no-op shim).
        #   - Sprite::pushRotateZoomWithAA(x, y, rot, sx, sy, t)
        #     (same -- declared in Sprite.h, no-op definition in
        #     MP24Sprite.cpp).
        #   - TFT_TRANSPARENT 0x0120 sentinel (added to TFT_eSPI.h
        #     shim alongside the existing canonical Bodmer
        #     palette).
        # The TU is still gc'd at link time -- nothing in SRCS
        # instantiates a StaticRC yet (only Game.h chain instantiates
        # the Rendering leaves, and Game.h is not in SRCS). Compile-
        # only validation, same pattern as the prior leaves.
        "${SRC_DIR}/Games/GameEngine/Rendering/StaticRC.cpp"

        # S-MP20/4e: SpriteRC.cpp -- fourth Rendering leaf
        # (18 lines). Owns a std::shared_ptr<Sprite> built
        # via std::make_shared<Sprite>((Sprite*)nullptr,
        # dim.x, dim.y); push() branches on rot == 0 to
        # either a direct sprite->push(parent, x, y) or a
        # pushRotateZoomWithAA(parent, mid_x, mid_y, rot, 1,
        # 1, TFT_TRANSPARENT) self-blit. External deps
        # (each addressed by the shim diff in this commit):
        #   - class Sprite : public TFT_eSprite (Sprite.h
        #     shim now inherits the TFT_eSprite base so the
        #     `static_cast<TFT_eSprite*>(sprite.get())
        #     ->setSwapBytes(false)` ctor body line
        #     compiles. setSwapBytes is inherited from
        #     TFT_eSPI and remains a no-op stub. The two
        #     non-delegating Sprite ctors now also
        #     forward their parent argument to the
        #     TFT_eSprite base via the init list.
        #   - int16_t Sprite::width() / Sprite::height()
        #     (added to Sprite.h shim alongside the
        #     existing getWidth() / getHeight() pair --
        #     upstream SpriteRC.cpp calls the .width() /
        #     .height() names to compute the rotation
        #     mid-point).
        #   - 7-arg Sprite::pushRotateZoomWithAA(Sprite*,
        #     int16_t, int16_t, float, float, float,
        #     uint16_t) -- declared in Sprite.h + defined
        #     no-op in MP24Sprite.cpp, alongside the 6-arg
        #     variant landed in S-MP20/4d.
        # The TU is still gc'd at link time -- nothing in
        # SRCS instantiates a SpriteRC yet (only Game.h
        # chain does, and Game.h is not in SRCS). Compile-
        # only validation, same pattern as the prior leaves.
        "${SRC_DIR}/Games/GameEngine/Rendering/SpriteRC.cpp"

        # S-MP20/4f: AnimRC.cpp -- fifth and last Rendering
        # leaf (48 lines). Owns a std::unique_ptr<
        # GIFAnimatedSprite> built via std::make_unique<
        # GIFAnimatedSprite>(nullptr, file); push() branches
        # on rot == 0 to either gif->push(parent, pos.x,
        # pos.y) or gif->pushRotate(parent, pos.x, pos.y,
        # rot). Also calls start() / stop() / reset() /
        # setLoopMode() / setLoopDoneCallback() on the gif.
        # External deps (addressed by the shim added in this
        # same commit):
        #   - class GIFAnimatedSprite (NEW shim header at
        #     circuitos_shim/include/Display/
        #     GIFAnimatedSprite.h -- forward-declares a
        #     class with the ctor + six method signatures
        #     AnimRC.cpp calls. Replaces the upstream
        #     circuitos/src/Display/GIFAnimatedSprite.h
        #     which would otherwise resolve via the
        #     circuitos REQUIRES; circuitos_shim is FIRST
        #     in chatter_app's REQUIRES so its include path
        #     wins for <Display/...> lookups.
        #   - GIFAnimatedSprite ctor + start/stop/reset/
        #     push/pushRotate/setLoopMode/
        #     setLoopDoneCallback -- declared no-op in
        #     MP24Sprite.cpp (piggy-backed on the existing
        #     TU for cohesion -- one ctor + six methods
        #     don't warrant a new MP24GIFAnimatedSprite.cpp
        #     yet).
        #   - <Util/GIF.h> (unchanged upstream header in
        #     circuitos, already on the include path)
        #     supplies GIF::LoopMode { Auto, Single,
        #     Infinite } that AnimRC + the shim both
        #     reference.
        # Same gc-sections gambit as /4d + /4e: nothing in
        # SRCS instantiates AnimRC yet (only Game.h chain
        # does, and Game.h is not in SRCS). With this leaf
        # landed, the Rendering subsystem's .cpp coverage
        # is complete and S-MP20/5 (Game.cpp + GameSystem.
        # cpp) is unblocked from the Rendering-deps angle.
        "${SRC_DIR}/Games/GameEngine/Rendering/AnimRC.cpp"

        # S-MP20/5: Game.cpp + GameSystem.cpp -- the engine
        # core. With all five Rendering leaves (RenderComponent
        # + RenderSystem + StaticRC + SpriteRC + AnimRC) and all
        # five Collision leaves (CollisionComponent + CircleCC +
        # RectCC + PolygonCC + CollisionSystem) plus GameObject
        # in SRCS, Game's ctor member-init list (`render(this,
        # Chatter.getDisplay()->getBaseSprite()), collision(this)`)
        # would resolve cleanly at link time IF the linker
        # retained Game::Game.
        #
        # The gc-sections gambit still holds though: NO file in
        # the 229-entry SRCS list pulls Game.h or any header
        # that pulls Game.h (audit at S-MP20/5 confirmed --
        # only PhoneDialerScreen.cpp + PhoneGamesScreen.cpp +
        # MainMenu.cpp + GamesScreen.cpp + ShutdownService.cpp +
        # SleepService.cpp pull it, and all six are excluded
        # from SRCS -- the first four held back for the games
        # session, the last two replaced by stubs). With nothing
        # referencing Game's vtable / ctor / instance methods,
        # --gc-sections drops both TUs and the binary stays at
        # 883408 bytes.
        #
        # If, contrary to expectation, the linker retains the
        # TUs, the only previously-unresolved external was
        # `extern Game* startedGame;` from Game.cpp's start() /
        # stop() bodies. The same commit defines it as
        # `Game* startedGame = nullptr;` alongside the existing
        # `bool gameStarted = false;` in SleepServiceStub.cpp
        # (forward-declared `class Game;` -- never derefed in
        # that TU). All other Game.cpp references (Chatter,
        # InputChatter::getInputInstance, FSLVGL::loadCache,
        # ChirpSystem::stop via Audio member) are at sites
        # inside function bodies that go away with the TU.
        #
        # GameSystem.cpp is a four-line file: ctor + getObjects()
        # which returns the Game::objects friend-accessed
        # private set. Same gc-sections gambit.
        #
        # With Game.cpp + GameSystem.cpp landing, the engine
        # surface is compile-complete. Next leaves (Highscore,
        # then the four game implementations, then GamesScreen
        # / PhoneGamesScreen wiring) build on top of this.
        "${SRC_DIR}/Games/GameEngine/Game.cpp"
        "${SRC_DIR}/Games/GameEngine/GameSystem.cpp"

        # S-MP18j: Storage layer stub. Replaces upstream's
        # Storage.cpp + Repo.cpp + MessageRepo.cpp + ConvoRepo.cpp
        # + PhoneContacts.cpp, none of which we list in SRCS. The
        # stub provides Repo<T> template definitions + explicit
        # instantiations for Friend/PhoneContact/Message/Convo,
        # MessageRepo/ConvoRepo write/read overrides, the global
        # Repositories Storage instance, and the 28-function
        # PhoneContacts namespace.
        #
        # Why stubbed: upstream Repo.cpp pulls FS/RamFile.h, and
        # RamFile inherits fs::FileImpl which has more pure
        # virtuals than RamFile overrides under our toolchain
        # (S-MP18i investigation). All-no-op semantics are correct
        # for a fresh device with no saved messages / contacts.
        "shim/StorageStub.cpp"

        # S-MP18i attempted to compile the upstream Storage chain
        # (Repo<T> + MessageRepo + ConvoRepo + PhoneContacts +
        # the RamFile dep) and confirmed the exclusion comment on
        # circuitos/FS/ was right: RamFile inherits from FileImpl
        # which has more pure virtuals in arduino-esp32 3.3.8
        # than RamFile overrides — std::make_shared<RamFile>
        # fails 'invalid new-expression of abstract class type'.
        # Backed out; Storage layer needs its own stub session.

        # S-MP18l: LoRaService stub. Upstream LoRaService.cpp
        # drives a SX1278/LLCC68 LoRa radio via RadioLib. MP2.4
        # Decision B replaces LoRa with the Quectel EG912U-GL
        # cellular modem; we're not vendoring RadioLib. The stub
        # provides every public symbol LoRaService.h declares as
        # a no-op (send/getMessage/getProfile/getPair*/rand*/etc.)
        # plus the `LoRaService LoRa;` global.
        #
        # The header has 'LLCC68 radio' as a private member, so
        # the header itself needs LLCC68 to be a complete type.
        # shim_includes/RadioLib.h (added via PRIV_INCLUDE_DIRS
        # above) provides an empty LLCC68 class to satisfy that.
        #
        # This is the biggest remaining unblocker for
        # message/profile/pair/call screens. Combined with the
        # MessageService stub (S-MP18h) and Storage stub
        # (S-MP18j), most upstream screens should now have all
        # their service deps satisfied.
        "shim/LoRaServiceStub.cpp"

        # S-MP18w: SleepService stub. Upstream SleepService.cpp
        # pulls Games/GameEngine/Game.h + LockScreen.h + FSLVGL,
        # all of which transitively need glm.h or Lock-flow
        # Elements we don't have yet. Same pattern as the other
        # service stubs — no-op public API, no-op global Sleep
        # singleton. Behaviour: device stays awake indefinitely.
        # Acceptable for compile coverage; real power management
        # lands in S-MP15 territory.
        "shim/SleepServiceStub.cpp"

        # S-MP19/1: MP24Piezo provides PiezoImpl class methods +
        # the global `Piezo` instance that CircuitOS Audio/Piezo
        # leaves undefined under our ESP-IDF v5 build (the impl
        # picker in Piezo.cpp matches none of CIRCUITOS_TONE /
        # CIRCUITOS_PIEZO_PWM / CIRCUITOS_PIEZO_DAC). Surfaced as
        # link errors once S-MP19's nav wiring brought
        # PhoneRingtoneEngine into the link graph.
        # Routes to hal/piezo (the same backend boot chime + key
        # click use).
        "shim/MP24Piezo.cpp"

        # S-MP18y: three more screens. AutoPop.cpp is a tiny
        # (22 LOC) root-level helper used by screens that
        # auto-pop on BTN_BACK; PairScreen needs it.
        # LockScreen now compiles because SleepService is
        # stubbed (just above) and every Element it references
        # (UnlockSlide, PhoneChargeBars, PhoneNotificationPreview,
        # PhoneMissedCallFlash, UserWithMessage etc.) was added
        # in earlier batches.
        "${SRC_DIR}/AutoPop.cpp"
        "${SRC_DIR}/Screens/LockScreen.cpp"
        "${SRC_DIR}/Screens/PairScreen.cpp"

        # S-MP18d: first screen that USES the status bar + soft-key
        # bar + wallpaper combo. 113 LOC, takes an uppercase app
        # name in the ctor, renders 'NOT YET BUILT' under it. We're
        # going to use it as our new boot screen to force StatusBar
        # + SoftKeyBar into the link graph and exercise them at
        # runtime.
        "${SRC_DIR}/Screens/PhoneAppStubScreen.cpp"

        # Our replacement for src/InputLVGL.cpp. The upstream one is
        # intentionally NOT listed.
        "shim/InputLVGL.cpp"

        # Linker anchor — its single extern "C" symbol is what
        # mp24/main/app_main.cpp references to keep this whole
        # archive from being dropped by --gc-sections. See file
        # header for the static-archive linking explanation.
        "shim/Anchor.cpp"

        # S-MP17c: first real LVScreen subclass that runs on
        # hardware. Replaces the temporary three-button boot UI
        # in app_main. Proves the LVScreen → SCREEN_LOADED →
        # LoopManager defer → onStart pipeline works at runtime.
        "screens/TestScreen.cpp"

        # S-MP18b: factory wrapper that wires PhoneWelcomeScreen
        # into the boot path. Pre-populates Settings.ownerName so
        # the greeting label has content, then constructs +
        # starts the screen.
        "screens/WelcomeFactory.cpp"

        # S-MP18d: factory for PhoneAppStubScreen. Both factories
        # coexist; app_main picks one to call. Keeps WelcomeFactory
        # available as the easy revert path.
        "screens/AppStubFactory.cpp"

        # S-MP18z: factory for PhoneHomeScreen — the Sony-Ericsson
        # silhouette homescreen, the most representative MP2.4
        # screen we have. No navigation callbacks set, so buttons
        # flash but don't navigate (smoke-test only).
        "screens/HomeFactory.cpp"

    INCLUDE_DIRS
        # Nothing exported to other components yet. Screens will be
        # invoked from chatter_app's own entry point eventually.

    PRIV_INCLUDE_DIRS
        # So shim/InputLVGL.cpp can find src/InputLVGL.h.
        "${SRC_DIR}"

        # S-MP18l: shim_includes provides drop-in headers that
        # replace unavailable third-party libraries. Currently:
        #   RadioLib.h  - empty LLCC68 class to satisfy
        #                 LoRaService.h's 'LLCC68 radio' member.
        #   glm/        - mainline header-only OpenGL Mathematics
        #                 (g-truc/glm 1.0.1), vendored at S-MP20/1
        #                 so <glm/vec2.hpp>, <glm/ext.hpp>,
        #                 <glm/gtx/matrix_transform_2d.hpp>, and
        #                 <glm/gtx/vector_angle.hpp> resolve.
        #   glm.h       - Arduino-GLM compatibility shim that
        #                 forwards to <glm/glm.hpp> + <glm/ext.hpp>
        #                 so upstream src/Games/ files written
        #                 against the Arduino fork keep building.
        # Listed after src/ in case any future shim header has
        # the same name as a src/ file — src/ wins by being
        # first. None of the current shim headers have a same-
        # named counterpart on any other include path, so
        # ordering doesn't actually matter for them.
        "${CMAKE_CURRENT_LIST_DIR}/shim_includes"

    REQUIRES
        circuitos_shim           # FIRST: <Pins.hpp> override
        espressif__arduino-esp32 # <Arduino.h>
        lvgl__lvgl               # <lvgl.h>
        circuitos                # <Loop/LoopManager.h>, <Input/...>
        log
)

# Soften a few warnings that are pervasive in the legacy code and
# would otherwise be promoted to errors by ESP-IDF's strict default.
# Matches the policy applied to the circuitos component in S-MP13b
# so behaviour is consistent across components compiling old code.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=reorder
    -Wno-error=maybe-uninitialized
    -Wno-error=return-type
    -Wno-error=unused-variable
    -Wno-error=unused-but-set-variable
    -Wno-error=sign-compare
    # S-MP18h/1: LoRaPacket.cpp's unpack helpers have static
    # analysis flags on `buffer` writes the compiler can't prove
    # are after all read paths. Upstream code, not patching.
    -Wno-error=uninitialized
    # S-MP18f/1: PhoneCalculator.cpp snprintfs an int into a 23-char
    # buffer and the compiler can't prove the conversion fits.
    # Demoting to warning — the actual call sites pass values that
    # always fit in practice.
    -Wno-error=format-truncation
    # S-MP18m/1: ProfileScreen.cpp printfs uint32_t with %lu / %d.
    # The integer ABI between Linux and xtensa-esp-elf differs on
    # the width of unsigned int vs unsigned long, so upstream code
    # written against another platform's format specifiers triggers
    # -Wformat=. Source isn't ours to fix.
    -Wno-error=format
    # S-MP18m/1: ProfileService.cpp at line 60 / 122 deletes a
    # ProfileResponse / ProfilePacket via a base pointer where the
    # base class has no virtual destructor. Upstream pattern,
    # legitimate on the original toolchain but flagged as
    # -Werror=delete-non-virtual-dtor in modern GCC. Demoting.
    -Wno-error=delete-non-virtual-dtor
    # S-MP18w/1: UnlockSlide.cpp declares loop() (no args) which
    # hides LoopListener::loop(uint). Upstream pattern, harmless
    # in practice (caller invokes the (uint) overload via base
    # pointer; the no-arg member is just an internal helper).
    -Wno-error=overloaded-virtual
    # S-MP20/4/1: CollisionSystem::drawPolygon declares
    # `typedef glm::vec2 Point;` and never references the alias.
    # Modern gcc -Wunused-local-typedefs flags this as an error
    # under our -Werror=all default. Upstream code, harmless.
    -Wno-error=unused-local-typedefs
    # Arduino.h gets pre-included so 'String', 'pinMode', 'millis'
    # etc. resolve without each src .cpp having to remember to
    # include it. Same trick used for the circuitos build.
    -include Arduino.h
    # S-MP20/3: Arduino.h's WMath.h defines `radians`, `degrees`,
    # `sq`, `constrain`, `round`, `bit` as preprocessor macros
    # that collide with glm's same-named template functions. This
    # compat header is force-included AFTER Arduino.h, with the
    # sole job of #undef-ing those colliding macros so any
    # <glm/...> include parses cleanly. See the header file for
    # the exhaustive rationale + which macros are intentionally
    # left in place (abs, min, max).
    "SHELL:-include ${CMAKE_CURRENT_LIST_DIR}/shim_includes/arduino_glm_compat.h"
)
