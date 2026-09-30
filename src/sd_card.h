/*
 * sd_card.h - microSD card mount (SDMMC 1-bit) for media stored on the card.
 */
#ifndef MODIPAD_SD_CARD_H
#define MODIPAD_SD_CARD_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mount the card at /sdcard. Safe to call with no card inserted (returns an
 * error and keeps running). Returns ESP_OK when mounted. */
esp_err_t sd_card_init(void);

/* Report mount state and capacity (KB). Safe to call when no card is present. */
void sd_card_info(bool *present, uint32_t *total_kb, uint32_t *free_kb);

/* Filesystem type string ("FAT16"/"FAT32"/"exFAT"/"-" when absent). */
const char *sd_card_fs_type(void);

#ifdef __cplusplus
}
#endif

#endif /* MODIPAD_SD_CARD_H */
