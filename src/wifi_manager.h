/*
 * wifi_manager.h - WiFi mode management (router client / access point).
 *
 * The board can either join an existing router (station, required for OBS) or
 * publish its own access point for the web configurator. The selection and the
 * credentials are stored in config.json:
 *
 *   "network": { "mode": "sta" | "ap", "ssid": "...", "password": "..." }
 */
#ifndef MODIPAD_WIFI_MANAGER_H
#define MODIPAD_WIFI_MANAGER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_APP_AP = 0,   /* Access point */
    WIFI_APP_STA = 1,  /* Router (client) */
} wifi_app_mode_t;

#define WIFI_SCAN_MAX 16

/* Bring up the TCP/IP stack + WiFi driver (idempotent, no interface started). */
void wifi_manager_init(void);

/* Start the interface selected in config.json and the HTTP server. */
void wifi_manager_start(void);

/* Stop WiFi and the HTTP server. */
void wifi_manager_stop(void);

/* Persist `mode`/`ssid`/`password` and (re)start the interface. */
void wifi_manager_apply(int mode, const char *ssid, const char *password);

/* Persist `mode`/`ssid`/`password` WITHOUT starting the interface (used when a
 * radio switch is applied by rebooting). */
void wifi_manager_configure(int mode, const char *ssid, const char *password);

/* Persist only the selected mode (no restart) while WiFi is disabled. */
void wifi_manager_set_mode(int mode);

/* Asynchronous variants (run on a helper task): use these from the UI so the
 * LVGL task never blocks on WiFi init/start/stop or flash writes. */
void wifi_manager_request_enable(bool enabled);
void wifi_manager_request_apply(int mode, const char *ssid, const char *password);
void wifi_manager_request_set_mode(int mode);

/* BLE transport control on the same helper task: enabling WiFi turns BLE off
 * (and vice versa) to free/allocate the internal RAM they share. */
void wifi_manager_request_ble_enable(void);
void wifi_manager_request_ble_disable(void);

int wifi_manager_mode(void);
const char *wifi_manager_ssid(void);
const char *wifi_manager_password(void);

bool wifi_manager_connected(void);
const char *wifi_manager_ip(void);

/* Signal strength of the connected AP in dBm (0 when not in STA mode). */
int wifi_manager_rssi(void);

/* Number of STA disconnects since boot (diagnostics). */
int wifi_manager_reconnect_count(void);

/* Access-point network name + password actually in use. */
const char *wifi_manager_ap_ssid(void);
const char *wifi_manager_ap_pass(void);

/* Asynchronous scan (only meaningful in router/client mode). */
void wifi_manager_request_scan(void);
bool wifi_manager_scan_running(void);
int wifi_manager_scan_count(void);
const char *wifi_manager_scan_ssid(int index);
int wifi_manager_scan_rssi(int index);

#ifdef __cplusplus
}
#endif

#endif /* MODIPAD_WIFI_MANAGER_H */
