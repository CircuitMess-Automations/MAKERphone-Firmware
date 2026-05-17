/*
 * MP2.4 shim -- SD.h.
 *
 * S-MP20/9a: Upstream Pong/GameState.cpp + Pong/TitleState.cpp
 * have a vestigial `#include <SD.h>` carried over from the
 * original Chatter codebase. Neither TU actually references any
 * SD-related symbol -- the SD global is never touched, no
 * `SD.begin()` / `SD.open()` calls exist in either file. The
 * include is dead-weight, but removing it from upstream would
 * be an unnecessary patch when an empty shim resolves the
 * dependency cleanly.
 *
 * On MP2.4 the device has no SD-card slot anyway -- Decision D
 * gives us a 2 MB SPIFFS partition at 0x210000 for persistent
 * data and that is mounted via hal/storage's spiffs_*. If a
 * future feature port actually wants SD-style filesystem
 * semantics, we'll either (a) wrap the SPIFFS mount behind an
 * SD-shaped FS object, or (b) vendor arduino-esp32's real SD
 * library. Until then this header exists solely so the
 * `#include <SD.h>` lines in upstream code parse to nothing.
 *
 * No symbols defined here -- the `SD` global from real Arduino
 * SD.h is intentionally absent so any latent caller that DOES
 * reference it will fail at link time loud-and-clear rather
 * than silently mis-route to a stub.
 */
#pragma once
