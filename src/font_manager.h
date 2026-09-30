/*
 * font_manager.h - Loads Roboto (Latin + Cyrillic) fonts from files and falls
 * back to the built-in Montserrat fonts when a .bin is missing.
 */
#ifndef MODIPAD_FONT_MANAGER_H
#define MODIPAD_FONT_MANAGER_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Load the .bin fonts (call once after LVGL is initialised). */
void init_fonts(void);

/* Roboto font of the given pixel size (falls back to Montserrat). */
const lv_font_t *get_font(int size);

/* Bold variant (10/12/14/16/18); falls back to get_font() when missing. */
const lv_font_t *get_font_bold(int size);

#ifdef __cplusplus
}
#endif

#endif /* MODIPAD_FONT_MANAGER_H */
