/*
 * wifi_manager.c - WiFi mode management (see wifi_manager.h).
 */
#include "wifi_manager.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i18n.h"
#include "keyboard_manager.h"
#include "system_info.h"
#include "toast.h"
#include "ui_loader.h"
#include "web_server.h"

static const char *TAG = "wifi_manager";

static bool s_netif_inited = false;
static bool s_wifi_inited = false;
static bool s_started = false;

static int s_mode = WIFI_APP_AP;
static char s_ap_ssid[64] = WIFI_AP_SSID;
static char s_ap_pass[64] = WIFI_AP_PASS;
static char s_sta_ssid[64] = "";
static char s_sta_pass[64] = "";
static char s_ip[20] = "";
static bool s_scan_from_ap = false;

static bool s_sta_connected = false;
static bool s_ap_active = false;
static int s_reconnect_count = 0;

static bool s_scan_running = false;
static int s_scan_count = 0;
static char s_scan_ssid[WIFI_SCAN_MAX][33];
static int s_scan_rssi[WIFI_SCAN_MAX];

static esp_netif_t *s_ap_netif = NULL;
static esp_netif_t *s_sta_netif = NULL;

/* Signalled by AP_STOP / STA_STOP so a stop() can wait until the interface is
 * really down before start() runs again (a stop/start race leaves the AP in a
 * half-initialised state where clients cannot associate). */
static SemaphoreHandle_t s_stop_sem = NULL;

/* Timestamp when STA mode started, for the "no router -> fall back to AP"
 * timeout (0 = disabled). */
static int64_t s_sta_start_us = 0;

/* Lazily create the radio command worker (also created at init so the STA
 * fallback timer runs even when WiFi was started directly from app_main). */
static void wifi_req_ensure(void);

/* ------------------------------------------------------------------ */
/* Config                                                             */
/* ------------------------------------------------------------------ */
static void copy_str(char *dst, size_t len, const char *src)
{
    if (src == NULL) {
        return;
    }
    strncpy(dst, src, len - 1);
    dst[len - 1] = '\0';
}

static void load_network(void)
{
    s_mode = WIFI_APP_AP;
    copy_str(s_ap_ssid, sizeof(s_ap_ssid), WIFI_AP_SSID);
    copy_str(s_ap_pass, sizeof(s_ap_pass), WIFI_AP_PASS);
    s_sta_ssid[0] = '\0';
    s_sta_pass[0] = '\0';

    cJSON *cfg = get_config();
    cJSON *net = cfg ? cJSON_GetObjectItemCaseSensitive(cfg, "network") : NULL;
    if (!cJSON_IsObject(net)) {
        return;
    }

    cJSON *mode = cJSON_GetObjectItemCaseSensitive(net, "mode");
    if (cJSON_IsString(mode) && strcmp(mode->valuestring, "sta") == 0) {
        s_mode = WIFI_APP_STA;
    }

    cJSON *ap_ssid = cJSON_GetObjectItemCaseSensitive(net, "ap_ssid");
    cJSON *ap_pass = cJSON_GetObjectItemCaseSensitive(net, "ap_password");
    cJSON *ssid = cJSON_GetObjectItemCaseSensitive(net, "ssid");
    cJSON *pass = cJSON_GetObjectItemCaseSensitive(net, "password");

    /* New schema has separate ap_* keys; both credential pairs are always
     * loaded so the device UI can show the client network even in AP mode. */
    bool new_schema = cJSON_IsString(ap_ssid) || cJSON_IsString(ap_pass);
    if (new_schema) {
        if (cJSON_IsString(ap_ssid)) copy_str(s_ap_ssid, sizeof(s_ap_ssid), ap_ssid->valuestring);
        if (cJSON_IsString(ap_pass)) copy_str(s_ap_pass, sizeof(s_ap_pass), ap_pass->valuestring);
        if (cJSON_IsString(ssid)) copy_str(s_sta_ssid, sizeof(s_sta_ssid), ssid->valuestring);
        if (cJSON_IsString(pass)) copy_str(s_sta_pass, sizeof(s_sta_pass), pass->valuestring);
    } else {
        /* Old single-credential schema: the pair belongs to the active mode. */
        if (s_mode == WIFI_APP_AP) {
            if (cJSON_IsString(ssid)) copy_str(s_ap_ssid, sizeof(s_ap_ssid), ssid->valuestring);
            if (cJSON_IsString(pass)) copy_str(s_ap_pass, sizeof(s_ap_pass), pass->valuestring);
        } else {
            if (cJSON_IsString(ssid)) copy_str(s_sta_ssid, sizeof(s_sta_ssid), ssid->valuestring);
            if (cJSON_IsString(pass)) copy_str(s_sta_pass, sizeof(s_sta_pass), pass->valuestring);
        }
    }

    if (s_ap_ssid[0] == '\0') {
        copy_str(s_ap_ssid, sizeof(s_ap_ssid), WIFI_AP_SSID);
    }
}

static void save_network(void)
{
    cJSON *cfg = get_config();
    if (cfg == NULL) {
        return;
    }
    cJSON *net = cJSON_GetObjectItemCaseSensitive(cfg, "network");
    if (net == NULL) {
        net = cJSON_AddObjectToObject(cfg, "network");
    }
    if (net == NULL) {
        return;
    }
    cJSON_DeleteItemFromObject(net, "mode");
    cJSON_DeleteItemFromObject(net, "ap_ssid");
    cJSON_DeleteItemFromObject(net, "ap_password");
    cJSON_DeleteItemFromObject(net, "ssid");
    cJSON_DeleteItemFromObject(net, "password");
    cJSON_AddStringToObject(net, "mode", s_mode == WIFI_APP_STA ? "sta" : "ap");
    cJSON_AddStringToObject(net, "ap_ssid", s_ap_ssid);
    cJSON_AddStringToObject(net, "ap_password", s_ap_pass);
    cJSON_AddStringToObject(net, "ssid", s_sta_ssid);
    cJSON_AddStringToObject(net, "password", s_sta_pass);
    save_config();
}

/* ------------------------------------------------------------------ */
/* Events                                                             */
/* ------------------------------------------------------------------ */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "STA connected to \"%s\"", s_sta_ssid);
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGW(TAG, "STA disconnected (reason %d)", d ? d->reason : -1);
            s_sta_connected = false;
            s_ip[0] = '\0';
            s_sta_start_us = esp_timer_get_time(); /* restart the fallback timer */
            s_reconnect_count++;
            if (s_started && s_mode == WIFI_APP_STA) {
                ESP_LOGW(TAG, "STA disconnected, retrying");
                esp_wifi_connect();
            }
            break;
        }
        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *c = (wifi_event_ap_staconnected_t *)event_data;
            if (c != NULL) {
                char mac[18];
                snprintf(mac, sizeof(mac), MACSTR, MAC2STR(c->mac));
                ESP_LOGI(TAG, "AP: station %s connected", mac);
            }
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t *c = (wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGW(TAG, "AP: station disconnected (reason %d)", c ? c->reason : -1);
            break;
        }
        case WIFI_EVENT_AP_START:
            s_ap_active = true;
            snprintf(s_ip, sizeof(s_ip), "192.168.4.1");
            ESP_LOGI(TAG, "SoftAP \"%s\" started (192.168.4.1)", s_ap_ssid);
            break;
        case WIFI_EVENT_AP_STOP:
            s_ap_active = false;
            if (s_stop_sem != NULL) {
                xSemaphoreGive(s_stop_sem);
            }
            break;
        case WIFI_EVENT_STA_STOP:
            if (s_stop_sem != NULL) {
                xSemaphoreGive(s_stop_sem);
            }
            break;
        case WIFI_EVENT_SCAN_DONE: {
            uint16_t n = 0;
            esp_wifi_scan_get_ap_num(&n);
            if (n > WIFI_SCAN_MAX) {
                n = WIFI_SCAN_MAX;
            }
            wifi_ap_record_t records[WIFI_SCAN_MAX];
            uint16_t got = n;
            if (esp_wifi_scan_get_ap_records(&got, records) == ESP_OK) {
                s_scan_count = (int)got;
                for (int i = 0; i < s_scan_count; i++) {
                    strncpy(s_scan_ssid[i], (const char *)records[i].ssid, 32);
                    s_scan_ssid[i][32] = '\0';
                    s_scan_rssi[i] = records[i].rssi;
                }
            }
            s_scan_running = false;
            if (s_scan_from_ap) {
                s_scan_from_ap = false;
                esp_wifi_set_mode(WIFI_MODE_AP);
            }
            ESP_LOGI(TAG, "Scan done: %d networks", s_scan_count);
            break;
        }
        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_sta_connected = true;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "STA got IP: %s", s_ip);
    }
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */
void wifi_manager_init(void)
{
    if (!s_netif_inited) {
        esp_netif_init();
        esp_event_loop_create_default();
        s_netif_inited = true;
    }
    if (s_stop_sem == NULL) {
        s_stop_sem = xSemaphoreCreateBinary();
    }
    if (!s_wifi_inited) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        if (esp_wifi_init(&cfg) != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_init failed (not enough internal RAM?) - WiFi not started");
            return;
        }
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            &wifi_event_handler, NULL, NULL);
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            &wifi_event_handler, NULL, NULL);
        s_wifi_inited = true;
    }
    load_network();
}

static void start_ap(void)
{
    if (!s_wifi_inited) {
        return;
    }
    if (s_ap_netif == NULL) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }
    if (s_ap_netif == NULL) {
        ESP_LOGE(TAG, "Cannot create AP network interface");
        return;
    }
    const char *ssid = s_ap_ssid[0] ? s_ap_ssid : WIFI_AP_SSID;
    const char *pass = s_ap_pass;
    size_t pass_len = strlen(pass);

    wifi_config_t ap_config;
    memset(&ap_config, 0, sizeof(ap_config));
    strncpy((char *)ap_config.ap.ssid, ssid, sizeof(ap_config.ap.ssid) - 1);
    ap_config.ap.ssid_len = strlen(ssid);
    strncpy((char *)ap_config.ap.password, pass, sizeof(ap_config.ap.password) - 1);
    ap_config.ap.channel = WIFI_AP_CHANNEL;
    ap_config.ap.max_connection = WIFI_AP_MAX_STA;
    if (pass_len == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    } else if (pass_len < 8) {
        /* WPA2 needs at least 8 characters; a shorter key makes set_config fail
         * and leaves the AP unusable, so fall back to an open network. */
        ESP_LOGW(TAG, "AP password shorter than 8 chars, starting without a password");
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    } else {
        ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_mode(AP): %s", esp_err_to_name(err));
    }
    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_config(AP): %s", esp_err_to_name(err));
    }
    err = esp_wifi_start();
    s_sta_start_us = 0;
    /* Never log the AP password. */
    ESP_LOGI(TAG, "Access point \"%s\" starting (channel %d, auth %d, pass length %u)",
             ssid, WIFI_AP_CHANNEL, (int)ap_config.ap.authmode, (unsigned)pass_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start: %s", esp_err_to_name(err));
    }
}

static void start_sta(void)
{
    if (!s_wifi_inited) {
        return;
    }
    if (s_sta_netif == NULL) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
    }
    if (s_sta_ssid[0] == '\0') {
        ESP_LOGW(TAG, "Router mode without an SSID configured");
        return;
    }

    wifi_config_t sta_config;
    memset(&sta_config, 0, sizeof(sta_config));
    strncpy((char *)sta_config.sta.ssid, s_sta_ssid, sizeof(sta_config.sta.ssid) - 1);
    strncpy((char *)sta_config.sta.password, s_sta_pass, sizeof(sta_config.sta.password) - 1);

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_mode(STA): %s", esp_err_to_name(err));
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_config(STA): %s", esp_err_to_name(err));
    }
    err = esp_wifi_start();
    s_sta_start_us = esp_timer_get_time();
    ESP_LOGI(TAG, "Connecting to router \"%s\"", s_sta_ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start: %s", esp_err_to_name(err));
    }
}

void wifi_manager_start(void)
{
    if (s_started) {
        return;
    }
    if (!s_wifi_inited) {
        wifi_manager_init();
    }
    if (!s_wifi_inited) {
        ESP_LOGE(TAG, "WiFi not initialised, cannot start the interface");
        return;
    }
    /* Create the command/fallback worker now that WiFi is actually used. */
    wifi_req_ensure();
    load_network();

    s_started = true;
    if (s_mode == WIFI_APP_STA) {
        start_sta();
    } else {
        start_ap();
    }
    web_server_start_http();
}

void wifi_manager_stop(void)
{
    if (!s_started) {
        return;
    }
    /* The HTTP server is intentionally kept running: it has no interface to
     * serve while WiFi is down, but restarting it from an HTTP handler would
     * stop the very server answering the request. */
    if (s_stop_sem != NULL) {
        xSemaphoreTake(s_stop_sem, 0); /* drop any stale signal */
    }
    esp_wifi_stop();
    /* Wait until the interface is actually down; starting again too early
     * leaves the AP unable to accept clients. */
    if (s_stop_sem != NULL) {
        xSemaphoreTake(s_stop_sem, pdMS_TO_TICKS(1000));
    }
    s_started = false;
    s_ap_active = false;
    s_sta_connected = false;
    s_ip[0] = '\0';
    s_sta_start_us = 0;
    ESP_LOGI(TAG, "WiFi stopped");
}

void wifi_manager_configure(int mode, const char *ssid, const char *password)
{
    s_mode = (mode == WIFI_APP_STA) ? WIFI_APP_STA : WIFI_APP_AP;
    if (s_mode == WIFI_APP_STA) {
        if (ssid != NULL) {
            copy_str(s_sta_ssid, sizeof(s_sta_ssid), ssid);
        }
        if (password != NULL) {
            copy_str(s_sta_pass, sizeof(s_sta_pass), password);
        }
    } else {
        if (ssid != NULL && ssid[0] != '\0') {
            copy_str(s_ap_ssid, sizeof(s_ap_ssid), ssid);
        }
        if (password != NULL) {
            copy_str(s_ap_pass, sizeof(s_ap_pass), password);
        }
    }
    save_network();
}

void wifi_manager_apply(int mode, const char *ssid, const char *password)
{
    wifi_manager_configure(mode, ssid, password);
    wifi_manager_stop();
    wifi_manager_start();
}

void wifi_manager_set_mode(int mode)
{
    s_mode = (mode == WIFI_APP_STA) ? WIFI_APP_STA : WIFI_APP_AP;
    save_network();
}

int wifi_manager_mode(void)
{
    return s_mode;
}

const char *wifi_manager_ssid(void)
{
    return s_sta_ssid;
}

const char *wifi_manager_password(void)
{
    return s_sta_pass;
}

bool wifi_manager_connected(void)
{
    return (s_mode == WIFI_APP_STA) ? s_sta_connected : s_ap_active;
}

const char *wifi_manager_ip(void)
{
    return s_ip;
}

int wifi_manager_rssi(void)
{    if (s_mode != WIFI_APP_STA || !s_sta_connected || !s_wifi_inited) {
        return 0;
    }
    wifi_ap_record_t ap;
    memset(&ap, 0, sizeof(ap));
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return ap.rssi;
    }
    return 0;
}

const char *wifi_manager_ap_ssid(void)
{
    return s_ap_ssid[0] ? s_ap_ssid : WIFI_AP_SSID;
}

const char *wifi_manager_ap_pass(void)
{
    return s_ap_pass;
}

int wifi_manager_reconnect_count(void)
{
    return s_reconnect_count;
}

/* ------------------------------------------------------------------ */
/* Scan                                                               */
/* ------------------------------------------------------------------ */
static void wifi_manager_scan_begin(void)
{
    if (!s_started || s_scan_running) {
        return;
    }

    /* In AP-only mode temporarily enable the station interface (APSTA) so the
     * scan can run without dropping the web-configurator clients. */
    if (s_mode != WIFI_APP_STA) {
        if (s_sta_netif == NULL) {
            s_sta_netif = esp_netif_create_default_wifi_sta();
        }
        if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK) {
            ESP_LOGW(TAG, "Scan not available in access-point mode");
            return;
        }
        s_scan_from_ap = true;
    }

    s_scan_running = true;
    s_scan_count = 0;
    if (esp_wifi_scan_start(NULL, false) != ESP_OK) {
        s_scan_running = false;
        if (s_scan_from_ap) {
            s_scan_from_ap = false;
            esp_wifi_set_mode(WIFI_MODE_AP);
        }
    }
}

bool wifi_manager_scan_running(void)
{
    return s_scan_running;
}

int wifi_manager_scan_count(void)
{
    return s_scan_count;
}

const char *wifi_manager_scan_ssid(int index)
{
    if (index < 0 || index >= s_scan_count) {
        return "";
    }
    return s_scan_ssid[index];
}

int wifi_manager_scan_rssi(int index)
{
    if (index < 0 || index >= s_scan_count) {
        return 0;
    }
    return s_scan_rssi[index];
}

/* ------------------------------------------------------------------ */
/* Asynchronous control                                               */
/*                                                                    */
/* Starting/stopping the WiFi driver, creating netifs and writing the */
/* config to flash all stall the calling task (flash writes disable   */
/* the cache; esp_wifi_init/start can take hundreds of ms). Running   */
/* them directly from an LVGL event handler froze the UI, so every    */
/* heavy operation is posted to a helper task instead.                */
/* ------------------------------------------------------------------ */
typedef enum {
    WIFI_REQ_ENABLE,
    WIFI_REQ_DISABLE,
    WIFI_REQ_APPLY,
    WIFI_REQ_SET_MODE,
    WIFI_REQ_SCAN,
    WIFI_REQ_BLE_ENABLE,
    WIFI_REQ_BLE_DISABLE,
} wifi_req_kind_t;

typedef struct {
    wifi_req_kind_t kind;
    int mode;
    char ssid[64];
    char pass[64];
} wifi_req_t;

static QueueHandle_t s_req_q = NULL;

/* If STA mode cannot reach a router within 30 s, fall back to the access point
 * so the device is never unreachable ("bricked") without a cable. */
static void wifi_fallback_check(void)
{
    if (s_mode != WIFI_APP_STA || !s_started || s_sta_connected || s_sta_start_us == 0) {
        return;
    }
    int64_t elapsed_s = (esp_timer_get_time() - s_sta_start_us) / 1000000;
    if (elapsed_s < 30) {
        return;
    }

    ESP_LOGW(TAG, "STA not connected after %lld s - falling back to AP", (long long)elapsed_s);
    s_sta_start_us = 0;

    /* Remember AP mode so a later reboot stays in the access point. */
    AppSettings *st = get_settings_mut();
    st->radio_mode = RADIO_MODE_AP;
    st->wifi_enabled = true;
    st->ble_enabled = false;
    save_settings(st);

    char msg[96];
    snprintf(msg, sizeof(msg), "%s %s", tr("wifi_fallback_ap"),
             s_ap_ssid[0] ? s_ap_ssid : WIFI_AP_SSID);
    toast_show(msg);

    wifi_manager_apply(WIFI_APP_AP, s_ap_ssid, s_ap_pass);
}

static void wifi_req_task(void *arg)
{
    (void)arg;
    wifi_req_t req;
    for (;;) {
        if (xQueueReceive(s_req_q, &req, pdMS_TO_TICKS(1000)) != pdTRUE) {
            wifi_fallback_check();
            continue;
        }
        switch (req.kind) {
        case WIFI_REQ_ENABLE:
            /* WiFi and BLE cannot coexist on this board (internal RAM): tear
             * down BLE first so esp_wifi_init() has memory to start with. */
            keyboard_set_ble_enabled(false);
            wifi_manager_init();
            wifi_manager_start();
            break;
        case WIFI_REQ_DISABLE:
            wifi_manager_stop();
            break;
        case WIFI_REQ_APPLY:
            keyboard_set_ble_enabled(false);
            wifi_manager_apply(req.mode, req.ssid, req.pass);
            break;
        case WIFI_REQ_SET_MODE:
            wifi_manager_set_mode(req.mode);
            break;
        case WIFI_REQ_SCAN:
            wifi_manager_scan_begin();
            break;
        case WIFI_REQ_BLE_ENABLE:
            /* Stop WiFi before re-initialising the BLE stack. */
            wifi_manager_stop();
            keyboard_set_ble_enabled(true);
            break;
        case WIFI_REQ_BLE_DISABLE:
            keyboard_set_ble_enabled(false);
            break;
        }
    }
}

static void wifi_req_ensure(void)
{
    if (s_req_q != NULL) {
        return;
    }
    TaskHandle_t h = NULL;
    s_req_q = xQueueCreate(6, sizeof(wifi_req_t));
    if (s_req_q == NULL) {
        return;
    }
    if (xTaskCreate(wifi_req_task, "wifi_req", 4096, NULL, 4, &h) != pdPASS) {
        vQueueDelete(s_req_q);
        s_req_q = NULL;
        return;
    }
    system_info_register_task("wifi_req", h, 4096);
}

static void wifi_req_post(const wifi_req_t *req)
{
    wifi_req_ensure();
    if (s_req_q == NULL) {
        return;
    }
    if (xQueueSend(s_req_q, req, 0) != pdTRUE) {
        ESP_LOGW(TAG, "WiFi command queue full, dropped");
    }
}

void wifi_manager_request_enable(bool enabled)
{
    wifi_req_t req;
    memset(&req, 0, sizeof(req));
    req.kind = enabled ? WIFI_REQ_ENABLE : WIFI_REQ_DISABLE;
    wifi_req_post(&req);
}

void wifi_manager_request_apply(int mode, const char *ssid, const char *password)
{
    wifi_req_t req;
    memset(&req, 0, sizeof(req));
    req.kind = WIFI_REQ_APPLY;
    req.mode = mode;
    if (ssid != NULL) {
        copy_str(req.ssid, sizeof(req.ssid), ssid);
    }
    if (password != NULL) {
        copy_str(req.pass, sizeof(req.pass), password);
    }
    wifi_req_post(&req);
}

void wifi_manager_request_set_mode(int mode)
{
    wifi_req_t req;
    memset(&req, 0, sizeof(req));
    req.kind = WIFI_REQ_SET_MODE;
    req.mode = mode;
    wifi_req_post(&req);
}

void wifi_manager_request_scan(void)
{
    wifi_req_t req;
    memset(&req, 0, sizeof(req));
    req.kind = WIFI_REQ_SCAN;
    wifi_req_post(&req);
}

void wifi_manager_request_ble_enable(void)
{
    wifi_req_t req;
    memset(&req, 0, sizeof(req));
    req.kind = WIFI_REQ_BLE_ENABLE;
    wifi_req_post(&req);
}

void wifi_manager_request_ble_disable(void)
{
    wifi_req_t req;
    memset(&req, 0, sizeof(req));
    req.kind = WIFI_REQ_BLE_DISABLE;
    wifi_req_post(&req);
}

