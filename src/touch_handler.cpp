/*
 * touch_handler.cpp - Access to the BSP touch input device.
 */
#include "touch_handler.h"

#include "config.h"
#include "esp_bsp.h"
#include "esp_log.h"

static const char *TAG = "touch_handler";

void init_touch(void)
{
    lv_indev_t *indev = bsp_display_get_input_dev();
    if (indev == NULL) {
        ESP_LOGE(TAG, "Touch input device is not available");
        return;
    }
    ESP_LOGI(TAG, "Touch ready (AXS15231B @ I2C addr 0x%02X)", TOUCH_I2C_ADDR);
}
