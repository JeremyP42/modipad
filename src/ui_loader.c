/*
 * ui_loader.c - LittleFS-backed configuration and settings storage.
 */
#include "ui_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_littlefs.h"
#include "esp_log.h"

static const char *TAG = "ui_loader";

static cJSON *s_config = NULL;
static AppSettings s_settings = {
    .brightness = BRIGHTNESS_DEFAULT,
    .sound_enabled = false,
    /* Default: BLE keyboard only. The user can pick Wi-Fi AP (configurator) or
     * Wi-Fi client (OBS) from Settings > Mode; only one radio runs at a time. */
    .radio_mode = RADIO_MODE_BLE,
    .wifi_enabled = false,
    .ble_enabled = true,
    .sleep_timeout = 300,
    .caption_font_size = 12,
    .caption_font_bold = false,
    .show_stats = false,
    .splash_bg = "splash_bg.png",
    .settings_bg = "settings_bg.png",
    .menu_bg = "menu_bg.png",
};

static const char *k_default_config =
    "{"
    "\"version\":\"3.0\","
    "\"device_name\":\"" BLE_DEVICE_NAME "\","
    "\"settings\":{\"language\":\"ru\",\"brightness\":80,\"sleep_timeout\":300},"
    "\"obs\":{\"host\":\"\",\"port\":4455,\"password\":\"\"},"
    "\"network\":{\"mode\":\"ap\",\"ap_ssid\":\"" WIFI_AP_SSID "\",\"ap_password\":\"" WIFI_AP_PASS "\","
    "\"ssid\":\"\",\"password\":\"\"},"
    "\"main_page\":{\"name\":\"Main\",\"display_name\":\"Main\","
    "\"grid\":{\"rows\":2,\"cols\":4,"
    "\"margins\":{\"top\":35,\"bottom\":10,\"left\":15,\"right\":15},"
    "\"spacing\":{\"horizontal\":10,\"vertical\":10}},"
    "\"buttons\":["
    "{\"id\":\"home_settings\",\"position\":{\"row\":0,\"col\":0},\"type\":\"settings\","
    "\"icon\":\"icon_settings.png\",\"caption\":\"Settings\","
    "\"background\":{\"type\":\"solid\",\"color\":\"#2C3E50\"}},"
    "{\"id\":\"home_ps\",\"position\":{\"row\":0,\"col\":1},\"type\":\"page_link\","
    "\"target_page\":\"photoshop\",\"icon\":\"icon_photoshop.png\",\"caption\":\"Photoshop\","
    "\"background\":{\"type\":\"image\",\"image\":\"gradient_ocean_blue.png\"}},"
    "{\"id\":\"home_br\",\"position\":{\"row\":0,\"col\":2},\"type\":\"page_link\","
    "\"target_page\":\"browser\",\"icon\":\"icon_browser.png\",\"caption\":\"Browser\","
    "\"background\":{\"type\":\"image\",\"image\":\"gradient_emerald.png\"}},"
    "{\"id\":\"home_vs\",\"position\":{\"row\":0,\"col\":3},\"type\":\"page_link\","
    "\"target_page\":\"vscode\",\"icon\":\"icon_vsc.png\",\"caption\":\"VS Code\","
    "\"background\":{\"type\":\"image\",\"image\":\"gradient_purple.png\"}},"
    "{\"id\":\"home_obs\",\"position\":{\"row\":1,\"col\":2},\"type\":\"page_link\","
    "\"target_page\":\"obsstudio\",\"icon\":\"icon_obsstudio.png\",\"caption\":\"OBS\","
    "\"background\":{\"type\":\"image\",\"image\":\"gradient_red_blue.png\"}},"
    "{\"id\":\"home_media\",\"position\":{\"row\":1,\"col\":3},\"type\":\"page_link\","
    "\"target_page\":\"multimedia\",\"icon\":\"icon_spotify.png\",\"caption\":\"Multimedia\","
    "\"background\":{\"type\":\"image\",\"image\":\"gradient_emerald.png\"}}"
    "]}"
    ","
    "\"pages\":["
    "{\"id\":\"photoshop\",\"name\":\"Photoshop\",\"display_name\":\"Photoshop\","
    "\"icon\":\"icon_photoshop.png\","
    "\"background\":{\"type\":\"image\",\"image\":\"page_carbon.png\"},"
    "\"grid\":{\"rows\":2,\"cols\":4,"
    "\"margins\":{\"top\":35,\"bottom\":10,\"left\":15,\"right\":15},"
    "\"spacing\":{\"horizontal\":10,\"vertical\":10}},"
    "\"buttons\":["
    "{\"id\":\"ps_copy\",\"position\":{\"row\":0,\"col\":0},\"text\":{\"lines\":[\"Copy\"]},"
    "\"background\":{\"type\":\"image\",\"image\":\"gradient_ocean_blue.png\"},"
    "\"border\":{\"width\":2,\"color\":\"#4ECDC4\"},\"action\":{\"type\":\"hotkey\",\"keys\":\"CTRL+C\"}},"
    "{\"id\":\"ps_paste\",\"position\":{\"row\":0,\"col\":1},\"text\":{\"lines\":[\"Paste\"]},"
    "\"background\":{\"type\":\"image\",\"image\":\"gradient_emerald.png\"},"
    "\"border\":{\"width\":2,\"color\":\"#4ECDC4\"},\"action\":{\"type\":\"hotkey\",\"keys\":\"CTRL+V\"}},"
    "{\"id\":\"ps_save\",\"position\":{\"row\":1,\"col\":0},\"text\":{\"lines\":[\"Save\"]},"
    "\"background\":{\"type\":\"image\",\"image\":\"gradient_dark.png\"},"
    "\"border\":{\"width\":2,\"color\":\"#4ECDC4\"},\"action\":{\"type\":\"hotkey\",\"keys\":\"CTRL+S\"}}"
    "]},"
    "{\"id\":\"browser\",\"name\":\"Browser\",\"display_name\":\"Browser\","
    "\"icon\":\"icon_browser.png\","
    "\"background\":{\"type\":\"image\",\"image\":\"page_blue.png\"},"
    "\"grid\":{\"rows\":2,\"cols\":3,"
    "\"margins\":{\"top\":35,\"bottom\":10,\"left\":15,\"right\":15},"
    "\"spacing\":{\"horizontal\":10,\"vertical\":10}},"
    "\"buttons\":["
    "{\"id\":\"br_back\",\"position\":{\"row\":0,\"col\":0},\"text\":{\"lines\":[\"Back\"]},"
    "\"action\":{\"type\":\"hotkey\",\"keys\":\"ALT+LEFT\"}},"
    "{\"id\":\"br_fwd\",\"position\":{\"row\":0,\"col\":1},\"text\":{\"lines\":[\"Forward\"]},"
    "\"action\":{\"type\":\"hotkey\",\"keys\":\"ALT+RIGHT\"}},"
    "{\"id\":\"br_new\",\"position\":{\"row\":0,\"col\":2},\"text\":{\"lines\":[\"New Tab\"]},"
    "\"action\":{\"type\":\"hotkey\",\"keys\":\"CTRL+T\"}}"
    "]},"
    "{\"id\":\"vscode\",\"name\":\"VS Code\",\"display_name\":\"VS Code\","
    "\"icon\":\"icon_vsc.png\","
    "\"background\":{\"type\":\"image\",\"image\":\"page_purple.png\"},"
    "\"grid\":{\"rows\":2,\"cols\":3,"
    "\"margins\":{\"top\":35,\"bottom\":10,\"left\":15,\"right\":15},"
    "\"spacing\":{\"horizontal\":10,\"vertical\":10}},"
    "\"buttons\":["
    "{\"id\":\"vs_cmd\",\"position\":{\"row\":0,\"col\":0},\"text\":{\"lines\":[\"Command\"]},"
    "\"action\":{\"type\":\"hotkey\",\"keys\":\"CTRL+SHIFT+P\"}},"
    "{\"id\":\"vs_term\",\"position\":{\"row\":0,\"col\":1},\"text\":{\"lines\":[\"Terminal\"]},"
    "\"action\":{\"type\":\"hotkey\",\"keys\":\"CTRL+GRAVE\"}},"
    "{\"id\":\"vs_save\",\"position\":{\"row\":0,\"col\":2},\"text\":{\"lines\":[\"Save\"]},"
    "\"action\":{\"type\":\"hotkey\",\"keys\":\"CTRL+S\"}}"
    "]},"
    "{\"id\":\"multimedia\",\"name\":\"Multimedia\",\"display_name\":\"Multimedia\","
    "\"icon\":\"icon_spotify.png\","
    "\"background\":{\"type\":\"image\",\"image\":\"age_bg_dark.png\"},"
    "\"grid\":{\"rows\":2,\"cols\":3,"
    "\"margins\":{\"top\":35,\"bottom\":10,\"left\":15,\"right\":15},"
    "\"spacing\":{\"horizontal\":10,\"vertical\":10}},"
    "\"buttons\":["
    "{\"id\":\"media_prev\",\"position\":{\"row\":0,\"col\":0},\"icon\":\"skip_previous.png\","
    "\"text\":{\"lines\":[\"Prev\"]},\"action\":{\"type\":\"multimedia\",\"value\":\"PREV\"}},"
    "{\"id\":\"media_play\",\"position\":{\"row\":0,\"col\":1},\"icon\":\"play.png\","
    "\"text\":{\"lines\":[\"Play/Pause\"]},\"action\":{\"type\":\"multimedia\",\"value\":\"PLAY_PAUSE\"}},"
    "{\"id\":\"media_next\",\"position\":{\"row\":0,\"col\":2},\"icon\":\"skip_next.png\","
    "\"text\":{\"lines\":[\"Next\"]},\"action\":{\"type\":\"multimedia\",\"value\":\"NEXT\"}},"
    "{\"id\":\"media_volup\",\"position\":{\"row\":1,\"col\":0},\"icon\":\"volume_up.png\","
    "\"text\":{\"lines\":[\"Vol+\"]},\"action\":{\"type\":\"multimedia\",\"value\":\"VOL_UP\"}},"
    "{\"id\":\"media_mute\",\"position\":{\"row\":1,\"col\":1},\"icon\":\"mute.png\","
    "\"text\":{\"lines\":[\"Mute\"]},\"action\":{\"type\":\"multimedia\",\"value\":\"MUTE\"}},"
    "{\"id\":\"media_voldown\",\"position\":{\"row\":1,\"col\":2},\"icon\":\"volume_down.png\","
    "\"text\":{\"lines\":[\"Vol-\"]},\"action\":{\"type\":\"multimedia\",\"value\":\"VOL_DOWN\"}}"
    "]},"
    "{\"id\":\"obsstudio\",\"name\":\"OBS Studio\",\"display_name\":\"OBS Studio\","
    "\"icon\":\"icon_obsstudio.png\","
    "\"background\":{\"type\":\"image\",\"image\":\"age_bg_dark.png\"},"
    "\"grid\":{\"rows\":2,\"cols\":3,"
    "\"margins\":{\"top\":35,\"bottom\":10,\"left\":15,\"right\":15},"
    "\"spacing\":{\"horizontal\":10,\"vertical\":10}},"
    "\"buttons\":["
    "{\"id\":\"obs_start\",\"position\":{\"row\":0,\"col\":0},\"icon\":\"fiber_manual_record.png\","
    "\"text\":{\"lines\":[\"Start\"]},"
    "\"action\":{\"type\":\"obs\",\"command\":\"START_REC\"}},"
    "{\"id\":\"obs_toggle\",\"position\":{\"row\":0,\"col\":1},\"icon\":\"fiber_manual_record.png\","
    "\"text\":{\"lines\":[\"Record\"]},"
    "\"action\":{\"type\":\"obs\",\"command\":\"TOGGLE_REC\",\"icon_rec\":\"stop.png\"}},"
    "{\"id\":\"obs_stop\",\"position\":{\"row\":0,\"col\":2},\"icon\":\"stop.png\","
    "\"text\":{\"lines\":[\"Stop\"]},"
    "\"action\":{\"type\":\"obs\",\"command\":\"STOP_REC\"}},"
    "{\"id\":\"obs_check\",\"position\":{\"row\":1,\"col\":1},\"icon\":\"refresh.png\","
    "\"text\":{\"lines\":[\"Check\"]},"
    "\"action\":{\"type\":\"obs\",\"command\":\"CHECK\"}}"
    "]}"
    "]"
    "}";

/* ------------------------------------------------------------------ */
/* Filesystem helpers                                                 */
/* ------------------------------------------------------------------ */
esp_err_t ui_loader_init(void)
{
    esp_vfs_littlefs_conf_t conf;
    memset(&conf, 0, sizeof(conf));
    conf.base_path = LFS_MOUNT_POINT;
    conf.partition_label = LFS_PARTITION_LABEL;
    conf.format_if_mount_failed = true;
    conf.dont_mount = false;

    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount LittleFS partition \"%s\": %s",
                 LFS_PARTITION_LABEL, esp_err_to_name(err));
        return err;
    }

    size_t total = 0, used = 0;
    esp_littlefs_info(LFS_PARTITION_LABEL, &total, &used);
    ESP_LOGI(TAG, "LittleFS mounted at %s (%u/%u bytes used)",
             LFS_MOUNT_POINT, (unsigned)used, (unsigned)total);

    if (!load_config()) {
        ESP_LOGW(TAG, "config.json not found, writing built-in default");
        s_config = cJSON_Parse(k_default_config);
        save_config();
    }

    /* Load directly into the active settings: previously this loaded into a
     * local copy that was then discarded, so every reboot silently fell back to
     * the compile-time defaults (e.g. BLE on, WiFi off). */
    if (!load_settings(&s_settings)) {
        ESP_LOGW(TAG, "settings.json not found, writing defaults");
        save_settings(&s_settings);
    }

    return ESP_OK;
}

static char *read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) {
        fclose(f);
        return NULL;
    }

    char *buf = (char *)malloc((size_t)len + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }

    size_t rd = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[rd] = '\0';

    if (out_len) {
        *out_len = rd;
    }
    return buf;
}

static bool write_file(const char *path, const char *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Cannot open %s for writing", path);
        return false;
    }
    size_t wr = fwrite(data, 1, len, f);
    fclose(f);
    return wr == len;
}

/* ------------------------------------------------------------------ */
/* config.json                                                        */
/* ------------------------------------------------------------------ */
/* A device provisioned before the ModiPAD rename keeps the old brand strings
 * in its config.json. Migrate them to the current defaults on load so the
 * Bluetooth device name / Wi-Fi AP SSID actually change after a firmware
 * update instead of silently keeping the previous solution name. */
static bool migrate_legacy_names(cJSON *root)
{
    if (root == NULL) {
        return false;
    }
    bool changed = false;

    cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "device_name");
    if (cJSON_IsString(name) && name->valuestring != NULL &&
        strcmp(name->valuestring, "Smart MacroPad") == 0) {
        cJSON_DeleteItemFromObjectCaseSensitive(root, "device_name");
        cJSON_AddStringToObject(root, "device_name", BLE_DEVICE_NAME);
        changed = true;
    }

    cJSON *net = cJSON_GetObjectItemCaseSensitive(root, "network");
    cJSON *ap_ssid = cJSON_IsObject(net)
                         ? cJSON_GetObjectItemCaseSensitive(net, "ap_ssid")
                         : NULL;
    if (cJSON_IsString(ap_ssid) && ap_ssid->valuestring != NULL &&
        strcmp(ap_ssid->valuestring, "MacroPad_Setup") == 0) {
        cJSON_DeleteItemFromObjectCaseSensitive(net, "ap_ssid");
        cJSON_AddStringToObject(net, "ap_ssid", WIFI_AP_SSID);
        changed = true;
    }

    return changed;
}

bool load_config(void)
{
    char *text = read_file(CONFIG_FILE, NULL);
    if (text == NULL) {
        return false;
    }

    cJSON *root = cJSON_Parse(text);
    free(text);
    if (root == NULL) {
        ESP_LOGE(TAG, "config.json parse error");
        return false;
    }

    if (s_config != NULL) {
        cJSON_Delete(s_config);
    }
    s_config = root;
    if (migrate_legacy_names(s_config)) {
        ESP_LOGI(TAG, "migrated legacy brand names to ModiPAD defaults");
        save_config();
    }
    ESP_LOGI(TAG, "config.json loaded");
    return true;
}

bool save_config(void)
{
    if (s_config == NULL) {
        return false;
    }

    char *text = cJSON_Print(s_config);
    if (text == NULL) {
        return false;
    }
    bool ok = write_file(CONFIG_FILE, text, strlen(text));
    cJSON_free(text);
    return ok;
}

bool load_config_from(const char *path)
{
    if (path == NULL) {
        return false;
    }
    char *text = read_file(path, NULL);
    if (text == NULL) {
        return false;
    }
    cJSON *root = cJSON_Parse(text);
    free(text);
    if (root == NULL) {
        return false;
    }
    if (s_config != NULL) {
        cJSON_Delete(s_config);
    }
    s_config = root;
    migrate_legacy_names(s_config);
    return true;
}

cJSON *get_config(void)
{
    return s_config;
}

/* BLE device name (what Windows/Bluetooth shows). Editable from the web UI
 * (config.json "device_name"); falls back to the compile-time default. Takes
 * effect on the next BLE init (i.e. after a reboot). */
const char *get_device_name(void)
{
    cJSON *n = s_config ? cJSON_GetObjectItemCaseSensitive(s_config, "device_name") : NULL;
    if (cJSON_IsString(n) && n->valuestring != NULL && n->valuestring[0] != '\0') {
        return n->valuestring;
    }
    return BLE_DEVICE_NAME;
}

/* ------------------------------------------------------------------ */
/* settings.json                                                      */
/* ------------------------------------------------------------------ */
bool load_settings(AppSettings *settings)
{
    if (settings == NULL) {
        settings = &s_settings;
    }

    char *text = read_file(SETTINGS_FILE, NULL);
    if (text == NULL) {
        return false;
    }

    cJSON *root = cJSON_Parse(text);
    free(text);
    if (root == NULL) {
        ESP_LOGE(TAG, "settings.json parse error");
        return false;
    }

    const cJSON *brightness = cJSON_GetObjectItemCaseSensitive(root, "brightness");
    const cJSON *sound = cJSON_GetObjectItemCaseSensitive(root, "sound_enabled");
    const cJSON *radio = cJSON_GetObjectItemCaseSensitive(root, "radio_mode");
    const cJSON *wifi = cJSON_GetObjectItemCaseSensitive(root, "wifi_enabled");
    const cJSON *ble = cJSON_GetObjectItemCaseSensitive(root, "ble_enabled");
    const cJSON *sleep = cJSON_GetObjectItemCaseSensitive(root, "sleep_timeout");
    const cJSON *caption = cJSON_GetObjectItemCaseSensitive(root, "caption_font_size");
    const cJSON *caption_bold = cJSON_GetObjectItemCaseSensitive(root, "caption_font_bold");
    const cJSON *show_stats = cJSON_GetObjectItemCaseSensitive(root, "show_stats");
    const cJSON *splash = cJSON_GetObjectItemCaseSensitive(root, "splash_bg");
    const cJSON *settings_bg = cJSON_GetObjectItemCaseSensitive(root, "settings_bg");
    const cJSON *menu_bg = cJSON_GetObjectItemCaseSensitive(root, "menu_bg");

    if (cJSON_IsNumber(brightness)) {
        settings->brightness = (uint8_t)brightness->valuedouble;
    }
    if (cJSON_IsBool(sound)) {
        settings->sound_enabled = cJSON_IsTrue(sound);
    }
    /* radio_mode is the source of truth; fall back to the legacy booleans. */
    if (cJSON_IsNumber(radio)) {
        settings->radio_mode = (radio_mode_t)radio->valueint;
    } else if (cJSON_IsBool(wifi) && cJSON_IsTrue(wifi)) {
        settings->radio_mode = RADIO_MODE_AP;
    } else if (cJSON_IsBool(ble) && cJSON_IsTrue(ble)) {
        settings->radio_mode = RADIO_MODE_BLE;
    }
    if (settings->radio_mode > RADIO_MODE_STA) {
        settings->radio_mode = RADIO_MODE_BLE;
    }
    settings->wifi_enabled = (settings->radio_mode != RADIO_MODE_BLE);
    settings->ble_enabled = (settings->radio_mode == RADIO_MODE_BLE);
    if (cJSON_IsNumber(sleep)) {
        settings->sleep_timeout = (uint16_t)sleep->valueint;
    }
    if (cJSON_IsNumber(caption)) {
        /* Only these Roboto sizes are used for captions. */
        static const int allowed[] = {10, 12, 14, 18};
        int px = caption->valueint;
        int best = allowed[0];
        for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++) {
            if (abs(px - allowed[i]) < abs(px - best)) {
                best = allowed[i];
            }
        }
        settings->caption_font_size = (uint8_t)best;
    }
    if (cJSON_IsBool(caption_bold)) {
        settings->caption_font_bold = cJSON_IsTrue(caption_bold);
    }
    if (cJSON_IsBool(show_stats)) {
        settings->show_stats = cJSON_IsTrue(show_stats);
    }
    if (cJSON_IsString(splash)) {
        strncpy(settings->splash_bg, splash->valuestring, sizeof(settings->splash_bg) - 1);
    }
    if (cJSON_IsString(settings_bg)) {
        strncpy(settings->settings_bg, settings_bg->valuestring, sizeof(settings->settings_bg) - 1);
    }
    if (cJSON_IsString(menu_bg)) {
        strncpy(settings->menu_bg, menu_bg->valuestring, sizeof(settings->menu_bg) - 1);
    }

    cJSON_Delete(root);

    if (settings == &s_settings) {
        ESP_LOGI(TAG, "settings loaded (brightness=%u, wifi=%d, ble=%d)",
                 s_settings.brightness, s_settings.wifi_enabled, s_settings.ble_enabled);
    }
    return true;
}

bool save_settings(const AppSettings *settings)
{
    if (settings == NULL) {
        return false;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return false;
    }
    cJSON_AddNumberToObject(root, "brightness", settings->brightness);
    cJSON_AddBoolToObject(root, "sound_enabled", settings->sound_enabled);
    cJSON_AddNumberToObject(root, "radio_mode", (int)settings->radio_mode);
    cJSON_AddBoolToObject(root, "wifi_enabled", settings->wifi_enabled);
    cJSON_AddBoolToObject(root, "ble_enabled", settings->ble_enabled);
    cJSON_AddNumberToObject(root, "sleep_timeout", settings->sleep_timeout);
    cJSON_AddNumberToObject(root, "caption_font_size", settings->caption_font_size);
    cJSON_AddBoolToObject(root, "caption_font_bold", settings->caption_font_bold);
    cJSON_AddBoolToObject(root, "show_stats", settings->show_stats);
    cJSON_AddStringToObject(root, "splash_bg", settings->splash_bg);
    cJSON_AddStringToObject(root, "settings_bg", settings->settings_bg);
    cJSON_AddStringToObject(root, "menu_bg", settings->menu_bg);

    char *text = cJSON_Print(root);
    cJSON_Delete(root);
    if (text == NULL) {
        return false;
    }

    bool ok = write_file(SETTINGS_FILE, text, strlen(text));
    cJSON_free(text);
    return ok;
}

const AppSettings *get_settings(void)
{
    return &s_settings;
}

AppSettings *get_settings_mut(void)
{
    return &s_settings;
}
