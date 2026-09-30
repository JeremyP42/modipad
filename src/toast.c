/*
 * toast.c - Small transient message overlay (see toast.h).
 */
#include "toast.h"

#include <string.h>

#include "config.h"
#include "esp_bsp.h"
#include "font_manager.h"
#include "lvgl.h"

static lv_obj_t *s_toast = NULL;
static lv_timer_t *s_timer = NULL;

static void toast_delete_cb(lv_event_t *e)
{
    (void)e;
    /* Widget was deleted by a screen rebuild; drop the stale pointer. */
    s_toast = NULL;
}

static void toast_timer_cb(lv_timer_t *timer)
{
    lv_obj_t *obj = (lv_obj_t *)timer->user_data;
    if (obj != NULL && lv_obj_is_valid(obj)) {
        lv_obj_del(obj);
    }
    s_toast = NULL;
    s_timer = NULL;
    lv_timer_del(timer);
}

void toast_show(const char *message)
{
    if (message == NULL || message[0] == '\0') {
        return;
    }

    /* The display mutex is recursive, so this also works from the LVGL task.
     * Bounded: never let a toast freeze the caller if the LVGL task is busy. */
    if (!bsp_display_lock(50)) {
        return;
    }

    if (s_timer != NULL) {
        lv_timer_del(s_timer);
        s_timer = NULL;
    }
    if (s_toast != NULL && lv_obj_is_valid(s_toast)) {
        lv_obj_del(s_toast);
    }
    s_toast = NULL;

    lv_obj_t *lbl = lv_label_create(lv_layer_top());
    lv_label_set_text(lbl, message);
    lv_obj_set_style_bg_color(lbl, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_80, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, get_font(14), 0);
    lv_obj_set_style_pad_all(lbl, 10, 0);
    lv_obj_set_style_radius(lbl, 8, 0);
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -18);

    s_toast = lbl;
    lv_obj_add_event_cb(lbl, toast_delete_cb, LV_EVENT_DELETE, NULL);
    s_timer = lv_timer_create(toast_timer_cb, 2200, lbl);

    bsp_display_unlock();
}
