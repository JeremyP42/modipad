
/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ESP BSP: ESP-BOX-3
 */

#pragma once

#ifdef HOST_BUILD
/* Host / PC simulator: the shared UI code only needs the LVGL mutex helpers;
 * the full board-support implementation is stubbed in
 * simulator/src/host_stubs.cpp. Keeping this branch free of IDF-only includes
 * lets the simulator compile this header in place (no source copies). */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock(void);
bool bsp_display_is_locked(void);

#ifdef __cplusplus
}
#endif

#else /* !HOST_BUILD */

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "lvgl.h"
#include "lv_port.h"

/**************************************************************************************************
 *  pinout
 **************************************************************************************************/
#define BSP_I2C_NUM                     (I2C_NUM_0)
#define BSP_I2C_CLK_SPEED_HZ            400000

#define MODIPAD_LCD_QSPI_HOST           (SPI2_HOST)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////// LCD spec of QSPI /////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define MODIPAD_PIN_NUM_QSPI_CS         (GPIO_NUM_45)
#define MODIPAD_PIN_NUM_QSPI_PCLK       (GPIO_NUM_47)
#define MODIPAD_PIN_NUM_QSPI_DATA0      (GPIO_NUM_21)
#define MODIPAD_PIN_NUM_QSPI_DATA1      (GPIO_NUM_48)
#define MODIPAD_PIN_NUM_QSPI_DATA2      (GPIO_NUM_40)
#define MODIPAD_PIN_NUM_QSPI_DATA3      (GPIO_NUM_39)
#define MODIPAD_PIN_NUM_QSPI_RST        (GPIO_NUM_NC)
#define MODIPAD_PIN_NUM_QSPI_DC         (GPIO_NUM_8)
#define MODIPAD_PIN_NUM_QSPI_TE         (GPIO_NUM_38)
#define MODIPAD_PIN_NUM_QSPI_BL         (GPIO_NUM_1)

#define MODIPAD_PIN_NUM_QSPI_TOUCH_SCL  (GPIO_NUM_8)
#define MODIPAD_PIN_NUM_QSPI_TOUCH_SDA  (GPIO_NUM_4)
#define MODIPAD_PIN_NUM_QSPI_TOUCH_RST  (-1)
#define MODIPAD_PIN_NUM_QSPI_TOUCH_INT  (-1)

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief BSP display configuration structure
 *
 */
typedef struct {
    lvgl_port_cfg_t lvgl_port_cfg;  /*!< Configuration for the LVGL port */
    uint32_t buffer_size;           /*!< Size of the buffer for the screen in pixels */
    lv_disp_rot_t rotate;           /*!< Rotation configuration for the display */
} bsp_display_cfg_t;

/**
 * @brief Init I2C driver
 *
 * @return
 *      - ESP_OK                On success
 *      - ESP_ERR_INVALID_ARG   I2C parameter error
 *      - ESP_FAIL              I2C driver installation error
 *
 */
esp_err_t bsp_i2c_init(void);

/**
 * @brief Deinit I2C driver and free its resources
 *
 * @return
 *      - ESP_OK                On success
 *      - ESP_ERR_INVALID_ARG   I2C parameter error
 *
 */
esp_err_t bsp_i2c_deinit(void);

/**
 * @brief Recover a wedged I2C bus (touch controller holding SDA low).
 *
 * Clocks the bus to release a stuck slave and resets the master. Safe to
 * call from any task; returns ESP_ERR_INVALID_STATE if I2C is not up.
 */
esp_err_t bsp_i2c_recover(void);

/**
 * @brief Initialize display
 *
 * This function initializes SPI, display controller and starts LVGL handling task.
 * LCD backlight must be enabled separately by calling bsp_display_brightness_set()
 *
 * @param cfg display configuration
 *
 * @return Pointer to LVGL display or NULL when error occurred
 */
lv_disp_t *bsp_display_start_with_config(const bsp_display_cfg_t *cfg);

/**
 * @brief Get pointer to input device (touch, buttons, ...)
 *
 * @note The LVGL input device is initialized in bsp_display_start() function.
 *
 * @return Pointer to LVGL input device or NULL when not initialized
 */
lv_indev_t *bsp_display_get_input_dev(void);

/**
 * @brief Take LVGL mutex
 *
 * @param timeout_ms Timeout in [ms]. 0 will block indefinitely.
 * @return true  Mutex was taken
 * @return false Mutex was NOT taken
 */
bool bsp_display_lock(uint32_t timeout_ms);

/**
 * @brief Give LVGL mutex
 *
 */
void bsp_display_unlock(void);

/**
 * @brief Is the LVGL mutex currently held by a task?
 *
 * Useful for diagnostics (e.g. the heartbeat log) to tell whether the LVGL
 * task is stuck while owning the lock.
 */
bool bsp_display_is_locked(void);

#ifdef __cplusplus
}
#endif

#endif /* HOST_BUILD */
