/*
 * display_init.cpp - Display bring-up for the JC3248W535 panel.
 */
#include "display_init.h"

#include "config.h"
#include "display.h"
#include "esp_bsp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "display_init";

static uint8_t s_brightness = BRIGHTNESS_DEFAULT;

static lv_disp_rot_t rotation_from_degrees(int deg)
{
    switch (deg) {
    case 90:  return LV_DISP_ROT_90;
    case 180: return LV_DISP_ROT_180;
    case 270: return LV_DISP_ROT_270;
    default:  return LV_DISP_ROT_NONE;
    }
}

esp_err_t init_display(void)
{
    ESP_LOGI(TAG, "Starting LVGL port + AXS15231B QSPI panel ...");

    bsp_display_cfg_t cfg = {};
    cfg.lvgl_port_cfg.task_priority     = 4;
    cfg.lvgl_port_cfg.task_stack        = 8192;
    /* Pin the graphics task to core 1 (APP_CPU). The WiFi/BT driver, LWIP and
     * the system event loop all run on core 0; sharing a core made the heavy
     * full-refresh rendering starve the network/ISRs and triggered the
     * interrupt watchdog. */
    cfg.lvgl_port_cfg.task_affinity     = 1;
    cfg.lvgl_port_cfg.task_max_sleep_ms = 500;
    cfg.lvgl_port_cfg.timer_period_ms   = 5;
    /* The AXS15231B panel/driver here only works in FULL-refresh mode:
     * partial-area flushes corrupt the image ("crumbling"/coloured noise) even
     * with CASET+RASET, so the draw buffer must cover the whole 480x320 frame. */
    cfg.buffer_size = LCD_NATIVE_WIDTH * LCD_NATIVE_HEIGHT;
    cfg.rotate      = rotation_from_degrees(LCD_ROTATION_DEG);

    lv_disp_t *disp = bsp_display_start_with_config(&cfg);
    if (disp == NULL) {
        ESP_LOGE(TAG, "bsp_display_start_with_config() failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Display ready (%dx%d, rotation %d deg)",
             LCD_WIDTH, LCD_HEIGHT, LCD_ROTATION_DEG);
    return ESP_OK;
}

void set_brightness(uint8_t brightness)
{
    if (brightness > BRIGHTNESS_MAX) {
        brightness = BRIGHTNESS_MAX;
    }
    s_brightness = brightness;
    bsp_display_brightness_set((int)brightness);
}

uint8_t get_brightness(void)
{
    return s_brightness;
}

void display_backlight_off(void)
{
    bsp_display_backlight_off();
}

void display_backlight_fade(uint8_t target, uint16_t ms)
{
    if (target > BRIGHTNESS_MAX) {
        target = BRIGHTNESS_MAX;
    }
    uint8_t from = s_brightness;
    if (ms == 0 || from == target) {
        set_brightness(target);
        return;
    }
    const int steps = 12;
    for (int i = 1; i <= steps; i++) {
        int v = (int)from + ((int)target - (int)from) * i / steps;
        bsp_display_brightness_set(v);
        vTaskDelay(pdMS_TO_TICKS(ms / steps));
    }
    s_brightness = target;
}
