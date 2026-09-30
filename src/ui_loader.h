/*
 * ui_loader.h - LittleFS-backed configuration and settings storage.
 */
#ifndef MODIPAD_UI_LOADER_H
#define MODIPAD_UI_LOADER_H

#include "cJSON.h"
#include "config.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t brightness;
    bool sound_enabled;
    radio_mode_t radio_mode;  /* BLE / AP / STA (single active radio) */
    bool wifi_enabled;        /* derived: radio_mode != BLE */
    bool ble_enabled;         /* derived: radio_mode == BLE */
    uint16_t sleep_timeout; /* seconds, 0 = never */
    uint8_t caption_font_size; /* px, caption under buttons */
    bool caption_font_bold;    /* caption weight */
    bool show_stats;           /* show FPS/CPU in the status bar */
    char splash_bg[48];
    char settings_bg[48];
    char menu_bg[48];
} AppSettings;

/* Mount the LittleFS partition (formats it on first boot if needed). */
esp_err_t ui_loader_init(void);

/* Load/save config.json (the UI layout). */
bool load_config(void);
bool save_config(void);
cJSON *get_config(void);

/* BLE device name from config.json ("device_name"), or the compile-time default
 * (BLE_DEVICE_NAME) when unset. Applied on the next BLE init (reboot). */
const char *get_device_name(void);

/* Replace the active config by parsing the JSON at `path` (e.g. an SD backup).
 * Does not save it; call save_config() to persist to LittleFS. */
bool load_config_from(const char *path);

/* Load/save settings.json. */
bool load_settings(AppSettings *settings);
bool save_settings(const AppSettings *settings);

/* Currently active settings (never NULL after ui_loader_init()). */
const AppSettings *get_settings(void);

/* Mutable access to the active settings. */
AppSettings *get_settings_mut(void);

#ifdef __cplusplus
}
#endif

#endif /* MODIPAD_UI_LOADER_H */
