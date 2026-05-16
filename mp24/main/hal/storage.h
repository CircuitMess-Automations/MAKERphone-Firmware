/*
 * mp24/main/hal/storage.h — SPIFFS storage HAL.
 *
 * Mounts the `spiffs` partition (see ../../partitions.csv: 1984 KB at
 * offset 0x210000) at the VFS root `/spiffs`. Files placed in
 * mp24/spiffs/ at build time are packaged into the partition image
 * by spiffs_create_partition_image() in main/CMakeLists.txt.
 *
 * Once mounted, the firmware uses standard C stdio (fopen/fread/etc.)
 * to access files via `/spiffs/<filename>`. SPIFFS is flat — no
 * directories. Use prefix conventions for namespacing (e.g.
 * "avatar_alice.gif" rather than "avatars/alice.gif").
 *
 * Public API exposes statistics for status display and a thin
 * convenience wrapper for the common "read a small file into a
 * buffer" pattern.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include "esp_err.h"

/* Mount /spiffs. format_if_mount_failed=true means a fresh chip with
 * an empty partition is silently formatted on first boot. Idempotent.
 *
 * On success, log line:
 *   I (xxx) STORE: mounted; N files, U/T bytes used (P%)
 *
 * Plus one log line per discovered file at INFO level, and one line
 * per sentinel read sanity check. */
esp_err_t storage_init(void);

/* True after storage_init() has returned ESP_OK. */
bool storage_mounted(void);

/* Number of files at the SPIFFS root (max 256 — readdir loop caps
 * to keep boot fast even on a future-bloated image). 0 if unmounted. */
size_t storage_file_count(void);

/* Partition sizing — populated by esp_spiffs_info() at mount time.
 * Both report 0 if unmounted or if the info call failed. */
size_t storage_used_bytes(void);
size_t storage_total_bytes(void);

/* Convenience reader: opens /spiffs/<name>, reads up to buf_len-1
 * bytes, NUL-terminates the buffer, closes. Returns bytes read
 * (>=0) on success or -1 on any failure (missing file, oversized,
 * read error). Useful for small text/config files; for big binaries
 * use plain fopen/fread directly. */
ssize_t storage_read_file(const char *name, void *buf, size_t buf_len);
