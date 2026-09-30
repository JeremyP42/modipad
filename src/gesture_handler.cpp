/*
 * gesture_handler.cpp - Touch gestures:
 *   swipe left  -> next page (cyclic)
 *   swipe right -> previous page (cyclic)
 *   swipe down  -> main page
 *   swipe up    -> toggle brightness (min / restore)
 */
#include "gesture_handler.h"

#include "config.h"
#include "display_init.h"
#include "esp_bsp.h"
#include "esp_log.h"
#include "settings_page.h"
#include "ui_renderer.h"

static const char *TAG = "gesture_handler";

static bool s_brightness_minimized = false;
static uint8_t s_saved_brightness = BRIGHTNESS_DEFAULT;

/* Timestamp of the last detected gesture: used to swallow the CLICKED event
 * LVGL also sends to the button under the finger when a swipe starts on it. */
static uint32_t s_last_gesture_ms = 0;
#define GESTURE_CLICK_GUARD_MS 700

/* ---- Release gate (see gesture_handler.h) ----
 * Both the button action callback and the touch read callback run inside the
 * LVGL task, so a plain flag is enough (no locking needed). */
static bool s_input_gate_armed = false;
static uint8_t s_input_gate_releases = 0;

void input_gate_arm(void)
{
    s_input_gate_armed = true;
    s_input_gate_releases = 0;

    /* Tell LVGL to ignore the current press until it is released. This cancels
     * the held press (so it cannot emit CLICKED on release) and stops
     * indev_proc_press from re-searching the object under the finger and
     * transferring the press to the button on the newly shown page. */
    lv_indev_t *indev = lv_indev_get_act();
    if (indev == NULL) {
        indev = bsp_display_get_input_dev();
    }
    lv_indev_wait_release(indev);
}

bool input_gate_armed(void)
{
    return s_input_gate_armed;
}

bool input_gate_filter(bool pressed)
{
    if (!s_input_gate_armed) {
        return pressed;
    }
    if (pressed) {
        /* Still touching: no release seen yet. */
        s_input_gate_releases = 0;
        return false;
    }
    /* Require the released state to persist across two input cycles, so a single
     * noisy "release" read (touch bounce while the finger is still down) cannot
     * reopen the gate and let the next page's button fire. */
    if (++s_input_gate_releases >= 2) {
        s_input_gate_armed = false;
        s_input_gate_releases = 0;
    }
    return false;
}

static void handle_swipe(lv_event_t *e);

static void gesture_event_cb(lv_event_t *e)
{
    s_last_gesture_ms = lv_tick_get();
    handle_swipe(e);
}

bool gesture_swallow_click(void)
{
    /* Any swipe (left/right/up/down) arms this guard (see gesture_event_cb), so
     * a CLICKED emitted by the button under the finger right after the swipe is
     * dropped. Swallow every click inside the window (not just one): a swipe can
     * also leave a second click on the freshly shown page. */
    if (s_last_gesture_ms != 0 && lv_tick_elaps(s_last_gesture_ms) < GESTURE_CLICK_GUARD_MS) {
        return true;
    }
    return false;
}

void init_gestures(void)
{
    lv_obj_t *scr = lv_scr_act();
    /* Do NOT set LV_OBJ_FLAG_GESTURE_BUBBLE on the screen: LVGL delivers the
     * gesture to the first ancestor WITHOUT that flag (lv_indev.c
     * indev_gesture()). With the flag set the walk reaches NULL and the event
     * is dropped, so no swipe ever reaches this handler. Children (pages,
     * buttons, status bar) carry the flag and bubble up to the screen. */
    lv_obj_add_event_cb(scr, gesture_event_cb, LV_EVENT_GESTURE, NULL);
    ESP_LOGI(TAG, "Gesture handler installed");
}

static void set_page(int index)
{
    if (get_ui_tabview() == NULL) {
        return;
    }
    /* ui_show_page builds the page on demand (see ui_renderer.h). */
    ui_show_page(index);
}

static void handle_swipe(lv_event_t *e)
{
    (void)e;

    lv_indev_t *indev = lv_indev_get_act();
    if (indev == NULL) {
        return;
    }

    lv_obj_t *tv = get_ui_tabview();
    if (tv == NULL) {
        return;
    }

    /* Swipes are disabled while the settings page is open. */
    if (is_settings_page_active()) {
        ESP_LOGI(TAG, "Swipe disabled on settings page");
        return;
    }

    int count = get_ui_tab_count();
    if (count < 1) {
        return;
    }
    int act = (int)lv_tabview_get_tab_act(tv);

    switch (lv_indev_get_gesture_dir(indev)) {
    case LV_DIR_LEFT:
        set_page((act + 1) % count);
        ESP_LOGI(TAG, "Swipe left -> page %d", (act + 1) % count);
        break;
    case LV_DIR_RIGHT:
        set_page((act - 1 + count) % count);
        ESP_LOGI(TAG, "Swipe right -> page %d", (act - 1 + count) % count);
        break;
    case LV_DIR_BOTTOM:
        set_page(0);
        ESP_LOGI(TAG, "Swipe down -> main page");
        break;
    case LV_DIR_TOP:
        toggle_brightness();
        break;
    default:
        break;
    }
}

void toggle_brightness(void)
{
    if (s_brightness_minimized) {
        set_brightness(s_saved_brightness);
        s_brightness_minimized = false;
        ESP_LOGI(TAG, "Brightness restored: %u%%", s_saved_brightness);
    } else {
        s_saved_brightness = get_brightness();
        set_brightness(10);
        s_brightness_minimized = true;
        ESP_LOGI(TAG, "Brightness minimized: 10%%");
    }
}
