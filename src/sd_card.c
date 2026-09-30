/*
 * sd_card.c - microSD card mount via SDMMC (1-bit), mounted at /sdcard.
 *
 * Pins and interface taken from the board reference (JC3248W535EN /
 * hd2_macropad): D0 = GPIO13, CLK = GPIO12, CMD = GPIO11, internal pull-ups.
 * The card holds the bulky media (backgrounds, gallery images, sounds); the
 * internal LittleFS keeps config, fonts and a small default asset set.
 */
#include "sd_card.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "ff.h"

static const char *TAG = "sd_card";

#define SD_MOUNT_POINT "/sdcard"
#define SD_PIN_D0  13
#define SD_PIN_CLK 12
#define SD_PIN_CMD 11

static sdmmc_card_t *s_card = NULL;
static bool s_mounted = false;

esp_err_t sd_card_init(void)
{
    if (s_mounted) {
        return ESP_OK;
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = SD_PIN_CLK;
    slot.cmd = SD_PIN_CMD;
    slot.d0 = SD_PIN_D0;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    ESP_LOGI(TAG, "Mounting SD card at %s (D0=%d CLK=%d CMD=%d)",
             SD_MOUNT_POINT, SD_PIN_D0, SD_PIN_CLK, SD_PIN_CMD);

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &mount_config, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD card not available (%s); continuing without it", esp_err_to_name(ret));
        s_card = NULL;
        return ret;
    }

    s_mounted = true;
    sdmmc_card_print_info(stdout, s_card);
    ESP_LOGI(TAG, "SD card ready");
    return ESP_OK;
}

void sd_card_info(bool *present, uint32_t *total_kb, uint32_t *free_kb)
{
    if (present) {
        *present = s_mounted;
    }
    if (total_kb) {
        *total_kb = 0;
    }
    if (free_kb) {
        *free_kb = 0;
    }
    if (!s_mounted) {
        return;
    }
    uint64_t total = 0;
    uint64_t free_bytes = 0;
    if (esp_vfs_fat_info(SD_MOUNT_POINT, &total, &free_bytes) == ESP_OK) {
        if (total_kb) {
            *total_kb = (uint32_t)(total / 1024u);
        }
        if (free_kb) {
            *free_kb = (uint32_t)(free_bytes / 1024u);
        }
    }
}

const char *sd_card_fs_type(void)
{
    if (!s_mounted) {
        return "-";
    }
    FATFS *fs = NULL;
    DWORD nclst = 0;
    if (f_getfree(SD_MOUNT_POINT, &nclst, &fs) == FR_OK && fs != NULL) {
        switch (fs->fs_type) {
        case FS_FAT12:
            return "FAT12";
        case FS_FAT16:
            return "FAT16";
        case FS_FAT32:
            return "FAT32";
        case FS_EXFAT:
            return "exFAT";
        default:
            break;
        }
    }
    return "FAT";
}
