/*
 * config_backup.h - Save/restore config.json on the SD card.
 */
#ifndef MODIPAD_CONFIG_BACKUP_H
#define MODIPAD_CONFIG_BACKUP_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Directory on the SD card holding the backup files. */
#define CONFIG_BACKUP_DIR "/sdcard/modipad/config"

/* True when the SD card is mounted (backups can be listed/saved). */
bool config_backup_sd_ready(void);

/* Write the current config to CONFIG_BACKUP_DIR/backup_<secs>.json.
 * Returns 0 on success, negative on error; copies the file name into name_out. */
int config_backup_save(char *name_out, size_t name_len);

/* List *.json files in the backup dir. Returns the count (<= max). */
int config_backup_list(char names[][64], int max);

/* Load <name> from the backup dir into the active config and persist it to the
 * device LittleFS. The caller should reboot afterwards to rebuild the UI. */
bool config_backup_import(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* MODIPAD_CONFIG_BACKUP_H */
