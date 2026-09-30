/*
 * button_style.h - Button style presets and resolution (device + host).
 *
 * Inheritance hierarchy (most specific wins):
 *   buttons[i].style  ->  pages[].button_style  ->  defaults.button_style  ->  glass
 *
 * A style may name a custom style from config.json "styles" (by "preset" name);
 * that object is merged over the built-in preset and can also carry a nested
 * "text" block (font size, bold, colour, shadow) used when a button shows text.
 */
#ifndef MODIPAD_BUTTON_STYLE_H
#define MODIPAD_BUTTON_STYLE_H

#include <stdbool.h>
#include <stdint.h>

#include "cJSON.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *preset;
    int radius;         /* px */
    int border_width;   /* px */
    uint32_t border_color;
    int border_opa;     /* 0..100 % */
    int shadow_width;   /* px */
    int shadow_ofs_y;   /* px */
    uint32_t shadow_color;
    int shadow_opa;     /* 0..100 % */

    /* Text content styling (content == "text"). */
    int text_size;       /* px (10..18), default 14 */
    bool text_bold;
    uint32_t text_color;
    int text_shadow_dir; /* 0..8 (3x3 grid), 4 = none */
    uint32_t text_shadow_color;
    int text_shadow_size;/* px offset */
    int text_shadow_opa; /* 0..100 % */
} button_style_t;

/* Known presets: glass, solid, flat, rounded, sharp, neon. Unknown -> glass. */
const button_style_t *button_style_preset(const char *name);

/* Parse a style object (merges its "preset" + custom config style + overrides).
 * `obj` may be NULL -> the glass preset. */
button_style_t button_style_parse(const cJSON *obj);

/* Resolve the effective style for a button using the hierarchy. */
button_style_t button_style_resolve(const cJSON *btn_cfg, const cJSON *page_cfg);

/* Apply the (box) style to a button container object. */
void button_style_apply(lv_obj_t *obj, const button_style_t *st);

/* Apply the text style to a label (font/colour). */
void button_style_apply_text(lv_obj_t *label, const button_style_t *st);

#ifdef __cplusplus
}
#endif

#endif /* MODIPAD_BUTTON_STYLE_H */
