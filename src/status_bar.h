/*
 * status_bar.h - Top status bar: page name + Bluetooth/WiFi indicator icons.
 */
#ifndef MODIPAD_STATUS_BAR_H
#define MODIPAD_STATUS_BAR_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

void create_status_bar(lv_obj_t *parent);

/* Keep the page name in sync with the active tab (button or swipe navigation). */
void status_bar_watch_tabview(lv_obj_t *tabview);

void update_status_bar(const char *page_name);
void update_status_bar_breadcrumbs(const char *level1, const char *level2);
void update_bluetooth_status(bool connected);
void update_wifi_status(bool connected);
void status_bar_set_hidden(bool hidden);

/* Status bar background transparency, 0..100 % (0 = opaque, 100 = clear). */
void status_bar_set_transparency(uint8_t transparency);

/* Show/hide the centred FPS / CPU readout (Settings > General). */
void status_bar_set_stats_visible(bool visible);

#ifdef __cplusplus
}
#endif

#endif /* MODIPAD_STATUS_BAR_H */
