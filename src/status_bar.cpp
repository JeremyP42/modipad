/*
 * status_bar.cpp - Top status bar: page name + BT/WiFi indicator icons.
 */
#include "status_bar.h"

#include <stdio.h>

#include "config.h"
#include "esp_log.h"
#include "font_manager.h"
#include "health.h"
#include "i18n.h"
#include "sd_card.h"
#include "settings_page.h"
#include "ui_assets.h"
#include "ui_loader.h"
#include "ui_renderer.h"

/* Provided by lv_port.c. Declared here (instead of including lv_port.h) so the
 * PC simulator build does not pull the IDF-only headers lv_port.h requires. */
#ifdef __cplusplus
extern "C" {
#endif
uint32_t lvgl_port_frame_count(void);
void lvgl_port_cpu_stats(uint64_t *busy_us, uint64_t *total_us);
#ifdef __cplusplus
}
#endif

static const char *TAG = "status_bar";

static lv_obj_t *s_bar = NULL;
static lv_obj_t *s_page_label = NULL;
static lv_obj_t *s_bt_icon = NULL;
static lv_obj_t *s_wifi_icon = NULL;
static lv_obj_t *s_sd_icon = NULL;
static lv_obj_t *s_warn_icon = NULL;
static lv_timer_t *s_sd_timer = NULL;
static uint8_t s_sd_state = 0xFF; /* 0 = absent, 1 = ok, 2 = low space */
static bool s_bt_state = false;
static bool s_wifi_state = false;

/* Optional FPS / CPU readout (single line, centred). Off by default; toggled
 * from Settings > General and persisted as settings.json "show_stats".
 * Two labels with a 5 px gap so the pair reads clearly. */
static lv_obj_t *s_stat_box = NULL;
static lv_obj_t *s_fps_label = NULL;
static lv_obj_t *s_cpu_label = NULL;
static lv_timer_t *s_stats_timer = NULL;
static bool s_stats_visible = false;
static uint32_t s_stats_last_frames = 0;
static uint32_t s_stats_last_ms = 0;
static uint64_t s_stats_last_busy = 0;
static uint64_t s_stats_last_total = 0;

static void status_stats_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_stats_visible || s_fps_label == NULL) {
        return; /* disabled: don't touch the labels (avoids a full redraw) */
    }
    uint32_t now = lv_tick_get();
    uint32_t dt = now - s_stats_last_ms;
    if (dt == 0) {
        return;
    }

    /* FPS = frames rendered since the previous tick, normalised to 1 s. */
    uint32_t frames = lvgl_port_frame_count();
    uint32_t fps = (uint32_t)((uint64_t)(frames - s_stats_last_frames) * 1000u / dt);

    /* CPU = share of the LVGL task loop spent inside lv_timer_handler(). */
    uint64_t busy = 0, total = 0;
    lvgl_port_cpu_stats(&busy, &total);
    uint64_t db = busy - s_stats_last_busy;
    uint64_t dtot = total - s_stats_last_total;
    uint32_t cpu = (dtot > 0) ? (uint32_t)(db * 100u / dtot) : 0;
    if (cpu > 100) {
        cpu = 100;
    }

    s_stats_last_frames = frames;
    s_stats_last_ms = now;
    s_stats_last_busy = busy;
    s_stats_last_total = total;

    lv_label_set_text_fmt(s_fps_label, "FPS %u", (unsigned)fps);
    lv_label_set_text_fmt(s_cpu_label, "CPU %u%%", (unsigned)cpu);
}

/* Icon names (in datadevice/images/icons/system). */
#define ICON_BT_ON   "icon_bt.png"
#define ICON_BT_OFF  "icon_bt_off.png"
#define ICON_WIFI_ON "icon_wifi.png"
#define ICON_WIFI_OFF "icon_wifi_off.png"

static void set_icon(lv_obj_t *img, bool on, const char *on_name, const char *off_name)
{
    if (img == NULL) {
        return;
    }
    const char *path = asset_persist_path(on ? on_name : off_name, 32, 32);
    if (path != NULL) {
        lv_img_set_src(img, path);
        lv_img_set_zoom(img, 160); /* ~20px */
    }
}

/* SD card status icon: grey when absent, white when mounted, orange when the
 * card is nearly full (< 10% free). Checked on a slow timer and redrawn only
 * when the state changes (the panel is full_refresh). */
static void set_sd_icon(uint8_t state)
{
    if (s_sd_icon == NULL) {
        return;
    }
    const char *name = (state == 1) ? "icon_sd.png"
                       : (state == 2) ? "icon_sd_low.png"
                                      : "icon_sd_off.png";
    const char *path = asset_persist_path(name, 32, 32);
    if (path != NULL) {
        lv_img_set_src(s_sd_icon, path);
        lv_img_set_zoom(s_sd_icon, 160);
    }
}

static void status_sd_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    bool present = false;
    uint32_t total_kb = 0, free_kb = 0;
    sd_card_info(&present, &total_kb, &free_kb);
    uint8_t st;
    if (!present) {
        st = 0;
    } else if (total_kb > 0 && (uint64_t)free_kb * 100u / total_kb < 10u) {
        st = 2;
    } else {
        st = 1;
    }
    if (st != s_sd_state) {
        s_sd_state = st;
        set_sd_icon(st);
    }
}

void create_status_bar(lv_obj_t *parent)
{
    if (parent == NULL) {
        parent = lv_scr_act();
    }

    s_bar = lv_obj_create(parent);
    lv_obj_set_size(s_bar, LCD_WIDTH, 30);
    lv_obj_align(s_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x111111), 0);
    /* Transparency is configurable via settings.json ("status_bar_transparency");
     * applied below, after the bar is built. At 0% it stays opaque (a plain
     * fill); above 0% LVGL blends the 480x30 strip on every full_refresh frame. */
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_bar, 0, 0);
    lv_obj_set_style_radius(s_bar, 0, 0);
    lv_obj_set_style_pad_all(s_bar, 5, 0);
    lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_SCROLLABLE);
    /* Let swipes that start on the status bar bubble to the screen. */
    lv_obj_add_flag(s_bar, LV_OBJ_FLAG_GESTURE_BUBBLE);

    s_page_label = lv_label_create(s_bar);
    lv_label_set_text(s_page_label, "Main");
    lv_obj_set_style_text_color(s_page_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_page_label, get_font(14), 0);
    lv_obj_align(s_page_label, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_add_flag(s_page_label, LV_OBJ_FLAG_GESTURE_BUBBLE);

    s_bt_icon = lv_img_create(s_bar);
    lv_obj_align(s_bt_icon, LV_ALIGN_RIGHT_MID, -72, 0);
    lv_obj_add_flag(s_bt_icon, LV_OBJ_FLAG_GESTURE_BUBBLE);
    set_icon(s_bt_icon, s_bt_state, ICON_BT_ON, ICON_BT_OFF);

    s_wifi_icon = lv_img_create(s_bar);
    lv_obj_align(s_wifi_icon, LV_ALIGN_RIGHT_MID, -40, 0);
    lv_obj_add_flag(s_wifi_icon, LV_OBJ_FLAG_GESTURE_BUBBLE);
    set_icon(s_wifi_icon, s_wifi_state, ICON_WIFI_ON, ICON_WIFI_OFF);

    s_sd_icon = lv_img_create(s_bar);
    lv_obj_align(s_sd_icon, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_add_flag(s_sd_icon, LV_OBJ_FLAG_GESTURE_BUBBLE);

    /* Red warning icon: shown only when the startup diagnostics found problems
     * (see Settings > Problems). */
    s_warn_icon = lv_img_create(s_bar);
    lv_obj_align(s_warn_icon, LV_ALIGN_RIGHT_MID, -104, 0);
    lv_obj_add_flag(s_warn_icon, LV_OBJ_FLAG_GESTURE_BUBBLE);
    {
        const char *wpath = asset_persist_path("icon_warning.png", 32, 32);
        if (wpath != NULL) {
            lv_img_set_src(s_warn_icon, wpath);
            lv_img_set_zoom(s_warn_icon, 160);
        }
    }
    if (health_problem_count() == 0) {
        lv_obj_add_flag(s_warn_icon, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_sd_timer != NULL) {
        lv_timer_del(s_sd_timer);
        s_sd_timer = NULL;
    }
    s_sd_state = 0xFF; /* force the first refresh to set the icon */
    status_sd_timer_cb(NULL);
    s_sd_timer = lv_timer_create(status_sd_timer_cb, 30000, NULL);

    /* FPS / CPU readout, centred in the bar on a single line (optional). */
    if (s_stats_timer != NULL) {
        lv_timer_del(s_stats_timer);
        s_stats_timer = NULL;
    }
    s_stats_visible = (get_settings() != NULL) ? get_settings()->show_stats : false;
    s_stats_last_frames = lvgl_port_frame_count();
    s_stats_last_ms = lv_tick_get();
    lvgl_port_cpu_stats(&s_stats_last_busy, &s_stats_last_total);

    s_stat_box = lv_obj_create(s_bar);
    lv_obj_set_size(s_stat_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_stat_box, LV_OPA_0, 0);
    lv_obj_set_style_border_width(s_stat_box, 0, 0);
    lv_obj_set_style_pad_all(s_stat_box, 0, 0);
    lv_obj_set_style_pad_column(s_stat_box, 5, 0); /* 5 px between FPS and CPU */
    lv_obj_clear_flag(s_stat_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_stat_box, LV_FLEX_FLOW_ROW);
    lv_obj_align(s_stat_box, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_stat_box, LV_OBJ_FLAG_GESTURE_BUBBLE);

    s_fps_label = lv_label_create(s_stat_box);
    lv_label_set_text(s_fps_label, "FPS 0");
    lv_obj_set_style_text_color(s_fps_label, lv_color_hex(0x9fd0ff), 0);
    lv_obj_set_style_text_font(s_fps_label, get_font(12), 0);
    lv_obj_add_flag(s_fps_label, LV_OBJ_FLAG_GESTURE_BUBBLE);

    s_cpu_label = lv_label_create(s_stat_box);
    lv_label_set_text(s_cpu_label, "CPU 0%");
    lv_obj_set_style_text_color(s_cpu_label, lv_color_hex(0x9fd0ff), 0);
    lv_obj_set_style_text_font(s_cpu_label, get_font(12), 0);
    lv_obj_add_flag(s_cpu_label, LV_OBJ_FLAG_GESTURE_BUBBLE);

    if (!s_stats_visible) {
        lv_obj_add_flag(s_stat_box, LV_OBJ_FLAG_HIDDEN);
    }
    s_stats_timer = lv_timer_create(status_stats_timer_cb, 1000, NULL);

    /* Apply visibility + opacity from settings. */
    status_bar_set_transparency(get_settings() ? get_settings()->status_bar_transparency : 0);
    status_bar_set_hidden(get_settings() ? !get_settings()->status_bar_visible : false);

    lv_obj_move_foreground(s_bar);

    /* Track tab changes so the page name follows button *and* swipe navigation. */
    status_bar_watch_tabview(get_ui_tabview());

    ESP_LOGI(TAG, "Status bar created");
}

static void status_tab_changed_cb(lv_event_t *e)
{
    lv_obj_t *tv = lv_event_get_target(e);
    uint16_t act = lv_tabview_get_tab_act(tv);
    ESP_LOGI(TAG, "tab changed -> %d", (int)act);
    if ((int)act == get_settings_tab_index()) {
        update_status_bar("Settings");
    } else {
        update_status_bar(ui_page_name((int)act));
    }
    status_bar_set_hidden(get_settings() ? !get_settings()->status_bar_visible : false);
}

void status_bar_watch_tabview(lv_obj_t *tabview)
{
    if (tabview != NULL) {
        lv_obj_add_event_cb(tabview, status_tab_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    }
}

void update_status_bar(const char *page_name)
{
    if (s_page_label != NULL && page_name != NULL) {
        lv_label_set_text(s_page_label, tr_page(page_name));
    }
}

void update_status_bar_breadcrumbs(const char *level1, const char *level2)
{
    if (s_page_label == NULL) {
        return;
    }
    char crumb[128];
    if (level2 != NULL && level2[0] != '\0') {
        snprintf(crumb, sizeof(crumb), "%s / %s", tr_page(level1 ? level1 : ""), tr_page(level2));
    } else {
        snprintf(crumb, sizeof(crumb), "%s", tr_page(level1 ? level1 : ""));
    }
    lv_label_set_text(s_page_label, crumb);
}

void update_bluetooth_status(bool connected)
{
    if (connected == s_bt_state) {
        return; /* avoid a needless invalidation / full refresh */
    }
    s_bt_state = connected;
    set_icon(s_bt_icon, connected, ICON_BT_ON, ICON_BT_OFF);
}

void update_wifi_status(bool connected)
{
    if (connected == s_wifi_state) {
        return;
    }
    s_wifi_state = connected;
    set_icon(s_wifi_icon, connected, ICON_WIFI_ON, ICON_WIFI_OFF);
}

void status_bar_set_hidden(bool hidden)
{
    if (s_bar == NULL) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_bar, LV_OBJ_FLAG_HIDDEN);
    }
}

void status_bar_set_transparency(uint8_t transparency)
{
    if (s_bar == NULL) {
        return;
    }
    if (transparency > 100) {
        transparency = 100;
    }
    /* 0 = opaque (LV_OPA_COVER), 100 = fully transparent. */
    lv_obj_set_style_bg_opa(s_bar, (lv_opa_t)((uint32_t)(100 - transparency) * 255 / 100), 0);
}

void status_bar_set_stats_visible(bool visible)
{
    s_stats_visible = visible;
    if (s_stat_box == NULL) {
        return;
    }
    if (visible) {
        s_stats_last_frames = lvgl_port_frame_count();
        s_stats_last_ms = lv_tick_get();
        lvgl_port_cpu_stats(&s_stats_last_busy, &s_stats_last_total);
        lv_obj_clear_flag(s_stat_box, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_stat_box, LV_OBJ_FLAG_HIDDEN);
    }
}
