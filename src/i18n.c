/*
 * i18n.c - UI language selection and string translation.
 *
 * Persistence uses the project's config.json via ui_loader (cJSON), so the
 * choice survives reboots. Applying the language rebuilds the UI, which is
 * scheduled with lv_async_call() to avoid deleting widgets from inside their
 * own event callback.
 */
#include "i18n.h"

#include <string.h>

#include "cJSON.h"
#include "lvgl.h"
#include "settings_page.h"
#include "status_bar.h"
#include "ui_loader.h"
#include "ui_renderer.h"

/* ------------------------------------------------------------------ */
/* Translation table                                                   */
/* ------------------------------------------------------------------ */
typedef struct {
    const char *key;
    const char *en;
    const char *ru;
} entry_t;

static const entry_t kStrings[] = {
    /* Pages / tiles */
    {"main", "Main", "Главная"},
    {"settings", "Settings", "Настройки"},
    {"general", "General", "Общие"},
    {"wifi", "Wi-Fi", "Wi-Fi"},
    {"bluetooth", "Bluetooth", "Bluetooth"},
    {"language", "Language", "Язык"},
    {"about", "About", "О системе"},
    {"back", "Back", "Назад"},
    {"photoshop", "Photoshop", "Photoshop"},
    {"browser", "Browser", "Браузер"},
    {"vsc", "VS Code", "VS Code"},
    {"premiere", "Premiere", "Premiere"},

    /* Settings rows */
    {"brightness", "Brightness", "Яркость"},
    {"button_sound", "Button Sound", "Звук кнопки"},
    {"sleep_timeout", "Sleep Timeout", "Тайм-аут сна"},
    {"show_stats", "Show FPS / CPU", "Показывать FPS / CPU"},
    {"status", "Status", "Состояние"},
    {"device_name", "Device Name", "Имя устройства"},
    {"web_ui_language", "Web UI Language", "Язык веб-интерфейса"},
    {"connected", "Connected", "Подключено"},
    {"not_connected", "Not connected", "Не подключено"},
    {"password", "Password", "Пароль"},

    /* New feature strings (macros / multimedia / OBS / network) */
    {"multimedia", "Multimedia", "Мультимедиа"},
    {"macro_busy", "Macro queue busy, aborted", "Очередь макросов занята, прервано"},
    {"ble_not_connected", "No BLE device connected", "Устройство по BLE не подключено"},
    {"obs_no_link", "No connection to OBS (check network and settings)",
     "Нет связи с OBS (проверьте сеть и настройки)"},
    {"obs_connected", "Connected", "Подключено"},
    {"obs_error", "Error", "Ошибка"},
    {"checking", "Checking...", "Проверка..."},
    {"router_mode", "Router (client)", "Роутер (клиент)"},
    {"ap_mode", "Access point", "Точка доступа"},
    {"wifi_mode", "Mode", "Режим"},
    {"wifi_mode_opts", "Access point\nRouter (client)", "Точка доступа\nРоутер (клиент)"},
    {"mac_addresses", "MAC addresses", "MAC-адреса"},
    {"scan_networks", "Scan networks", "Сканировать сети"},
    {"scan_none", "No networks found", "Сети не найдены"},
    {"network_scan_title", "Available networks", "Доступные сети"},
    {"wifi_no_network", "No network selected", "Сеть не выбрана"},
    {"ssid", "Network (SSID)", "Сеть (SSID)"},
    {"connect", "Connect", "Подключиться"},
    {"start_ap", "Start access point", "Запустить точку доступа"},
    {"check_connection", "Check connection", "Проверить подключение"},
    {"save", "Save", "Сохранить"},
    {"saved", "Saved", "Сохранено"},
    {"rebooting", "Rebooting...", "Перезагрузка..."},
    {"backup", "Config backup (SD)", "Резервная копия (SD)"},
    {"save_to_sd", "Save config to SD", "Сохранить конфиг на SD"},
    {"import_from_sd", "Import config from SD", "Импорт конфига с SD"},
    {"no_sd", "SD card not available", "SD-карта недоступна"},
    {"no_files", "No backups found", "Резервные копии не найдены"},
    {"firmware_sd", "Firmware (SD)", "Прошивка (SD)"},
    {"update_from_sd", "Update from SD", "Обновить с SD"},
    {"no_update_file", "update.bin not found", "Файл update.bin не найден"},
    {"updating", "Updating firmware...", "Обновление прошивки..."},
    {"update_failed", "Firmware update failed", "Ошибка обновления прошивки"},
    {"host", "Host (PC IP)", "Хост (IP ПК)"},
    {"port", "Port", "Порт"},
    {"wifi_obs_hint", "For OBS choose the Router mode", "Для работы с OBS выберите режим Роутер"},
    {"wifi_switch_warn", "Network is restarting, web configurator may disconnect",
     "Сеть перезапускается, веб-конфигуратор может отключиться"},
    {"wifi_connecting", "Connecting...", "Подключение..."},
    {"wifi_disabled_for_ble", "Wi-Fi disabled (Bluetooth enabled)",
     "Wi-Fi выключен (включён Bluetooth)"},
    {"ble_disabled_for_wifi", "Bluetooth disabled (Wi-Fi enabled)",
     "Bluetooth выключен (включён Wi-Fi)"},
    {"ap_started", "Access point starting", "Точка доступа запускается"},
    {"wifi_fallback_ap", "No Wi-Fi, starting access point", "Нет Wi-Fi, запускаю точку доступа"},
    {"ap_active_fmt", "Access point active. Connect to \"%s\" then open 192.168.4.1",
     "Точка активна. Подключитесь к сети \"%s\", затем откройте 192.168.4.1"},
    {"obs_router_required", "OBS requires the \"Wi-Fi client (OBS)\" network mode",
     "Для OBS требуется режим сети \"Wi-Fi клиент (OBS)\""},

    {"sleep_opts", "1 min\n3 min\n5 min\n10 min\nNever",
     "1 мин\n3 мин\n5 мин\n10 мин\nНикогда"},
    {"lang_opts", "English\nRussian", "English\nРусский"},
    {"lang_note", "Device UI: English + Russian (Roboto)\nWeb interface: follows this setting",
     "Язык устройства: русский + английский (Roboto)\nВеб-интерфейс: следует этой настройке"},

    /* Mode / Configuration pages */
    {"mode", "Mode", "Режим"},
    {"configuration", "Configuration", "Конфигурация"},
    {"system_tile", "System", "Система"},
    {"mode_hint", "Choose the device mode, then save and reboot:",
     "Выберите режим устройства, затем сохраните и перезагрузите:"},
    {"mode_opts_radio", "Bluetooth\nWi-Fi AP\nWi-Fi client (OBS)",
     "Bluetooth\nWi-Fi AP\nWi-Fi клиент (OBS)"},
    {"save_reboot", "Save and reboot", "Сохранить и перезагрузить"},
    {"mode_note", "Only one mode runs at a time. Bluetooth = BLE keyboard; Wi-Fi AP = web setup; Wi-Fi client = router + OBS.",
     "Одновременно работает только один режим. Bluetooth = BLE-клавиатура; Wi-Fi AP = веб-настройка; Wi-Fi клиент = роутер + OBS."},

    /* About */
    {"hardware", "Hardware", "Оборудование"},
    {"system_info", "System Info", "Системная информация"},
    {"information", "Information", "Информация"},
    {"versions", "Versions", "Версии"},
    {"firmware", "Firmware", "Прошивка"},
    {"web_interface", "Web interface", "Веб-интерфейс"},
    {"diagnostics", "Diagnostics", "Диагностика"},
    {"version", "Version", "Версия"},
    {"build", "Build", "Сборка"},
    {"temperature", "Temperature", "Температура"},
    {"chip_short", "Chip", "Чип"},
    {"reset_reason", "Reset", "Сброс"},
    {"not_present", "not present", "нет"},
    {"task_stacks", "Task stacks (free/total)", "Стеки задач (свободно/всего)"},
    {"fs_type", "SD filesystem", "SD Файловая система"},
    {"fs_device", "Device FS", "FS Устройства"},
    {"chip", "Chip", "Чип"},
    {"cores", "cores", "ядер"},
    {"uptime", "Uptime", "Время работы"},
    {"hw_text",
     "Board: JC3248W535EN\n"
     "MCU: ESP32-S3-WROOM-1\n"
     "Flash: 16 MB\n"
     "PSRAM: 8 MB\n"
     "Display: 480x320 IPS\n"
     "Controller: AXS15231B\n"
     "Touch: Capacitive I2C\n"
     "Connectivity: BLE HID",
     "Плата: JC3248W535EN\n"
     "MCU: ESP32-S3-WROOM-1\n"
     "Flash: 16 МБ\n"
     "PSRAM: 8 МБ\n"
     "Дисплей: 480x320 IPS\n"
     "Контроллер: AXS15231B\n"
     "Сенсор: ёмкостный I2C\n"
     "Связь: BLE HID"},

    {NULL, NULL, NULL}
};

/* Config page display names -> translation keys. */
static const struct {
    const char *name;
    const char *key;
} kPageNames[] = {
    {"Main", "main"},
    {"Settings", "settings"},
    {"General", "general"},
    {"Wi-Fi", "wifi"},
    {"Bluetooth", "bluetooth"},
    {"Language", "language"},
    {"About", "about"},
    {"Back", "back"},
    {"Multimedia", "multimedia"},
    {"Photoshop", "photoshop"},
    {"Browser", "browser"},
    {"VS Code", "vsc"},
    {"Premiere", "premiere"},
    {NULL, NULL}
};

static language_t s_lang = LANG_EN;

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */
void i18n_init(void)
{
    cJSON *cfg = get_config();
    cJSON *settings = cfg ? cJSON_GetObjectItemCaseSensitive(cfg, "settings") : NULL;
    cJSON *lang = settings ? cJSON_GetObjectItemCaseSensitive(settings, "language") : NULL;
    s_lang = (cJSON_IsString(lang) && strcmp(lang->valuestring, "ru") == 0) ? LANG_RU : LANG_EN;
}

language_t i18n_get_language(void)
{
    return s_lang;
}

const char *tr(const char *key)
{
    if (key == NULL) {
        return "";
    }
    for (int i = 0; kStrings[i].key != NULL; i++) {
        if (strcmp(kStrings[i].key, key) == 0) {
            return (s_lang == LANG_RU) ? kStrings[i].ru : kStrings[i].en;
        }
    }
    return key;
}

const char *tr_page(const char *name)
{
    if (name == NULL) {
        return "";
    }
    for (int i = 0; kPageNames[i].name != NULL; i++) {
        if (strcmp(kPageNames[i].name, name) == 0) {
            return tr(kPageNames[i].key);
        }
    }
    return name;
}

static void i18n_rebuild_async(void *unused)
{
    (void)unused;
    i18n_refresh_ui();
}

void i18n_set_language(language_t lang)
{
    if (lang == s_lang) {
        return;
    }
    s_lang = lang;

    /* Persist to config.json (settings.language). */
    cJSON *cfg = get_config();
    if (cfg != NULL) {
        cJSON *settings = cJSON_GetObjectItemCaseSensitive(cfg, "settings");
        if (settings == NULL) {
            settings = cJSON_AddObjectToObject(cfg, "settings");
        }
        if (settings != NULL) {
            cJSON_DeleteItemFromObject(settings, "language");
            cJSON_AddStringToObject(settings, "language", (lang == LANG_RU) ? "ru" : "en");
            save_config();
        }
    }

    /* Rebuild the UI after the current event callback has returned. */
    lv_async_call(i18n_rebuild_async, NULL);
}

void i18n_refresh_ui(void)
{
    lv_obj_t *scr = lv_scr_act();
    if (scr == NULL) {
        return;
    }

    /* Rebuild the whole UI: screen children are recreated, while the screen's
     * own gesture handler stays attached. */
    lv_obj_clean(scr);
    create_ui();
    create_settings_page(get_ui_tabview());
    create_status_bar(scr);

    /* Return to Settings / Language (the page that triggered the switch). */
    lv_obj_t *tv = get_ui_tabview();
    int settings_tab = get_settings_tab_index();
    if (tv != NULL && settings_tab >= 0) {
        lv_tabview_set_act(tv, (uint16_t)settings_tab, LV_ANIM_OFF);
        settings_page_show_language();
    } else {
        update_status_bar(tr("main"));
    }
}
