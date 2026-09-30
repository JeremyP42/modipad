/*
 * ota_update.c - Firmware update helpers.
 *
 * Variant A (web) streams the uploaded image straight to the inactive OTA
 * partition (see web_server.c). Variant B (SD) stages update.bin on the card
 * and flashes it only when the user confirms (device UI or web button).
 *
 * Rollback: CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE lets the bootloader revert to
 * the previous image if the new one does not call
 * esp_ota_mark_app_valid_cancel_rollback() within its first boots.
 */
#include "ota_update.h"

#include <stdio.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

static const char *TAG = "ota";

bool ota_sd_update_present(void)
{
    FILE *f = fopen(OTA_SD_PATH, "rb");
    if (f == NULL) {
        return false;
    }
    fclose(f);
    return true;
}

bool ota_flash_file(const char *path)
{
    if (path == NULL) {
        return false;
    }
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (part == NULL) {
        ESP_LOGE(TAG, "No OTA update partition");
        return false;
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGW(TAG, "cannot open %s", path);
        return false;
    }

    esp_ota_handle_t h = 0;
    if (esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &h) != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed");
        fclose(f);
        return false;
    }

    char buf[4096];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (esp_ota_write(h, buf, n) != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed");
            ok = false;
            break;
        }
    }
    fclose(f);

    if (!ok || esp_ota_end(h) != ESP_OK) {
        esp_ota_abort(h);
        return false;
    }
    if (esp_ota_set_boot_partition(part) != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed");
        return false;
    }
    ESP_LOGI(TAG, "firmware written to \"%s\"; reboot to apply", part->label);
    return true;
}
