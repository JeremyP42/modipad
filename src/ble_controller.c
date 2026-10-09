/*
 * ble_controller.c - Bluetooth LE HID keyboard.
 *
 * Adapted from the hd2_macropad reference project (ESP-IDF, Bluedroid HID
 * device profile). The pairing/connection state is exposed through
 * ble_connected() and polled by keyboard_manager.
 */
#include <string.h>

#include "esp_bt.h"
#include "esp_hidd_prf_api.h"
#include "esp_bt_defs.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_log.h"

#include "ble_controller.h"
#include "config.h"
#include "ui_loader.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG_BLE = "ble_controller";

uint16_t hid_conn_id = 0;
static bool sec_conn = false;
/* Link = GATT/ACL connection is up; sec_conn = the link is also encrypted, so
 * HID reports can actually be sent. They are tracked separately because after a
 * host sleep/hibernate the link comes back with the existing bond and AUTH_CMPL
 * is not always re-emitted, which used to leave sec_conn stuck false. */
static bool s_link_connected = false;
/* Optional "state changed" hook (used by keyboard_manager to push the status
 * bar update immediately instead of waiting for the poll). Called from the
 * Bluedroid task; it must stay small. */
static void (*s_link_evt_cb)(bool ready) = NULL;

static void ble_notify_link(void)
{
    if (s_link_evt_cb != NULL) {
        s_link_evt_cb(s_link_connected && sec_conn);
    }
}
static bool s_initialized = false;   /* Bluedroid + HID profile brought up */
static bool s_enabled = true;        /* user wants BLE active (advertising) */
static bool s_advertising = false;
static bool s_have_bda = false;
static esp_bd_addr_t s_remote_bda = {0, 0, 0, 0, 0, 0};

void hidd_event_callback(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param);
void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);

static uint8_t hidd_service_uuid128[] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00,
};

static esp_ble_adv_data_t hidd_adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x0006,
    .max_interval = 0x0010,
    .appearance = 0x03c0, /* HID generic */
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(hidd_service_uuid128),
    .p_service_uuid = hidd_service_uuid128,
    .flag = 0x6,
};

static esp_ble_adv_params_t hidd_adv_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x30,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

/* Start/stop advertising, respecting the enabled flag. */
static void ble_start_advertising(void)
{
    if (!s_enabled || !s_initialized || s_advertising) {
        return;
    }
    esp_err_t err = esp_ble_gap_start_advertising(&hidd_adv_params);
    if (err == ESP_OK) {
        s_advertising = true;
    } else {
        ESP_LOGW(TAG_BLE, "start advertising failed: %s", esp_err_to_name(err));
    }
}

static void ble_stop_advertising(void)
{
    if (s_advertising) {
        esp_ble_gap_stop_advertising();
        s_advertising = false;
    }
}

void hidd_event_callback(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param)
{
    switch (event) {
    case ESP_HIDD_EVENT_REG_FINISH:
        if (param->init_finish.state == ESP_HIDD_INIT_OK) {
            esp_ble_gap_set_device_name(get_device_name());
            esp_ble_gap_config_adv_data(&hidd_adv_data);
        }
        break;
    case ESP_BAT_EVENT_REG:
        break;
    case ESP_HIDD_EVENT_DEINIT_FINISH:
        break;
    case ESP_HIDD_EVENT_BLE_CONNECT:
        ESP_LOGI(TAG_BLE, "Host connected");
        s_advertising = false; /* the stack stops advertising on connect */
        hid_conn_id = param->connect.conn_id;
        memcpy(s_remote_bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        s_have_bda = true;
        s_link_connected = true;
        /* (Re)establish encryption. On reconnect to a bonded host the central
         * usually re-encrypts, but after a host hibernate/resume AUTH_CMPL is
         * not always delivered, so ask explicitly to make the stack raise it
         * (otherwise sec_conn stays false: grey icon and dropped keystrokes). */
        if (!sec_conn) {
            esp_err_t enc = esp_ble_set_encryption(s_remote_bda, ESP_BLE_SEC_ENCRYPT);
            if (enc != ESP_OK) {
                ESP_LOGD(TAG_BLE, "set_encryption: %s", esp_err_to_name(enc));
            }
        }
        ble_notify_link();
        break;
    case ESP_HIDD_EVENT_BLE_DISCONNECT:
        sec_conn = false;
        s_link_connected = false;
        s_have_bda = false;
        ESP_LOGI(TAG_BLE, "Host disconnected");
        ble_start_advertising(); /* no-op while disabled */
        ble_notify_link();
        break;
    case ESP_HIDD_EVENT_BLE_VENDOR_REPORT_WRITE_EVT:
        ESP_LOGI(TAG_BLE, "Vendor report write event");
        ESP_LOG_BUFFER_HEX(TAG_BLE, param->vendor_write.data, param->vendor_write.length);
        break;
    case ESP_HIDD_EVENT_BLE_LED_REPORT_WRITE_EVT:
        ESP_LOGI(TAG_BLE, "LED report write event");
        ESP_LOG_BUFFER_HEX(TAG_BLE, param->led_write.data, param->led_write.length);
        break;
    default:
        break;
    }
}

void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        ble_start_advertising();
        break;
    case ESP_GAP_BLE_SEC_REQ_EVT:
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT: {
        bool ok = param->ble_security.auth_cmpl.success;
        sec_conn = ok;
        esp_bd_addr_t bd_addr;
        memcpy(bd_addr, param->ble_security.auth_cmpl.bd_addr, sizeof(esp_bd_addr_t));
        ESP_LOGI(TAG_BLE, "Paired with %02x:%02x:%02x:%02x:%02x:%02x (%s)",
                 bd_addr[0], bd_addr[1], bd_addr[2], bd_addr[3], bd_addr[4], bd_addr[5],
                 ok ? "ok" : "fail");
        if (!ok) {
            ESP_LOGE(TAG_BLE, "Pairing failed, reason 0x%x", param->ble_security.auth_cmpl.fail_reason);
            /* Drop the inconsistent bond so the host can pair cleanly on the
             * next attempt (fixes Windows "paired <-> connected" flapping after
             * a firmware update left stale keys on both sides). */
            esp_ble_remove_bond_device(bd_addr);
            sec_conn = false;
            ble_start_advertising();
        }
        ble_notify_link();
        break;
    }
    default:
        break;
    }
}

void ble_keyboard_send(uint8_t special_key_mask, uint8_t keyboard_cmd, uint8_t num_key)
{
    if (!sec_conn) {
        return;
    }
    esp_hidd_send_keyboard_value(hid_conn_id, (key_mask_t)special_key_mask, &keyboard_cmd, num_key);
}

void ble_consumer_send(uint8_t usage)
{
    if (!sec_conn) {
        return;
    }
    esp_hidd_send_consumer_value(hid_conn_id, usage, true);
    vTaskDelay(pdMS_TO_TICKS(15));
    esp_hidd_send_consumer_value(hid_conn_id, usage, false);
}

bool ble_connected(void)
{
    return sec_conn;
}

bool ble_link_connected(void)
{
    return s_link_connected;
}

void ble_set_link_callback(void (*cb)(bool ready))
{
    s_link_evt_cb = cb;
}

esp_err_t ble_controller_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t ret;

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG_BLE, "%s: controller init failed", __func__);
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG_BLE, "%s: controller enable failed", __func__);
        return ret;
    }

    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG_BLE, "%s: bluedroid init failed", __func__);
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG_BLE, "%s: bluedroid enable failed", __func__);
        return ret;
    }

    if ((ret = esp_hidd_profile_init()) != ESP_OK) {
        ESP_LOGE(TAG_BLE, "%s: HID profile init failed", __func__);
        return ret;
    }

    /* Mark ready before registering callbacks: the async REG_FINISH event that
     * triggers advertising checks s_initialized. */
    s_initialized = true;

    esp_ble_gap_register_callback(gap_event_handler);
    esp_hidd_register_callbacks(hidd_event_callback);

    /* Apply the configured Bluetooth name (config.json "device_name") as early
     * as possible so both the GAP name and the advertising data carry it; the
     * HIDD REG_FINISH callback sets it again just before configuring adv data. */
    esp_ble_gap_set_device_name(get_device_name());

    /* Security parameters: bond, no I/O capability. */
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_BOND;
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));

    ESP_LOGI(TAG_BLE, "BLE HID ready (advertising=%d) as \"%s\"", (int)s_enabled, get_device_name());
    return ESP_OK;
}

esp_err_t ble_controller_set_enabled(bool enabled)
{
    if (enabled == s_enabled && (s_initialized || !enabled)) {
        return ESP_OK;
    }
    s_enabled = enabled;

    if (!enabled) {
        ble_stop_advertising();
        if (sec_conn && s_have_bda) {
            esp_ble_gap_disconnect(s_remote_bda);
        }
        sec_conn = false;
        s_link_connected = false;
        s_have_bda = false;
        s_advertising = false;
        ble_notify_link();
        /* NOTE: the stack is intentionally NOT deinitialised here. Tearing
         * Bluedroid/the BT controller down at runtime corrupts memory (coex
         * keeps stale pointers) and crash-loops the device. Switching radios
         * is done by rebooting into the desired mode instead (see
         * settings_page). */
        ESP_LOGI(TAG_BLE, "BLE disabled (advertising stopped)");
        return ESP_OK;
    }

    if (!s_initialized) {
        ESP_LOGI(TAG_BLE, "BLE enabling (lazy init)");
        return ble_controller_init();
    }

    ESP_LOGI(TAG_BLE, "BLE enabled");
    ble_start_advertising();
    return ESP_OK;
}

bool ble_controller_enabled(void)
{
    return s_enabled;
}


