#pragma once
/*
 * Host stand-in for esp_littlefs.h. LittleFS is not mounted on the host; files
 * are read from ./data via the host_fopen() redirect (see host_redirect.h).
 */
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *base_path;
    const char *partition_label;
    bool format_if_mount_failed;
    bool dont_mount;
} esp_vfs_littlefs_conf_t;

static inline esp_err_t esp_vfs_littlefs_register(const esp_vfs_littlefs_conf_t *c)
{
    (void)c;
    return ESP_OK;
}

static inline esp_err_t esp_littlefs_info(const char *label, size_t *total, size_t *used)
{
    (void)label;
    if (total) *total = 1000000;
    if (used) *used = 4096;
    return ESP_OK;
}

#ifdef __cplusplus
}
#endif
