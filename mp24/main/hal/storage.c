/*
 * mp24/main/hal/storage.c — SPIFFS mount, stats, file helpers.
 */

#include "hal/storage.h"
#include "esp_spiffs.h"
#include "esp_log.h"

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "STORE";

static bool   s_mounted    = false;
static size_t s_total      = 0;
static size_t s_used       = 0;
static size_t s_file_count = 0;

#define BASE_PATH      "/spiffs"
#define PART_LABEL     "spiffs"
#define READDIR_LIMIT  256
#define SENTINEL_NAME  "hello.txt"

/* ----------------------------------------------------------------- */

static void enumerate_and_log(void)
{
    DIR *d = opendir(BASE_PATH);
    if (!d) {
        ESP_LOGW(TAG, "opendir(%s) failed", BASE_PATH);
        return;
    }
    s_file_count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && s_file_count < READDIR_LIMIT) {
        /* Stat the file to surface its size in the log. SPIFFS is
         * flat so we don't need to recurse. */
        char path[96];
        snprintf(path, sizeof(path), "%s/%s", BASE_PATH, ent->d_name);
        struct stat st;
        if (stat(path, &st) == 0) {
            ESP_LOGI(TAG, "  %-32s %lu B",
                     ent->d_name, (unsigned long) st.st_size);
        } else {
            ESP_LOGI(TAG, "  %s  (stat failed)", ent->d_name);
        }
        s_file_count++;
    }
    closedir(d);
}

static void read_sentinel(void)
{
    char buf[160];
    ssize_t n = storage_read_file(SENTINEL_NAME, buf, sizeof(buf));
    if (n < 0) {
        ESP_LOGW(TAG, "sentinel %s missing or unreadable", SENTINEL_NAME);
        return;
    }
    /* Strip trailing newlines so the log line stays single-row. */
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
        buf[--n] = '\0';
    }
    ESP_LOGI(TAG, "sentinel read OK (%zd B): \"%s\"", n, buf);
}

/* ----------------------------------------------------------------- */

esp_err_t storage_init(void)
{
    if (s_mounted) return ESP_OK;

    esp_vfs_spiffs_conf_t conf = {
        .base_path              = BASE_PATH,
        .partition_label        = PART_LABEL,
        .max_files              = 8,        /* simultaneous open files */
        .format_if_mount_failed = true,
    };

    esp_err_t r = esp_vfs_spiffs_register(&conf);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "esp_vfs_spiffs_register: %s", esp_err_to_name(r));
        return r;
    }

    if (esp_spiffs_info(PART_LABEL, &s_total, &s_used) != ESP_OK) {
        ESP_LOGW(TAG, "esp_spiffs_info failed (continuing)");
        s_total = s_used = 0;
    }

    enumerate_and_log();
    read_sentinel();

    int pct = (s_total > 0) ? (int)((s_used * 100) / s_total) : 0;
    ESP_LOGI(TAG, "mounted; %zu files, %zu / %zu bytes used (%d%%)",
             s_file_count, s_used, s_total, pct);

    s_mounted = true;
    return ESP_OK;
}

bool storage_mounted(void)         { return s_mounted; }
size_t storage_file_count(void)    { return s_file_count; }
size_t storage_used_bytes(void)    { return s_used; }
size_t storage_total_bytes(void)   { return s_total; }

/* ----------------------------------------------------------------- */

ssize_t storage_read_file(const char *name, void *buf, size_t buf_len)
{
    if (!s_mounted || !name || !buf || buf_len == 0) return -1;

    char path[96];
    if (snprintf(path, sizeof(path), "%s/%s", BASE_PATH, name)
        >= (int) sizeof(path)) {
        ESP_LOGW(TAG, "path too long: %s", name);
        return -1;
    }

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    size_t n = fread(buf, 1, buf_len - 1, f);
    fclose(f);
    ((char *) buf)[n] = '\0';
    return (ssize_t) n;
}
