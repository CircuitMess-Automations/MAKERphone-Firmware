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
      green colour. Deferred to follow-up sessions (Decision F-2
      pending): power button → shutdown service (need uBUTTON_PWR
      schematic trace), USB detect → charge chime (need to find
      USB_DETECT bit on expanders), TL431/CALIB_EN calibration
      (schematic shows CALIB_EN only as `input` to sub-sheets —
      nothing drives it on v2.4 PCB, eFuse cal is ±20 mV which is
      sufficient), idle-dim = blank display (need to define what
      "idle" means first).

## Transport (GSM modem)

- [ ] **S-MP07** SPIFFS — generate image, flash, mount, sanity-check
      `intro.gif` / `splash.bin` / avatars resolve from FSLVGL.
- [ ] **S-MP08** GSM modem layer 1 — UART driver, AT request/response,
      URC parser, power sequencing (PWR_KEY pulse, monitor STATUS,
      wait for `+CPIN: READY` then `+CEREG: 0,1`).
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
