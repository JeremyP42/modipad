/*
 * config.h - Central configuration for ModiPAD_custom
 *
 * Board: JC3248W535EN (ESP32-S3, AXS15231B QSPI display + capacitive touch)
 *
 * This header is included from both C and C++ translation units, so it must
 * stay C-compatible.
 */
#ifndef MODIPAD_CONFIG_H
#define MODIPAD_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* === FIRMWARE VERSION ============================================= */
/* Single source of truth. Used by the About page and by the SD config backups
 * (backup_<version>_<n>.json), and mirrored in the web UI: keep it in sync with
 * `APP_VERSION` in datadevice/web/app.js and the `?v=` cache busters. */
#define MODIPAD_FIRMWARE_VERSION   "5.3.6"

/* ------------------------------------------------------------------ */
/* === DISPLAY (AXS15231B over QSPI) ================================ */
/* Native panel resolution (portrait). The UI is rotated 90 degrees   */
/* so it appears as 480 x 320 landscape.                              */
#define LCD_NATIVE_WIDTH     320
#define LCD_NATIVE_HEIGHT    480
#define LCD_WIDTH            480
#define LCD_HEIGHT           320
/* Rotation in degrees: 0, 90, 180 or 270 (mapped to lv_disp_rot_t in display_init.cpp).
 * Note: the AXS15231B MADCTL (0x36) on this panel supports mirroring (180 deg)
 * but NOT the axis swap needed for 90/270, so landscape is produced by the
 * LVGL flush callback (software rotation). There is no hardware 90-deg option. */
#define LCD_ROTATION_DEG     90

/* QSPI pins of the JC3248W535 / AXS15231B panel */
#define PIN_LCD_QSPI_CS      45
#define PIN_LCD_QSPI_PCLK    47
#define PIN_LCD_QSPI_D0      21
#define PIN_LCD_QSPI_D1      48
#define PIN_LCD_QSPI_D2      40
#define PIN_LCD_QSPI_D3      39
#define PIN_LCD_QSPI_DC      8
#define PIN_LCD_QSPI_TE      38
#define PIN_LCD_QSPI_BL      1
#define PIN_LCD_QSPI_RST     (-1)

/* ------------------------------------------------------------------ */
/* === TOUCH (AXS15231B, I2C) ======================================= */
#define PIN_TOUCH_SDA        4
#define PIN_TOUCH_SCL        8
#define PIN_TOUCH_INT        (-1)
#define PIN_TOUCH_RST        (-1)
#define TOUCH_I2C_PORT       0
#define TOUCH_I2C_ADDR       0x3B
#define TOUCH_I2C_SPEED_HZ   400000

/* ------------------------------------------------------------------ */
/* === WIFI ========================================================= */
#define WIFI_AP_SSID         "ModiPAD_Setup"
#define WIFI_AP_PASS         "12345678"
#define WIFI_AP_CHANNEL      1
#define WIFI_AP_MAX_STA      4

/* ------------------------------------------------------------------ */
/* === BLUETOOTH ==================================================== */
#define BLE_DEVICE_NAME      "ModiPAD"
#define BLE_DEVICE_MANUFACTURER "ModiPAD"

/* ------------------------------------------------------------------ */
/* === FILES (LittleFS) ============================================= */
#define LFS_PARTITION_LABEL  "storage"
#define LFS_MOUNT_POINT      "/littlefs"
#define CONFIG_FILE          "/littlefs/config.json"
#define SETTINGS_FILE        "/littlefs/settings.json"
#define WEB_DIR              "/littlefs/web"

/* ------------------------------------------------------------------ */
/* === SYSTEM ======================================================= */
#define SLEEP_TIMEOUT        300000
#define BRIGHTNESS_DEFAULT   80
#define BRIGHTNESS_MIN       10
#define BRIGHTNESS_MAX       100

/* ------------------------------------------------------------------ */
/* === DIAGNOSTIC: "ghost hunt" stages ============================== */
/* Progressive isolation. Set MODIPAD_DIAG_STAGE and rebuild:
 *   0 = normal firmware (PRODUCTION). Tested stable.
 *   1 = UI only (display+touch+LVGL+heartbeat)
 *   2 = UI + macro player + OBS worker (radios OFF)
 *   3 = UI + keyboard/radio only (macro/OBS OFF)
 *   4 = UI + macro + OBS + keyboard/radio (== stage 0)
 * Auto-sleep is disabled for every stage except 0.
 *
 * Root cause found with these stages: the SPI2 (QSPI panel) interrupt was
 * serviced on core 0 together with Bluedroid, so a QSPI color transfer could
 * stall and the LVGL task would block forever. The panel is now initialised
 * from a core-1 task so the SPI2 ISR runs on core 1 (see main.cpp). */
#define MODIPAD_DIAG_STAGE     0

/* Single active radio mode. Only one runs at a time (they share the antenna and
 * scarce internal RAM), chosen from the settings "Mode" page. */
typedef enum {
    RADIO_MODE_BLE = 0,  /* BLE HID keyboard only (no network) */
    RADIO_MODE_AP  = 1,  /* Wi-Fi access point for the web configurator */
    RADIO_MODE_STA = 2,  /* Wi-Fi client, used to reach OBS Studio */
} radio_mode_t;

#ifdef __cplusplus
}
#endif

#endif /* MODIPAD_CONFIG_H */
