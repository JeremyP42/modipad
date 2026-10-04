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

/* Capacity and filesystem type are captured ONCE, right after the (early) mount.
 * Reading them later via esp_vfs_fat_info()/f_getfree() makes FatFS read the FAT
 * into its window; once the radios have claimed the scarce internal RAM those
 * reads need a temporary internal-DMA bounce buffer that can no longer be
 * allocated (sdmmc_read_blocks -> ESP_ERR_NO_MEM, "Failed to get number of free
 * clusters"). At mount time there is still free internal RAM, so the capture
 * succeeds and the status bar / About screen use the cached values. */
static uint32_t s_total_kb = 0;
static uint32_t s_free_kb = 0;
static char s_fs_type[8] = "FAT";

static void capture_fs_info(void)
{
    uint64_t total = 0;
    uint64_t free_bytes = 0;
    if (esp_vfs_fat_info(SD_MOUNT_POINT, &total, &free_bytes) == ESP_OK) {
        s_total_kb = (uint32_t)(total / 1024u);
        s_free_kb = (uint32_t)(free_bytes / 1024u);
    }
    FATFS *fs = NULL;
    DWORD nclst = 0;
    if (f_getfree(SD_MOUNT_POINT, &nclst, &fs) == FR_OK && fs != NULL) {
        switch (fs->fs_type) {
        case FS_FAT12: snprintf(s_fs_type, sizeof(s_fs_type), "FAT12"); break;
        case FS_FAT16: snprintf(s_fs_type, sizeof(s_fs_type), "FAT16"); break;
        case FS_FAT32: snprintf(s_fs_type, sizeof(s_fs_type), "FAT32"); break;
        case FS_EXFAT: snprintf(s_fs_type, sizeof(s_fs_type), "exFAT"); break;
        default: break;
        }
    }
}

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
    capture_fs_info();
    ESP_LOGI(TAG, "SD card ready (%u/%u KB free, %s)",
             (unsigned)s_free_kb, (unsigned)s_total_kb, s_fs_type);
    return ESP_OK;
}

void sd_card_info(bool *present, uint32_t *total_kb, uint32_t *free_kb)
{
    if (present) {
        *present = s_mounted;
    }
    if (total_kb) {
        *total_kb = s_total_kb;
    }
    if (free_kb) {
        *free_kb = s_free_kb;
    }
}

const char *sd_card_fs_type(void)
{
    return s_mounted ? s_fs_type : "-";
}
