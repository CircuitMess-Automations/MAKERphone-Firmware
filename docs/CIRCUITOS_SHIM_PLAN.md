# CircuitOS Shim Strategy (S-MP13+)

> **Status as of 2026-05-16:** S-MP13 (foundation) and the first
> piece of S-MP14 (MP24Input) are shipped on `main`. The next
> blocking architectural decision is the Display / Sprite /
> TFT_eSPI question — see §9 below.

## What's shipped

| Session | Commit | What it delivers |
|---|---|---|
| S-MP13a | `9cbdb2b` + `26bbe15` | arduino-esp32 3.3.8 as managed component, `app_main.cpp` with `initArduino()` |
| S-MP13b | `e0f1b1d` + 5 fix-forwards through `f4003a4` | CircuitOS vendored at `mp24/components/circuitos/` (MIT), 11 subsystems excluded |
| S-MP13c | `39d2064` | Chatter-Library wrapper at `mp24/components/chatter_library/` pointing at the existing `libraries/Chatter-Library/src/` tree |
| S-MP14a | `7cbe34f` | `MP24Input` class in `circuitos_shim`, subclasses CircuitOS `Input`, polls `hal/input_keypad` cached state in `scanButtons()` |

Binary size still **0x5fd00 / 0x200000 (81 % free)** — the linker
strips every shim class that's not yet instantiated.

## What's still open

## 1. Why this is the long pole

The CircuitOS shim is what stands between the working C HAL and the
existing 506-file C++ application code under `src/`. Every screen,
service, model, and storage class in that tree includes one or more
CircuitOS or Chatter-Library headers. Until those headers resolve to
real implementations, none of the application code can be ported,
no matter how good the underlying HAL is.

The C HAL is currently in good shape: display, input, audio, battery,
storage, modem, SMS, and call control all work without C++ dependencies.
The shim's job is **wrapping** the C HAL in the class shapes that the
existing C++ code expects, not reimplementing functionality.

## 2. Dependency surface (what the app actually needs)

From `grep -rh '^#include <' src/`, the angle-bracket includes that
have to resolve break into four buckets:

### CircuitOS (`github.com/CircuitMess/CircuitOS`, 77 headers)

Most-used by the existing firmware:

| Header | Use |
|---|---|
| `CircuitOS.h` | Umbrella — just re-includes `Arduino.h` upstream. |
| `Loop/LoopManager.h` | Static dispatch + `defer()` queue. Heart of the event loop. |
| `Loop/LoopListener.h` | Base class for anything that takes `loop(uint micros)`. |
| `Display/Display.h` | The `Display` class. Wraps `TFT_eSPI` (default) or `LovyanGFX`. Exposes a `Sprite*` base canvas. |
| `Display/Sprite.h` | Sprite — a `TFT_eSprite`-backed drawing surface. |
| `Input/Input.h` | Base `Input` class with listener fan-out. |
| `Input/InputListener.h` | Listener interface (`buttonPressed/Released/Held`). |
| `Input/InputShift.h` | Shift-register 16-button input. **Not used on MP2.4** — replaced by I²C-expander variant. |
| `Input/InputI2C.h` + `Input/I2cExpander.h` | I²C-expander input. Closer match to MP2.4. |
| `Audio/Piezo.h` | `extern PiezoImpl Piezo` singleton. PWM- or DAC-backed on Chatter; MP2.4 uses I²S amp. |
| `Audio/ChirpSystem.h` | Multi-tone composition over `Piezo`. |
| `Battery/BatteryService.h` | **In Chatter-Library, not CircuitOS** — see below. |
| `Buffer/RingBuffer.h` | Header-only ring buffer template. |
| `Util/Vector.h` | `std::vector<T>` subclass with `relocate / swap / remove / indexOf`. |
| `Util/WithListeners.h` | Mixin for adding `addListener / removeListener` to any class. |
| `Util/HWRevision.h` | Reads a board-rev pin set; returns int. |
| `Util/Settings.h` | NVS- or LittleFS-backed key/value store. |
| `Util/Task.h` | FreeRTOS task wrapper. |
| `Sync/Mutex.h` | Wraps `SemaphoreHandle_t`. |
| `Support/Context.h` | DI-style container for cross-screen state. |
| `FS/CompressedFile.h` + `FS/RamFile.h` | File-format adapters. |
| `UI/Image.h` | Bitmap → Sprite blit helper. |
| `Network/Net.h` | Wi-Fi/HTTP — **not used by the phone firmware**, can be skipped. |
| `Devices/Motion/*` | IMU drivers — **not on MP2.4**, can be skipped. |

### Chatter-Library (vendored at `libraries/Chatter-Library/`)

| Header | Use |
|---|---|
| `Chatter.h` | `extern ChatterImpl Chatter` singleton. `Chatter.begin(backlight)` bootstraps display + input + Piezo + SPIFFS + Settings + battery + LoRa SPI. |
| `ChatterDisplay.h` | TFT_eSPI panel definitions for the two Chatter hardware revs. |
| `Battery/BatteryService.h` | `extern BatteryService Battery`. Reads a divider on a fixed ADC pin. |
| `Settings.h` | Project-level settings persistence (subclasses CircuitOS `Util/Settings`). |
| `Notes.h` | `NOTE_C4 = 261`, etc. constants. Pure header. |
| `Pins.hpp` | Board pin definitions — **completely replaced** for MP2.4. |

### Third-party Arduino libraries

- **`Arduino.h`** — `millis`, `delay`, `digitalRead`, `digitalWrite`, `Serial`, `String`, `byte`, `uint`. The foundation.
- **`SPI.h`, `SPIFFS.h`, `SD.h`, `FS.h`** — Arduino's standard filesystem and bus APIs.
- **`lvgl.h`** — LVGL graphics framework (mp24 firmware will need this anyway for the existing UI).
- **`TFT_eSPI`** — CircuitOS `Display.h` includes it by default. The phone firmware grabs the underlying `TFT_eSPI&` to push framebuffers to LVGL.
- **`RadioLib.h`** — LoRa stack. **Drop entirely** on MP2.4; cellular replaces it.

### Standard C++

`<algorithm>`, `<climits>`, `<cmath>`, `<cstdint>`, `<functional>`,
`<iostream>`, `<list>`, `<map>`, `<memory>`, `<queue>`, `<set>`, `<string>`,
`<type_traits>`, `<unordered_map>`, `<unordered_set>`, `<utility>`,
`<vector>`. ESP-IDF's libstdc++ provides all of these out of the box.

## 3. Three architectural options

### Option A — Vendor `arduino-esp32` as an ESP-IDF component

Pull `espressif/arduino-esp32` in as a managed component. Its `Arduino.h`,
`Serial`, `SPI`, `SPIFFS`, `FS` etc. all become real. Then vendor
upstream `CircuitOS` and `Chatter-Library` as additional components,
and replace only the pieces that don't match MP2.4 hardware (the input
implementation, the battery service, the Chatter bootstrap).

**Pros**
- Maximum reuse — `LoopManager`, `Display` superstructure, `Sprite`,
  `Util/Vector`, `Sync/Mutex` etc. all compile unmodified.
- The 506 `.cpp` files in `src/` mostly compile as-is.
- Well-trodden path: many real-world ESP-IDF projects use arduino-esp32
  as a component.

**Cons**
- ~60 MB of vendored dependency.
- Some sdkconfig collisions to resolve (USB-Serial/JTAG console, FreeRTOS
  tick rate, partition table layout). All tractable but each is its own
  bring-up.
- TFT_eSPI is included transitively and needs configuration for the
  ST7735 panel on MP2.4 SPI pins — separate config-header dance.
- `RadioLib` and other unused libraries inflate the build unless gated
  off in `library.properties`.

### Option B — Minimal hand-rolled `Arduino.h` + CircuitOS shim

Write our own narrow `Arduino.h` covering just `millis`, `delay`,
`Serial` (mapped to `printf` over USB-Serial/JTAG), `digitalRead/Write`,
`pinMode`, `String` (use `std::string`). Reimplement each CircuitOS
class we actually use against the C HAL.

**Pros**
- Stays in the ESP-IDF idiom we already know.
- No 60 MB of vendored Arduino code.
- Full control over the implementation — we can route `Display` directly
  to our `hal/display`, `Piezo` to `hal/piezo`, `Input` to
  `hal/input_keypad`, with no extra layer.

**Cons**
- Real work for every class we need. 30+ classes is a lot of
  reimplementation.
- `Sprite` / `TFT_eSpriteBuffer`-style drawing primitives are
  non-trivial — we'd be reimplementing chunks of TFT_eSPI's sprite
  engine.
- `LVGL` will still need a display-flush adapter that uses *something*
  to push pixels — we'd write it against `hal/display` directly,
  bypassing CircuitOS `Display` entirely. That's actually fine for
  LVGL-only screens, but breaks any screen that uses CircuitOS sprites
  for direct drawing.

### Option C — Replace touchpoints in the app code

Modify the 506 `.cpp` files to use ESP-IDF natives directly. Drop the
CircuitOS/Chatter dependency entirely.

**Pros**
- Cleanest end-state — no shim, no vendored libraries.

**Cons**
- 506 files to modify is infeasible for a small team / single-developer
  project.
- Every future upstream change to the existing firmware becomes a
  merge conflict nightmare.
- This is **not** a port — it's a rewrite.

## 4. Recommendation: Option A

Adopt **`arduino-esp32` as an ESP-IDF component**, vendor CircuitOS and
Chatter-Library, replace only what doesn't match MP2.4 hardware.

Rationale:
- The dependency surface is too large for Option B to be economical.
  We'd spend more time reimplementing `Sprite` and `Util/Vector` than
  we'd ever save by avoiding the vendored deps.
- Option C is a rewrite, not a port.
- Option A is the standard way teams move Arduino projects to ESP-IDF.

The MP2.4-specific work under Option A reduces to:
1. **Replace `InputShift` with an I²C-expander variant** (we already
   have the keypad scanning logic in `hal/input_keypad`; the shim is
   a thin C++ adapter).
2. **Replace `ChatterImpl::begin`** with an MP2.4 version that
   bootstraps using our HAL instead of Chatter's shift-register +
   PWM-backlight stack.
3. **Replace `BatteryService`** with a thin wrapper around `hal/battery`.
4. **Drop `LoRaService`** — substitute with adapters that talk to
   `hal/sms.h` and `hal/calls.h`.
5. **Override `Pins.hpp`** for MP2.4 GPIO assignments.
6. **Provide `Setup.hpp`** at project root with the right `CIRCUITOS_*`
   feature flags.

That's six well-bounded pieces of work. Everything else compiles
unchanged.

## 5. Class-by-class porting order

Once Option A is wired up:

### S-MP13 — Build-system foundation
- Add `arduino-esp32` as a managed component (`idf_component.yml`).
- Add CircuitOS as a managed component, pointing at a forked/pinned
  revision so upstream changes don't break us.
- Resolve sdkconfig collisions (console, FreeRTOS tick, partition layout).
- Write `Setup.hpp` at project root.
- Build target: empty C++ `extern "C" void app_main()` that pulls in
  `<Arduino.h>` and `<CircuitOS.h>` and links cleanly. CI green.

### S-MP14 — MP2.4 board bring-up classes
- `mp24/components/circuitos_shim/include/Pins.hpp` overriding the
  vendored Chatter `Pins.hpp` (CMake `target_include_directories`
  earlier-wins).
- `mp24/components/circuitos_shim/MP24Battery.{h,cpp}` —
  `class MP24Battery : public BatteryService` overriding `getVoltage()`
  and `getPercentage()` to call `hal/battery`.
- `mp24/components/circuitos_shim/MP24Input.{h,cpp}` — subclass of
  `Input` that registers as a `hal/input_keypad` listener and fires
  CircuitOS `InputListener` callbacks.
- `mp24/components/circuitos_shim/MP24Chatter.{h,cpp}` — replaces
  `ChatterImpl::begin` with an MP2.4-flavoured init: ST7735 via our
  `hal/display` wrapped in a CircuitOS `Display`, `MP24Input`,
  `Piezo` via our `hal/piezo`, SPIFFS, settings, battery.

### S-MP15 — Service substitutions
- `mp24/components/circuitos_shim/MP24LoRaService.{h,cpp}` — implements
  the `LoRaService` interface that existing screens use, but routes
  every "send" through `hal/sms` and every inbound URC into the
  service's event surface. The point isn't to emulate LoRa
  faithfully — it's to keep the screens that call `LoRaService::send`
  compiling and working.
- `mp24/components/circuitos_shim/MP24CallService.{h,cpp}` — wraps
  `hal/calls` in a class with the existing `PhoneCallService`
  interface.

### S-MP16 — App entry
- `mp24/main/app_main.cpp` (renamed from `app_main.c`, or a sibling C++
  source).
- Calls `Chatter.begin(true)` and `LoopManager::loop()` from inside the
  FreeRTOS app_main, exactly like the existing `MAKERphone-Firmware.ino`
  does in `setup()` / `loop()`.
- LVGL init: pulls in `lvgl` as a managed component; the existing
  `lvglFlush` callback already in `MAKERphone-Firmware.ino` works with
  the new `Display*`.
- The first port milestone: `IntroScreen` builds and shows on the
  display. That's the smoke test.

### S-MP17+ — Screens and services in dependency order
Roughly:
1. `Storage` (DB-style access; standalone).
2. `Settings` + `Profile`.
3. `PhoneClock`, `PhoneRingtoneEngine`, `PhoneVibrationEngine`.
4. `MessageService` + `MessageRepo` + `ConvoRepo`.
5. `PhoneCallService` (already exists in the codebase — just gets the
   new backend).
6. `MainMenu`, then screen-by-screen.

## 6. Build-system implications

- `idf_component.yml` at `mp24/main/` will list `espressif/arduino-esp32`
  as a managed dependency. arduino-esp32 takes ownership of `tinyusb`,
  bluetooth stack and a few other deep ESP-IDF components — sdkconfig
  needs to allow that without conflicting with our USB-Serial/JTAG
  console (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`). The arduino-esp32 docs
  call out the relevant `CONFIG_AUTOSTART_ARDUINO` toggles.
- CMake-wise the `circuitos_shim` component becomes a real C++ library
  with `REQUIRES arduino-esp32 circuitos chatter-library lvgl`.
- C and C++ both need to find each other. The `hal/*` headers should
  stay `extern "C"`-guarded (some already are; the rest get a sweep).

## 7. Risk register

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| 1 | arduino-esp32 + ESP-IDF sdkconfig collision (USB console) | High | Medium | Pin arduino-esp32 to a known-good release. Test S-MP13 in isolation before piling on. |
| 2 | TFT_eSPI ST7735 driver mismatches our existing `hal/display` SPI config | Medium | Medium | Easiest fix is to NOT use TFT_eSPI's driver — override `Display::begin` in `MP24Chatter` so `display->getTft()` returns a stub that delegates to our SPI driver. Or just live with TFT_eSPI + our pins. |
| 3 | LVGL flush perf regression versus native display | Medium | Low | The existing flush pushes whole-rect bytes via `tft.pushColors()`. We can do the same against `hal/display` directly. Defer optimisation. |
| 4 | Memory budget: Arduino-ESP32 footprint + LVGL + CircuitOS + app code might not fit 4 MB flash | Medium | High | Strip unused libraries from arduino-esp32 (BLE, OTA, WiFi if not needed). 2 MB app slice is generous but not infinite. |
| 5 | Settings/NVS layout collision with our partition table | Low | Medium | Use a dedicated NVS partition reserved for app data; keep our SPIFFS at `0x210000` untouched. |
| 6 | Build CI time blows up from 30 s to 5 min once arduino-esp32 is in | Medium | Low | Ccache. Self-hosted runner caches the IDF tree already; arduino-esp32 will rebuild only on revision changes. |

## 8. First concrete next step

When picking this up:

1. Read this document.
2. Start a new branch `mp24-shim-bringup` — keep `main` clean while the
   experiment plays out.
3. Add the arduino-esp32 managed-component entry and confirm the empty
   target builds.
4. Only after that proves out, vendor CircuitOS + Chatter-Library and
   confirm `LoopManager.h` and `Display.h` headers resolve.
5. Then start S-MP14.

This document does not commit any code under `mp24/components/`.
The placeholder shim component there from S-MP01 (just
`circuitos_shim_version()`) is still the only thing shipped.

## 9. Display / Sprite / TFT_eSPI — the next architectural call

S-MP13a/b/c shipped without resolving `<Display/Display.h>`. We
excluded every CircuitOS subsystem that depends on TFT_eSPI
(Display/, UI/, Elements/, Support/) so the foundation could
link. Any commit that wants to compile a screen — the IntroScreen
in S-MP16, the MainMenu in S-MP19, anything — has to unblock
this first.

The reason it's not trivial: upstream CircuitOS `Display.h`
declares the class with **`TFT_eSPI tft;` as a member** (not a
pointer). That means `<Display/Display.h>` cannot be included
without a *complete* `TFT_eSPI` type, not a forward declaration.
Same story for `Sprite` (uses `TFT_eSprite` as a member).

Three viable paths, in increasing order of effort:

### Path 9A — Vendor TFT_eSPI

`Bodmer/TFT_eSPI` is the de-facto Arduino library for ST77xx /
ILI9xxx displays. It's a real library with proper Arduino API,
broadly compatible with arduino-esp32, MIT-licensed. Vendor it
the same way we vendored CircuitOS: drop the source into
`mp24/components/tft_espi/`, write a CMakeLists, exclude the
chip variants we don't use.

  Pros:
  - Upstream `Display.h` / `Sprite.h` work unchanged.
  - The 506-file phone firmware compiles against TFT_eSPI without
    edits.
  - Bodmer maintains the library actively; pin configs for our
    ST7735 panel are pre-baked.

  Cons:
  - ~80 files, ~40 KB compiled (estimated, before LVGL).
  - Pin configuration is done via macros — needs a User_Setup.h
    matching our ST7735 GPIO wiring. We'd `define` over the
    upstream's defaults.
  - Adds a dependency that's effectively unused for LVGL flush
    (we can call our `hal/display` directly). But it's used by
    the Sprite class which the screens may use for direct
    drawing.

### Path 9B — Replace Display.h + Sprite.h + TFT_eSPI.h with shim stubs

Ship slim header files in `mp24/components/circuitos_shim/include/`
that:
  - Declare `Display` with the same public API but back it with
    `hal/display` directly (no TFT_eSPI dependency).
  - Declare `Sprite` as a software framebuffer in PSRAM (no
    TFT_eSprite dependency).
  - Declare `TFT_eSPI` as a complete type with the few methods
    the LVGL flush callback uses (`startWrite`, `setAddrWindow`,
    `pushColors`), each delegating to `hal/display`.

  Pros:
  - Zero external dependency.
  - Direct route from LVGL flush to our `hal/display`.

  Cons:
  - Sprite is the heavy part — `drawPixel`, `drawLine`, `drawRect`,
    `fillRect`, `drawCircle`, `drawString`, font rendering, etc.
    Replicating all of it is multi-week.
  - Diverges from upstream — future Chatter changes to Sprite
    won't trickle in.

### Path 9C — Compile only LVGL-driven screens; punt on Sprite

Only port screens that go through LVGL widgets and don't touch
Sprite directly. The lock screen, MainMenu, message list, dialer,
contacts — these are all LVGL. Skip the games (which use Sprite
for sprite-sheet rendering).

  Pros:
  - Minimal shim work — provide a stub Display whose `getTft()`
    returns nullptr, and never instantiate Sprite.
  - Gets a functional phone UI fastest.

  Cons:
  - Half the firmware (games + low-level screens) doesn't port
    until Sprite is solved.
  - Sprite likely comes back as a requirement during S-MP17+.

### Recommendation

For "first MainMenu visible" milestone, **9C is enough**. The
phone UI uses LVGL; Sprite isn't on the critical path.

For "full phone firmware feature parity," **9A is the right answer**
— vendor TFT_eSPI and let the upstream Display + Sprite work.

The lowest-risk move now: ship 9C as a fast path to a visible UI.
Promote to 9A when the games / sprite-using screens become the
next blocker. The stub Display.h from 9C is easy to throw away
once 9A lands; the architecture isn't path-dependent.

If you want me to start either of these, the work begins on a
branch (same pattern as `mp24-shim-bringup`): write the headers,
push, watch CI, fix-forward the inevitable issues.
