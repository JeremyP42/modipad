/*
 * ui_renderer.cpp - Builds the LVGL UI from config.json (schema v3).
 *
 * Hotkey/main buttons (schema v3):
 *   [ background image (pre-sized gradient or custom "cover" image) ]
 *   [ icon (transparent PNG), centred, optional                     ]
 *   caption under the button: one line, 12 px, centred, #CCCCCC
 *
 * Matrices are limited to (rows x cols) in {2x3, 2x4, 3x3, 3x4}; buttons are
 * square (side = min(102, per-row height)) and the spare space becomes larger
 * gaps. Legacy v2 configs ("grid", "text": {"lines": [...]}) are still accepted.
 */
#include "ui_renderer.h"

#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "button_style.h"
#include "esp_log.h"
#include "font_manager.h"
#include "gesture_handler.h"
#include "i18n.h"
#include "keyboard_manager.h"
#include "macro_player.h"
#include "obs_client.h"
#include "settings_page.h"
#include "toast.h"
#include "ui_assets.h"
#include "ui_loader.h"

static const char *TAG = "ui_renderer";

static lv_obj_t *s_tabview = NULL;

/* ---- geometry ---- */
#define STATUS_BAR_H 30
#define CAPTION_H    16   /* fallback reserved height for the caption + gap */
#define GAP_V         8
#define BTN_MAX     100   /* max square side for 2-row matrices (2x3 / 2x4) */

/* Caption font comes from settings.json ("caption_font_size" px and
 * "caption_font_bold"). Bold is drawn by thickening the white label. */
static int s_caption_px = 12;
static bool s_caption_bold = false;
static int caption_h(void) { return s_caption_px + 4; }

/* ---- global button style (redesign) ---- */
static lv_style_t s_style_btn;
static lv_style_t s_style_btn_pr;
static bool s_style_ready = false;

static void ensure_btn_styles(void)
{
    if (s_style_ready) {
        return;
    }
    lv_style_init(&s_style_btn);
    /* Base only: fallback background + no padding. radius/border/shadow come
     * from the resolved button style (see button_style_apply). */
    lv_style_set_bg_color(&s_style_btn, lv_color_hex(0x2C3E50));
    lv_style_set_bg_opa(&s_style_btn, LV_OPA_COVER);
    lv_style_set_pad_all(&s_style_btn, 0);

    lv_style_init(&s_style_btn_pr);
    lv_style_set_transform_width(&s_style_btn_pr, -4);
    lv_style_set_transform_height(&s_style_btn_pr, -4);

    s_style_ready = true;
}

/* Explicit opaque page background style. */
static lv_style_t s_style_page;
static bool s_style_page_ready = false;
static void ensure_page_style(void)
{
    if (s_style_page_ready) {
        return;
    }
    lv_style_init(&s_style_page);
    lv_style_set_bg_color(&s_style_page, lv_color_hex(0x1a1a2e));
    lv_style_set_bg_opa(&s_style_page, LV_OPA_COVER);
    lv_style_set_radius(&s_style_page, 0);
    lv_style_set_pad_all(&s_style_page, 0);
    s_style_page_ready = true;
}

typedef enum {
    ACT_HOTKEY,
    ACT_TEXT,
    ACT_MACRO,
    ACT_PAGE,
    ACT_SETTINGS,
    ACT_MULTIMEDIA,
    ACT_OBS,
} action_type_t;

typedef struct {
    action_type_t type;
    char *value;
    int target_page;
    bool has_steps; /* ACT_MACRO: value is a JSON step array (v3) */
} button_action_t;

/* ------------------------------------------------------------------ */
/* Small JSON helpers                                                 */
/* ------------------------------------------------------------------ */
static cJSON *jobj(cJSON *parent, const char *key)
{
    return parent ? cJSON_GetObjectItemCaseSensitive(parent, key) : NULL;
}

static int jint(cJSON *parent, const char *key, int def)
{
    cJSON *item = jobj(parent, key);
    return cJSON_IsNumber(item) ? item->valueint : def;
}

static const char *jstr(cJSON *parent, const char *key, const char *def)
{
    cJSON *item = jobj(parent, key);
    return cJSON_IsString(item) ? item->valuestring : def;
}

static bool jbool(cJSON *parent, const char *key, bool def)
{
    cJSON *item = jobj(parent, key);
    return cJSON_IsBool(item) ? cJSON_IsTrue(item) : def;
}

static int find_page_index(const char *id)
{
    if (id == NULL || get_config() == NULL) {
        return -1;
    }

    /* main_page (when present) is always tab 0; "__HOME__"/"main" targets it. */
    int base = (jobj(get_config(), "main_page") != NULL) ? 1 : 0;
    if (strcmp(id, "__HOME__") == 0 || strcmp(id, "main") == 0 || strcmp(id, "Main") == 0) {
        return 0;
    }

    cJSON *pages = jobj(get_config(), "pages");
    int index = 0;
    cJSON *page = NULL;
    cJSON_ArrayForEach(page, pages) {
        const char *page_id = jstr(page, "id", NULL);
        if (page_id != NULL && strcmp(page_id, id) == 0) {
            return base + index;
        }
        index++;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Button events                                                      */
/* ------------------------------------------------------------------ */
static void send_macro(const char *value)
{
    char buf[192];
    strncpy(buf, value, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *saveptr = NULL;
    for (char *tok = strtok_r(buf, ";", &saveptr); tok != NULL;
         tok = strtok_r(NULL, ";", &saveptr)) {
        while (*tok == ' ') {
            tok++;
        }
        if (*tok != '\0') {
            send_hotkey(tok);
        }
    }
}

static void button_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *btn = lv_event_get_target(e);
    button_action_t *action = (button_action_t *)lv_obj_get_user_data(btn);

    if (action == NULL) {
        return;
    }

    if (code == LV_EVENT_DELETE) {
        free(action->value);
        free(action);
        return;
    }

    if (code != LV_EVENT_CLICKED) {
        return;
    }

    /* A swipe that started on this button also emits CLICKED; drop that click
     * so a swipe down (to main) does not press a button as well. */
    if (gesture_swallow_click()) {
        return;
    }

    switch (action->type) {
    case ACT_TEXT:
        ESP_LOGI(TAG, "Text action: \"%s\"", action->value);
        send_text(action->value);
        break;
    case ACT_MACRO:
        if (action->has_steps) {
            ESP_LOGI(TAG, "Macro (steps) action");
            if (!macro_play_json(action->value)) {
                toast_show(tr("macro_busy"));
            }
        } else {
            ESP_LOGI(TAG, "Macro action: \"%s\"", action->value);
            send_macro(action->value);
        }
        break;
    case ACT_MULTIMEDIA:
        ESP_LOGI(TAG, "Multimedia action: \"%s\"", action->value);
        send_multimedia(action->value);
        break;
    case ACT_OBS:
        ESP_LOGI(TAG, "OBS action: \"%s\"", action->value);
        obs_client_command(action->value);
        break;
    case ACT_PAGE:
        ESP_LOGI(TAG, "Page action -> %d", action->target_page);
        if (action->target_page >= 0) {
            ui_show_page(action->target_page);
        }
        break;
    case ACT_SETTINGS:
        ESP_LOGI(TAG, "Open settings");
        go_to_settings();
        break;
    case ACT_HOTKEY:
    default:
        ESP_LOGI(TAG, "Hotkey action: \"%s\"", action->value);
        send_hotkey(action->value);
        break;
    }

    /* Require the finger to be lifted before the next press is accepted, so a
     * page switch (or any action) cannot also fire the button that appears
     * under the same spot on the newly shown page. */
    input_gate_arm();
}

/* ------------------------------------------------------------------ */
/* Geometry                                                           */
/* ------------------------------------------------------------------ */
/* Square buttons: side = min(BTN_MAX, per-row height). The remaining space is
 * distributed evenly so wider gaps appear in the 2x4 / 3x4 matrices. */
static void grid_geometry(int rows, int cols, int *side, int *gap_h, int *gap_v)
{
    int cap_h = caption_h();
    int row_h = (LCD_HEIGHT - STATUS_BAR_H - rows * cap_h - (rows + 1) * GAP_V) / rows;
    int s = (BTN_MAX < row_h) ? BTN_MAX : row_h;
    if (s < 8) {
        s = 8;
    }
    *side = s;
    *gap_h = (LCD_WIDTH - cols * s) / (cols + 1);
    if (*gap_h < 0) {
        *gap_h = 0;
    }
    int used_v = rows * s + rows * cap_h;
    *gap_v = (LCD_HEIGHT - STATUS_BAR_H - used_v) / (rows + 1);
    if (*gap_v < 0) {
        *gap_v = 0;
    }
}

static void clamp_matrix(int *rows, int *cols)
{
    int r = *rows;
    int c = *cols;
    if (*rows < 2) *rows = 2;
    if (*rows > 3) *rows = 3;
    if (*cols < 3) *cols = 3;
    if (*cols > 4) *cols = 4;
    if (r != *rows || c != *cols) {
        ESP_LOGW(TAG, "Matrix %dx%d not supported, using %dx%d", r, c, *rows, *cols);
    }
}

static void caption_label(lv_obj_t *page, int x, int y, int w, const char *text, lv_color_t color)
{
    lv_obj_t *lbl = lv_label_create(page);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, s_caption_bold ? get_font_bold(s_caption_px) : get_font(s_caption_px), 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    /* Show the caption in full: size the label to its text and centre it under
     * the button. It is allowed to overflow the button - the caption length is
     * up to the user (no wrap, no "..."). */
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_size(lbl, LV_SIZE_CONTENT, caption_h());
    lv_obj_update_layout(lbl);
    int lw = lv_obj_get_width(lbl);
    lv_obj_set_pos(lbl, x + (w - lw) / 2, y);
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
}

/* One-line caption. No text shadow/outline: it was expensive (extra labels and
 * render passes) and is not worth the cost on this device. */
static void add_caption(lv_obj_t *page, int x, int y, int w, const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }
    caption_label(page, x, y, w, text, lv_color_hex(0xFFFFFF));
}

/* ------------------------------------------------------------------ */
/* OBS button: red overlay that blinks while OBS is recording          */
/* ------------------------------------------------------------------ */
typedef struct {
    lv_obj_t *overlay;
    lv_obj_t *icon;
    const char *idle_path;
    const char *rec_path;
    lv_timer_t *timer;
    lv_anim_t anim;
    bool anim_started;
} obs_visual_t;

static void obs_anim_cb(void *var, int32_t value)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)var, (lv_opa_t)value, 0);
}

static void obs_visual_update(obs_visual_t *v)
{
    if (v == NULL) {
        return;
    }
    bool recording = (obs_client_get_state() == OBS_STATE_RECORDING);

    /* Preferred: swap the button icon and tint it red while recording. */
    if (v->icon != NULL && v->rec_path != NULL) {
        if (lv_obj_is_valid(v->icon)) {
            lv_img_set_src(v->icon, recording ? v->rec_path : v->idle_path);
            lv_obj_set_style_img_recolor(v->icon, lv_color_hex(0xFF3030), 0);
            lv_obj_set_style_img_recolor_opa(v->icon, recording ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        }
        return;
    }

    if (v->overlay == NULL) {
        return;
    }

    if (recording) {
        lv_obj_clear_flag(v->overlay, LV_OBJ_FLAG_HIDDEN);
        if (!v->anim_started) {
            lv_anim_init(&v->anim);
            lv_anim_set_var(&v->anim, v->overlay);
            lv_anim_set_exec_cb(&v->anim, obs_anim_cb);
            lv_anim_set_values(&v->anim, 25, 190);
            lv_anim_set_time(&v->anim, 500);
            lv_anim_set_playback_time(&v->anim, 500);
            lv_anim_set_repeat_count(&v->anim, LV_ANIM_REPEAT_INFINITE);
            lv_anim_start(&v->anim);
            v->anim_started = true;
        }
    } else {
        if (v->anim_started) {
            lv_anim_del(v->overlay, obs_anim_cb);
            v->anim_started = false;
        }
        lv_obj_add_flag(v->overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void obs_timer_cb(lv_timer_t *timer)
{
    obs_visual_update((obs_visual_t *)timer->user_data);
}

static void obs_visual_delete_cb(lv_event_t *e)
{
    obs_visual_t *v = (obs_visual_t *)lv_event_get_user_data(e);
    if (v == NULL) {
        return;
    }
    if (v->timer != NULL) {
        lv_timer_del(v->timer);
    }
    if (v->anim_started && v->overlay != NULL) {
        lv_anim_del(v->overlay, obs_anim_cb);
    }
    free(v);
}

/*
 * Attach recording-state feedback to an OBS button. If a "record" icon is
 * configured the button icon is swapped (and tinted red) while recording;
 * otherwise a blinking red overlay is used.
 */
static void create_obs_visual(lv_obj_t *button, lv_obj_t *icon,
                              const char *idle_name, const char *rec_name)
{
    obs_visual_t *v = (obs_visual_t *)calloc(1, sizeof(obs_visual_t));
    if (v == NULL) {
        return;
    }

    v->icon = icon;
    if (idle_name != NULL && idle_name[0] != '\0') {
        v->idle_path = asset_persist_path(idle_name, 64, 64);
    }
    if (rec_name != NULL && rec_name[0] != '\0') {
        v->rec_path = asset_persist_path(rec_name, 64, 64);
    }

    /* Fall back to the blinking overlay when icon swapping is not possible. */
    if (v->icon == NULL || v->rec_path == NULL) {
        v->overlay = lv_obj_create(button);
        lv_obj_set_size(v->overlay, LV_PCT(100), LV_PCT(100));
        lv_obj_set_pos(v->overlay, 0, 0);
        lv_obj_set_style_radius(v->overlay, 0, 0);
        lv_obj_set_style_border_width(v->overlay, 0, 0);
        lv_obj_set_style_bg_color(v->overlay, lv_color_hex(0xFF2020), 0);
        lv_obj_set_style_bg_opa(v->overlay, 120, 0);
        lv_obj_clear_flag(v->overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(v->overlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(v->overlay, LV_OBJ_FLAG_HIDDEN);
    }

    v->timer = lv_timer_create(obs_timer_cb, 300, v);
    lv_obj_add_event_cb(button, obs_visual_delete_cb, LV_EVENT_DELETE, v);
    obs_visual_update(v);
}

/* ------------------------------------------------------------------ */
/* Button                                                             */
/* ------------------------------------------------------------------ */
void create_button(lv_obj_t *parent, cJSON *btn_cfg, const button_style_t *style,
                   int x, int y, int width, int height, int rows)
{
    cJSON *bg = jobj(btn_cfg, "background");
    const char *bg_type = jstr(bg, "type", "gradient");
    const char *bg_image = jstr(bg, "image", "");

    lv_obj_t *button = lv_obj_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    /* Keep the press pinned to this button while it is held. Without it LVGL
     * re-searches the object under the finger on every input cycle; after a
     * page switch that is the button sitting at the same spot on the newly
     * shown page, which then fires from the same (held) touch. */
    lv_obj_add_flag(button, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_clip_corner(button, true, 0);

    ensure_btn_styles();
    lv_obj_add_style(button, &s_style_btn, 0);
    lv_obj_add_style(button, &s_style_btn_pr, LV_STATE_PRESSED);
    button_style_apply(button, style);

    /* Background: pre-sized gradient from buttons/<W>x<H>/ or a custom image
     * scaled to cover the button. */
    if (btn_cfg != NULL && strcmp(bg_type, "transparent") == 0) {
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    } else if (btn_cfg != NULL && strcmp(bg_type, "solid") == 0) {
        lv_color_t color = asset_parse_color(jstr(bg, "color", "#2C3E50"), lv_color_hex(0x2C3E50));
        lv_obj_set_style_bg_color(button, color, 0);
        lv_obj_set_style_bg_opa(button, (lv_opa_t)((jint(bg, "opacity", 100) * 255) / 100), 0);
    } else if (btn_cfg != NULL && strcmp(bg_type, "gradient") == 0) {
        /* Native LVGL gradient: no PNG decode and no image blit - much cheaper
         * than a background image. */
        lv_color_t c1 = asset_parse_color(jstr(bg, "color", "#2C3E50"), lv_color_hex(0x2C3E50));
        lv_color_t c2 = asset_parse_color(jstr(bg, "color2", jstr(bg, "gradient_color", "#1a1a2e")), c1);
        const char *dir = jstr(bg, "direction", jstr(bg, "gradient_dir", "vertical"));
        lv_obj_set_style_bg_color(button, c1, 0);
        lv_obj_set_style_bg_grad_color(button, c2, 0);
        lv_obj_set_style_bg_grad_dir(button,
                                     (strcmp(dir, "horizontal") == 0) ? LV_GRAD_DIR_HOR : LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(button, (lv_opa_t)((jint(bg, "opacity", 100) * 255) / 100), 0);
    } else if (bg_image[0] != '\0') {
        asset_create_bg_image(button, bg_image, width, height, "cover", jint(bg, "opacity", 100));
    }

    /* Content: an icon from the library, or styled text. */
    const char *content = jstr(btn_cfg, "content", "icon");
    bool text_mode = (content != NULL && strcmp(content, "text") == 0);

    lv_obj_t *icon_obj = NULL;
    const char *icon_name = jstr(btn_cfg, "icon", NULL);
    if (text_mode) {
        const char *txt = jstr(btn_cfg, "label", NULL);
        if (txt == NULL) {
            txt = jstr(btn_cfg, "content_text", NULL);
        }
        if (txt != NULL && txt[0] != '\0') {
            /* Emulated text shadow: offset copies behind the main label.
             * Direction 4 (centre) = shadow all around (8 offsets). */
            if (style != NULL && style->text_shadow_size > 0 && style->text_shadow_opa > 0) {
                static const int8_t kDirs[8][2] = {
                    {-1, -1}, {0, -1}, {1, -1}, {-1, 0},
                    {1, 0}, {-1, 1}, {0, 1}, {1, 1},
                };
                int count = (style->text_shadow_dir == 4) ? 8 : 1;
                lv_opa_t opa = (lv_opa_t)(style->text_shadow_opa * 255 / 100);
                for (int s = 0; s < count; s++) {
                    int dx, dy;
                    if (count == 8) {
                        dx = kDirs[s][0] * style->text_shadow_size;
                        dy = kDirs[s][1] * style->text_shadow_size;
                    } else {
                        dx = ((style->text_shadow_dir % 3) - 1) * style->text_shadow_size;
                        dy = ((style->text_shadow_dir / 3) - 1) * style->text_shadow_size;
                    }
                    lv_obj_t *sh = lv_label_create(button);
                    lv_label_set_text(sh, txt);
                    button_style_apply_text(sh, style);
                    lv_obj_set_style_text_color(sh, lv_color_hex(style->text_shadow_color), 0);
                    lv_obj_set_style_text_opa(sh, opa, 0);
                    lv_obj_align(sh, LV_ALIGN_CENTER, dx, dy);
                    lv_obj_clear_flag(sh, LV_OBJ_FLAG_CLICKABLE);
                }
            }
            lv_obj_t *lb = lv_label_create(button);
            lv_label_set_text(lb, txt);
            button_style_apply_text(lb, style);
            lv_obj_set_style_text_align(lb, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(lb, LV_ALIGN_CENTER, 0, 0);
            lv_obj_clear_flag(lb, LV_OBJ_FLAG_CLICKABLE);
        }
    } else if (icon_name != NULL && icon_name[0] != '\0') {
        const char *ipath = asset_persist_path(icon_name, 64, 64);
        if (ipath != NULL) {
            lv_img_header_t hh;
            memset(&hh, 0, sizeof(hh));
            lv_res_t r = lv_img_decoder_get_info(ipath, &hh);
            lv_obj_t *ic = lv_img_create(button);
            lv_img_set_src(ic, ipath);
            if (r == LV_RES_OK && hh.w > 0) {
                int target = (rows >= 3) ? 36 : 56;
                lv_img_set_zoom(ic, (uint16_t)((target * 256) / hh.w));
            }
            lv_obj_center(ic);
            lv_obj_clear_flag(ic, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_clear_flag(ic, LV_OBJ_FLAG_SCROLLABLE);
            icon_obj = ic;
        }
    }

    /* Caption (one line), under the button. Migrates v2 "text.lines". No length
     * limit: the full string from config.json is shown (get_config() stays alive
     * for the whole UI lifetime; lv_label_set_text copies it). */
    const char *caption = NULL;
    cJSON *cap = jobj(btn_cfg, "caption");
    if (cJSON_IsString(cap)) {
        caption = cap->valuestring;
    } else if (cJSON_IsObject(cap) && jbool(cap, "enabled", false)) {
        caption = jstr(cap, "text", "");
    }
    if (caption == NULL || caption[0] == '\0') {
        cJSON *lines = jobj(jobj(btn_cfg, "text"), "lines");
        if (cJSON_IsArray(lines) && cJSON_GetArraySize(lines) > 0) {
            cJSON *first = cJSON_GetArrayItem(lines, 0);
            if (cJSON_IsString(first)) {
                caption = first->valuestring;
            }
        }
    }
    add_caption(parent, x, y + height + 2, width, caption);

    /* Action / button type (unchanged). */
    const char *btype = jstr(btn_cfg, "type", NULL);
    cJSON *action_cfg = jobj(btn_cfg, "action");
    const char *action_type = jstr(action_cfg, "type", "hotkey");

    button_action_t *action = (button_action_t *)calloc(1, sizeof(button_action_t));
    if (action != NULL) {
        if (btype != NULL && strcmp(btype, "settings") == 0) {
            action->type = ACT_SETTINGS;
            action->value = strdup("");
        } else if (btype != NULL && strcmp(btype, "page_link") == 0) {
            action->type = ACT_PAGE;
            action->target_page = find_page_index(jstr(btn_cfg, "target_page",
                                                       jstr(action_cfg, "target", "")));
            action->value = strdup(jstr(btn_cfg, "target_page", ""));
        } else if (strcmp(action_type, "multimedia") == 0) {
            action->type = ACT_MULTIMEDIA;
            const char *value = jstr(action_cfg, "value", NULL);
            if (value == NULL) value = jstr(action_cfg, "keys", NULL);
            action->value = strdup(value != NULL ? value : "");
        } else if (strcmp(action_type, "obs") == 0) {
            action->type = ACT_OBS;
            const char *cmd = jstr(action_cfg, "command", "TOGGLE_REC");
            action->value = strdup(cmd != NULL ? cmd : "TOGGLE_REC");
        } else {
            if (strcmp(action_type, "text") == 0) {
                action->type = ACT_TEXT;
            } else if (strcmp(action_type, "macro") == 0) {
                action->type = ACT_MACRO;
                cJSON *steps = jobj(action_cfg, "steps");
                if (cJSON_IsArray(steps)) {
                    char *js = cJSON_PrintUnformatted(steps);
                    if (js != NULL) {
                        action->value = strdup(js);
                        action->has_steps = true;
                        cJSON_free(js);
                    }
                }
            } else if (strcmp(action_type, "page") == 0) {
                action->type = ACT_PAGE;
                action->target_page = find_page_index(jstr(action_cfg, "target",
                                                           jstr(action_cfg, "keys", "")));
            } else {
                action->type = ACT_HOTKEY;
            }
            if (action->value == NULL) {
                const char *value = jstr(action_cfg, "keys", NULL);
                if (value == NULL) value = jstr(action_cfg, "text", NULL);
                if (value == NULL) value = jstr(action_cfg, "target", "");
                action->value = strdup(value);
            }
        }
        if (action->value == NULL) {
            action->value = strdup("");
        }
        lv_obj_set_user_data(button, action);
        if (action->type == ACT_OBS) {
            create_obs_visual(button, icon_obj, icon_name,
                              jstr(action_cfg, "icon_rec", NULL));
        }
    }

    lv_obj_add_event_cb(button, button_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(button, button_event_cb, LV_EVENT_DELETE, NULL);
}

/* ------------------------------------------------------------------ */
/* Page - tabs are registered up front but their widgets are built      */
/* lazily, the first time the tab is shown. Creating all 8 pages at     */
/* boot needlessly allocates/keeps hundreds of widgets and their        */
/* (possibly full-screen) images.                                       */
/* ------------------------------------------------------------------ */
#define MAX_UI_PAGES 16
static lv_obj_t *s_tabs[MAX_UI_PAGES];
static cJSON *s_page_cfgs[MAX_UI_PAGES];
static bool s_tab_built[MAX_UI_PAGES];
static int s_tab_count = 0;
static bool s_building_tab = false;

/* Build the background + matrix + buttons of a page into an existing tab. */
static void build_page_content(lv_obj_t *tab, cJSON *page_cfg)
{
    cJSON *bg = jobj(page_cfg, "background");
    const char *bg_type = jstr(bg, "type", NULL);
    if (bg_type != NULL && strcmp(bg_type, "solid") == 0) {
        lv_obj_set_style_bg_color(tab, asset_parse_color(jstr(bg, "color", "#1a1a1a"),
                                                         lv_color_hex(0x1a1a1a)), 0);
    } else if (bg_type != NULL && strcmp(bg_type, "gradient") == 0) {
        lv_color_t c1 = asset_parse_color(jstr(bg, "color", "#2C3E50"), lv_color_hex(0x2C3E50));
        lv_color_t c2 = asset_parse_color(jstr(bg, "color2", jstr(bg, "gradient_color", "#1a1a2e")), c1);
        const char *dir = jstr(bg, "direction", jstr(bg, "gradient_dir", "vertical"));
        lv_obj_set_style_bg_color(tab, c1, 0);
        lv_obj_set_style_bg_grad_color(tab, c2, 0);
        lv_obj_set_style_bg_grad_dir(tab, (strcmp(dir, "horizontal") == 0) ? LV_GRAD_DIR_HOR : LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_bg_color(tab, lv_color_hex(0x1a1a2e), 0);
        lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
        asset_create_bg_image(tab, jstr(bg, "image", "age_bg_dark.png"),
                              LCD_WIDTH, LCD_HEIGHT, "cover", jint(bg, "opacity", 100));
    }

    /* Matrix: v3 "matrix" or legacy "grid". Only 2x3/2x4/3x3/3x4 are allowed. */
    cJSON *matrix = jobj(page_cfg, "matrix");
    if (matrix == NULL) {
        matrix = jobj(page_cfg, "grid");
    }
    int rows = jint(matrix, "rows", 2);
    int cols = jint(matrix, "cols", 4);
    clamp_matrix(&rows, &cols);

    int side = 0;
    int gap_h = 0;
    int gap_v = 0;
    grid_geometry(rows, cols, &side, &gap_h, &gap_v);

    cJSON *buttons = jobj(page_cfg, "buttons");
    cJSON *btn = NULL;
    cJSON_ArrayForEach(btn, buttons) {
        cJSON *pos = jobj(btn, "position");
        int row = jint(pos, "row", 0);
        int col = jint(pos, "col", 0);
        if (row < 0 || row >= rows || col < 0 || col >= cols) {
            ESP_LOGW(TAG, "Button (%d,%d) outside %dx%d grid, skipped", row, col, rows, cols);
            continue;
        }
        int x = gap_h + col * (side + gap_h);
        int y = STATUS_BAR_H + gap_v + row * (side + caption_h() + gap_v);
        button_style_t st = button_style_resolve(btn, page_cfg);
        create_button(tab, btn, &st, x, y, side, side, rows);
    }
}

/* Add the tab (cheap) and remember its config for later content build. */
static void register_tab(lv_obj_t *parent, cJSON *page_cfg)
{
    if (page_cfg == NULL || s_tab_count >= MAX_UI_PAGES) {
        return;
    }
    const char *name = jstr(page_cfg, "display_name", jstr(page_cfg, "name", "Page"));
    lv_obj_t *tab = lv_tabview_add_tab(parent, name);

    lv_obj_set_style_pad_all(tab, 0, 0);
    lv_obj_set_style_border_width(tab, 0, 0);
    lv_obj_set_style_bg_opa(tab, LV_OPA_COVER, 0);
    lv_obj_clear_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(tab, LV_OBJ_FLAG_GESTURE_BUBBLE);

    ensure_page_style();
    lv_obj_add_style(tab, &s_style_page, 0);

    s_tabs[s_tab_count] = tab;
    s_page_cfgs[s_tab_count] = page_cfg;
    s_tab_built[s_tab_count] = false;
    s_tab_count++;
}

static void build_tab_index(uint16_t idx)
{
    if (idx >= (uint16_t)s_tab_count || s_tab_built[idx] || s_building_tab) {
        return;
    }
    s_building_tab = true;
    s_tab_built[idx] = true;
    ESP_LOGI(TAG, "Lazy build tab %u", (unsigned)idx);
    build_page_content(s_tabs[idx], s_page_cfgs[idx]);
    s_building_tab = false;
}

static void lazy_tab_event_cb(lv_event_t *e)
{
    lv_obj_t *tv = lv_event_get_target(e);
    build_tab_index(lv_tabview_get_tab_act(tv));
}

void ui_show_page(int index)
{
    if (s_tabview == NULL || index < 0) {
        return;
    }
    /* Build before scrolling so the content is ready when the tab appears.
     * (Beware: lv_tabview_set_act() itself never sends VALUE_CHANGED, so the
     * lazy build must be triggered here, not from the tabview event.) */
    build_tab_index((uint16_t)index);

    /* Switch WITHOUT animation: the panel is full-refresh-only, so every
     * animation frame costs a full-screen render + software rotate + QSPI
     * transfer. The tabview widget has a built-in "snap to nearest tab" handler
     * that runs on LV_EVENT_SCROLL_END with LV_ANIM_ON (hardcoded in lvgl's
     * lv_tabview.c); cancel any such scroll animation, then jump instantly. */
    lv_obj_t *content = lv_tabview_get_content(s_tabview);
    if (content != NULL) {
        lv_anim_del(content, NULL);
    }
    lv_tabview_set_act(s_tabview, (uint16_t)index, LV_ANIM_OFF);

    /* lv_tabview_set_act() does not emit VALUE_CHANGED, and the widget's only
     * other emitter (the native scroll-snap in lv_tabview.c) bails out while the
     * finger is still pressed - i.e. during a swipe. So emit it ourselves after
     * every programmatic switch; the status bar (title) and lazy builder listen
     * for it and then follow BOTH button and swipe navigation. */
    lv_event_send(s_tabview, LV_EVENT_VALUE_CHANGED, NULL);

    /* The page changed: require the finger to be lifted before the next press
     * is accepted, so the button that lands under the same point on the new
     * page cannot fire from the same touch (applies to button and swipe nav). */
    input_gate_arm();
}

/* Kept for external callers: register then build immediately. */
void create_page(lv_obj_t *parent, cJSON *page_cfg)
{
    register_tab(parent, page_cfg);
    build_tab_index((uint16_t)(s_tab_count - 1));
}

void create_ui(void)
{
    cJSON *config = get_config();
    if (config == NULL) {
        ESP_LOGE(TAG, "No config loaded, cannot build UI");
        return;
    }

    const AppSettings *st = get_settings();
    if (st != NULL) {
        if (st->caption_font_size >= 10 && st->caption_font_size <= 18) {
            s_caption_px = st->caption_font_size;
        }
        s_caption_bold = st->caption_font_bold;
    }

    s_tabview = lv_tabview_create(lv_scr_act(), LV_DIR_TOP, 0);
    lv_obj_set_style_bg_color(s_tabview, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_tabview, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_tabview, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);

    /* Navigation is handled by gesture_handler, so block native swiping
     * (programmatic lv_tabview_set_act() still works). */
    lv_obj_t *content = lv_tabview_get_content(s_tabview);
    if (content != NULL) {
        lv_obj_set_scroll_dir(content, LV_DIR_NONE);
        lv_obj_add_flag(content, LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_set_style_bg_color(content, lv_color_hex(0x1a1a2e), 0);
        lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    }

    s_tab_count = 0;

    cJSON *main_page = jobj(config, "main_page");
    if (main_page != NULL) {
        register_tab(s_tabview, main_page);
    }

    cJSON *pages = jobj(config, "pages");
    cJSON *page = NULL;
    cJSON_ArrayForEach(page, pages) {
        register_tab(s_tabview, page);
    }

    /* Build only the initially visible tab; the rest are built on first show. */
    build_tab_index(0);
    lv_obj_add_event_cb(s_tabview, lazy_tab_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    ESP_LOGI(TAG, "UI created (%d tabs, lazily-filled)", get_ui_tab_count());
}

lv_obj_t *get_ui_tabview(void)
{
    return s_tabview;
}

int get_ui_tab_count(void)
{
    if (s_tabview == NULL) {
        return 0;
    }
    lv_obj_t *content = lv_tabview_get_content(s_tabview);
    return content ? (int)lv_obj_get_child_cnt(content) : 0;
}

const char *ui_page_name(int index)
{
    cJSON *config = get_config();
    if (config == NULL) {
        return "";
    }

    int i = 0;
    cJSON *main_page = jobj(config, "main_page");
    if (main_page != NULL) {
        if (i == index) {
            return jstr(main_page, "display_name", jstr(main_page, "name", "Main"));
        }
        i++;
    }

    cJSON *pages = jobj(config, "pages");
    cJSON *page = NULL;
    cJSON_ArrayForEach(page, pages) {
        if (i == index) {
            return jstr(page, "display_name", jstr(page, "name", "Page"));
        }
        i++;
    }
    return "";
}
