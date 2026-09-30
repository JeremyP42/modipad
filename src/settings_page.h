/*
 * settings_page.h - On-device settings tab.
 */
#ifndef MODIPAD_SETTINGS_PAGE_H
#define MODIPAD_SETTINGS_PAGE_H

#include <lvgl.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Add a "Settings" tab to the given tabview (or screen). */
void create_settings_page(lv_obj_t *parent);

/* Switch the tabview to the settings tab. */
void go_to_settings(void);

/* Tab index of the settings page, or -1 if not created. */
int get_settings_tab_index(void);

/* True while the settings tab is the active tab (used to disable swipes). */
bool is_settings_page_active(void);

/* Refresh the connection status indicator. */
void settings_page_update_status(void);

/* Re-open the Language sub-page (used after a language change rebuild). */
void settings_page_show_language(void);

#ifdef HOST_BUILD
/* Host-only: open a settings sub-page by index (used by the PC simulator's
 * screenshot tooling). Not compiled into the device firmware. */
void settings_debug_show_sub(int idx);
#endif

#ifdef __cplusplus
}
#endif

#endif /* MODIPAD_SETTINGS_PAGE_H */
