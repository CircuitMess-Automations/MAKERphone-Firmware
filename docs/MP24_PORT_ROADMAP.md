# MAKERphone v2.4 port — session roadmap

Parallel stream to `MAKERPHONE_ROADMAP.md`. Each `S-MP-NN` session is sized
to ship in one autonomous run and commit. Pattern:
`feat(mp24): S-MP-NN – <one-line summary>`. Same agent loop as the legacy
roadmap (lowest un-done first, tick box in same commit, push, watch CI).

Architectural context: see `MP24_PORT_PLAN.md` for the locked decisions
(A=ESP-IDF, B=cellular Quectel EG912U-GL, C=brightness fixed, D=add SPIFFS
partition, E=button alias remap to joystick/A/B/C/D, F=USB CDC console).

## Foundation

- [x] **S-MP01** ESP-IDF skeleton + CircuitOS shim placeholder.
      `mp24/` tree, partition table, sdkconfig.defaults, USB CDC console,
      I²C bus, AW9523B init, LED blink, two-job CI.
- [x] **S-MP02** Display — ST7735 over SPI (SCK 4 / MOSI 5 / DC 6 / RST 7),
      MADCTL 0x60, COL+1 ROW+2 offsets. Native driver under `mp24/main/hal/`
      with fill / fill_rect / 5x7 text. Boot splash drawn before any other
      subsystem init. LVGL integration is deferred to its own session
      after the rest of the HAL settles.
- [x] **S-MP03** I²C expanders — refactor I²C + AW9523B into `hal/`,
      add XL9555 driver (U5 @ 0x20, U9 @ 0x21), AW9523B LED API,
      INT1/INT2 interrupt sources wired but not yet consumed.
- [x] **S-MP04** Input HAL — `InputI2cKeypad` reading both XL9555s on
      interrupt, exposing CircuitOS-compatible `buttonPressed/Released`.
      Button enum: full MP2.4 set + Decision-E legacy aliases.
      Lifts `dashboard.c` `k_buttons[]` bit map verbatim.
- [x] **S-MP05** Audio shim — native `hal/audio_i2s.{c,h}` and
      `hal/piezo.{c,h}` with `piezo_tone(freq, dur_ms)` /
      `piezo_no_tone()` synthesising integer-math square waves at
      22.05 kHz into MAX98357A on I²S1. SD_MODE is asserted by
      `aw9523b_init()` (P1_1). C++ CircuitOS `Audio/Piezo.h` shim
      forwarding to this C API — and the BuzzerService /
      PhoneRingtoneEngine / PhoneSystemTones bring-up — is left for
      the next audio-side session once the shim component starts
      being populated.
- [x] **S-MP06** Battery monitor — ADC1_CH2 on GPIO 3, eFuse
      curve-fit calibration, 1 Hz background sampler, Li-Po SOC
      curve, dashboard renders `Batt : X.XX V (N%)` with red/amber/
      green colour.
- [~] **S-MP06b** Power-related deferred items, addressed in pieces:
      - [x] **Power button** read-only — `uBUTTON_PWR` traced to
            GPIO2, 50 Hz debounced poller in `hal/power.c`,
            dashboard `Pwr  : idle / HELD X.Ys (#N)` row. Kill
            output (`uPOWER_OFF` on GPIO1) left high-Z this
            session — polarity unverified, driving wrong cuts
            our own supply. Phase 2 wires actual shutdown.
      - [~] **USB detect** — *not implementable on v2.4 hardware.*
            Schematic trace: `uUSB_DETECT` label exists but both
            endpoints (top sheet + Screen_and_buttons sub-sheet)
            connect to dead-end wires with no driver and no
            receiver. `pin_config.h`'s claim of "U5 P0.4 =
            USB_DETECT" is stale — `dashboard.c`'s hardware-
            verified `k_buttons[]` shows that bit is the `#`
            keypad button. The Power_supply sub-sheet has
            CHRG_IN / CHRG_OUT labels for the charger IC but
            they're local to that sheet (not exposed to ESP32).
            Future revisions could route USB-VBUS to a free
            expander pin; current option on v2.4 is to query
            the ESP32-S3 USB-OTG VBUS-sense register directly
            (separate session, conflicts with USB-Serial/JTAG
            console pin ownership).
      - [x] **Idle-dim** — Decision-C blank-display behaviour
            implemented in app_main's dashboard loop. After 30 s
            without a keypad or power-button press the display is
            cleared to black (`display_fill(0x0000)`) and the
            update_dashboard call is skipped. Any subsequent press
            (tracked via the existing `s_press_count` atomic and
            `power_button_press_count()` getter) restores the boot
            screen + dashboard. Backlight stays on the whole time
            since it's hardware-biased to always-on, so the user
            sees a black-but-lit screen during idle.
      - **TL431 / CALIB_EN** stays deferred *permanently* —
            schematic shows `CALIB_EN` only as `input` on every
            sheet with no driver, eFuse cal is already ±20 mV
            which is well below the per-1%-SOC voltage step on
            a Li-Po. Not a bug, not a gap.

## Transport (GSM modem)

- [x] **S-MP07** SPIFFS — partition image built from `mp24/spiffs/`
      via `spiffs_create_partition_image`, mounted at `/spiffs`,
      enumerated + sentinel-read at boot. Drop CircuitOS assets
      (`intro.gif` / `splash.bin` / avatars / sfx) into the
      `mp24/spiffs/` directory alongside the C++ shim work; the
      build picks them up automatically.
- [x] **S-MP08** GSM modem layer 1 — UART1 driver on GPIO 17/18,
      PWR_KEY pulse + RESET_N control on GPIO 12/15, full AT
      request/response framework with synchronous `modem_at_send`
      (mutex-serialised, queue-coordinated with the RX line splitter)
      and URC dispatcher. State machine: OFF → POWERING_ON → BOOTING
      → READY (probes AT every 500 ms with a 30 s cap, then ATE0 +
      AT+CGMM for identity) or → FAILED. Dashboard surfaces the
      state. Deferred to follow-up sessions: PSM external wake on
      GPIO 11, hard-reset recovery path, +CMUX multiplexing, AT
      command timeouts tuned per command class.
- [ ] **S-MP09** SMS — `MessageService` rewired to `AT+CMGS` / `+CMTI`.
      Each Friend gains a phone number field. `PairService` becomes
      "add contact by number."
- [ ] **S-MP10** Voice calls + I²S2 audio bridge — `PhoneCallService` to
      `ATD` / `ATA` / `ATH`. PCM forwarding between I²S1 and I²S2
      under a FreeRTOS task. AEC deferred to polish.

## CI & bring-up

- [ ] **S-MP11** CI — verify `build-mp24.yml` produces a flashable image
      on every push that touches `mp24/**`. SPIFFS image artifact upload.
- [ ] **S-MP12+** Hardware bring-up shakeout. One commit per fix.
