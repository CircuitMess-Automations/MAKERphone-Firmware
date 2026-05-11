# MAKERphone v2.4 firmware (mp24)

ESP-IDF firmware for the **MAKERphone v2.4** PCB (ESP32-S3FH4R2 + cellular).

Sibling to the legacy Arduino-based Chatter firmware at the repository
root. Both firmwares live in the same repo because they share the
same upper-layer LVGL feature code (270 sessions across `src/`) that
the port progressively migrates into ESP-IDF over the S-MP-NN
sessions tracked in [`../docs/MP24_PORT_ROADMAP.md`](../docs/MP24_PORT_ROADMAP.md).

## Build

Requires ESP-IDF v5.5 or later sourced in the shell.

```bash
cd mp24
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor
```

The monitor connects to the **USB CDC console** (Decision F1) — there
is no separate UART console on the production firmware.

## Layout

```
mp24/
├── CMakeLists.txt              top-level ESP-IDF project
├── partitions.csv              Decision-D 4-entry table (2 MB app + 2 MB SPIFFS)
├── sdkconfig.defaults          base config (USB CDC console, octal PSRAM, WDT on)
├── main/                       app entry point + glue
│   ├── CMakeLists.txt
│   └── app_main.c              S-MP01: I²C + AW9523B init + LED blink
└── components/
    └── circuitos_shim/         ESP-IDF reimplementation of CircuitOS headers
                                used by the legacy Chatter firmware. Populated
                                across Sessions S-MP02 .. S-MP06.
```

## Current state (S-MP01)

Skeleton compiles. Flashed board prints a banner over USB CDC, brings
up the main I²C bus at GPIO 47/48 @ 100 kHz, initialises the AW9523B
with the canonical sequence lifted from the standalone hw_test
project, and blinks the 8 on-board SMD LEDs at 2 Hz. Display, input,
audio, battery, and the GSM modem all land in later sessions.

## Hardware references

Schematics and the standalone `hw_test` source live in project
knowledge at `/mnt/project/`. The single source of truth for button
bit assignments is `dashboard.c` (`k_buttons[]` table) — the
`pin_config.h` comments are stale (schematic-derived, superseded by
hardware verification in 2025-05). See `../docs/MP24_PORT_PLAN.md` §7
for the audit trail.
