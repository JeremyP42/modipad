/*
 * ota_update.h - Firmware update helpers (web upload + SD staged file).
 */
#ifndef MODIPAD_OTA_UPDATE_H
#define MODIPAD_OTA_UPDATE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Staged firmware file on the SD card (Variant B). */
#define OTA_SD_PATH "/sdcard/modipad/update.bin"

/* True when OTA_SD_PATH exists. */
bool ota_sd_update_present(void);

/* Write the firmware image at `path` into the inactive OTA slot and select it
 * as the next boot partition. Returns true on success (caller should reboot). */
bool ota_flash_file(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* MODIPAD_OTA_UPDATE_H */
