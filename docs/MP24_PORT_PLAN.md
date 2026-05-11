# MAKERphone v2.4 Firmware Port — Coding Plan v2

Status: planning. Generated 2026-05-11. Updates the v1 plan after the user supplied schematics and decided A2 / B-cellular / C-fixed / D-SPIFFS / E-remap.

---

## 1. Hardware truth (from KiCad schematics)

| Sheet | Confirmed parts and signals |
|---|---|
| `Makerphone_v2_4.kicad_sch` | U2 = **ESP32-S3FH4R2** with Y2 40 MHz crystal, AE1 chip antenna for ESP WiFi/BT. J1 USB-C 2.0, J3 microSD HYC77-TF09-200. SW21/SW24 likely BOOT/EN buttons. Two I²C buses on the MCU: `I2C_SDA`/`I2C_SCL` (main) and `I2C_SDA_UMAX`/`I2C_SCL_UMAX` (UMAX side). |
| `Audio.kicad_sch` | U8 **MAX98357A** I²S amp + **MK2 SPH0645LM4H-B** I²S MEMS microphone (new — not mentioned in HW test notes). J7 speaker JST. J2 3.5 mm jack. JP6 3-way jumper (probably speaker/headphone routing). R17 100k pull-up on SD_MODE. I²S1 bus: `uI2S1_CLK_MIC_AMP` (BCLK), `uI2S1_WS`, `uI2S1_DATA_SPK` (to amp), `uI2S1_DATA_MIC` (from mic), `uI2S1_SD_MODE` (amp enable, driven from AW9523B). |
| `GSM_module.kicad_sch` | **U7 Quectel EG912U-GL** (LTE Cat-1 modem, 2G fallback). J4 nano-SIM. E1 cellular antenna YC0017EA. AT command UART: `uUART1_GSM_RX`/`TX`. Control: `uGSM_PWR_KEY`, `uGSM_RESET_N`, `uGSM_PSM_EXT_INT`. Status outputs: `STATUS`, `NET_STATUS`, `PSM_IND`. **Second I²S bus to the modem for voice calls**: `uI2S2_PHONE_CLK`, `uI2S2_PHONE_WS`, `uI2S2_PHONE_DATA`. |
| `Screen_and_buttons.kicad_sch` | U1 1.77" TFT ribbon (ST7735). Q1 G3035 confirms backlight is hardware-MOSFET-on. **U5 + U9 XL9555** I²C expanders (numpad + joystick/face/volume). **U12 AW9523B** drives 8 LEDs (D4-D11 = `LED_1`..`LED_8`) and SD_MODE for the amp. **U13 7×7×5-6P 5-way joystick** (4 directions + click). Discrete button nets `BTN_0`..`BTN_9`, `BTN_*`, `BTN_#`, `BTN_A`..`BTN_D`, `BTN_VOL_UP/DOWN`, `BTN_JOY_UP/DOWN/LEFT/RIGHT/CLICK`. No L/R shoulder buttons exist. Two expander interrupt lines: `GPIO_INT1`, `GPIO_INT2`. SW18/SW19 are TS-1101VS — probably board-level BOOT/RESET. |
| `Power_supply.kicad_sch` | U4 TP4056 Li-Po charger, BT1 1300 mAh battery. Two ME6217C33 LDOs (probably main 3V3 and modem 3V3). U6 TL431 reference. Q16 NCE2003 P-MOS for the soft-power switch. SW20 power button. The `CALIB_EN` net is back — battery ADC IS calibrated against the TL431, contrary to the simpler "2× divider" picture in the HW test notes. |
| `UMAX_connector.kicad_sch` | **Surprise**: a *third* **XL9555 (U3)** sits on the UMAX side, on a **separate** I²C bus (`I2C_SDA_UMAX`/`SCL_UMAX`). 6 GPIO + 6 address-strap nets + 2 side-detect nets cross the connector to host UMAX modules. |

### Updated key facts vs. HW_TEST_NOTES.md

- **There IS a cellular modem.** Quectel EG912U-GL. SMS + voice calls + IP data. Replaces LoRa as the radio transport.
- **There IS a microphone.** I²S MEMS on the same bus as the amp.
- **There are TWO I²C buses, not one.** Main bus + UMAX bus.
- **There are TWO I²S buses, not one.** Speaker/mic + modem voice channel.
- **No L/R shoulder buttons.** Confirmed by net inventory.
- **Battery ADC is calibrated**, not just a raw 2× divider. The TL431 reference path survives.
- **All keypad signals are routed through I²C expanders**, with interrupts on `GPIO_INT1`/`GPIO_INT2` (means polling is unnecessary — we can use interrupt-driven input).

### GPIO numbers still TBD from schematic (not in HW test notes)

These need extraction from the U2 instance pin → wire → label trace in `Makerphone_v2_4.kicad_sch`. Will resolve in Session 2 (KiCad open or full S-expression parse):

```
UART to GSM:      uUART1_GSM_TX, uUART1_GSM_RX
GSM control:      uGSM_PWR_KEY, uGSM_RESET_N, uGSM_PSM_EXT_INT
GSM status in:    STATUS, NET_STATUS (probably also routed to MCU)
I²S2 (modem):     uI2S2_PHONE_CLK, uI2S2_PHONE_WS, uI2S2_PHONE_DATA
I²S1 mic in:      uI2S1_DATA_MIC  (DOUT and BCLK/WS known from notes)
UMAX I²C:         I2C_SDA_UMAX, I2C_SCL_UMAX
SD card SPI:      SD_CS, SD_SPI_MISO, SD_SPI_MOSI, SD_SPI_SCL, SD_SENSE
Power:            uBUTTON_PWR, uPOWER_OFF, uUSB_DETECT, CALIB_EN, uEN, uBOOT
Expander INTs:    GPIO_INT1, GPIO_INT2
Battery ADC:      uBATTERY_READ (the notes say GPIO 3; need confirmation)
```

---

## 2. Decisions (locked)

| # | Decision | Implication |
|---|---|---|
| A | **ESP-IDF v5.5** (matches the standalone `hw_test` build environment) | CircuitOS doesn't exist in ESP-IDF — needs a shim layer. LovyanGFX works under IDF with the ESP-IDF SPI driver. Arduino libraries used elsewhere (RadioLib, SPIFFS, Settings persistence) need IDF equivalents or wrappers. |
| B | **Cellular modem** (Quectel EG912U-GL), not LoRa | Replace `LoRaService` with `GsmService` (AT command stack). Re-purpose messaging to **real SMS** via `AT+CMGS` / `+CMTI` URC. Re-purpose calls to **real LTE voice** via `ATD` / `ATA` / `ATH` plus I²S2 voice routing. UID-based peers become E.164 phone numbers. PairService becomes "add contact by number." |
| C | **Brightness fixed** (backlight HW-on) | `setBrightness()` is a no-op. The Brightness slider (S51) becomes a stub or is hidden. Idle-dim (S69) replaces "dim" with "blank-display" (`display->clear(TFT_BLACK)`); wake unblanks. Boot/power-off fade animations are skipped or replaced with overlay-alpha fades. |
| D | **Add a SPIFFS partition** | New partition table with 2 MB app + 2 MB SPIFFS (or 1.5 MB + 2.5 MB if we ever need OTA). Existing `data/` assets (intro.gif, splash, avatars, emojis, games art) are flashed into SPIFFS via `esptool.py write_flash` step in CI. |
| E | **Button alias remap** approved | `BTN_LEFT → BTN_JOY_LEFT`, `BTN_RIGHT → BTN_JOY_RIGHT`, `BTN_ENTER → BTN_JOY_CLICK`, `BTN_BACK → BTN_C`, `BTN_L → BTN_A`, `BTN_R → BTN_B`. Up/down navigation in the firmware currently uses `BTN_LEFT`/`BTN_RIGHT` (Chatter quirk where the encoder maps L/R to LV_KEY_LEFT/RIGHT — see `InputChatter.cpp`). Joystick LEFT/RIGHT preserves that semantic exactly. Volume keys + the new D button are extra inputs we now have to wire up. |

### Decision D — proposed partition table

```csv
# name,     type, subtype, offset,   size
nvs,        data, nvs,     0x9000,   0x6000     # 24 KB — settings, contacts, etc.
phy_init,   data, phy,     0xF000,   0x1000     # 4 KB — RF calibration
factory,    app,  factory, 0x10000,  0x200000   # 2 MB — firmware
spiffs,     data, spiffs,  0x210000, 0x1F0000   # ~2 MB — assets
```

FH4R2 flash is 4 MB total; this uses 4 MB exactly. PSRAM is 2 MB (R2 suffix), available for LVGL double buffers and audio DMA scratch.

---

## 3. Revised session breakdown

The CircuitOS layer is the single biggest blocker for ESP-IDF migration. The firmware's services and screens call `LoopManager::addListener(this)` everywhere, expect a `Display*` from CircuitOS, use `Audio/Piezo.h`, expect an `Input` base class. None of those exist in ESP-IDF. **Building a thin "CircuitOS-on-IDF" shim is the first real coding task** — once it compiles, the existing upper layers come along almost for free.

Then each peripheral comes online in its own session, then the modem stack, then bring-up.

### S-MP01 — ESP-IDF project skeleton + CircuitOS shim
Create a new top-level `mp24/` ESP-IDF project. Partition table from above. Bring up an ESP-IDF `app_main` that initializes:
- Wi-Fi/BT off (save power for now)
- SPIFFS mount
- FreeRTOS scheduler
- A new internal library `components/circuitos_shim/` that exposes `LoopManager`, `Vector`, `Display` (LVGL-backed), `Input` base class, `Audio/Piezo.h` (stubbed for now), `Util/HWRevision.h`. The shim's job is API compatibility — same headers, different implementation.
- Compile every existing `src/Services/*.cpp` and `libraries/Chatter-Library/src/*.cpp` against the shim. **First milestone: the firmware compiles under ESP-IDF.** No display yet, no input yet, just a clean build.

### S-MP02 — Display
Rewrite `ChatterDisplay::panel1()` for MP2.4 pins (SCK=4, MOSI=5, DC=6, RST=7, CS=GND) and the 1.77" ST7735 offsets (col +1, row +2, MADCTL=0x60). Drop `panel2()` and the HWRevision-based panel switch — there's only one panel here. Wire LVGL flush to the new bus. **Milestone: boot splash renders.**

### S-MP03 — I²C buses + expander HAL
Initialize the main I²C bus at GPIO 47/48 @ 100 kHz and the UMAX bus at its dedicated GPIOs (TBD). Write small drivers for:
- `XL9555Driver` — generic, 16-bit IO, used three times (U5 @ 0x20, U9 @ 0x21, U3 on the UMAX bus, address TBD).
- `AW9523BDriver` — for U12 @ 0x5B (LEDs + SD_MODE). Init sequence verbatim from HW test notes' AW9523B section.
- `I2cBus` wrapper with retry + timeout (so a flaky module on UMAX can't hang the firmware).

### S-MP04 — Input HAL + button enum
Replace `InputShift` consumer in `Chatter.cpp` with `InputI2cKeypad`. Read interrupts on `GPIO_INT1`/`GPIO_INT2` to know *when* to read the expanders (interrupt-driven, not polled). Apply the hardware-verified bit assignments from the HW test notes (U9 differs from the schematic net names — this is critical and is the bit that the test notes warn about).

Define the canonical `BTN_*` enum with all 26 button identities (0-9, *, #, A, B, C, D, JOY_UP/DOWN/LEFT/RIGHT/CLICK, VOL_UP/VOL_DOWN, plus the Decision-E legacy aliases). Update `BuzzerService::noteMap` to cover the new keys. The dialer screen's `*` and `#` press handlers stay wired to `BTN_*` and `BTN_#`. The two new "spare" keys A/B/C/D become softkey-bound (and per Decision E feed BTN_L/BTN_R/BTN_BACK).

**Milestone: live dashboard (per HW-TESTER skill) runs inside the firmware — battery bar, joystick, every button highlights green on press.**

### S-MP05 — Audio shim (the Piezo replacement)
Build `PiezoI2S` exposing the existing `Piezo.tone(freq)`, `Piezo.tone(freq, durMs)`, `Piezo.noTone()` API but synthesizing a square wave into the MAX98357A over I²S1 (BCLK=39, WS=40, DOUT=38). Drive SD_MODE high via the AW9523B before any audio. Verify unchanged:
- `BuzzerService` (per-key clicks)
- `PhoneRingtoneEngine` (ringtones, S39-S43)
- `PhoneVibrationEngine` (no real vibrator; either stub silently or play a low-frequency rumble through the speaker — TBD; probably stub)
- `PhoneSystemTones` 18-chime catalogue (S192)

Microphone bring-up is deferred to S-MP10 (voice calls). The mic exists; the firmware just doesn't use it yet.

### S-MP06 — Battery + power button + USB detect
Implement `BatteryService` reading ADC1_CH2 with the TL431-calibrated path (`CALIB_EN` toggle + read `CALIB_READ`, same calibration math the legacy `BatteryService.cpp` already encodes for HWRevision==1). Apply Decision C: `Chatter::setBrightness()` no-op; `Chatter::fadeIn/fadeOut` becomes "clear screen, then redraw / draw, then clear." Wire the power button (SW20 / `uBUTTON_PWR`) to `ShutdownService`. Wire USB detect to `ChargeChime` and the charging overlay (S59).

### S-MP07 — SPIFFS + assets
Apply the partition table. CI step: build app, then `esptool.py write_flash` the SPIFFS image. Sanity-check the FSLVGL asset loader against the new mount point. Confirm `intro.gif`, `splash.bin`, every avatar binary still resolves. Keep the project's direction toward code-only widgets — don't add new SPIFFS dependencies.

### S-MP08 — GSM modem driver, layer 1
Lowest layer: UART driver, AT command request/response state machine, URC parser. Power sequencing: pulse `PWR_KEY` for ~500 ms to boot the modem, monitor `STATUS` to know it came up, wait for `+CFUN: 1` then `+CPIN: READY` then `+CEREG: 0,1` (registered). Handle `AT+CSQ` for signal strength, `AT+CREG?`/`AT+CEREG?` for registration. Expose a simple async API: `gsm_send_at(cmd, timeout, on_response)`.

### S-MP09 — SMS messaging
Map `MessageService` onto SMS:
- `MessageService::send(uid, body)` → `AT+CMGS=<number>\r\n<body><Ctrl-Z>`
- Incoming SMS URC `+CMTI: "ME",<index>` → `AT+CMGR=<index>` → push into `Storage.Messages`
- Each "Friend" gains a phone number field. `PairService` becomes "Add contact by number."

This is the largest UX surface change. The `PhoneIncomingCall`/`PhoneActiveCall`/`PhoneDialerScreen` flow gets rewired in S-MP10. Inbox/Convo/T9 screens keep working as long as `MessageService` keeps the same public API — which is the whole point of the shim approach.

### S-MP10 — Cellular voice calls + I²S2 audio routing
Configure I²S2 as master towards the modem (the modem is the I²S clock slave — confirm with EG912U datasheet; usually the modem is master, but Quectel modules can be either). Each direction is an 8 kHz PCM stream typically. Forward PCM frames from I²S2 RX (modem mic) → I²S1 TX (speaker), and I²S1 RX (mic) → I²S2 TX (modem speaker). Use FreeRTOS task with DMA callbacks. Map `PhoneCallService` actions:
- `PhoneCallService::dial(number)` → `ATD<number>;\r`
- Incoming call URC `RING` plus `+CLIP: "<number>"` → raise `PhoneIncomingCall`
- `PhoneCallService::answer()` → `ATA\r` + start audio bridge
- `PhoneCallService::hangup()` → `ATH\r` + stop audio bridge

Audio quality testing is mandatory here. Echo cancellation may be needed (ESP-DSP has AEC). Defer AEC to a polish session unless quality is unacceptable on first cut.

### S-MP11 — CI rewrite
New `.github/workflows/build.yml`:
```yaml
- name: Setup ESP-IDF
  uses: espressif/esp-idf-ci-action@v1
  with:
    esp_idf_version: v5.5
    target: esp32s3
    path: 'mp24'
```
Plus a separate step to build the SPIFFS image and upload artifacts. The self-hosted Mac runner can host this; only the toolchain install step differs. Keep the legacy `cm:esp32:chatter` workflow on a different branch so the Chatter firmware doesn't bit-rot.

### S-MP12 onward — bring-up + polish
Each subsequent session resolves one issue surfaced on real hardware: I²S clock domain mismatches, modem power sequencing edge cases, SPIFFS asset corruption, joystick deadzones, AW9523B init order quirks (the test notes call this out — exact 7-step sequence is non-negotiable), etc. Same `feat(makerphone): SXX – <summary>` convention.

---

## 4. Order-of-operations summary

```
S-MP01  ESP-IDF skeleton + CircuitOS shim          [blocks all else]
S-MP02  Display                                    [needs S-MP01]
S-MP03  I²C buses + XL9555/AW9523B drivers         [needs S-MP01]
S-MP04  Input HAL + button enum                    [needs S-MP03]
S-MP05  Audio shim (Piezo→I²S1)                    [needs S-MP01, S-MP03]
S-MP06  Battery + power button                     [needs S-MP01]
S-MP07  SPIFFS + assets                            [needs S-MP01, S-MP02]
S-MP08  GSM modem layer 1                          [needs S-MP01, parallel-OK with S-MP02..07]
S-MP09  SMS messaging                              [needs S-MP08]
S-MP10  Voice calls + I²S2 bridge                  [needs S-MP08, S-MP05]
S-MP11  CI rewrite                                 [anytime, but easier after S-MP02 boots]
S-MP12+ Hardware bring-up + polish                 [needs everything compiled and flashable]
```

S-MP02 through S-MP07 can run roughly in parallel after S-MP01 — they touch disjoint subsystems. S-MP08 has no dependency on the display/input/audio bring-up, so it can be drafted in parallel by a second worker if the team has bandwidth.

---

## 5. Known risks

- **CircuitOS shim is the long pole.** Until it compiles, nothing else does. Worst case: the firmware's services use CircuitOS in deeper ways than the headers suggest (e.g., reaching into internal vectors, expecting specific scheduling guarantees). If the shim turns into a multi-week rewrite, the fallback is to convert the firmware to use FreeRTOS primitives directly instead, which touches every service file.
- **I²S2 modem audio is timing-sensitive.** The clock domain between ESP32-S3 I²S2 and the modem's I²S has to match (8 or 16 kHz typically, mono, 16-bit, LJ/RJ format per Quectel datasheet). Misconfiguration produces static or silence with no clear error code.
- **The `panic-on-no-battery` path** (`MAKERphone-Firmware.ino:361`: `if(Battery.getPercentage()==0) { LoRa.initStateless(); Sleep.turnOff(); }`) still calls `LoRa` — that has to come out cleanly in S-MP08.
- **The Quectel EG912U-GL boot time** is 5-15 s after PWR_KEY pulse. The firmware's existing splash screen is ~3 s. Need to either lengthen the splash, parallelize modem boot with the rest of startup, or both.
- **No L/R shoulder buttons** breaks `PhonePinball`'s flipper controls and `PhoneTiltSimulator`'s chord. Per Decision E, those map to `BTN_A`/`BTN_B` (the schematic's BTN_A/B nets) — but pinball with side buttons feels different. Polish session.
- **CALIB_EN battery calibration path** depends on the TL431 being populated and the routing not changing — verify in DVT.

---

## 6. Repository layout (decided)

`mp24/` is a **sibling directory** inside the existing `MAKERphone-Firmware` repo on branch `main`. The legacy Chatter firmware at the repo root keeps building unchanged; the new MP2.4 firmware lives under `mp24/` with its own `CMakeLists.txt`, `partitions.csv`, and `main/` source tree. Shared content (docs, KNOWN_ISSUES, this plan) lives at the repo root.

```
MAKERphone-Firmware/
├── MAKERphone-Firmware.ino       ← legacy Chatter, untouched
├── libraries/Chatter-Library/     ← legacy HAL, untouched
├── src/                           ← legacy Arduino code, untouched
├── data/                          ← legacy SPIFFS assets (shared with mp24?)
├── docs/MAKERPHONE_ROADMAP.md
├── docs/MP24_PORT_PLAN.md         ← this file
└── mp24/                          ← NEW ESP-IDF firmware
    ├── CMakeLists.txt
    ├── sdkconfig.defaults
    ├── partitions.csv             ← Decision-D table
    ├── components/circuitos_shim/
    └── main/
        ├── CMakeLists.txt
        ├── app_main.c
        ├── hal/                   ← Pins.h, ChatterDisplay, InputI2cKeypad, BatteryService
        ├── audio/                 ← PiezoI2S shim + I²S1 driver
        ├── gsm/                   ← Quectel modem stack
        └── ...
```

### CI implication

`.github/workflows/build.yml` becomes two jobs in the same workflow:

```yaml
jobs:
  build-chatter:
    if: contains(github.event.head_commit.modified, 'src/') ||
        contains(github.event.head_commit.modified, 'MAKERphone-Firmware.ino') ||
        contains(github.event.head_commit.modified, 'libraries/')
    runs-on: [self-hosted, bit-flash]
    # ... existing arduino-cli flow ...

  build-mp24:
    if: contains(github.event.head_commit.modified, 'mp24/')
    runs-on: [self-hosted, bit-flash]
    steps:
      - uses: actions/checkout@v4
      - uses: espressif/esp-idf-ci-action@v1
        with:
          esp_idf_version: v5.5
          target: esp32s3
          path: 'mp24'
```

Path-filtered jobs keep build time reasonable when a commit only touches one firmware. A workflow_dispatch trigger always builds both for sanity.

---

## 7. Verified inputs (post-upload audit)

All `hw_test` sources and schematics are now in project knowledge at `/mnt/project/`. The verification pass found:

- **`dashboard.c`'s `k_buttons[]` table is the canonical button-bit map.** It agrees byte-for-byte with the "Hardware-Verified" table in `HW_TEST_NOTES.md`. `pin_config.h`'s comment block at the bottom is the older schematic-derived map that was discovered to be wrong — kept in the file as stale comments, not used by the actual driver code. **Session 4 lifts `dashboard.c`'s table, not the header comments.**
- **AW9523B init sequence** in `test_i2c.c` lines 121-135 is the canonical sequence — SWRST → CTL push-pull → OUT0 0xFF → CFG0 0x83 → OUT1 0xFF (drives SD_MODE HIGH = amp on) → CFG1 0x1D. Lift verbatim in Session 3.
- **AW9523B pin map** (from `test_i2c.c` comments, lines 126-128):
  - P0_2..P0_6 = LED_3, LED_6, LED_4, LED_7, LED_5
  - P1_5..P1_7 = LED_8, LED_2, LED_1
  - P1_1 = `uI2S1_SD_MODE` (amp enable, drive HIGH)
  - P1_0 = `TFT_BL_DRAIN` (hardware-on, configured as input — leave alone)
- **GPIO 9 / GPIO 10** are dedicated interrupt lines from U5 / U9 respectively — interrupt-driven input is on the table, no need to poll.
- **Battery ADC** is `ADC_UNIT_1, ADC_CHANNEL_2`, divider ratio 2.0, voltage thresholds 3.36 V / 3.70 V (red / yellow / green).
- **`hw_test` partition table** has 2 MB app and no SPIFFS — confirms Decision D needs to add the 2 MB SPIFFS region cleanly.

### Decision F (new) — console destination

`pin_config.h` reveals **UMAX I²C uses GPIO 43 / GPIO 44**, which collide with the ESP32-S3 default UART0 console (`U0TXD`/`U0RXD`). The standalone hw_test gets away with UART0 console because it does not exercise UMAX. The production firmware must pick one:

- **F1 (recommended) — Move console to USB CDC.** `CONFIG_ESP_CONSOLE_USB_CDC=y` in `sdkconfig.defaults`. ESP32-S3's native USB-OTG peripheral exposes a CDC serial device on the existing USB-C connector. Frees GPIO 43/44 for UMAX I²C. No extra hardware.
- **F2** — Keep UART0 console on GPIO 43/44, leave UMAX I²C unimplemented.

Locked-in: **F1**. UMAX I²C bus stays usable; the console is on USB CDC.

### Session 2 sdkconfig changes (vs. hw_test's)

```diff
- # CONFIG_SPIRAM=n
+ CONFIG_SPIRAM=y
+ CONFIG_SPIRAM_MODE_OCT=y          # ESP32-S3FH4R2 has octal PSRAM
+ CONFIG_SPIRAM_SPEED_80M=y
+ CONFIG_SPIRAM_USE_MALLOC=y        # heap_caps_malloc(MALLOC_CAP_SPIRAM) usable
- CONFIG_ESP_CONSOLE_UART_NUM=0
- CONFIG_ESP_CONSOLE_UART_BAUDRATE=115200
+ CONFIG_ESP_CONSOLE_USB_CDC=y
- CONFIG_ESP_TASK_WDT_EN=n
+ CONFIG_ESP_TASK_WDT_EN=y          # re-enable for production
+ CONFIG_ESP_TASK_WDT_TIMEOUT_S=10
```

Plus the partition table swap to the Decision-D 4-entry layout that adds SPIFFS at 0x210000.

## 8. Action items before Session 2

1. **Rotate the GitHub PAT** — still in the chat transcript. The user has stated this will be done later; flagged for completeness.
2. **No other prerequisites.** Project knowledge has the schematics + hw_test sources; HW_TEST_NOTES bit map is verified canonical via `dashboard.c`; Decision F is locked. **Session 2 is unblocked.**
