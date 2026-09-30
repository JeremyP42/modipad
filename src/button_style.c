/*
 * button_style.c - Button style presets and resolution (device + host).
 */
#include "button_style.h"

#include <string.h>

#include "font_manager.h"
#include "ui_loader.h"

/* ------------------------------------------------------------------ */
/* Presets                                                             */
/* ------------------------------------------------------------------ */
/* Shadows are intentionally disabled in every preset: drawing them costs a
 * blur pass per button (very slow on this panel) and the flat look reads fine.
 * Borders are off by default too (an extra blend pass). */
/* Fully initialised on purpose: the text_* values match the defaults applied in
 * button_style_parse(), so a preset used directly behaves the same and the
 * compiler does not warn about missing initializers. */
static const button_style_t kPresets[] = {
    /* name      radius bw  border     bo  sw soy sc        so  tsz tb     tcol      tsd tsc       tss tso */
    {"glass",   12, 0, 0xFFFFFF, 0,   0, 0, 0x000000, 0,  14, false, 0xFFFFFF, 4, 0x000000, 0, 0},
    {"solid",   10, 0, 0xFFFFFF, 0,   0, 0, 0x000000, 0,  14, false, 0xFFFFFF, 4, 0x000000, 0, 0},
    {"flat",    10, 0, 0xFFFFFF, 0,   0, 0, 0x000000, 0,  14, false, 0xFFFFFF, 4, 0x000000, 0, 0},
    {"rounded", 18, 0, 0xFFFFFF, 0,   0, 0, 0x000000, 0,  14, false, 0xFFFFFF, 4, 0x000000, 0, 0},
    {"sharp",   2,  0, 0xFFFFFF, 0,   0, 0, 0x000000, 0,  14, false, 0xFFFFFF, 4, 0x000000, 0, 0},
    {"neon",    14, 1, 0x00E5FF, 100, 0, 0, 0x00E5FF, 0,  14, false, 0xFFFFFF, 4, 0x000000, 0, 0},
    {NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0, 0},
};

const button_style_t *button_style_preset(const char *name)
{
    if (name != NULL) {
        for (int i = 0; kPresets[i].preset != NULL; i++) {
            if (strcmp(kPresets[i].preset, name) == 0) {
                return &kPresets[i];
            }
        }
    }
    return &kPresets[0]; /* glass */
}

/* ------------------------------------------------------------------ */
/* JSON helpers                                                        */
/* ------------------------------------------------------------------ */
static int jint(const cJSON *o, const char *key, int def)
{
    cJSON *i = cJSON_GetObjectItemCaseSensitive((cJSON *)o, key);
    return cJSON_IsNumber(i) ? i->valueint : def;
}

static const char *jstr(const cJSON *o, const char *key, const char *def)
{
    cJSON *i = cJSON_GetObjectItemCaseSensitive((cJSON *)o, key);
    return cJSON_IsString(i) ? i->valuestring : def;
}

static uint32_t parse_hex(const char *s, uint32_t def)
{
    if (s == NULL) {
        return def;
    }
    while (*s == ' ' || *s == '#') {
        s++;
    }
    unsigned int v = 0;
    int n = 0;
    while (n < 6 && s[n] != '\0') {
        char c = s[n];
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return def;
        v = (v << 4) | (uint32_t)d;
        n++;
    }
    return (n == 6) ? v : def;
}

static uint32_t jcolor(const cJSON *o, const char *key, uint32_t def)
{
    cJSON *i = cJSON_GetObjectItemCaseSensitive((cJSON *)o, key);
    return cJSON_IsString(i) ? parse_hex(i->valuestring, def) : def;
}

static void set_text_defaults(button_style_t *st)
{
    st->text_size = 14;
    st->text_bold = false;
    st->text_color = 0xFFFFFF;
    st->text_shadow_dir = 4; /* centre = none */
    st->text_shadow_color = 0x000000;
    st->text_shadow_size = 0;
    st->text_shadow_opa = 0;
}

static void apply_text_obj(button_style_t *st, const cJSON *t)
{
    st->text_size = jint(t, "size", st->text_size);
    cJSON *b = cJSON_GetObjectItemCaseSensitive((cJSON *)t, "bold");
    if (cJSON_IsBool(b)) {
        st->text_bold = cJSON_IsTrue(b);
    } else if (cJSON_IsNumber(b)) {
        st->text_bold = (b->valueint != 0);
    }
    st->text_color = jcolor(t, "color", st->text_color);
    st->text_shadow_dir = jint(t, "shadow_dir", st->text_shadow_dir);
    st->text_shadow_color = jcolor(t, "shadow_color", st->text_shadow_color);
    st->text_shadow_size = jint(t, "shadow_size", st->text_shadow_size);
    st->text_shadow_opa = jint(t, "shadow_strength", st->text_shadow_opa);
}

static void apply_fields(button_style_t *st, const cJSON *obj)
{
    st->radius = jint(obj, "radius", st->radius);
    st->border_width = jint(obj, "border_width", st->border_width);
    st->border_color = jcolor(obj, "border_color", st->border_color);
    st->border_opa = jint(obj, "border_opa", st->border_opa);
    st->shadow_width = jint(obj, "shadow_width", st->shadow_width);
    st->shadow_ofs_y = jint(obj, "shadow_ofs_y", st->shadow_ofs_y);
    st->shadow_color = jcolor(obj, "shadow_color", st->shadow_color);
    st->shadow_opa = jint(obj, "shadow_opa", st->shadow_opa);

    cJSON *tx = cJSON_GetObjectItemCaseSensitive((cJSON *)obj, "text");
    if (cJSON_IsObject(tx)) {
        apply_text_obj(st, tx);
    }
}

/* Look up a named style in config.json "styles" and merge it over its preset. */
static bool style_from_config(const char *name, button_style_t *out)
{
    if (name == NULL) {
        return false;
    }
    cJSON *cfg = get_config();
    cJSON *styles = cfg ? cJSON_GetObjectItemCaseSensitive(cfg, "styles") : NULL;
    cJSON *s = styles ? cJSON_GetObjectItemCaseSensitive(styles, name) : NULL;
    if (!cJSON_IsObject(s)) {
        return false;
    }
    const char *base = jstr(s, "preset", "glass");
    *out = *button_style_preset(base);
    set_text_defaults(out);
    apply_fields(out, s);
    return true;
}

button_style_t button_style_parse(const cJSON *obj)
{
    const char *name = "glass";
    if (cJSON_IsObject((cJSON *)obj)) {
        cJSON *p = cJSON_GetObjectItemCaseSensitive((cJSON *)obj, "preset");
        if (cJSON_IsString(p)) {
            name = p->valuestring;
        }
    }

    button_style_t st;
    if (!style_from_config(name, &st)) {
        st = *button_style_preset(name);
    }
    set_text_defaults(&st);

    if (cJSON_IsObject((cJSON *)obj)) {
        apply_fields(&st, obj);
    }
    return st;
}

button_style_t button_style_resolve(const cJSON *btn_cfg, const cJSON *page_cfg)
{
    /* 1. Per-button style */
    cJSON *st = cJSON_GetObjectItemCaseSensitive((cJSON *)btn_cfg, "style");
    if (cJSON_IsObject(st)) {
        return button_style_parse(st);
    }

    /* 2. Per-page style */
    cJSON *pst = cJSON_GetObjectItemCaseSensitive((cJSON *)page_cfg, "button_style");
    if (cJSON_IsObject(pst)) {
        return button_style_parse(pst);
    }

    /* 3. Global default */
    cJSON *cfg = get_config();
    cJSON *def = cfg ? cJSON_GetObjectItemCaseSensitive(cfg, "defaults") : NULL;
    cJSON *gst = def ? cJSON_GetObjectItemCaseSensitive(def, "button_style") : NULL;
    if (cJSON_IsObject(gst)) {
        return button_style_parse(gst);
    }

    /* 4. Absolute default */
    button_style_t d;
    if (!style_from_config("glass", &d)) {
        d = *button_style_preset("glass");
    }
    set_text_defaults(&d);
    return d;
}

void button_style_apply(lv_obj_t *obj, const button_style_t *st)
{
    if (obj == NULL || st == NULL) {
        return;
    }
    lv_obj_set_style_radius(obj, st->radius, 0);
    lv_obj_set_style_border_width(obj, st->border_width, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(st->border_color), 0);
    lv_obj_set_style_border_opa(obj, (lv_opa_t)(st->border_opa * 255 / 100), 0);
    /* Box shadows are off everywhere (costly); force it even if a config object
     * still carries shadow_* fields. */
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_shadow_opa(obj, LV_OPA_TRANSP, 0);
}

void button_style_apply_text(lv_obj_t *label, const button_style_t *st)
{
    if (label == NULL || st == NULL) {
        return;
    }
    int size = st->text_size;
    if (size < 10 || size > 18) {
        size = 14;
    }
    lv_obj_set_style_text_font(label, st->text_bold ? get_font_bold(size) : get_font(size), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(st->text_color), 0);
}
