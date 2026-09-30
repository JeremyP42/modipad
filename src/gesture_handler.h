/*
 * gesture_handler.h - Touch gesture handling (page navigation + brightness).
 */
#ifndef MODIPAD_GESTURE_HANDLER_H
#define MODIPAD_GESTURE_HANDLER_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

void init_gestures(void);
void toggle_brightness(void);

/* A swipe can also deliver a CLICKED event to the button under the finger.
 * Returns true (once) when a click should be swallowed because a gesture was
 * just detected, so a swipe never also presses a button. */
bool gesture_swallow_click(void);

#ifdef __cplusplus
}
#endif

#endif /* MODIPAD_GESTURE_HANDLER_H */
