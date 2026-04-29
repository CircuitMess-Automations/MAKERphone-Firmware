# MAKERphone 2.0 Firmware

Prototype firmware for the MAKERphone 2.0, built on the CircuitMess Chatter hardware platform.

## Hardware
- ESP32 (Chatter board: `cm:esp32:chatter`)
- 160x128 TFT display (LovyanGFX)
- 16-button shift-register keypad (0-9, arrows, A/B/C, L/R)
- LoRa radio (SX1262 via RadioLib)
- Piezo buzzer
- Battery with ADC monitoring

## Architecture
- **UI Framework**: LVGL 8.x
- **HAL**: Chatter-Library (in `libraries/Chatter-Library/`)
- **Application**: Arduino sketch (`MAKERphone-Firmware.ino`)

## Vision: MAKERphone 2.0 UI
Transforming the Chatter into a retro feature-phone experience inspired by classic Sony Ericsson phones:
- Pixel-art homescreen with clock, date, animated sunset/mountain/grid background
- Status bar: signal strength, battery, time
- Softkey labels: CALL / MENU
- Icon-based main menu: Phone, Mail, Contacts, Music, Camera, Games, Settings
- SMS chat bubbles with pixel avatars
- Incoming call screen with answer/hang-up
- T9-style text input using the numpad

## Building

### Using arduino-cli
```bash
arduino-cli compile --fqbn cm:esp32:chatter --libraries ./libraries MAKERphone-Firmware.ino
```

### Board package installation
Follow the CircuitMess Arduino package installation instructions at
[CircuitMess/Arduino-Packages](https://github.com/CircuitMess/Arduino-Packages).

## CI
GitHub Actions workflow runs on the self-hosted Mac runner (`bit-flash` label) on every push to master.

## SPIFFS
UI and audio assets are stored in the `data/` directory and uploaded to the ESP32's SPIFFS partition.

```bash
mkspiffs -c data -s 0x1EF000 -b 4096 -p 256 spiffs.bin
esptool --chip esp32 --baud 921600 write_flash -z --flash_mode dio --flash_freq 80m --flash_size detect 0x211000 spiffs.bin
```

---
Based on [Chatter-Firmware](https://github.com/CircuitMess/Chatter-Firmware) by CircuitMess.

Copyright (c) 2025 CircuitMess. Licensed under [MIT License](https://opensource.org/licenses/MIT).
