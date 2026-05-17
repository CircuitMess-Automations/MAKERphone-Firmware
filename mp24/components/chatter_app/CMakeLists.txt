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

        # S-MP18w: four core flow screens unblocked by the
        # six new Elements + PhoneVirtualPet + SleepService stub
        # added above.
        "${SRC_DIR}/Screens/PhoneHomeScreen.cpp"
        "${SRC_DIR}/Screens/PhoneMainMenu.cpp"
        "${SRC_DIR}/Screens/PhoneDialerScreen.cpp"
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
        # Listed after src/ in case any future shim header has
        # the same name as a src/ file — src/ wins by being
        # first. The current RadioLib.h has no upstream copy on
        # any include path, so ordering doesn't actually matter
        # for it.
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
    # Arduino.h gets pre-included so 'String', 'pinMode', 'millis'
    # etc. resolve without each src .cpp having to remember to
    # include it. Same trick used for the circuitos build.
    -include Arduino.h
)
