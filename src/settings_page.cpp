/*
 * settings_page.cpp - Settings tab.
 *
 * Main screen is a 2x4 grid of tiles (8 cells):
 *   [Wi-Fi] [OBS] [General] [Bluetooth]
 *   [Language] [About] [System] [Back]
 * Each of the first seven opens a sub-page (with a Back button); "Back"
 * returns to the main page. The current location is shown via status-bar
 * breadcrumbs ("Settings" / "Settings / <sub>").
 */
#include "settings_page.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "config_backup.h"
#include "display_init.h"
#include "esp_log.h"
#include "esp_system.h"
#include "font_manager.h"
#include "freertos/FreeRTOS.h"
#include "gesture_handler.h"
#include "freertos/task.h"
#include "i18n.h"
#include "keyboard_manager.h"
#include "obs_client.h"
#include "ota_update.h"
#include "status_bar.h"
#include "system_info.h"
#include "toast.h"
#include "ui_assets.h"
#include "ui_loader.h"
#include "ui_renderer.h"
#include "web_server.h"
#include "wifi_manager.h"

static const char *TAG = "settings_page";

static int s_settings_tab = -1;

/* Radio switching (BLE <-> WiFi) is applied by rebooting: the two share the
 * scarce internal RAM and tearing one stack down at runtime corrupts memory. */
static void reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(700)); /* let the toast/display update */
    esp_restart();
}

static void request_radio_reboot(void)
{
    ESP_LOGI(TAG, "radio switch: showing reboot toast");
    toast_show(tr("rebooting"));
    ESP_LOGI(TAG, "radio switch: starting reboot task");
    xTaskCreate(reboot_task, "radio_reboot", 2560, NULL, 5, NULL);
}

static lv_obj_t *s_root = NULL;
static lv_obj_t *s_menu = NULL;
static lv_obj_t *s_sub = NULL;

/* Sub-page indices. */
enum {
    SUBP_WIFI = 0,   /* Mode */
    SUBP_OBS,
    SUBP_GENERAL,
    SUBP_BT,         /* Configuration */
    SUBP_SYSTEM,     /* Config backup + firmware update */
    SUBP_LANG,
    SUBP_ABOUT,
    SUBP_INFO,
    SUBP_MAX,
};

/* controls */
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_bright_label = NULL;
static lv_obj_t *s_bright_slider = NULL;
static lv_obj_t *s_sound_switch = NULL;
static lv_obj_t *s_sleep_dd = NULL;
static lv_obj_t *s_lang_dd = NULL;
static lv_obj_t *s_stats_switch = NULL;

/* on-screen keyboard (shown while a text field is focused) */
static lv_obj_t *s_keyboard = NULL;

/* Mode / Configuration pages */
static lv_obj_t *s_radio_dd = NULL;      /* radio mode selector */
static lv_obj_t *s_sta_pass_ta = NULL;
static lv_obj_t *s_scan_dd = NULL;
static int s_scan_shown = -1;
static lv_obj_t *s_ap_ssid_ta = NULL;
static lv_obj_t *s_ap_pass_ta = NULL;
static lv_obj_t *s_cfg_dd = NULL;

/* OBS page */
static lv_obj_t *s_obs_host_ta = NULL;
static lv_obj_t *s_obs_port_ta = NULL;
static lv_obj_t *s_obs_pass_ta = NULL;
static lv_obj_t *s_obs_result = NULL;
static lv_obj_t *s_obs_warn = NULL;
static int s_obs_shown = -1;       /* last OBS result shown (skip redundant updates) */
static int s_obs_warn_shown = -1;  /* last visibility of the OBS warning */

/* About page */
static lv_obj_t *s_about_label = NULL;
static system_info_t s_sysinfo;

/* timers */
static lv_timer_t *s_wifi_timer = NULL;
static lv_timer_t *s_obs_timer = NULL;
static lv_timer_t *s_sys_timer = NULL;
static lv_timer_t *s_save_timer = NULL;

typedef struct {
    lv_obj_t *sub;
    const char *label;
} sub_ref_t;
static sub_ref_t s_subs[SUBP_MAX];

/* ------------------------------------------------------------------ */
/* Events                                                            */
/* ------------------------------------------------------------------ */
/* Settings writes go to LittleFS/flash, and a flash write disables the cache
 * (which stalls every PSRAM access for the duration). Saving on every slider
 * tick made dragging the brightness slider lag, so continuous changes are
 * coalesced into a single deferred write. */
static void settings_save_cb(lv_timer_t *timer)
{
    lv_timer_pause(timer);
    save_settings(get_settings());
}

static void settings_save_soon(void)
{
    if (s_save_timer != NULL) {
        lv_timer_reset(s_save_timer);
        lv_timer_resume(s_save_timer);
    }
}

static void brightness_event(lv_event_t *e)
{
    (void)e;
    int v = lv_slider_get_value(s_bright_slider);
    AppSettings *s = get_settings_mut();
    s->brightness = (uint8_t)v;
    lv_label_set_text_fmt(s_bright_label, "%d%%", v);
    set_brightness((uint8_t)v);
    settings_save_soon();
}

static void sound_event(lv_event_t *e)
{
    (void)e;
    AppSettings *s = get_settings_mut();
    s->sound_enabled = lv_obj_has_state(s_sound_switch, LV_STATE_CHECKED);
    save_settings(s);
}

static void stats_event(lv_event_t *e)
{
    (void)e;
    AppSettings *s = get_settings_mut();
    s->show_stats = lv_obj_has_state(s_stats_switch, LV_STATE_CHECKED);
    save_settings(s);
    status_bar_set_stats_visible(s->show_stats);
}

static void sleep_event(lv_event_t *e)
{
    (void)e;
    static const uint16_t vals[] = {60, 180, 300, 600, 0};
    uint16_t sel = lv_dropdown_get_selected(s_sleep_dd);
    if (sel >= 5) sel = 2;
    AppSettings *s = get_settings_mut();
    s->sleep_timeout = vals[sel];
    save_settings(s);
}

static void lang_event(lv_event_t *e)
{
    (void)e;
    uint16_t sel = lv_dropdown_get_selected(s_lang_dd);
    i18n_set_language(sel == 1 ? LANG_RU : LANG_EN);
}

/* ------------------------------------------------------------------ */
/* Navigation                                                        */
/* ------------------------------------------------------------------ */
static void show_sub(sub_ref_t *ref)
{
    if (s_sub != NULL) {
        lv_obj_add_flag(s_sub, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_menu != NULL) {
        lv_obj_add_flag(s_menu, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(ref->sub, LV_OBJ_FLAG_HIDDEN);
    s_sub = ref->sub;
    update_status_bar_breadcrumbs(tr("settings"), ref->label);

    /* Require a finger lift before the next tap so a tile tap cannot also hit a
     * control that lands under the same spot on the shown sub-page. */
    input_gate_arm();
}

static void show_menu(void)
{
    if (s_sub != NULL) {
        lv_obj_add_flag(s_sub, LV_OBJ_FLAG_HIDDEN);
        s_sub = NULL;
    }
    if (s_menu != NULL) {
        lv_obj_clear_flag(s_menu, LV_OBJ_FLAG_HIDDEN);
    }
    update_status_bar_breadcrumbs(tr("settings"), NULL);

    /* Back to the menu: require a finger lift before the next tap. */
    input_gate_arm();
}

static void tile_cb(lv_event_t *e)
{
    show_sub((sub_ref_t *)lv_event_get_user_data(e));
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    show_menu();
}

static void home_cb(lv_event_t *e)
{
    (void)e;
    ui_show_page(0);
}

bool is_settings_page_active(void)
{
    lv_obj_t *tv = get_ui_tabview();
    if (tv == NULL) {
        return false;
    }
    return (int)lv_tabview_get_tab_act(tv) == s_settings_tab;
}

/* ------------------------------------------------------------------ */
/* Widget builders                                                   */
/* ------------------------------------------------------------------ */
static lv_obj_t *make_page_area(lv_obj_t *parent)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_size(p, LCD_WIDTH, LCD_HEIGHT - 30);
    lv_obj_set_pos(p, 0, 30);
    lv_obj_set_style_bg_opa(p, LV_OPA_0, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 10, 0);
    lv_obj_set_scrollbar_mode(p, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(p, LV_DIR_VER);   /* vertical scroll only */
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(p, 8, 0);
    return p;
}

static lv_obj_t *make_subpage(lv_obj_t *parent, const char *label, int idx)
{
    lv_obj_t *p = make_page_area(parent);
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    s_subs[idx].sub = p;
    s_subs[idx].label = label;

    lv_obj_t *back = lv_btn_create(p);
    lv_obj_set_size(back, 90, 40);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x2a2f45), 0);
    lv_obj_set_style_radius(back, 10, 0);
    lv_obj_t *bl = lv_label_create(back);
    lv_label_set_text_fmt(bl, "< %s", tr("back"));
    lv_obj_set_style_text_font(bl, get_font(14), 0);
    lv_obj_center(bl);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    return p;
}

static lv_obj_t *make_row(lv_obj_t *parent, int height)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), height);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x22283a), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_left(row, 12, 0);
    lv_obj_set_style_pad_right(row, 12, 0);
    lv_obj_set_style_pad_top(row, 0, 0);
    lv_obj_set_style_pad_bottom(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return row;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, int size, uint32_t color)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, get_font(size), 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    return lbl;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *label)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_height(btn, 42);
    lv_obj_set_width(btn, LV_PCT(100));
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2a2f45), 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, get_font(14), 0);
    lv_obj_center(lbl);
    return btn;
}

static void ta_focus_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);
    if (s_keyboard == NULL) {
        return;
    }
    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_mode(s_keyboard, (ta == s_obs_port_ta) ? LV_KEYBOARD_MODE_NUMBER
                                                               : LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_keyboard_set_textarea(s_keyboard, ta);
        lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_keyboard);
    } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_keyboard_set_textarea(s_keyboard, NULL);
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static lv_obj_t *make_ta(lv_obj_t *parent, bool password, const char *initial)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_width(ta, LV_PCT(100));
    lv_textarea_set_text(ta, initial != NULL ? initial : "");
    if (password) {
        lv_textarea_set_password_mode(ta, true);
    }
    lv_obj_set_style_text_font(ta, get_font(14), 0);
    /* No cursor blink: on this full-refresh panel every blink is a full-frame
     * redraw (~100 ms) and the periodic repaint made the on-screen keyboard
     * appear frozen. anim_time=0 stops the blink animation (cursor stays). */
    lv_obj_set_style_anim_time(ta, 0, LV_PART_CURSOR);
    lv_obj_add_event_cb(ta, ta_focus_cb, LV_EVENT_ALL, NULL);
    return ta;
}

/* 2x4 grid tile: icon in a coloured disc + label. */
static void make_grid_tile(lv_obj_t *grid, const char *label, const char *icon_name,
                           uint32_t disc_color, sub_ref_t *ref)
{
    lv_obj_t *tile = lv_btn_create(grid);
    lv_obj_set_size(tile, 104, 96);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x2a2f45), 0);
    lv_obj_set_style_radius(tile, 14, 0);
    lv_obj_set_style_shadow_width(tile, 0, 0);
    lv_obj_set_style_pad_all(tile, 0, 0);

    lv_obj_t *disc = lv_obj_create(tile);
    lv_obj_set_size(disc, 40, 40);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc, lv_color_hex(disc_color), 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(disc, 0, 0);
    lv_obj_set_style_pad_all(disc, 0, 0);
    lv_obj_clear_flag(disc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(disc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(disc, LV_ALIGN_TOP_MID, 0, 14);

    if (icon_name != NULL) {
        /* Max icon box inside the 38px disc, leaving a 4px margin all around. */
        const int box = 30;
        const char *path = asset_persist_path(icon_name, box, box);
        if (path != NULL) {
            lv_img_header_t hh;
            memset(&hh, 0, sizeof(hh));
            lv_res_t r = lv_img_decoder_get_info(path, &hh);
            lv_obj_t *ic = lv_img_create(disc);
            lv_img_set_src(ic, path);
            /* Icons come in different sizes (16..64px); scale each down to fit
             * fully inside the circle instead of overflowing it. */
            if (r == LV_RES_OK && hh.w > 0 && hh.h > 0) {
                uint32_t zx = (uint32_t)((box * 256) / hh.w);
                uint32_t zy = (uint32_t)((box * 256) / hh.h);
                lv_img_set_zoom(ic, (uint16_t)(zx < zy ? zx : zy));
            }
            lv_obj_center(ic);
            lv_obj_clear_flag(ic, LV_OBJ_FLAG_CLICKABLE);
        }
    }

    lv_obj_t *lbl = lv_label_create(tile);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, get_font(12), 0);
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -12);

    if (ref != NULL) {
        lv_obj_add_event_cb(tile, tile_cb, LV_EVENT_CLICKED, ref);
    } else {
        lv_obj_add_event_cb(tile, home_cb, LV_EVENT_CLICKED, NULL);
    }
}

/* ------------------------------------------------------------------ */
/* Mode / Configuration pages                                          */
/* ------------------------------------------------------------------ */
/* The SSID is the selected entry of the scan dropdown (no separate field). */
static const char *sta_selected_ssid(void)
{
    if (s_scan_dd == NULL) {
        return NULL;
    }
    const char *txt = lv_dropdown_get_text(s_scan_dd);
    if (txt == NULL || txt[0] == '\0' || strcmp(txt, tr("scan_none")) == 0) {
        return NULL;
    }
    return txt;
}

static void wifi_scan_event(lv_event_t *e)
{
    (void)e;
    wifi_manager_request_scan();
}

/* Save the selected radio mode (and the credentials of the matching Wi-Fi
 * section), then reboot so only that one stack is brought up. */
static void mode_save_event(lv_event_t *e)
{
    (void)e;
    uint16_t sel = lv_dropdown_get_selected(s_radio_dd);
    AppSettings *s = get_settings_mut();
    s->radio_mode = (sel <= RADIO_MODE_STA) ? (radio_mode_t)sel : RADIO_MODE_BLE;
    s->wifi_enabled = (s->radio_mode != RADIO_MODE_BLE);
    s->ble_enabled = (s->radio_mode == RADIO_MODE_BLE);

    if (s->radio_mode == RADIO_MODE_AP) {
        wifi_manager_configure(WIFI_APP_AP, lv_textarea_get_text(s_ap_ssid_ta),
                               lv_textarea_get_text(s_ap_pass_ta));
    } else if (s->radio_mode == RADIO_MODE_STA) {
        wifi_manager_configure(WIFI_APP_STA, sta_selected_ssid(),
                               lv_textarea_get_text(s_sta_pass_ta));
    }

    save_settings(s);
    request_radio_reboot();
}

/* Transparent column container for a settings sub-section. */
static lv_obj_t *make_settings_section(lv_obj_t *parent)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_width(p, LV_PCT(100));
    lv_obj_set_height(p, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(p, LV_OPA_0, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(p, 8, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

/* Transparent horizontal container that lays its children out as columns. */
static lv_obj_t *make_columns(lv_obj_t *parent)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_width(p, LV_PCT(100));
    lv_obj_set_height(p, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(p, LV_OPA_0, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_pad_all(p, 0, 0);
    lv_obj_set_style_pad_column(p, 8, 0);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

/* Column cell: a small title above a control (label on top, field below). */
static lv_obj_t *make_field_cell(lv_obj_t *parent, int width_pct, const char *title)
{
    lv_obj_t *cell = make_settings_section(parent);
    lv_obj_set_width(cell, LV_PCT(width_pct));
    make_label(cell, title, 14, 0xcccccc);
    return cell;
}

/* Transparent single line with a label on the left and a control on the right. */
static lv_obj_t *make_line(lv_obj_t *parent, int height)
{
    lv_obj_t *l = lv_obj_create(parent);
    lv_obj_set_size(l, LV_PCT(100), height);
    lv_obj_set_style_bg_opa(l, LV_OPA_0, 0);
    lv_obj_set_style_border_width(l, 0, 0);
    lv_obj_set_style_pad_all(l, 0, 0);
    lv_obj_clear_flag(l, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(l, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(l, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return l;
}

static void build_wifi_sta_panel(lv_obj_t *parent)
{
    make_label(parent, tr("network_scan_title"), 14, 0xcccccc);

    lv_obj_t *scan = make_button(parent, tr("scan_networks"));
    lv_obj_add_event_cb(scan, wifi_scan_event, LV_EVENT_CLICKED, NULL);

    /* SSID selector and password on one line, labels above. */
    lv_obj_t *row = make_columns(parent);

    lv_obj_t *c1 = make_field_cell(row, 48, tr("ssid"));
    /* The dropdown *is* the SSID selector (saved network pre-selected). */
    s_scan_dd = lv_dropdown_create(c1);
    lv_obj_set_width(s_scan_dd, LV_PCT(100));
    const char *saved = wifi_manager_ssid();
    lv_dropdown_set_options(s_scan_dd, (saved != NULL && saved[0] != '\0') ? saved : tr("scan_none"));
    lv_dropdown_set_selected(s_scan_dd, 0);
    lv_obj_set_style_text_font(s_scan_dd, get_font(14), 0);
    lv_obj_set_style_text_font(lv_dropdown_get_list(s_scan_dd), get_font(14), 0);
    /* The arrow is LV_SYMBOL_DOWN, which only the (built-in) Montserrat symbol
     * font contains - keep the value text in Roboto but the arrow in Montserrat. */
    lv_obj_set_style_text_font(s_scan_dd, &lv_font_montserrat_14, LV_PART_INDICATOR);
    s_scan_shown = 0; /* avoid replacing the saved SSID before the first scan */

    lv_obj_t *c2 = make_field_cell(row, 48, tr("password"));
    s_sta_pass_ta = make_ta(c2, true, wifi_manager_password());
}

static void build_wifi_ap_panel(lv_obj_t *parent)
{
    /* SSID and password on one line, equal width, labels above. */
    lv_obj_t *row = make_columns(parent);
    lv_obj_t *c1 = make_field_cell(row, 48, tr("ssid"));
    s_ap_ssid_ta = make_ta(c1, false, wifi_manager_ap_ssid());
    lv_obj_t *c2 = make_field_cell(row, 48, tr("password"));
    s_ap_pass_ta = make_ta(c2, false, wifi_manager_ap_pass());
}

/* ------------------------------------------------------------------ */
/* Config backup (SD card)                                            */
/* ------------------------------------------------------------------ */
static void cfg_dd_populate(void)
{
    if (s_cfg_dd == NULL) {
        return;
    }
    static char names[16][64];
    int n = config_backup_list(names, 16);
    if (n <= 0) {
        lv_dropdown_set_options(s_cfg_dd, tr("no_files"));
        return;
    }
    static char opts[16 * 64 + 1];
    opts[0] = '\0';
    for (int i = 0; i < n; i++) {
        if (i > 0) {
            strncat(opts, "\n", sizeof(opts) - strlen(opts) - 1);
        }
        strncat(opts, names[i], sizeof(opts) - strlen(opts) - 1);
    }
    lv_dropdown_set_options(s_cfg_dd, opts);
    lv_dropdown_set_selected(s_cfg_dd, 0);
}

static void backup_save_event(lv_event_t *e)
{
    (void)e;
    char name[64];
    if (config_backup_save(name, sizeof(name)) == 0) {
        char msg[96];
        snprintf(msg, sizeof(msg), "%s: %s", tr("saved"), name);
        toast_show(msg);
        cfg_dd_populate();
    } else {
        toast_show(tr("no_sd"));
    }
}

static void backup_import_event(lv_event_t *e)
{
    (void)e;
    if (s_cfg_dd == NULL) {
        return;
    }
    const char *name = lv_dropdown_get_text(s_cfg_dd);
    if (name == NULL || name[0] == '\0' || strcmp(name, tr("no_files")) == 0) {
        toast_show(tr("no_files"));
        return;
    }
    if (config_backup_import(name)) {
        toast_show(tr("rebooting"));
        request_radio_reboot();
    } else {
        toast_show(tr("no_files"));
    }
}

/* Firmware update from the SD card (update.bin) - runs on a worker task so the
 * UI is not blocked while the image is written to the OTA slot. */
static void ota_sd_task(void *arg)
{
    (void)arg;
    if (ota_flash_file(OTA_SD_PATH)) {
        esp_restart();
    }
    toast_show(tr("update_failed"));
    vTaskDelete(NULL);
}

static void ota_sd_event(lv_event_t *e)
{
    (void)e;
    if (!ota_sd_update_present()) {
        toast_show(tr("no_update_file"));
        return;
    }
    toast_show(tr("updating"));
    xTaskCreate(ota_sd_task, "ota_sd", 6144, NULL, 5, NULL);
}

/* ------------------------------------------------------------------ */
/* OBS page                                                          */
/* ------------------------------------------------------------------ */
static void obs_check_event(lv_event_t *e)
{
    (void)e;
    obs_client_request_check();
    if (s_obs_result != NULL) {
        lv_label_set_text(s_obs_result, tr("checking"));
        lv_obj_set_style_text_color(s_obs_result, lv_color_hex(0xffcc00), 0);
    }
}

static int parse_port(const char *text, int fallback)
{
    if (text == NULL || text[0] == '\0') {
        return fallback;
    }
    int port = atoi(text);
    return (port > 0 && port <= 65535) ? port : fallback;
}

static void obs_save_event(lv_event_t *e)
{
    (void)e;
    int port = parse_port(lv_textarea_get_text(s_obs_port_ta), 4455);
    obs_client_set_config(lv_textarea_get_text(s_obs_host_ta), port,
                          lv_textarea_get_text(s_obs_pass_ta));
    toast_show(tr("saved"));
}

/* ------------------------------------------------------------------ */
/* Timers                                                            */
/* ------------------------------------------------------------------ */
/* Rebuild the SSID dropdown from the scan results, keeping the selection. */
static void wifi_scan_list_refresh(void)
{
    if (s_scan_dd == NULL) {
        return;
    }
    int n = wifi_manager_scan_count();
    if (n == s_scan_shown) {
        return;
    }

    char prev[64];
    prev[0] = '\0';
    const char *cur = lv_dropdown_get_text(s_scan_dd);
    if (cur != NULL) {
        strncpy(prev, cur, sizeof(prev) - 1);
        prev[sizeof(prev) - 1] = '\0';
    }

    static char opts[WIFI_SCAN_MAX * 36 + 1];
    opts[0] = '\0';
    if (n <= 0) {
        const char *saved = wifi_manager_ssid();
        if (prev[0] != '\0' && strcmp(prev, tr("scan_none")) != 0) {
            snprintf(opts, sizeof(opts), "%s", prev);
        } else if (saved != NULL && saved[0] != '\0') {
            snprintf(opts, sizeof(opts), "%s", saved);
        } else {
            snprintf(opts, sizeof(opts), "%s", tr("scan_none"));
        }
    } else {
        for (int i = 0; i < n; i++) {
            if (i > 0) {
                strncat(opts, "\n", sizeof(opts) - strlen(opts) - 1);
            }
            strncat(opts, wifi_manager_scan_ssid(i), sizeof(opts) - strlen(opts) - 1);
        }
    }
    lv_dropdown_set_options(s_scan_dd, opts);

    int sel = 0;
    if (n > 0 && prev[0] != '\0') {
        for (int i = 0; i < n; i++) {
            if (strcmp(wifi_manager_scan_ssid(i), prev) == 0) {
                sel = i;
                break;
            }
        }
    }
    lv_dropdown_set_selected(s_scan_dd, (uint16_t)sel);
    s_scan_shown = n;
}

static void wifi_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    /* The status-bar icon is always visible and only invalidates on change. */
    update_wifi_status(wifi_manager_connected());
    /* Everything below touches settings widgets; doing that while another page
     * is shown would invalidate the UI and force a full-screen repaint (the
     * panel is full_refresh-only), so skip it when the page is not visible. */
    if (!is_settings_page_active()) {
        return;
    }
    wifi_scan_list_refresh();
}

static void obs_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!is_settings_page_active()) {
        return; /* no settings widgets visible -> avoid invalidating the UI */
    }
    if (s_obs_result != NULL) {
        obs_check_result_t r = obs_client_check_result();
        if ((int)r != s_obs_shown) {
            s_obs_shown = (int)r;
            switch (r) {
            case OBS_CHECK_PENDING:
                lv_label_set_text(s_obs_result, tr("checking"));
                lv_obj_set_style_text_color(s_obs_result, lv_color_hex(0xffcc00), 0);
                break;
            case OBS_CHECK_OK:
                lv_label_set_text(s_obs_result, tr("obs_connected"));
                lv_obj_set_style_text_color(s_obs_result, lv_color_hex(0x00ff88), 0);
                break;
            case OBS_CHECK_FAIL:
                lv_label_set_text(s_obs_result, tr("obs_error"));
                lv_obj_set_style_text_color(s_obs_result, lv_color_hex(0xff4444), 0);
                break;
            default:
                break;
            }
        }
    }

    if (s_obs_warn != NULL) {
        bool hide_warn = (wifi_manager_mode() == WIFI_APP_STA);
        if ((int)hide_warn != s_obs_warn_shown) {
            s_obs_warn_shown = (int)hide_warn;
            if (hide_warn) {
                lv_obj_add_flag(s_obs_warn, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(s_obs_warn, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

/* Format a size given in KB as "N KB" / "N.N MB" / "N.NN GB". */
static void fmt_kb(char *out, size_t n, uint32_t kb)
{
    if (kb >= 1024u * 1024u) {
        snprintf(out, n, "%u.%02u GB", (unsigned)(kb / (1024u * 1024u)),
                 (unsigned)((kb % (1024u * 1024u)) * 100u / (1024u * 1024u)));
    } else if (kb >= 1024u) {
        snprintf(out, n, "%u.%u MB", (unsigned)(kb / 1024u),
                 (unsigned)((kb % 1024u) * 10u / 1024u));
    } else {
        snprintf(out, n, "%u KB", (unsigned)kb);
    }
}

/* Map an esp_reset_reason_t value to a short token. */
static const char *reset_reason_str(int r)
{
    switch (r) {
    case 1: return "Power-on";
    case 2: return "External";
    case 3: return "Software";
    case 4: return "Panic";
    case 5: return "Int-WDT";
    case 6: return "Task-WDT";
    case 7: return "WDT";
    case 8: return "Deepsleep";
    case 9: return "Brownout";
    case 10: return "SDIO";
    case 11: return "USB";
    case 12: return "JTAG";
    default: return "Unknown";
    }
}

/* __DATE__ is "Mmm dd yyyy" (e.g. "Sep 25 2026") and __TIME__ is "HH:MM:SS".
 * Render the firmware build stamp as "dd.MM.yyyy HH:MM:SS". */
static void format_build(char *out, size_t n)
{
    static const char *mon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char date[16];
    strncpy(date, __DATE__, sizeof(date) - 1);
    date[sizeof(date) - 1] = '\0';

    char mmm[4] = {0, 0, 0, 0};
    int day = 0, year = 0;
    sscanf(date, "%3s %d %d", mmm, &day, &year);
    int mi = 1;
    for (int i = 0; i < 12; i++) {
        if (strncmp(mmm, mon[i], 3) == 0) {
            mi = i + 1;
            break;
        }
    }
    snprintf(out, n, "%02d.%02d.%04d %s", day, mi, year, __TIME__);
}

/* Detailed system info shown on the About page (values only). */
static void about_update_label(void)
{
    if (s_about_label == NULL) {
        return;
    }
    int temp10 = (int)(s_sysinfo.temperature_c * 10.0f);
    if (temp10 < 0) {
        temp10 = -temp10;
    }

    char sram_f[20], sram_t[20], psram_f[20], psram_t[20];
    char heap_f[20], heap_t[20], fs_f[20], fs_t[20], sd_f[20], sd_t[20];
    fmt_kb(sram_f, sizeof(sram_f), s_sysinfo.sram_free);
    fmt_kb(sram_t, sizeof(sram_t), s_sysinfo.sram_total);
    fmt_kb(psram_f, sizeof(psram_f), s_sysinfo.psram_free);
    fmt_kb(psram_t, sizeof(psram_t), s_sysinfo.psram_total);
    fmt_kb(heap_f, sizeof(heap_f), s_sysinfo.heap_free);
    fmt_kb(heap_t, sizeof(heap_t), s_sysinfo.heap_total);
    fmt_kb(fs_f, sizeof(fs_f), s_sysinfo.fs_free);
    fmt_kb(fs_t, sizeof(fs_t), s_sysinfo.fs_total);
    fmt_kb(sd_f, sizeof(sd_f), s_sysinfo.sd_free_kb);
    fmt_kb(sd_t, sizeof(sd_t), s_sysinfo.sd_total_kb);

    char build[32];
    format_build(build, sizeof(build));

    char text[1024];
    int n = snprintf(text, sizeof(text),
                     "%s: %d.%d C\n"
                     "%s: %s rev %u, %u %s\n"
                     "CPU: %u MHz, Flash: %u MHz\n"
                     "%s: %ud %02u:%02u:%02u\n"
                     "\n"
                     "SRAM:  %s free / %s\n"
                     "PSRAM: %s free / %s\n"
                     "Heap:  %s free / %s\n"
                     "%s: %s free / %s\n",
                     tr("temperature"), temp10 / 10, temp10 % 10,
                     tr("chip"), s_sysinfo.chip_model ? s_sysinfo.chip_model : "-",
                     (unsigned)s_sysinfo.chip_revision, (unsigned)s_sysinfo.cores, tr("cores"),
                     (unsigned)s_sysinfo.cpu_freq_mhz, (unsigned)s_sysinfo.flash_freq_mhz,
                     tr("uptime"),
                     (unsigned)(s_sysinfo.uptime_seconds / 86400u),
                     (unsigned)((s_sysinfo.uptime_seconds / 3600u) % 24u),
                     (unsigned)((s_sysinfo.uptime_seconds / 60u) % 60u),
                     (unsigned)(s_sysinfo.uptime_seconds % 60u),
                     sram_f, sram_t, psram_f, psram_t, heap_f, heap_t,
                     tr("fs_device"), fs_f, fs_t);

    if (s_sysinfo.sd_present) {
        /* Two lines: free/total, then the card filesystem type. */
        n += snprintf(text + n, sizeof(text) - n,
                      "SD:    %s free / %s\n"
                      "%s: %s\n",
                      sd_f, sd_t,
                      tr("fs_type"), s_sysinfo.sd_fs_type ? s_sysinfo.sd_fs_type : "FAT");
    } else {
        n += snprintf(text + n, sizeof(text) - n, "SD:    %s\n", tr("not_present"));
    }

    n += snprintf(text + n, sizeof(text) - n,
                  "WiFi RSSI: %d dBm, reconnects: %d\n"
                  "\n"
                  "WiFi MAC: %02X:%02X:%02X:%02X:%02X:%02X\n"
                  "BT MAC:   %02X:%02X:%02X:%02X:%02X:%02X\n"
                  "%s: %s\n"
                  "%s: %s\n"
                  "IDF: %s",
                  s_sysinfo.wifi_rssi, s_sysinfo.wifi_reconnects,
                  s_sysinfo.wifi_mac[0], s_sysinfo.wifi_mac[1], s_sysinfo.wifi_mac[2],
                  s_sysinfo.wifi_mac[3], s_sysinfo.wifi_mac[4], s_sysinfo.wifi_mac[5],
                  s_sysinfo.ble_mac[0], s_sysinfo.ble_mac[1], s_sysinfo.ble_mac[2],
                  s_sysinfo.ble_mac[3], s_sysinfo.ble_mac[4], s_sysinfo.ble_mac[5],
                  tr("build"), build,
                  tr("reset_reason"), reset_reason_str(s_sysinfo.reset_reason),
                  s_sysinfo.idf_version ? s_sysinfo.idf_version : "-");

    int tn = system_info_task_count();
    if (tn > 0 && n < (int)sizeof(text) - 32) {
        n += snprintf(text + n, sizeof(text) - n, "\n\n%s:\n", tr("task_stacks"));
        for (int i = 0; i < tn && n < (int)sizeof(text) - 40; i++) {
            n += snprintf(text + n, sizeof(text) - n, " %s: %u/%u B\n",
                          system_info_task_name(i),
                          (unsigned)system_info_task_stack_free(i),
                          (unsigned)system_info_task_stack_total(i));
        }
    }

    lv_label_set_text(s_about_label, text);
}

static void sys_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!is_settings_page_active()) {
        return; /* About widgets hidden -> don't invalidate the UI */
    }
    system_info_collect(&s_sysinfo);
    about_update_label();
}

/* ------------------------------------------------------------------ */
/* Page                                                              */
/* ------------------------------------------------------------------ */
void settings_page_update_status(void)
{
    if (s_status_label == NULL) {
        return;
    }
    if (is_keyboard_connected()) {
        lv_label_set_text(s_status_label, tr("connected"));
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x00ff88), 0);
    } else {
        lv_label_set_text(s_status_label, tr("not_connected"));
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xff4444), 0);
    }
}

void create_settings_page(lv_obj_t *parent)
{
    const AppSettings *settings = get_settings();

    /* Drop stale references / timers from a previous build. */
    if (s_wifi_timer != NULL) { lv_timer_del(s_wifi_timer); s_wifi_timer = NULL; }
    if (s_obs_timer != NULL) { lv_timer_del(s_obs_timer); s_obs_timer = NULL; }
    if (s_sys_timer != NULL) { lv_timer_del(s_sys_timer); s_sys_timer = NULL; }
    if (s_save_timer != NULL) { lv_timer_del(s_save_timer); s_save_timer = NULL; }

    memset(s_subs, 0, sizeof(s_subs)); /* drop stale sub-page refs (pages removed) */
    s_root = NULL;
    s_menu = NULL;
    s_sub = NULL;
    s_status_label = NULL;
    s_bright_label = NULL;
    s_bright_slider = NULL;
    s_sound_switch = NULL;
    s_sleep_dd = NULL;
    s_lang_dd = NULL;
    s_stats_switch = NULL;
    s_keyboard = NULL;
    s_radio_dd = NULL;
    s_sta_pass_ta = NULL;
    s_scan_dd = NULL;
    s_scan_shown = -1;
    s_ap_ssid_ta = NULL;
    s_ap_pass_ta = NULL;
    s_cfg_dd = NULL;
    s_obs_host_ta = NULL;
    s_obs_port_ta = NULL;
    s_obs_pass_ta = NULL;
    s_obs_result = NULL;
    s_obs_warn = NULL;
    s_obs_shown = -1;
    s_obs_warn_shown = -1;
    s_about_label = NULL;

    if (parent == NULL) {
        parent = lv_scr_act();
    }

    if (lv_obj_check_type(parent, &lv_tabview_class)) {
        s_root = lv_tabview_add_tab(parent, tr("settings"));
        s_settings_tab = get_ui_tab_count() - 1;
    } else {
        s_root = lv_obj_create(parent);
        lv_obj_set_size(s_root, LCD_WIDTH, LCD_HEIGHT);
    }

    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    /* Single flat background colour (dark blue) - intentionally no image. */
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0x1a1a2e), 0);
    lv_obj_clear_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    /* Shared on-screen keyboard (hidden until a text field is focused). */
    s_keyboard = lv_keyboard_create(s_root);
    lv_obj_set_size(s_keyboard, LCD_WIDTH, 150);
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_keyboard, ta_focus_cb, LV_EVENT_ALL, NULL);

    /* ---- Sub-pages (created first so tiles can reference them) ---- */
    lv_obj_t *sub_mode = make_subpage(s_root, tr("mode"), SUBP_WIFI);
    lv_obj_t *sub_obs = make_subpage(s_root, "OBS", SUBP_OBS);
    lv_obj_t *sub_general = make_subpage(s_root, tr("general"), SUBP_GENERAL);
    lv_obj_t *sub_config = make_subpage(s_root, tr("configuration"), SUBP_BT);
    lv_obj_t *sub_system = make_subpage(s_root, tr("system_tile"), SUBP_SYSTEM);
    lv_obj_t *sub_about = make_subpage(s_root, tr("about"), SUBP_ABOUT);

    /* ---- Main: 2x4 grid ---- */
    s_menu = make_page_area(s_root);
    lv_obj_set_flex_flow(s_menu, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(s_menu, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_menu, 12, 0);

    make_grid_tile(s_menu, tr("mode"), "icon_mode.png", 0x2d6cdf, &s_subs[SUBP_WIFI]);
    make_grid_tile(s_menu, "OBS", "icon_obsstudio.png", 0xc0392b, &s_subs[SUBP_OBS]);
    make_grid_tile(s_menu, tr("general"), "icon_wrench.png", 0x2563eb, &s_subs[SUBP_GENERAL]);
    make_grid_tile(s_menu, tr("configuration"), "icon_config.png", 0x1a73e8, &s_subs[SUBP_BT]);
    make_grid_tile(s_menu, tr("system_tile"), "icon_system.png", 0x1e88e5, &s_subs[SUBP_SYSTEM]);
    make_grid_tile(s_menu, tr("about"), "icon_info.png", 0x1a73e8, &s_subs[SUBP_ABOUT]);
    make_grid_tile(s_menu, tr("back"), "icon_home.png", 0x1e88e5, NULL);

    /* ---- Mode: choose the single active radio + save/reboot ---- */
    {
        make_label(sub_mode, tr("mode_hint"), 12, 0x9fd0ff);

        lv_obj_t *mrow = make_row(sub_mode, 56);
        lv_obj_t *mlab = lv_label_create(mrow);
        lv_label_set_text(mlab, tr("mode"));
        lv_obj_set_style_text_color(mlab, lv_color_white(), 0);
        lv_obj_set_style_text_font(mlab, get_font(14), 0);
        s_radio_dd = lv_dropdown_create(mrow);
        lv_dropdown_set_options(s_radio_dd, tr("mode_opts_radio"));
        lv_obj_set_width(s_radio_dd, 230);
        lv_obj_set_style_text_font(s_radio_dd, get_font(14), 0);
        lv_obj_set_style_text_font(lv_dropdown_get_list(s_radio_dd), get_font(14), 0);
        lv_obj_set_style_text_font(s_radio_dd, &lv_font_montserrat_14, LV_PART_INDICATOR);
        lv_dropdown_set_selected(s_radio_dd, (uint16_t)settings->radio_mode);

        lv_obj_t *save = make_button(sub_mode, tr("save_reboot"));
        lv_obj_add_event_cb(save, mode_save_event, LV_EVENT_CLICKED, NULL);

        lv_obj_t *note = make_label(sub_mode, tr("mode_note"), 12, 0xcccccc);
        lv_obj_set_width(note, LV_PCT(100));
        lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    }

    /* ---- OBS ---- */
    {
        s_obs_warn = make_label(sub_obs, tr("obs_router_required"), 12, 0xffcc00);
        lv_obj_set_width(s_obs_warn, LV_PCT(100));
        lv_label_set_long_mode(s_obs_warn, LV_LABEL_LONG_WRAP);

        /* Host / Port / Password on one line, labels above (38% / 20% / 38%). */
        char host[64];
        char pass[64];
        int port = 4455;
        obs_client_get_config(host, sizeof(host), &port, pass, sizeof(pass));

        lv_obj_t *orow = make_columns(sub_obs);
        lv_obj_t *oc1 = make_field_cell(orow, 38, tr("host"));
        s_obs_host_ta = make_ta(oc1, false, host);
        lv_obj_t *oc2 = make_field_cell(orow, 20, tr("port"));
        char portbuf[8];
        snprintf(portbuf, sizeof(portbuf), "%d", port);
        s_obs_port_ta = make_ta(oc2, false, portbuf);
        lv_obj_t *oc3 = make_field_cell(orow, 38, tr("password"));
        s_obs_pass_ta = make_ta(oc3, true, pass);

        lv_obj_t *check = make_button(sub_obs, tr("check_connection"));
        lv_obj_add_event_cb(check, obs_check_event, LV_EVENT_CLICKED, NULL);

        s_obs_result = make_label(sub_obs, "", 14, 0xcccccc);

        lv_obj_t *save = make_button(sub_obs, tr("save"));
        lv_obj_add_event_cb(save, obs_save_event, LV_EVENT_CLICKED, NULL);
    }

    /* ---- General ---- */
    {
        /* 2x2 grid: [brightness | language] / [button sound | sleep timeout]. */
        lv_obj_t *gcols = make_columns(sub_general);
        lv_obj_t *gcolL = make_settings_section(gcols);
        lv_obj_set_width(gcolL, LV_PCT(48));
        lv_obj_t *gcolR = make_settings_section(gcols);
        lv_obj_set_width(gcolR, LV_PCT(48));

        /* Left column: brightness (with value) + button sound */
        lv_obj_t *brow = make_line(gcolL, 26);
        make_label(brow, tr("brightness"), 14, 0xcccccc);
        s_bright_label = make_label(brow, "", 14, 0x00ff88);
        lv_label_set_text_fmt(s_bright_label, "%d%%", settings->brightness);

        s_bright_slider = lv_slider_create(gcolL);
        lv_obj_set_width(s_bright_slider, LV_PCT(100));
        lv_slider_set_range(s_bright_slider, BRIGHTNESS_MIN, BRIGHTNESS_MAX);
        lv_slider_set_value(s_bright_slider, settings->brightness, LV_ANIM_OFF);
        lv_obj_add_event_cb(s_bright_slider, brightness_event, LV_EVENT_VALUE_CHANGED, NULL);

        /* +8px above the key-sound row (extra gap under the brightness slider). */
        lv_obj_t *srow = make_line(gcolL, 52);
        lv_obj_set_style_pad_top(srow, 8, 0);
        make_label(srow, tr("button_sound"), 14, 0xcccccc);
        s_sound_switch = lv_switch_create(srow);
        if (settings->sound_enabled) {
            lv_obj_add_state(s_sound_switch, LV_STATE_CHECKED);
        }
        lv_obj_add_event_cb(s_sound_switch, sound_event, LV_EVENT_VALUE_CHANGED, NULL);

        /* Right column: language + sleep timeout */
        make_label(gcolR, tr("language"), 14, 0xcccccc);
        s_lang_dd = lv_dropdown_create(gcolR);
        lv_dropdown_set_options(s_lang_dd, tr("lang_opts"));
        lv_obj_set_width(s_lang_dd, LV_PCT(100));
        lv_obj_set_style_text_font(s_lang_dd, get_font(14), 0);
        lv_obj_set_style_text_font(lv_dropdown_get_list(s_lang_dd), get_font(14), 0);
        lv_obj_set_style_text_font(s_lang_dd, &lv_font_montserrat_14, LV_PART_INDICATOR);
        lv_dropdown_set_selected(s_lang_dd, (uint16_t)i18n_get_language());
        lv_obj_add_event_cb(s_lang_dd, lang_event, LV_EVENT_VALUE_CHANGED, NULL);

        make_label(gcolR, tr("sleep_timeout"), 14, 0xcccccc);
        s_sleep_dd = lv_dropdown_create(gcolR);
        lv_dropdown_set_options(s_sleep_dd, tr("sleep_opts"));
        lv_obj_set_width(s_sleep_dd, LV_PCT(100));
        lv_obj_set_style_text_font(s_sleep_dd, get_font(14), 0);
        lv_obj_set_style_text_font(lv_dropdown_get_list(s_sleep_dd), get_font(14), 0);
        lv_obj_set_style_text_font(s_sleep_dd, &lv_font_montserrat_14, LV_PART_INDICATOR);
        static const uint16_t vals[] = {60, 180, 300, 600, 0};
        uint16_t sidx = 2;
        for (int i = 0; i < 5; i++) {
            if (vals[i] == settings->sleep_timeout) sidx = (uint16_t)i;
        }
        lv_dropdown_set_selected(s_sleep_dd, sidx);
        lv_obj_add_event_cb(s_sleep_dd, sleep_event, LV_EVENT_VALUE_CHANGED, NULL);

        /* Full-width toggle: show FPS / CPU in the status bar. */
        lv_obj_t *strow = make_line(sub_general, 44);
        make_label(strow, tr("show_stats"), 14, 0xcccccc);
        s_stats_switch = lv_switch_create(strow);
        if (settings->show_stats) {
            lv_obj_add_state(s_stats_switch, LV_STATE_CHECKED);
        }
        lv_obj_add_event_cb(s_stats_switch, stats_event, LV_EVENT_VALUE_CHANGED, NULL);
    }

    /* ---- Configuration: settings for each radio variant ---- */
    {
        /* Bluetooth */
        make_label(sub_config, tr("bluetooth"), 18, 0x667eea);

        lv_obj_t *name = lv_label_create(sub_config);
        /* Show the same name the host sees when pairing (BLE advertising).
         * Connection status is intentionally not repeated here: it is already
         * shown by the Bluetooth icon in the top status bar. */
        lv_label_set_text_fmt(name, "%s: %s", tr("device_name"), get_device_name());
        lv_obj_set_style_text_color(name, lv_color_hex(0xdddddd), 0);
        lv_obj_set_style_text_font(name, get_font(14), 0);

        /* Wi-Fi access point */
        make_label(sub_config, tr("ap_mode"), 18, 0x667eea);
        build_wifi_ap_panel(make_settings_section(sub_config));

        /* Wi-Fi client (OBS) */
        make_label(sub_config, tr("router_mode"), 18, 0x667eea);
        build_wifi_sta_panel(make_settings_section(sub_config));
    }

    /* ---- System: config backup + firmware update ---- */
    {
        /* Config backup on the SD card (modipad/config) */
        make_label(sub_system, tr("backup"), 18, 0x667eea);

        /* Dropdown with the available backups (above the buttons). */
        s_cfg_dd = lv_dropdown_create(sub_system);
        lv_obj_set_width(s_cfg_dd, LV_PCT(100));
        lv_obj_set_style_text_font(s_cfg_dd, get_font(14), 0);
        lv_obj_set_style_text_font(lv_dropdown_get_list(s_cfg_dd), get_font(14), 0);
        lv_obj_set_style_text_font(s_cfg_dd, &lv_font_montserrat_14, LV_PART_INDICATOR);
        cfg_dd_populate();

        /* "Save to SD" and "Import from SD" on one line. */
        lv_obj_t *brow = make_columns(sub_system);
        lv_obj_t *saveb = make_button(brow, tr("save_to_sd"));
        lv_obj_set_width(saveb, LV_PCT(48));
        lv_obj_add_event_cb(saveb, backup_save_event, LV_EVENT_CLICKED, NULL);
        lv_obj_t *impb = make_button(brow, tr("import_from_sd"));
        lv_obj_set_width(impb, LV_PCT(48));
        lv_obj_add_event_cb(impb, backup_import_event, LV_EVENT_CLICKED, NULL);

        /* Firmware update from SD (update.bin, user-confirmed) */
        make_label(sub_system, tr("firmware_sd"), 18, 0x667eea);
        lv_obj_t *otab = make_button(sub_system, tr("update_from_sd"));
        lv_obj_add_event_cb(otab, ota_sd_event, LV_EVENT_CLICKED, NULL);
    }

    /* ---- About: hardware + firmware (left) and system info (right) ---- */
    {
        lv_obj_t *cols = make_columns(sub_about);

        /* Left column: hardware + firmware/versions */
        lv_obj_t *colL = make_settings_section(cols);
        lv_obj_set_width(colL, LV_PCT(48));

        lv_obj_t *hw = make_label(colL, tr("hardware"), 18, 0x667eea);
        (void)hw;
        lv_obj_t *hwi = lv_label_create(colL);
        lv_label_set_text(hwi, tr("hw_text"));
        lv_obj_set_style_text_font(hwi, get_font(14), 0);
        lv_obj_set_style_text_color(hwi, lv_color_hex(0xdddddd), 0);
        lv_obj_set_style_text_line_space(hwi, 4, 0);

        lv_obj_t *fw = make_label(colL, tr("firmware"), 18, 0x667eea);
        lv_obj_set_style_pad_top(fw, 12, 0);

        char build[32];
        format_build(build, sizeof(build));
        char fw_text[192];
        snprintf(fw_text, sizeof(fw_text),
                 "%s: " MODIPAD_FIRMWARE_VERSION "\n"
                 "%s: %s\n"
                 "LVGL: %d.%d.%d\n"
                 "ESP-IDF: %s",
                 tr("version"), tr("build"), build,
                 LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
                 s_sysinfo.idf_version ? s_sysinfo.idf_version : "-");
        lv_obj_t *fwi = lv_label_create(colL);
        lv_label_set_text(fwi, fw_text);
        lv_obj_set_style_text_font(fwi, get_font(14), 0);
        lv_obj_set_style_text_color(fwi, lv_color_hex(0xdddddd), 0);
        lv_obj_set_style_text_line_space(fwi, 4, 0);

        /* Right column: system information (memory, FS, SD, MACs, task stacks) */
        lv_obj_t *colR = make_settings_section(cols);
        lv_obj_set_width(colR, LV_PCT(48));

        system_info_collect(&s_sysinfo);
        lv_obj_t *si = make_label(colR, tr("system_info"), 18, 0x667eea);
        (void)si;

        s_about_label = lv_label_create(colR);
        lv_obj_set_style_text_font(s_about_label, get_font(12), 0);
        lv_obj_set_style_text_color(s_about_label, lv_color_hex(0x9fd0ff), 0);
        lv_obj_set_style_text_line_space(s_about_label, 3, 0);
        lv_obj_set_width(s_about_label, LV_PCT(100));
        lv_label_set_long_mode(s_about_label, LV_LABEL_LONG_WRAP);
        about_update_label();
    }

    settings_page_update_status();

    /* Periodic status refreshes while the page is alive. */
    s_wifi_timer = lv_timer_create(wifi_timer_cb, 1500, NULL);
    s_obs_timer = lv_timer_create(obs_timer_cb, 400, NULL);
    s_sys_timer = lv_timer_create(sys_timer_cb, 2000, NULL);
    s_save_timer = lv_timer_create(settings_save_cb, 400, NULL);
    if (s_save_timer != NULL) {
        lv_timer_pause(s_save_timer);
    }
    wifi_timer_cb(NULL);
    obs_timer_cb(NULL);

    (void)TAG;
}

void settings_page_show_language(void)
{
    /* Language now lives on the General page (2-column layout). */
    show_sub(&s_subs[SUBP_GENERAL]);
}

#ifdef HOST_BUILD
/* Host-only: open a settings sub-page by index (PC screenshot tooling). */
void settings_debug_show_sub(int idx)
{
    if (idx >= 0 && idx < SUBP_MAX && s_subs[idx].sub != NULL) {
        show_sub(&s_subs[idx]);
    }
}
#endif

void go_to_settings(void)
{
    if (s_settings_tab < 0) {
        return;
    }
    /* Go through ui_show_page(): it switches without animation and emits
     * LV_EVENT_VALUE_CHANGED so the status bar title becomes "Settings" (plain
     * lv_tabview_set_act() would leave the previous page name). */
    ui_show_page(s_settings_tab);
}

int get_settings_tab_index(void)
{
    return s_settings_tab;
}
