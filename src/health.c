/*
 * health.c - Startup diagnostics (see health.h).
 */
#include "health.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "config.h"
#include "i18n.h"
#include "sd_card.h"
#include "ui_assets.h"
#include "ui_loader.h"

static char s_items[HEALTH_MAX_ITEMS][HEALTH_ITEM_LEN];
static int s_count = 0;

void health_add_problem(const char *text)
{
    if (text == NULL || text[0] == '\0' || s_count >= HEALTH_MAX_ITEMS) {
        return;
    }
    strncpy(s_items[s_count], text, HEALTH_ITEM_LEN - 1);
    s_items[s_count][HEALTH_ITEM_LEN - 1] = '\0';
    s_count++;
}

int health_problem_count(void)
{
    return s_count;
}

const char *health_problem(int index)
{
    return (index >= 0 && index < s_count) ? s_items[index] : NULL;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */
static const char *obj_str(const cJSON *o, const char *key, const char *def)
{
    if (!cJSON_IsObject((cJSON *)o)) {
        return def;
    }
    cJSON *v = cJSON_GetObjectItemCaseSensitive((cJSON *)o, key);
    return cJSON_IsString(v) ? v->valuestring : def;
}

/* A referenced image is "missing" when it is not present in the internal
 * (LittleFS) store. The SD card is intentionally ignored: it is external media
 * (a source of new files), not part of the device's own asset set. */
static bool internal_has(const char *name)
{
    char buf[192];
    if (asset_internal_resolve(name, 100, 100, buf, sizeof(buf))) {
        return true;
    }
    if (asset_internal_resolve(name, 70, 70, buf, sizeof(buf))) {
        return true;
    }
    return asset_internal_resolve(name, 64, 64, buf, sizeof(buf));
}

static void check_image(const char *name, char *names, size_t cap, int *count)
{
    if (name == NULL || name[0] == '\0') {
        return;
    }
    if (internal_has(name)) {
        return;
    }
    (*count)++;
    if (names[0] != '\0' && strlen(names) + 2 < cap) {
        strncat(names, ", ", cap - strlen(names) - 1);
    }
    strncat(names, name, cap - strlen(names) - 1);
}

static void scan_page(const cJSON *page, char *names, size_t cap, int *count)
{
    if (!cJSON_IsObject((cJSON *)page)) {
        return;
    }
    cJSON *bg = cJSON_GetObjectItemCaseSensitive((cJSON *)page, "background");
    if (cJSON_IsObject(bg) && strcmp(obj_str(bg, "type", ""), "image") == 0) {
        check_image(obj_str(bg, "image", NULL), names, cap, count);
    }
    cJSON *buttons = cJSON_GetObjectItemCaseSensitive((cJSON *)page, "buttons");
    cJSON *btn = NULL;
    cJSON_ArrayForEach(btn, buttons) {
        check_image(obj_str(btn, "icon", NULL), names, cap, count);
        cJSON *b = cJSON_GetObjectItemCaseSensitive(btn, "background");
        if (cJSON_IsObject(b) && strcmp(obj_str(b, "type", ""), "image") == 0) {
            check_image(obj_str(b, "image", NULL), names, cap, count);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Checks                                                             */
/* ------------------------------------------------------------------ */
void health_check_all(void)
{
    s_count = 0;

    /* SD card: absent, or nearly full (< 10% free). */
    bool present = false;
    uint32_t total_kb = 0, free_kb = 0;
    sd_card_info(&present, &total_kb, &free_kb);
    if (!present) {
        health_add_problem(tr("problem_no_sd"));
    } else if (total_kb > 0 && (uint64_t)free_kb * 100u / total_kb < 10u) {
        health_add_problem(tr("problem_sd_low"));
    }

    /* Config file was missing or damaged (defaults were written). */
    if (ui_config_was_repaired()) {
        health_add_problem(tr("problem_config"));
    }

    /* Images / icons referenced by the config but not found on the device. */
    cJSON *cfg = get_config();
    if (cfg != NULL) {
        char names[160];
        names[0] = '\0';
        int missing = 0;
        scan_page(cJSON_GetObjectItemCaseSensitive(cfg, "main_page"), names, sizeof(names), &missing);
        cJSON *pages = cJSON_GetObjectItemCaseSensitive(cfg, "pages");
        cJSON *p = NULL;
        cJSON_ArrayForEach(p, pages) {
            scan_page(p, names, sizeof(names), &missing);
        }
        if (missing > 0) {
            char line[HEALTH_ITEM_LEN];
            snprintf(line, sizeof(line), tr("problem_missing_fmt"), missing, names);
            health_add_problem(line);
        }
    }
}
