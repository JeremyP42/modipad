/*
 * touch_handler.h - Capacitive touch input (AXS15231B, I2C)
 *
 * The touch controller is initialised together with the panel by the BSP and
 * registered with LVGL as a pointer input device. This module exposes that
 * device to the application layer.
 */
#ifndef MODIPAD_TOUCH_HANDLER_H
#define MODIPAD_TOUCH_HANDLER_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fetch the LVGL touch input device created by the BSP. */
void init_touch(void);

#ifdef __cplusplus
}
#endif

#endif /* MODIPAD_TOUCH_HANDLER_H */
