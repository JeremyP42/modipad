/*
 * toast.h - Small transient message overlay.
 *
 * Thread-safe: may be called from the LVGL task or from a background task
 * (macro player, OBS client). The label lives on the top layer so it survives
 * screen rebuilds, and is removed by a one-shot LVGL timer.
 */
#ifndef MODIPAD_TOAST_H
#define MODIPAD_TOAST_H

#ifdef __cplusplus
extern "C" {
#endif

/* Show `message` for a couple of seconds (replaces the previous toast). */
void toast_show(const char *message);

#ifdef __cplusplus
}
#endif

#endif /* MODIPAD_TOAST_H */
