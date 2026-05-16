# MP2.4 Open Hardware Questions

Items that need physical-board observation or KiCad GUI tracing to
close. The firmware can't proceed on these without ground truth.

## Q1 — SPH0645 mic DATA pin ✓ RESOLVED 2026-05-16

**Answer: `uI2S1_DATA_MIC` = GPIO41 (MTDI / JTAG TDI pad).**

Confirmed from KiCad schematic screenshots:
- Root sheet ESP32-S3 pinout shows MTDI (chip pad 47) wired to net
  `uI2S1_DATA_MIC`. MTDI is GPIO41 in the ESP32-S3 IO_MUX.
- Audio sub-sheet shows `uI2S1_DATA_MIC` connecting to pin 6 (DOUT)
  of MK2 = SPH0645LM4H-B.
- The SPH0645's SELECT pin (pin 2) is tied LOW — the mic outputs on
  the LEFT slot of the WS frame, so I²S RX should read MONO LEFT
  (or filter the L channel from a stereo stream).

Pin landed in `mp24/main/hal/pins.h` as `PIN_I2S1_DIN_MIC = 41`.
Driver in `mp24/main/hal/audio_i2s.{c,h}` is now full-duplex on
I²S NUM 0: TX channel to the speaker amp + RX channel from the mic,
sharing BCLK and WS. Slot width raised to 32 bits explicitly so BCLK
lands at ~1.41 MHz, inside the SPH0645's 1.024–4.096 MHz window.

What still needs hardware time:
- Verify `audio_i2s_mic_read()` actually returns sample data when the
  mic is started.
- Sample-rate conversion (22.05 kHz → 8 kHz) + buffer hand-off from
  the I²S1 RX path to the modem audio uplink. The latter depends on
  whether the Quectel I²S2 bus supports a TX direction at all — our
  current schematic only shows one DATA wire (RX into ESP32). If
  the modem expects PCM TX over a separate pin we haven't traced
  yet, or via TDM on the same wire, that determines the bridge
  design.

## Q2 — uPOWER_OFF (GPIO1) polarity (gates real shutdown)

`hal/power.c` already configures GPIO2 as the power-button INPUT
(read-only, debounced). The matching OUTPUT for hard-killing the
supply is `uPOWER_OFF` on GPIO1, currently left in input/Hi-Z state.

**Why it's still Hi-Z:**
- Polarity not confirmed. Driving the wrong level cuts our own
  power mid-bringup.
- One of: active-high latch (drive HIGH to kill), active-low latch
  (drive LOW to kill), or pulse-then-release.

**Resolution path:**
- Press the user-facing power button, hold for >2 s. Observe what the
  hardware shutdown path does on its own (TP4056 + load-switch
  combination, typically).
- Probe GPIO1 with a scope during a normal shutdown to read polarity.
- Once known, set `POWER_OFF_ASSERTED_LEVEL` in `hal/power.c` and wire
  it into `power_request_shutdown()`.

## Q3 — Modem boot-FAIL (gates everything modem-dependent)

S-MP08 modem reaches `MODEM_FAILED` on hardware as of the last test.
Commit `11d7aa7` ships a speculative fix flipping PWR_KEY polarity to
active-low with 2.5 s pulse duration per Quectel EG912U-GL HW Design
V1.1 §3.4.1.

**If `11d7aa7` doesn't fix it:**

Triage in order of likelihood:

1. **VBAT_RF / VBAT_BB rails** — multimeter the modem-side supply
   nets. Must be 3.4–4.2 V before PWR_KEY pulse.
2. **UART TX/RX wiring** — `screen /dev/cu.usbmodem2101 115200` at
   boot. Any byte from the modem during the 30 s probe window
   confirms the RX path; total silence points back at PWR_KEY or
   supply.
3. **PWR_KEY pulse duration too long** — older Quectel firmwares
   interpret a >7 s low as "power off". If 2.5 s causes immediate
   power-off cycle, drop to 1.5 s.
4. **RESET_N stuck low** — verify GPIO16 is high at idle.

## Q4 — STATUS / NET_STATUS modem LEDs

These pins exist on the Quectel module but are not currently wired
into `hal/modem`. Useful for "is the radio attached to a network"
state without polling AT+CREG.

**Resolution path:**
- Inspect `GSM_module.kicad_sch` for STATUS / NETLIGHT hierarchical
  labels.
- If routed back to an ESP32 GPIO, wire as input + falling-edge ISR.
- If only routed to LEDs on the module itself, leave as-is and use
  AT polling.

---

When picking up after bench time: each Q* answered here moves a
dependent firmware session out of "blocked" state. Q1 unblocks
S-MP10c, Q2 unblocks real shutdown, Q3 unblocks SMS/calls/I²S2.
