/*
 * keyboard_manager.cpp - Keyboard layer (BLE HID).
 *
 * HID reports are dispatched to the Bluetooth LE HID transport.
 */
#include "keyboard_manager.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

#include "ble_controller.h"
#include "i18n.h"
#include "system_info.h"
#include "toast.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <stdlib.h>

static const char *TAG = "keyboard_manager";

/* HID modifier bits */
#define MOD_CTRL  0x01
#define MOD_SHIFT 0x02
#define MOD_ALT   0x04
#define MOD_GUI   0x08

static bool s_transports_ready = false;
static bool s_ble_enabled = true;

static void (*s_conn_cb)(bool) = NULL;
static bool s_last_ble = false;
static int64_t s_last_poll_us = 0;

/* ------------------------------------------------------------------ */
/* HID key usage lookup                                               */
/* ------------------------------------------------------------------ */
typedef struct {
    const char *name;
    uint8_t code;
} key_entry_t;

static const key_entry_t k_keys[] = {
    {"ENTER", 0x28}, {"RETURN", 0x28}, {"ESC", 0x29}, {"ESCAPE", 0x29},
    {"BACKSPACE", 0x2A}, {"TAB", 0x2B}, {"SPACE", 0x2C},
    {"MINUS", 0x2D}, {"EQUAL", 0x2E}, {"LBRACKET", 0x2F}, {"RBRACKET", 0x30},
    {"BACKSLASH", 0x31}, {"SEMICOLON", 0x33}, {"QUOTE", 0x34}, {"GRAVE", 0x35},
    {"COMMA", 0x36}, {"PERIOD", 0x37}, {"SLASH", 0x38}, {"CAPSLOCK", 0x39},
    {"F1", 0x3A}, {"F2", 0x3B}, {"F3", 0x3C}, {"F4", 0x3D}, {"F5", 0x3E},
    {"F6", 0x3F}, {"F7", 0x40}, {"F8", 0x41}, {"F9", 0x42}, {"F10", 0x43},
    {"F11", 0x44}, {"F12", 0x45},
    {"PRINTSCREEN", 0x46}, {"SCROLLLOCK", 0x47}, {"PAUSE", 0x48},
    {"INSERT", 0x49}, {"HOME", 0x4A}, {"PAGEUP", 0x4B}, {"DELETE", 0x4C},
    {"DEL", 0x4C}, {"END", 0x4D}, {"PAGEDOWN", 0x4E},
    {"RIGHT", 0x4F}, {"LEFT", 0x50}, {"DOWN", 0x51}, {"UP", 0x52},
};

static uint8_t keycode_from_name(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return 0;
    }

    if (name[1] == '\0') {
        char c = (char)toupper((unsigned char)name[0]);
        if (c >= 'A' && c <= 'Z') {
            return (uint8_t)(0x04 + (c - 'A'));
        }
        if (c == '0') {
            return 0x27;
        }
        if (c >= '1' && c <= '9') {
            return (uint8_t)(0x1E + (c - '1'));
        }
    }

    for (size_t i = 0; i < sizeof(k_keys) / sizeof(k_keys[0]); i++) {
        if (strcasecmp(name, k_keys[i].name) == 0) {
            return k_keys[i].code;
        }
    }
    return 0;
}

static bool modifier_from_name(const char *name, uint8_t *bit)
{
    if (strcasecmp(name, "CTRL") == 0 || strcasecmp(name, "CONTROL") == 0) {
        *bit = MOD_CTRL;
    } else if (strcasecmp(name, "SHIFT") == 0) {
        *bit = MOD_SHIFT;
    } else if (strcasecmp(name, "ALT") == 0 || strcasecmp(name, "OPTION") == 0) {
        *bit = MOD_ALT;
    } else if (strcasecmp(name, "GUI") == 0 || strcasecmp(name, "WIN") == 0 ||
               strcasecmp(name, "CMD") == 0 || strcasecmp(name, "META") == 0) {
        *bit = MOD_GUI;
    } else {
        return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Transport dispatch                                                 */
/* ------------------------------------------------------------------ */
static void send_report(uint8_t modifiers, uint8_t keycode, uint8_t pressed)
{
    ble_keyboard_send(modifiers, keycode, pressed);
}

/* ------------------------------------------------------------------ */
/* Asynchronous output queue                                          */
/*                                                                    */
/* Hotkeys/text are dispatched from a dedicated task instead of from  */
/* the LVGL event callback. A HID call that blocks (or the BLE stack  */
/* stalling) can then never freeze the UI / hold the LVGL mutex, and  */
/* a button press returns immediately.                                */
/* ------------------------------------------------------------------ */
static void do_hotkey(const char *keys);
static void do_text(const char *text);

typedef enum {
    KBJOB_HOTKEY = 0,
    KBJOB_TEXT,
} kb_job_type_t;

typedef struct {
    kb_job_type_t type;
    char *data;
} kb_job_t;

#define KB_QUEUE_LEN 16
static QueueHandle_t s_kb_queue = NULL;

static void kb_output_task(void *arg)
{
    (void)arg;
    kb_job_t job;
    while (1) {
        if (xQueueReceive(s_kb_queue, &job, portMAX_DELAY) == pdTRUE) {
            if (job.type == KBJOB_HOTKEY) {
                do_hotkey(job.data);
            } else if (job.type == KBJOB_TEXT) {
                do_text(job.data);
            }
            free(job.data);
        }
    }
}

static void kb_enqueue(kb_job_type_t type, const char *text)
{
    if (text == NULL) {
        return;
    }
    if (s_kb_queue == NULL) {
        /* Not initialised yet: fall back to a direct (blocking) send. */
        if (type == KBJOB_HOTKEY) {
            do_hotkey(text);
        } else {
            do_text(text);
        }
        return;
    }
    char *copy = strdup(text);
    if (copy == NULL) {
        return;
    }
    kb_job_t job = { type, copy };
    if (xQueueSend(s_kb_queue, &job, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Output queue full, dropped");
        free(copy);
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */
void init_keyboard(bool enable_ble)
{
    s_ble_enabled = enable_ble;

    if (s_transports_ready) {
        ESP_LOGI(TAG, "Keyboard already initialised, ble = %d", (int)enable_ble);
        return;
    }

    if (enable_ble) {
        ESP_LOGI(TAG, "Bringing up BLE HID ...");
        if (ble_controller_init() != ESP_OK) {
            ESP_LOGE(TAG, "BLE init failed");
        }
    } else {
        /* Remember the disabled state without allocating the BLE stack. */
        ESP_LOGI(TAG, "BLE HID disabled at boot (not initialised)");
        ble_controller_set_enabled(false);
    }

    s_transports_ready = true;

    if (s_kb_queue == NULL) {
        s_kb_queue = xQueueCreate(KB_QUEUE_LEN, sizeof(kb_job_t));
        if (s_kb_queue != NULL) {
            TaskHandle_t h = NULL;
            xTaskCreate(kb_output_task, "kb_out", 4096, NULL, 4, &h);
            system_info_register_task("kb_out", h, 4096);
        } else {
            ESP_LOGE(TAG, "Failed to create keyboard output queue");
        }
    }

    ESP_LOGI(TAG, "Keyboard ready, ble = %d", (int)s_ble_enabled);
}

void keyboard_set_ble_enabled(bool enabled)
{
    s_ble_enabled = enabled;
    esp_err_t err = ble_controller_set_enabled(enabled);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BLE enable(%d) failed: %s", (int)enabled, esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "BLE transport %s", enabled ? "enabled" : "disabled");
}

bool keyboard_ble_enabled(void)
{
    return s_ble_enabled;
}

bool is_keyboard_connected(void)
{
    /* "Ready": the link is up AND encrypted, i.e. HID reports can be sent.
     * Taking only the link state would show the status-bar icon white while the
     * link is up but encryption is not finished (or never completes). */
    return ble_link_connected() && ble_connected();
}

static void do_hotkey(const char *keys)
{
    if (keys == NULL) {
        return;
    }

    char buf[128];
    strncpy(buf, keys, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    uint8_t modifiers = 0;
    uint8_t keycode = 0;

    char *saveptr = NULL;
    for (char *tok = strtok_r(buf, "+", &saveptr); tok != NULL;
         tok = strtok_r(NULL, "+", &saveptr)) {
        while (*tok == ' ') {
            tok++;
        }
        uint8_t bit = 0;
        if (modifier_from_name(tok, &bit)) {
            modifiers |= bit;
        } else {
            keycode = keycode_from_name(tok);
        }
    }

    ESP_LOGI(TAG, "Hotkey \"%s\" (mods=0x%02X code=0x%02X)", keys, modifiers, keycode);

    send_report(modifiers, keycode, 1);
    vTaskDelay(pdMS_TO_TICKS(30));
    send_report(0, 0, 0);
}

static void do_text(const char *text)
{
    if (text == NULL) {
        return;
    }

    for (const char *p = text; *p != '\0'; p++) {
        char c = *p;
        uint8_t mod = 0;
        uint8_t code = 0;

        if (c >= 'a' && c <= 'z') {
            code = (uint8_t)(0x04 + (c - 'a'));
        } else if (c >= 'A' && c <= 'Z') {
            mod = MOD_SHIFT;
            code = (uint8_t)(0x04 + (c - 'A'));
        } else if (c >= '1' && c <= '9') {
            code = (uint8_t)(0x1E + (c - '1'));
        } else if (c == '0') {
            code = 0x27;
        } else if (c == ' ') {
            code = 0x2C;
        } else if (c == '\n' || c == '\r') {
            code = 0x28;
        } else if (c == '\t') {
            code = 0x2B;
        } else if (c == '-') {
            code = 0x2D;
        } else if (c == '=') {
            code = 0x2E;
        } else if (c == '.') {
            code = 0x37;
        } else if (c == ',') {
            code = 0x36;
        } else if (c == '/') {
            code = 0x38;
        } else if (c == ';') {
            code = 0x33;
        } else if (c == '\'') {
            code = 0x34;
        } else {
            continue;
        }

        send_report(mod, code, 1);
        vTaskDelay(pdMS_TO_TICKS(8));
        send_report(0, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(4));
    }
}

/* Public entry points: enqueue and return immediately (see kb_output_task). */
void send_hotkey(const char *keys)
{
    kb_enqueue(KBJOB_HOTKEY, keys);
}

void send_text(const char *text)
{
    kb_enqueue(KBJOB_TEXT, text);
}

/* ------------------------------------------------------------------ */
/* Consumer Control (multimedia)                                      */
/* ------------------------------------------------------------------ */
typedef struct {
    const char *name;
    uint8_t usage;
} media_entry_t;

static const media_entry_t k_media[] = {
    {"PLAY_PAUSE", 0xCD}, {"PLAYPAUSE", 0xCD}, {"PLAY/PAUSE", 0xCD},
    {"NEXT", 0xB5}, {"NEXT_TRACK", 0xB5}, {"SCAN_NEXT", 0xB5},
    {"PREV", 0xB6}, {"PREV_TRACK", 0xB6}, {"PREVIOUS", 0xB6}, {"SCAN_PREV", 0xB6},
    {"VOL_UP", 0xE9}, {"VOLUME_UP", 0xE9}, {"VOLUME_INCREMENT", 0xE9},
    {"VOL_DOWN", 0xEA}, {"VOLUME_DOWN", 0xEA}, {"VOLUME_DECREMENT", 0xEA},
    {"MUTE", 0xE2},
    {"PLAY", 0xB0}, {"PAUSE", 0xB1}, {"RECORD", 0xB2}, {"STOP", 0xB7},
};

static uint8_t multimedia_usage(const char *command)
{
    if (command == NULL || command[0] == '\0') {
        return 0;
    }

    /* Numeric usage id: "0xCD" / "205". */
    char *end = NULL;
    long numeric = strtol(command, &end, 0);
    if (end != command && *end == '\0' && numeric > 0 && numeric <= 0xFFFF) {
        return (uint8_t)(numeric & 0xFF);
    }

    for (size_t i = 0; i < sizeof(k_media) / sizeof(k_media[0]); i++) {
        if (strcasecmp(command, k_media[i].name) == 0) {
            return k_media[i].usage;
        }
    }
    return 0;
}

bool send_multimedia(const char *command)
{
    uint8_t usage = multimedia_usage(command);
    if (usage == 0) {
        ESP_LOGW(TAG, "Unknown multimedia command \"%s\"", command ? command : "");
        return false;
    }

    if (!ble_connected()) {
        ESP_LOGI(TAG, "Multimedia \"%s\" ignored: no BLE host", command);
        toast_show(tr("ble_not_connected"));
        return false;
    }

    ESP_LOGI(TAG, "Multimedia \"%s\" (usage=0x%02X)", command, usage);
    ble_consumer_send(usage);
    return true;
}

/* BLE event push: called from the Bluedroid task on connect/disconnect and
 * when encryption completes. It only invokes the UI callback (which takes a
 * bounded LVGL lock); the 250 ms poll below still reconciles if that update is
 * ever dropped, so nothing can get stuck. */
static void kb_ble_link_event(bool ready)
{
    (void)ready;
    if (s_conn_cb) {
        s_conn_cb(is_keyboard_connected());
    }
}

void keyboard_loop(void)
{
    int64_t now = esp_timer_get_time();
    if (now - s_last_poll_us < 250000) {
        return;
    }
    s_last_poll_us = now;

    bool ble = is_keyboard_connected();

    if (ble != s_last_ble) {
        s_last_ble = ble;
        if (s_conn_cb) {
            s_conn_cb(ble);
        }
    }
}

void keyboard_set_connection_cb(void (*cb)(bool ble_connected))
{
    s_conn_cb = cb;
    ble_set_link_callback(kb_ble_link_event);
    s_last_ble = is_keyboard_connected();
}
