/*
 * ui_renderer.h - Builds the LVGL UI from config.json.
 */
#ifndef MODIPAD_UI_RENDERER_H
#define MODIPAD_UI_RENDERER_H

#include <lvgl.h>
#include "button_style.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Create the tabview (main_page + pages) from the loaded config. */
void create_ui(void);

/* Show tab `index`, building its widgets on demand first. All navigation
 * (buttons, gestures, settings "home") must use this instead of calling
 * lv_tabview_set_act() directly, because set_act() does not emit
 * LV_EVENT_VALUE_CHANGED and the lazy builder would never run. */
void ui_show_page(int index);

void create_page(lv_obj_t *parent, cJSON *page_cfg);
void create_button(lv_obj_t *parent, cJSON *btn_cfg, const button_style_t *style,
                   int x, int y, int width, int height, int rows);

/* Root tabview (used to attach the settings page). NULL before create_ui(). */
lv_obj_t *get_ui_tabview(void);

/* Number of tabs created so far (main_page + pages + settings). */
int get_ui_tab_count(void);

/* Display name of tab `index` (main_page first, then pages[]). */
const char *ui_page_name(int index);

#ifdef __cplusplus
}
#endif

#endif /* MODIPAD_UI_RENDERER_H */
