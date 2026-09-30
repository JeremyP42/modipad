/*
 * ui_assets.cpp - Image/background helpers backed by LittleFS (via the LVGL
 * POSIX filesystem driver, drive letter 'S' -> /littlefs).
 */
#include "ui_assets.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "esp_log.h"
#include "font_manager.h"

#include "cJSON.h"
#include "ui_loader.h"

#ifndef HOST_BUILD
#include "esp_heap_caps.h"
/* Directory listings go to PSRAM (internal SRAM is scarce). */
#define asset_dir_realloc(p, n) heap_caps_realloc((p), (n), MALLOC_CAP_SPIRAM)
#define asset_dir_free(p)       heap_caps_free(p)
#else
#define asset_dir_realloc(p, n) realloc((p), (n))
#define asset_dir_free(p)       free(p)
#endif

static const char *TAG = "ui_assets";

#define LFS_PREFIX "/littlefs"

bool asset_vfs_exists(const char *vfs_path)
{
    FILE *f = fopen(vfs_path, "rb");
    if (f == NULL) {
        return false;
    }
    fclose(f);
    return true;
}

/* ------------------------------------------------------------------ */
/* Directory listing cache                                            */
/*                                                                    */
/* Resolving an image used to fopen() up to ~6 candidate paths, and    */
/* every lookup scanned the whole directory (hundreds of files on      */
/* LittleFS), which dominated the first page build (~140 ms/image).    */
/* Each directory is listed once and membership is answered from RAM.  */
/* ------------------------------------------------------------------ */
#define DIR_CACHE_MAX 24
typedef struct {
    char   dir[160];
    char  *blob;      /* newline-separated file names (PSRAM) */
    size_t len;
    bool   loaded;
    bool   exists;
} dir_cache_t;

static dir_cache_t s_dir_cache[DIR_CACHE_MAX];
static int s_dir_cache_n = 0;

static bool blob_has(const char *blob, size_t len, const char *name)
{
    if (blob == NULL) {
        return false;
    }
    size_t n = strlen(name);
    const char *p = blob;
    const char *end = blob + len;
    while (p < end) {
        const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p));
        size_t l = nl ? (size_t)(nl - p) : (size_t)(end - p);
        if (l == n && memcmp(p, name, n) == 0) {
            return true;
        }
        if (nl == NULL) {
            break;
        }
        p = nl + 1;
    }
    return false;
}

static void dir_cache_load(dir_cache_t *d)
{
    d->loaded = true;
    DIR *dir = opendir(d->dir);
    if (dir == NULL) {
        d->exists = false;
        return;
    }
    d->exists = true;

    size_t cap = 0;
    size_t len = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        const char *n = ent->d_name;
        if (n == NULL || n[0] == '\0') {
            continue;
        }
        size_t nl = strlen(n);
        if (len + nl + 2 > cap) {
            size_t ncap = cap ? cap * 2 : 2048;
            while (ncap < len + nl + 2) {
                ncap *= 2;
            }
            char *nb = (char *)asset_dir_realloc(d->blob, ncap);
            if (nb == NULL) {
                break;
            }
            d->blob = nb;
            cap = ncap;
        }
        memcpy(d->blob + len, n, nl);
        len += nl;
        d->blob[len++] = '\n';
    }
    closedir(dir);
    if (d->blob != NULL) {
        d->blob[len] = '\0';
    }
    d->len = len;
    ESP_LOGD(TAG, "cached dir %s (%u bytes)", d->dir, (unsigned)len);
}

static bool dir_contains(const char *vfs_dir, const char *name)
{
    dir_cache_t *d = NULL;
    for (int i = 0; i < s_dir_cache_n; i++) {
        if (strcmp(s_dir_cache[i].dir, vfs_dir) == 0) {
            d = &s_dir_cache[i];
            break;
        }
    }
    if (d == NULL) {
        if (s_dir_cache_n >= DIR_CACHE_MAX) {
            /* Cache full: fall back to a direct existence check. */
            char p[192];
            snprintf(p, sizeof(p), "%s/%s", vfs_dir, name);
            return asset_vfs_exists(p);
        }
        d = &s_dir_cache[s_dir_cache_n++];
        strncpy(d->dir, vfs_dir, sizeof(d->dir) - 1);
        d->dir[sizeof(d->dir) - 1] = '\0';
        d->blob = NULL;
        d->len = 0;
        d->loaded = false;
        d->exists = false;
    }
    if (!d->loaded) {
        dir_cache_load(d);
    }
    if (!d->exists) {
        return false;
    }
    return blob_has(d->blob, d->len, name);
}

static bool try_candidate(const char *dir, const char *name, char *out, size_t out_len)
{
    char dpath[160];
    char lvgl[192];

    if (dir == NULL || dir[0] == '\0') {
        snprintf(dpath, sizeof(dpath), "%s/images", LFS_PREFIX);
        snprintf(lvgl, sizeof(lvgl), "S:/images/%s", name);
    } else {
        snprintf(dpath, sizeof(dpath), "%s/images/%s", LFS_PREFIX, dir);
        snprintf(lvgl, sizeof(lvgl), "S:/images/%s/%s", dir, name);
    }

    if (dir_contains(dpath, name)) {
        strncpy(out, lvgl, out_len - 1);
        out[out_len - 1] = '\0';
        return true;
    }
    return false;
}

/* SD-card candidates (media on the card): /sdcard/<sub>/<name> -> D:/<sub>/<name>. */
#define SD_PREFIX "/sdcard/modipad"

static bool try_sd(const char *sub, const char *name, char *out, size_t out_len)
{
    char dpath[160];
    char lvgl[192];

    if (sub == NULL || sub[0] == '\0') {
        snprintf(dpath, sizeof(dpath), "%s", SD_PREFIX);
        snprintf(lvgl, sizeof(lvgl), "D:/%s", name);
    } else {
        snprintf(dpath, sizeof(dpath), "%s/%s", SD_PREFIX, sub);
        snprintf(lvgl, sizeof(lvgl), "D:/%s/%s", sub, name);
    }

    if (dir_contains(dpath, name)) {
        strncpy(out, lvgl, out_len - 1);
        out[out_len - 1] = '\0';
        return true;
    }
    return false;
}

/* Mirror of the device images tree kept on the SD card:
 * /sdcard/modipad/images/<dir>/<name> -> D:/images/<dir>/<name>. */
static bool try_sd_images(const char *dir, const char *name, char *out, size_t out_len)
{
    char dpath[160];
    char lvgl[192];

    if (dir == NULL || dir[0] == '\0') {
        snprintf(dpath, sizeof(dpath), "/sdcard/modipad/images");
        snprintf(lvgl, sizeof(lvgl), "D:/images/%s", name);
    } else {
        snprintf(dpath, sizeof(dpath), "/sdcard/modipad/images/%s", dir);
        snprintf(lvgl, sizeof(lvgl), "D:/images/%s/%s", dir, name);
    }

    if (dir_contains(dpath, name)) {
        strncpy(out, lvgl, out_len - 1);
        out[out_len - 1] = '\0';
        return true;
    }
    return false;
}

/* Look in the device FS first, then in the SD mirror of the images tree. */
static bool try_dir(const char *dir, const char *name, char *out, size_t out_len)
{
    return try_candidate(dir, name, out, out_len) ||
           try_sd_images(dir, name, out, out_len);
}

bool asset_resolve_image(const char *name, int w, int h, char *out, size_t out_len)
{
    if (name == NULL || name[0] == '\0' || out == NULL || out_len == 0) {
        return false;
    }

    char dir[64];

    /* Button-size cache first: buttons/<W>x<H>/ */
    snprintf(dir, sizeof(dir), "buttons/%dx%d", w, h);
    if (try_dir(dir, name, out, out_len)) {
        return true;
    }

    /* If the name already contains a sub-path, try it directly. */
    if (strchr(name, '/') != NULL) {
        if (try_dir(NULL, name, out, out_len)) {
            return true;
        }
    }

    static const char *kDirs[] = {
        "pages",
        "icons/system",
        "icons/buttons",
        "icons/actions",
        "icons/pages",
        NULL, /* /images root */
    };

    for (int i = 0; kDirs[i] != NULL; i++) {
        if (try_dir(kDirs[i], name, out, out_len)) {
            return true;
        }
    }
    if (try_dir(NULL, name, out, out_len)) {
        return true;
    }

    /* SD card fallback for media kept on the card. */
    static const char *kSdDirs[] = { "backgrounds", NULL };
    for (int i = 0; kSdDirs[i] != NULL; i++) {
        if (try_sd(kSdDirs[i], name, out, out_len)) {
            return true;
        }
    }
    if (try_sd(NULL, name, out, out_len)) {
        return true;
    }

    ESP_LOGD(TAG, "Image not found: %s", name);
    return false;
}

/* Persistent path pool: LVGL keeps image-source pointers, so the strings must
 * outlive the widget. */
#define ASSET_PATH_SLOTS 192
#define ASSET_PATH_LEN   192
static char s_path_pool[ASSET_PATH_SLOTS][ASSET_PATH_LEN];
static int s_path_pool_next = 0;

const char *asset_persist_path(const char *name, int w, int h)
{
    int slot = s_path_pool_next;
    s_path_pool_next = (s_path_pool_next + 1) % ASSET_PATH_SLOTS;
    if (asset_resolve_image(name, w, h, s_path_pool[slot], ASSET_PATH_LEN)) {
        return s_path_pool[slot];
    }
    return NULL;
}

lv_obj_t *asset_create_bg_image(lv_obj_t *parent, const char *name, int w, int h,
                                const char *scale_mode, int opacity)
{
    const char *path = asset_persist_path(name, w, h);
    if (path == NULL) {
        return NULL;
    }

    lv_obj_t *img = lv_img_create(parent);
    lv_img_set_src(img, path);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(img, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(img, LV_OBJ_FLAG_IGNORE_LAYOUT);

    /* Compute a uniform zoom based on the requested scaling mode.
     * (Read the header once; it used to be fetched twice, i.e. one extra file
     * open per image during page builds.) */
    lv_img_header_t header;
    uint32_t zoom = 256;
    if ((w > 0) && (h > 0) && (lv_img_decoder_get_info(path, &header) == LV_RES_OK) &&
        (header.w > 0) && (header.h > 0)) {
        uint32_t zx = (uint32_t)((w * 256) / header.w);
        uint32_t zy = (uint32_t)((h * 256) / header.h);
        if (scale_mode != NULL && strcmp(scale_mode, "contain") == 0) {
            zoom = (zx < zy) ? zx : zy;
        } else if (scale_mode != NULL && strcmp(scale_mode, "center") == 0) {
            zoom = 256;
        } else {
            /* cover (default), stretch and tile all use fill-cover here. */
            zoom = (zx > zy) ? zx : zy;
        }
    }

    lv_img_set_zoom(img, (uint16_t)zoom);
    lv_obj_set_size(img, w, h);
    lv_obj_center(img);
    if (opacity >= 0 && opacity <= 100) {
        lv_obj_set_style_img_opa(img, (lv_opa_t)((opacity * 255) / 100), 0);
    }
    lv_obj_move_background(img);
    return img;
}

lv_color_t asset_parse_color(const char *hex, lv_color_t fallback)
{
    if (hex == NULL) {
        return fallback;
    }
    while (*hex == ' ' || *hex == '#') {
        hex++;
    }
    unsigned int value = 0;
    if (sscanf(hex, "%6x", &value) != 1) {
        return fallback;
    }
    return lv_color_hex(value);
}

/* ------------------------------------------------------------------ */
/* Used-asset sync                                                    */
/*                                                                    */
/* The full image library lives on the SD card; the device only needs  */
/* what its config actually uses. This copies any used asset that is   */
/* missing from the device out of the SD mirror into a small directory */
/* the resolver probes first.                                          */
/* ------------------------------------------------------------------ */
static const char *kProbeDirs[] = {
    "pages", "icons/system", "icons/buttons", "icons/actions", "icons/pages",
    "buttons/100x100", "buttons/70x70", NULL
};

void asset_dir_cache_reset(void)
{
    for (int i = 0; i < s_dir_cache_n; i++) {
        if (s_dir_cache[i].blob != NULL) {
            asset_dir_free(s_dir_cache[i].blob);
            s_dir_cache[i].blob = NULL;
        }
        s_dir_cache[i].len = 0;
        s_dir_cache[i].loaded = false;
        s_dir_cache[i].exists = false;
    }
    s_dir_cache_n = 0;
}

static bool lfs_has_image(const char *name)
{
    char dpath[160];
    for (int i = 0; kProbeDirs[i] != NULL; i++) {
        snprintf(dpath, sizeof(dpath), "%s/images/%s", LFS_PREFIX, kProbeDirs[i]);
        if (dir_contains(dpath, name)) {
            return true;
        }
    }
    snprintf(dpath, sizeof(dpath), "%s/images", LFS_PREFIX);
    return dir_contains(dpath, name);
}

static bool copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    if (in == NULL) {
        return false;
    }
    FILE *out = fopen(dst, "wb");
    if (out == NULL) {
        fclose(in);
        return false;
    }
    char buf[1024];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            ok = false;
            break;
        }
    }
    fclose(in);
    fclose(out);
    return ok;
}

#define ASSET_SYNC_MAX 256
static void used_add(const char **list, int *n, const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return;
    }
    const char *slash = strrchr(name, '/');
    const char *base = slash ? slash + 1 : name;
    for (int i = 0; i < *n; i++) {
        if (strcmp(list[i], base) == 0) {
            return;
        }
    }
    if (*n < ASSET_SYNC_MAX) {
        list[(*n)++] = base; /* points into the (stable) config strings */
    }
}

static void used_collect_page(cJSON *page, const char **list, int *n)
{
    if (!cJSON_IsObject(page)) {
        return;
    }
    cJSON *bg = cJSON_GetObjectItemCaseSensitive(page, "background");
    cJSON *img = bg ? cJSON_GetObjectItemCaseSensitive(bg, "image") : NULL;
    if (cJSON_IsString(img)) {
        used_add(list, n, img->valuestring);
    }
    cJSON *btn = NULL;
    cJSON_ArrayForEach(btn, cJSON_GetObjectItemCaseSensitive(page, "buttons")) {
        cJSON *bb = cJSON_GetObjectItemCaseSensitive(btn, "background");
        cJSON *bi = bb ? cJSON_GetObjectItemCaseSensitive(bb, "image") : NULL;
        if (cJSON_IsString(bi)) {
            used_add(list, n, bi->valuestring);
        }
        cJSON *ic = cJSON_GetObjectItemCaseSensitive(btn, "icon");
        if (cJSON_IsString(ic)) {
            used_add(list, n, ic->valuestring);
        }
        cJSON *act = cJSON_GetObjectItemCaseSensitive(btn, "action");
        cJSON *rec = act ? cJSON_GetObjectItemCaseSensitive(act, "icon_rec") : NULL;
        if (cJSON_IsString(rec)) {
            used_add(list, n, rec->valuestring);
        }
    }
}

int asset_sync_used(void)
{
    const char *used[ASSET_SYNC_MAX];
    int n = 0;

    cJSON *cfg = get_config();
    if (cfg != NULL) {
        used_collect_page(cJSON_GetObjectItemCaseSensitive(cfg, "main_page"), used, &n);
        cJSON *p = NULL;
        cJSON_ArrayForEach(p, cJSON_GetObjectItemCaseSensitive(cfg, "pages")) {
            used_collect_page(p, used, &n);
        }
    }
    const AppSettings *st = get_settings();
    if (st != NULL) {
        used_add(used, &n, st->splash_bg);
        used_add(used, &n, st->settings_bg);
        used_add(used, &n, st->menu_bg);
    }
    used_add(used, &n, "icon_bt.png");
    used_add(used, &n, "icon_bt_off.png");
    used_add(used, &n, "icon_wifi.png");
    used_add(used, &n, "icon_wifi_off.png");
    /* System icons referenced directly by the settings page. */
    used_add(used, &n, "icon_globe.png");
    used_add(used, &n, "icon_home.png");
    used_add(used, &n, "icon_info.png");
    used_add(used, &n, "icon_wrench.png");
    used_add(used, &n, "icon_obsstudio.png");

    int copied = 0;
    for (int i = 0; i < n; i++) {
        if (lfs_has_image(used[i])) {
            continue;
        }
        char src[192];
        bool found = false;
        for (int d = 0; kProbeDirs[d] != NULL && !found; d++) {
            snprintf(src, sizeof(src), "/sdcard/modipad/images/%s/%s", kProbeDirs[d], used[i]);
            found = asset_vfs_exists(src);
        }
        if (!found) {
            snprintf(src, sizeof(src), "/sdcard/modipad/backgrounds/%s", used[i]);
            found = asset_vfs_exists(src);
        }
        if (!found) {
            snprintf(src, sizeof(src), "/sdcard/modipad/images/%s", used[i]);
            found = asset_vfs_exists(src);
        }
        if (!found) {
            ESP_LOGW(TAG, "sync: no SD source for %s", used[i]);
            continue;
        }
        /* pages is probed for every asset type and stays small. */
        char dst[192];
        snprintf(dst, sizeof(dst), "%s/images/pages/%s", LFS_PREFIX, used[i]);
        if (copy_file(src, dst)) {
            copied++;
            ESP_LOGI(TAG, "sync: %s -> device", used[i]);
        }
    }
    asset_dir_cache_reset();
    return copied;
}
