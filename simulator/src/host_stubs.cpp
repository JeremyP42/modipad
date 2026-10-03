/*
 * host_stubs.cpp - Host implementations of the "hardware" functions called by
 * the real UI modules, plus the /littlefs -> ./data path redirect.
 *
 * Add any new "undefined reference" symbol here until the native build links.
 */
#include <stdio.h>
#include <string.h>
#include <dirent.h>

#include "esp_bsp.h"
#include "config_backup.h"
#include "display_init.h"
#include "keyboard_manager.h"
#include "macro_player.h"
#include "obs_client.h"
#include "ota_update.h"
#include "toast.h"
#include "web_server.h"
#include "wifi_manager.h"

extern "C" {

/* --- path redirect: "/littlefs/..." -> "../datadevice/..." and
 *     "/sdcard/..." -> "../datasdcard/".
 * The simulator runs with its CWD set to simulator/, so "../datadevice" is the
 * project's real data folder - the SAME one that `uploadfs` flashes. Saving the
 * config in the emulator therefore updates the config that gets built/flashed
 * (no intermediate copy).
 * Enabled via -Wl,--wrap=fopen (see extra_paths.py). This intercepts fopen()
 * calls from our code and from LVGL's POSIX/STDIO FS drivers without touching
 * any headers. */
FILE *__real_fopen(const char *path, const char *mode);
FILE *__wrap_fopen(const char *path, const char *mode)
{
    char buf[512];
    const char *p = path;
    if (p != NULL && strncmp(p, "/littlefs", 9) == 0) {
        snprintf(buf, sizeof(buf), "../datadevice%s", p + 9);
        p = buf;
    } else if (p != NULL && strncmp(p, "/sdcard", 7) == 0) {
        snprintf(buf, sizeof(buf), "../datasdcard%s", p + 7);
        p = buf;
    }
    return __real_fopen(p, mode);
}

/* ui_assets lists directories via opendir() with raw "/littlefs/..." paths, so
 * the same redirect is needed there (otherwise every image lookup fails). */
DIR *__real_opendir(const char *name);
DIR *__wrap_opendir(const char *name)
{
    char buf[512];
    const char *p = name;
    if (p != NULL && strncmp(p, "/littlefs", 9) == 0) {
        snprintf(buf, sizeof(buf), "../datadevice%s", p + 9);
        p = buf;
    } else if (p != NULL && strncmp(p, "/sdcard", 7) == 0) {
        snprintf(buf, sizeof(buf), "../datasdcard%s", p + 7);
        p = buf;
    }
    return __real_opendir(p);
}

/* --- LVGL mutex (no threading on host) --- */
bool bsp_display_lock(uint32_t timeout_ms) { (void)timeout_ms; return true; }
void bsp_display_unlock(void) {}

/* --- LVGL port diagnostics (real impl is in src/lv_port.c, not built here) --- */
uint32_t lvgl_port_frame_count(void) { return 0; }
void lvgl_port_cpu_stats(uint64_t *busy_us, uint64_t *total_us)
{
    if (busy_us) *busy_us = 0;
    if (total_us) *total_us = 0;
}

/* --- display / backlight --- */
void set_brightness(uint8_t b) { printf("[HOST] set_brightness(%u)\n", (unsigned)b); }
uint8_t get_brightness(void) { return 80; }
void display_backlight_off(void) {}

/* --- keyboard --- */
void send_hotkey(const char *keys) { printf("[HOST] send_hotkey(%s)\n", keys ? keys : ""); }
void send_text(const char *text) { printf("[HOST] send_text(%s)\n", text ? text : ""); }
void init_keyboard(bool enable_ble) { printf("[HOST] init_keyboard(ble=%d)\n", (int)enable_ble); }
void keyboard_set_ble_enabled(bool enabled) { printf("[HOST] keyboard_set_ble_enabled(%d)\n", (int)enabled); }
bool keyboard_ble_enabled(void) { return true; }
bool is_keyboard_connected(void) { return false; }
void keyboard_loop(void) {}
void keyboard_set_connection_cb(void (*cb)(bool)) { (void)cb; }

/* --- web server --- */
void web_server_loop(void) {}
bool web_server_running(void) { return false; }

/* --- multimedia (BLE consumer control) --- */
bool send_multimedia(const char *command) { printf("[HOST] send_multimedia(%s)\n", command ? command : ""); return true; }

/* --- toast --- */
void toast_show(const char *message) { printf("[HOST] toast(%s)\n", message ? message : ""); }

/* --- macro player --- */
void macro_player_init(void) {}
bool macro_play_json(const char *steps_json) { printf("[HOST] macro(%s)\n", steps_json ? steps_json : ""); return true; }

/* --- OBS client --- */
void obs_client_init(void) {}
void obs_client_loop(void) {}
void obs_client_command(const char *command) { printf("[HOST] obs command(%s)\n", command ? command : ""); }
void obs_client_request_check(void) {}
obs_check_result_t obs_client_check_result(void) { return OBS_CHECK_NONE; }
obs_state_t obs_client_get_state(void) { return OBS_STATE_UNKNOWN; }
void obs_client_get_config(char *host, unsigned host_len, int *port, char *password, unsigned pwd_len)
{
    if (host && host_len) host[0] = '\0';
    if (port) *port = 4455;
    if (password && pwd_len) password[0] = '\0';
}
void obs_client_set_config(const char *host, int port, const char *password)
{
    (void)host; (void)port; (void)password;
}

/* --- wifi manager --- */
void wifi_manager_init(void) {}
void wifi_manager_start(void) {}
void wifi_manager_stop(void) {}
void wifi_manager_apply(int mode, const char *ssid, const char *password)
{ (void)mode; (void)ssid; (void)password; }
void wifi_manager_configure(int mode, const char *ssid, const char *password)
{ (void)mode; (void)ssid; (void)password; }
void wifi_manager_set_mode(int mode) { (void)mode; }
void wifi_manager_request_enable(bool enabled) { (void)enabled; }
void wifi_manager_request_apply(int mode, const char *ssid, const char *password)
{ (void)mode; (void)ssid; (void)password; }
void wifi_manager_request_set_mode(int mode) { (void)mode; }
void wifi_manager_request_ble_enable(void) {}
void wifi_manager_request_ble_disable(void) {}
int wifi_manager_mode(void) { return WIFI_APP_AP; }
const char *wifi_manager_ssid(void) { return ""; }
const char *wifi_manager_password(void) { return ""; }
bool wifi_manager_connected(void) { return false; }
const char *wifi_manager_ip(void) { return ""; }
const char *wifi_manager_ap_ssid(void) { return "ModiPAD_Setup"; }
const char *wifi_manager_ap_pass(void) { return "12345678"; }
void wifi_manager_request_scan(void) {}
bool wifi_manager_scan_running(void) { return false; }
int wifi_manager_scan_count(void) { return 0; }
const char *wifi_manager_scan_ssid(int index) { (void)index; return ""; }
int wifi_manager_scan_rssi(int index) { (void)index; return 0; }

/* --- config backup / OTA / SD (device only) --- */
void sd_card_info(bool *present, uint32_t *total_kb, uint32_t *free_kb)
{
    if (present) *present = false;
    if (total_kb) *total_kb = 0;
    if (free_kb) *free_kb = 0;
}
bool config_backup_sd_ready(void) { return false; }
int config_backup_save(char *name_out, size_t name_len)
{ (void)name_out; (void)name_len; return -1; }
int config_backup_list(char names[][64], int max) { (void)names; (void)max; return 0; }
bool config_backup_import(const char *name) { (void)name; return false; }
bool ota_sd_update_present(void) { return false; }
bool ota_flash_file(const char *path) { (void)path; return false; }

} /* extern "C" */
