# MP2.4 Open Hardware Questions

Items that need physical-board observation or KiCad GUI tracing to
close. The firmware can't proceed on these without ground truth.

## Q1 — SPH0645 mic DATA pin (gates S-MP10c)

**Need:** the ESP32 GPIO that connects to `uI2S1_DATA_MIC` (the
SPH0645LM4H-B microphone's DOUT line).

**What we know from text-mode schematic inspection:**

- `pins.h` has only `PIN_I2S1_BCLK=39`, `PIN_I2S1_WS=40`, and
  `PIN_I2S1_DOUT=38`. The mic pin is missing.
- `pin_config.h` (from the hw_test project) also only declares the
  BCLK pin (`#define PIN_I2S_BCLK 39`).
- The schematic top-level (`Makerphone_v2_4.kicad_sch`) has a label
  `uI2S1_DATA_MIC` at coordinate `(139.7, 90.17)` — that's on the
  right-of-chip wire rail, same column as known I²S1 labels.
- The ESP32-S3FH4R2 instance is anchored at `(95.25, 77.47)`. Its
  right-side pin column at `X=118.11` only contains GPIO34, 35, 36,
  37, 38 (= DATA_SPK) and 46. The first three Y rows below GPIO38
  (Y=82.55) are occupied by `uI2S1_CLK_MIC_AMP` (Y=85.09), `uI2S1_WS`
  (Y=87.63), and `uI2S1_DATA_MIC` (Y=90.17). But the chip's
  right-side column ends at GPIO46 (Y=85.09).
- That means `uI2S1_WS` and `uI2S1_DATA_MIC` must route via wires that
  leave the right rail and go to pins on other sides of the chip —
  most likely the bottom or left side. The text-parsing approach can't
  follow this without a wire-graph implementation.

**Candidate GPIOs by elimination:**

GPIOs the ESP32-S3 schematic symbol exposes that are not yet allocated
in `pins.h`:

```
GPIO0   — strapping pin (BOOT)
GPIO15  — currently unused
GPIO33  — currently unused
GPIO34  — currently unused
GPIO35  — currently unused
GPIO36  — currently unused
GPIO37  — currently unused
GPIO45  — currently unused
GPIO46  — currently unused (strapping)
```

Of these, the most plausible for an I²S MIC data line are the ones
physically adjacent to known I²S1 pins on the QFN footprint. GPIO45
sits next to GPIO38 on the chip's pad ordering and is a common
choice. GPIO34–37 are also adjacent on the right side.

**Resolution path:**

1. Open `Makerphone_v2_4.kicad_sch` in KiCad in GUI mode.
2. Click the label `uI2S1_DATA_MIC` at (139.7, 90.17).
3. Follow the wire visually to the ESP32 chip pad and read off the
   GPIO number from the pin name.
4. Update `mp24/main/hal/pins.h` with `PIN_I2S1_DIN_MIC` and document
   it in the comment block.

Once the pin is locked, S-MP10c can ship: extend `hal/audio_i2s.c` to
add an RX channel on the same I²S NUM 0 port (full-duplex shares BCLK
and WS with the existing TX path; only DIN gets added). Sample-rate
conversion (8 kHz mono ↔ 22.05 kHz stereo for the modem↔speaker
bridge) and the bidirectional buffer hand-off complete the work.

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
