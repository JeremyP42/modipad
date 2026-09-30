/*
 * display_init.h - Display (AXS15231B QSPI) initialisation and backlight control
 *
 * Thin, app-facing layer on top of the board support package (esp_bsp.c) and
 * the LVGL port (lv_port.c). The BSP already contains the AXS15231B driver and
 * registers the LVGL display; this module simply configures and starts it.
 */
#ifndef MODIPAD_DISPLAY_INIT_H
#define MODIPAD_DISPLAY_INIT_H

#include <lvgl.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the LVGL port, the QSPI panel and the touch input device.
 * Returns ESP_OK on success. */
esp_err_t init_display(void);

/* Set LCD backlight brightness in percent (0..100). */
void set_brightness(uint8_t brightness);

/* Last brightness value applied via set_brightness()/init_display(). */
uint8_t get_brightness(void);

void display_backlight_off(void);

/* Smoothly ramp the backlight to `target` percent over `ms` milliseconds. */
void display_backlight_fade(uint8_t target, uint16_t ms);

#ifdef __cplusplus
}
#endif

#endif /* MODIPAD_DISPLAY_INIT_H */
