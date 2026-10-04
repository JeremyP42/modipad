/*
 * main.cpp - Application entry point.
 *
 * Boot order:
 *   1. NVS + LittleFS
 *   2. Display / touch (starts the LVGL task)
 *   3. Splash screen
 *   4. Keyboard transports (BLE + USB)
 *   5. WiFi access point + web configurator (optional)
 *   6. JSON driven UI (main_page + pages) + settings page + status bar + gestures
 */
#include "config.h"
#include "display_init.h"
#include "esp_bsp.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "font_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gesture_handler.h"
#include "health.h"
#include "i18n.h"
#include "keyboard_manager.h"
#include "macro_player.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "obs_client.h"
#include "sd_card.h"
#include "settings_page.h"
#include "splash_screen.h"
#include "status_bar.h"
#include "sys_log.h"
#include "system_info.h"
#include "touch_handler.h"
#include "ui_loader.h"
#include "ui_renderer.h"
#include "web_server.h"
#include "wifi_manager.h"

static const char *TAG = "main";

/* --- Diagnostic stage -> per-subsystem enable flags (see config.h) ---- */
#if MODIPAD_DIAG_STAGE == 1
#define DIAG_USE_KEYBOARD 0
#define DIAG_USE_WIFI     0
#define DIAG_USE_MACRO    0
#define DIAG_USE_OBS      0
#elif MODIPAD_DIAG_STAGE == 2
#define DIAG_USE_KEYBOARD 0
#define DIAG_USE_WIFI     0
#define DIAG_USE_MACRO    1
#define DIAG_USE_OBS      1
#elif MODIPAD_DIAG_STAGE == 3
#define DIAG_USE_KEYBOARD 1
#define DIAG_USE_WIFI     1
#define DIAG_USE_MACRO    0
#define DIAG_USE_OBS      0
#else
#define DIAG_USE_KEYBOARD 1
#define DIAG_USE_WIFI     1
#define DIAG_USE_MACRO    1
#define DIAG_USE_OBS      1
#endif
#define DIAG_SLEEP_ENABLED (MODIPAD_DIAG_STAGE == 0)

static void log_heap(const char *stage)
{
    ESP_LOGI(TAG, "[heap] %-18s int_free=%6u int_max=%6u psram_free=%6u",
             stage,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

/* Run the panel bring-up from a core-1 task: spi_bus_initialize() binds the
 * SPI2 interrupt to the calling core, and keeping it on core 1 (with LVGL)
 * instead of core 0 (Bluedroid) prevents the QSPI transfer from stalling while
 * Bluetooth is active. */
static esp_err_t s_display_init_err = ESP_ERR_INVALID_STATE;

static void display_init_task(void *arg)
{
    (void)arg;
    s_display_init_err = init_display();
    if (s_display_init_err != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed");
    }
    vTaskDelete(NULL);
}

static void refresh_connection_indicators(void)
{
    update_bluetooth_status(is_keyboard_connected());
}

static void __attribute__((unused)) connection_changed(bool ble_connected)
{
    ESP_LOGI(TAG, "Connection state changed (BLE=%d)", ble_connected);
    /* Bounded lock: this is called from the keyboard poll (app_loop task), not
     * from the LVGL task. An unbounded wait here would deadlock the device if
     * the LVGL task were ever stuck. Losing one icon update is harmless. */
    if (!bsp_display_lock(100)) {
        ESP_LOGW(TAG, "connection_changed: LVGL lock timeout, skipping UI update");
        return;
    }
    refresh_connection_indicators();
    settings_page_update_status();
    bsp_display_unlock();
}

/* ---- Diagnostics: heartbeat ------------------------------------------
 * Low-priority task on core 0. When the UI "hangs", this keeps printing, so
 * the serial log shows whether the scheduler is alive, whether the heap is
 * draining, and whether the LVGL mutex is stuck. */
static void heartbeat_task(void *arg)
{
    (void)arg;
    uint32_t last_loops = 0;
    int stall_samples = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        uint32_t fl = 0, dg = 0, ex = 0, sk = 0;
        lvgl_port_stats(&fl, &dg, &ex, &sk);
        uint32_t loops = lvgl_port_loops();
        ESP_LOGI("HEARTBEAT",
                 "alive heap=%u int_min=%u int_largest=%u psram=%u tasks=%u locked=%d stage=%u loops=%u flush=%u done=%u exit=%u skip=%u uptime=%us",
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 (unsigned)uxTaskGetNumberOfTasks(),
                 (int)bsp_display_is_locked(),
                 (unsigned)lvgl_port_stage(),
                 (unsigned)loops,
                 (unsigned)fl, (unsigned)dg, (unsigned)ex, (unsigned)sk,
                 (unsigned)(esp_timer_get_time() / 1000000));

        /* True stall detection: the LVGL task loop counter must keep advancing.
         * Sampling the stage is unreliable (the task is legitimately found
         * inside the flush path very often), so only a frozen loop counter means
         * the task is genuinely blocked and the device needs a reboot. */
        if (loops == last_loops) {
            if (++stall_samples >= 2) {
                ESP_LOGE("HEARTBEAT", "LVGL task stalled (stage=%u loops=%u) - rebooting to recover",
                         (unsigned)lvgl_port_stage(), (unsigned)loops);
                vTaskDelay(pdMS_TO_TICKS(200));
                esp_restart();
            }
        } else {
            stall_samples = 0;
        }
        last_loops = loops;
    }
}

/* ---- Boot-loop guard -------------------------------------------------
 * The radio is switched by "save + reboot". A bad persisted setting (e.g. a
 * Wi-Fi that crashes the driver) could loop forever. Count fast reboots in NVS
 * and, after 3 of them, force the access point so the device stays reachable
 * for reconfiguration. The counter is cleared once the device ran 15 s. */
static void boot_guard_check(void)
{
    nvs_handle_t h;
    if (nvs_open("boot", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    uint8_t cnt = 0;
    nvs_get_u8(h, "cnt", &cnt);
    if (cnt < 255) {
        cnt++;
    }
    nvs_set_u8(h, "cnt", cnt);
    nvs_commit(h);
    ESP_LOGI(TAG, "boot counter = %u", (unsigned)cnt);

    if (cnt > 3) {
        AppSettings *st = get_settings_mut();
        st->radio_mode = RADIO_MODE_AP;
        st->wifi_enabled = true;
        st->ble_enabled = false;
        save_settings(st);
        ESP_LOGW(TAG, "Boot-loop detected (%u fast reboots) - forcing Wi-Fi AP mode", (unsigned)cnt);
        nvs_set_u8(h, "cnt", 0);
        nvs_commit(h);
    }
    nvs_close(h);
}

static void boot_guard_clear(void)
{
    nvs_handle_t h;
    if (nvs_open("boot", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_u8(h, "cnt", 0);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "boot counter cleared (device stable)");
}

static void app_loop_task(void *arg)
{
    (void)arg;
    bool sleeping = false;
    bool boot_ok = false;

    int heartbeat = 0;
    while (1) {
        keyboard_loop();
        web_server_loop();
        obs_client_loop();

        if (!boot_ok && esp_timer_get_time() > 15000000) {
            /* Running long enough: confirm this image and cancel OTA rollback. */
            esp_ota_mark_app_valid_cancel_rollback();
            boot_guard_clear();
            boot_ok = true;
        }

        if (++heartbeat >= 25) { /* ~5 s */
            heartbeat = 0;
            ESP_LOGD(TAG, "alive: free heap %u", (unsigned)esp_get_free_heap_size());
        }

        const AppSettings *settings = get_settings();
#if DIAG_SLEEP_ENABLED
        if (settings != NULL && settings->sleep_timeout > 0) {
            uint32_t inactive_ms = lv_disp_get_inactive_time(NULL);
            if (!sleeping && inactive_ms > (uint32_t)settings->sleep_timeout * 1000u) {
                ESP_LOGI(TAG, "Screen sleep");
                display_backlight_fade(0, 500);
                display_backlight_off();
                sleeping = true;
            } else if (sleeping && inactive_ms < 500) {
                ESP_LOGI(TAG, "Screen wake");
                display_backlight_fade(settings->brightness, 300);
                sleeping = false;
            }
        } else if (sleeping) {
            set_brightness(settings->brightness);
            sleeping = false;
        }
#else
        /* Diagnostic build: keep the backlight on so a dark screen can only
         * mean a real hang, not the intentional inactivity sleep. */
        (void)settings;
        (void)sleeping;
#endif

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

extern "C" void app_main(void)
{
    sys_log_init(); /* capture logs into the RAM ring buffer for the web UI */
    ESP_LOGI(TAG, "====================================");
    ESP_LOGI(TAG, "  ModiPAD - starting up");
    ESP_LOGI(TAG, "  reset reason: %d", (int)esp_reset_reason());
    ESP_LOGW(TAG, "  DIAGNOSTIC STAGE = %d", MODIPAD_DIAG_STAGE);
    ESP_LOGI(TAG, "====================================");

    /* --- NVS --- */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* --- Display + touch FIRST: the splash can then be shown before LittleFS
     * is mounted and before the fonts are loaded, so the user sees boot
     * progress almost immediately instead of a long black screen. --- */
    /* Panel bring-up on core 1 (see display_init_task): binds the SPI2 ISR to
     * core 1, away from Bluedroid. */
    if (xTaskCreatePinnedToCore(display_init_task, "display_init", 8192, NULL, 5, NULL, 1) != pdPASS) {
        ESP_LOGE(TAG, "Display init task create failed");
        return;
    }
    while (s_display_init_err == ESP_ERR_INVALID_STATE) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_display_init_err != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed");
        return;
    }
    init_touch();
    show_splash_screen();          /* early: solid bg + built-in font only */
    update_progress(5, SPLASH_STATUS_INIT);

    /* --- LittleFS + configuration --- */
    if (ui_loader_init() != ESP_OK) {
        ESP_LOGE(TAG, "Filesystem init failed, continuing with defaults");
    }
    log_heap("after fs");
    const AppSettings *settings = get_settings();

    /* --- SD card: mount EARLY, before the radios (BLE/Wi-Fi) allocate their
     * internal RAM. The SDMMC driver allocates its buffers from internal RAM;
     * mounting after the radios are up can fail with ESP_ERR_NO_MEM
     * ("could not allocate sd_ssr") even with a card inserted, because the
     * internal-RAM largest block is tiny by then. Non-fatal if absent. --- */
    sd_card_init();
    log_heap("after sd");

    /* Safety net: if we keep crashing/rebooting, fall back to AP mode. */
    boot_guard_check();
    settings = get_settings();
    ESP_LOGI(TAG, "radio_mode=%d (wifi=%d ble=%d)", (int)settings->radio_mode,
             (int)settings->wifi_enabled, (int)settings->ble_enabled);

    /* --- Fonts + language + rich splash (background image, Roboto) --- */
    init_fonts();
    i18n_init();
    system_info_init();
    set_splash_language(i18n_get_language() == LANG_RU);
    splash_screen_apply_assets();
    log_heap("after display+fonts");
    update_progress(10, SPLASH_STATUS_CONFIG);

    /* --- Keyboard --- */
    update_progress(15, SPLASH_STATUS_KEYBOARD);
#if DIAG_USE_KEYBOARD
    init_keyboard(settings->ble_enabled);
    keyboard_set_connection_cb(connection_changed);
#else
    ESP_LOGW(TAG, "DIAG stage %d: keyboard/BLE disabled", MODIPAD_DIAG_STAGE);
#endif
    log_heap("after keyboard");

    /* --- Background macro player + OBS client --- */
#if DIAG_USE_MACRO
    macro_player_init();
#else
    ESP_LOGW(TAG, "DIAG stage %d: macro player disabled", MODIPAD_DIAG_STAGE);
#endif
#if DIAG_USE_OBS
    obs_client_init();
#else
    ESP_LOGW(TAG, "DIAG stage %d: OBS client disabled", MODIPAD_DIAG_STAGE);
#endif

    /* --- Network: only in Wi-Fi modes. BLE and Wi-Fi are exclusive (internal
     * RAM), so the WiFi driver is not even initialised in BLE mode. --- */
    update_progress(40, SPLASH_STATUS_WEBSERVER);
#if DIAG_USE_WIFI
    if (settings->wifi_enabled) {
        wifi_manager_init();
        wifi_manager_set_mode(settings->radio_mode == RADIO_MODE_STA ? WIFI_APP_STA : WIFI_APP_AP);
        wifi_manager_start();
    }
#else
    ESP_LOGW(TAG, "DIAG stage %d: Wi-Fi / web server disabled", MODIPAD_DIAG_STAGE);
#endif
    log_heap("after wifi");

    /* --- Startup diagnostics (SD / config / missing assets) --- */
    health_check_all();

    /* --- UI --- */
    update_progress(70, SPLASH_STATUS_UI);
    log_heap("before ui");
    bsp_display_lock(0);
    create_ui();
    log_heap("after create_ui");
    create_settings_page(get_ui_tabview());
    log_heap("after settings");
    init_gestures();
    set_brightness(settings->brightness);
    bsp_display_unlock();

    /* --- Status bar overlay --- */
    bsp_display_lock(0);
    create_status_bar(lv_scr_act());
    update_status_bar(ui_page_name(0));
    refresh_connection_indicators();
    update_wifi_status(wifi_manager_connected());
    bsp_display_unlock();

    update_progress(100, SPLASH_STATUS_READY);

    vTaskDelay(pdMS_TO_TICKS(300));
    close_splash_screen();

    ESP_LOGI(TAG, "System ready");

    system_info_t sysinfo;
    system_info_collect(&sysinfo);
    system_info_print(&sysinfo);

    TaskHandle_t app_handle = NULL;
    xTaskCreate(app_loop_task, "app_loop", 4096, NULL, 3, &app_handle);
    system_info_register_task("app_loop", app_handle, 4096);

    /* Heartbeat on core 0 (with the radio / IDLE). Priority 1 so it can never
     * starve the UI or network tasks. */
    TaskHandle_t hb_handle = NULL;
    xTaskCreatePinnedToCore(heartbeat_task, "heartbeat", 3072, NULL, 1, &hb_handle, 0);
    system_info_register_task("heartbeat", hb_handle, 3072);
}
