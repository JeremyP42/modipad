#pragma once
/*
 * Minimal stand-in for the project's board-support header. The real
 * src/esp_bsp.h pulls in driver/i2c.h, lv_port.h and esp_lcd headers; the UI
 * modules only need the LVGL mutex helpers, so we expose just those.
 */
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock(void);

#ifdef __cplusplus
}
#endif
