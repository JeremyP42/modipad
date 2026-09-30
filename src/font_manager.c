/*
 * font_manager.c - Roboto (Latin + Cyrillic) loaded from LittleFS, with
 * Montserrat fallback for sizes/symbols that Roboto does not provide.
 *
 * Font files live in /fonts (datadevice/fonts in the repo): roboto_<size>.bin and
 * roboto_<size>_bold.bin for sizes 10/12/14/16/18. On the device these are at
 * "/littlefs/fonts/...", exposed to LVGL as drive 'S'; in the host simulator
 * LV_FS_POSIX_PATH is "./data", so the same "S:/fonts/..." path works.
 */
#include "font_manager.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "font_manager";

/* Roboto 14/18 are compiled into the firmware (no filesystem needed), so even
 * the very first splash frame is Roboto. 10/12 are loaded from the device or
 * SD; bold variants come from files too. */
extern const lv_font_t roboto_14;
extern const lv_font_t roboto_18;

#define FONT_MAX_SIZE 18

static lv_font_t *s_font[FONT_MAX_SIZE + 1];
static lv_font_t *s_font_bold[FONT_MAX_SIZE + 1];
static bool s_tried[FONT_MAX_SIZE + 1];
static bool s_tried_bold[FONT_MAX_SIZE + 1];

static const lv_font_t *montserrat(int size)
{
    switch (size) {
    case 10: return &lv_font_montserrat_10;
    case 12: return &lv_font_montserrat_12;
    case 14: return &lv_font_montserrat_14;
    case 16: return &lv_font_montserrat_16;
    case 18: return &lv_font_montserrat_18;
    case 20: return &lv_font_montserrat_20;
    case 24: return &lv_font_montserrat_24;
    case 28: return &lv_font_montserrat_28;
    default: return &lv_font_montserrat_14;
    }
}

/* Load "S:/fonts/<name>.bin" from the device, falling back to the SD card
 * (which holds the full font set; the device only ships the sizes it uses). */
static lv_font_t *load_one(const char *name)
{
    char path[64];
    lv_font_t *f = NULL;

    snprintf(path, sizeof(path), "S:/fonts/%s.bin", name);
    f = lv_font_load(path);
    if (f == NULL) {
        snprintf(path, sizeof(path), "D:/fonts/%s.bin", name);
        f = lv_font_load(path);
    }
    if (f != NULL) {
        ESP_LOGI(TAG, "loaded %s", path);
    } else {
        ESP_LOGW(TAG, "font not found, using Montserrat fallback: %s", name);
    }
    return f;
}

/* Lazily load a size/bold on first use. */
static lv_font_t *font_get(int size, bool bold)
{
    if (size <= 0 || size > FONT_MAX_SIZE) {
        return NULL;
    }
    lv_font_t **slot = bold ? &s_font_bold[size] : &s_font[size];
    bool *tried = bold ? &s_tried_bold[size] : &s_tried[size];
    if (*slot != NULL || *tried) {
        return *slot;
    }
    *tried = true;
    char name[24];
    snprintf(name, sizeof(name), bold ? "roboto_%d_bold" : "roboto_%d", size);
    *slot = load_one(name);
    return *slot;
}

void init_fonts(void)
{
    /* Fonts are loaded on demand (see font_get): the device only needs the
     * sizes it actually renders, the rest live on the SD card. */
}

const lv_font_t *get_font(int size)
{
    if (size == 14) {
        return &roboto_14;
    }
    if (size == 18) {
        return &roboto_18;
    }
    lv_font_t *f = font_get(size, false);
    return f != NULL ? f : montserrat(size);
}

const lv_font_t *get_font_bold(int size)
{
    lv_font_t *f = font_get(size, true);
    if (f != NULL) {
        return f;
    }
    return get_font(size);
}
