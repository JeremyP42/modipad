/*
 * config_backup.c - Save/restore config.json on the SD card.
 *
 * Backups live in /sdcard/modipad/config as backup_<version>_<n>.json (n is the
 * device uptime in seconds, as before). Importing validates the JSON, replaces
 * the active config and writes it back to the device LittleFS (a reboot then
 * rebuilds the UI).
 */
#include "config_backup.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "cJSON.h"
#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ui_loader.h"

static const char *TAG = "config_backup";

bool config_backup_sd_ready(void)
{
    DIR *d = opendir("/sdcard/modipad");
    if (d == NULL) {
        return false;
    }
    closedir(d);
    return true;
}

int config_backup_save(char *name_out, size_t name_len)
{
    mkdir("/sdcard/modipad", 0777);
    mkdir(CONFIG_BACKUP_DIR, 0777);

    cJSON *cfg = get_config();
    if (cfg == NULL) {
        return -1;
    }
    char *text = cJSON_Print(cfg);
    if (text == NULL) {
        return -1;
    }
    size_t len = strlen(text);

    char name[64];
    snprintf(name, sizeof(name), "backup_%s_%u.json", MODIPAD_FIRMWARE_VERSION,
             (unsigned)(esp_timer_get_time() / 1000000));
    char path[192];
    snprintf(path, sizeof(path), "%s/%s", CONFIG_BACKUP_DIR, name);

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        cJSON_free(text);
        ESP_LOGW(TAG, "cannot write %s", path);
        return -2;
    }
    size_t wr = fwrite(text, 1, len, f);
    fclose(f);
    cJSON_free(text);

    if (wr != len) {
        return -3;
    }
    if (name_out != NULL) {
        snprintf(name_out, name_len, "%s", name);
    }
    ESP_LOGI(TAG, "saved %s (%u bytes)", path, (unsigned)len);
    return 0;
}

int config_backup_list(char names[][64], int max)
{
    DIR *d = opendir(CONFIG_BACKUP_DIR);
    if (d == NULL) {
        return 0;
    }
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < max) {
        if (e->d_name[0] == '.') {
            continue;
        }
        size_t l = strlen(e->d_name);
        if (l < 5 || strcasecmp(e->d_name + l - 5, ".json") != 0) {
            continue;
        }
        strncpy(names[n], e->d_name, 63);
        names[n][63] = '\0';
        n++;
    }
    closedir(d);
    return n;
}

bool config_backup_import(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return false;
    }
    char path[192];
    snprintf(path, sizeof(path), "%s/%s", CONFIG_BACKUP_DIR, name);
    if (!load_config_from(path)) {
        ESP_LOGW(TAG, "import failed: %s", path);
        return false;
    }
    save_config();
    ESP_LOGI(TAG, "imported %s", path);
    return true;
}
