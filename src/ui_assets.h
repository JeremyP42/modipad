/*
 * ui_assets.h - Helpers for loading images/backgrounds from LittleFS and for
 * mapping JSON style values to LVGL types.
 */
#ifndef MODIPAD_UI_ASSETS_H
#define MODIPAD_UI_ASSETS_H

#include <stdbool.h>
#include <stddef.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Resolve a library image name (e.g. "gradient_blue.png") to an LVGL path
 * ("S:/images/...") that exists on LittleFS. Searches the button-size cache
 * first (WxH), then the template/page/system/icon folders.
 */
bool asset_resolve_image(const char *name, int w, int h, char *out, size_t out_len);

/*
 * Like asset_resolve_image() but returns a pointer into an internal pool that
 * stays valid for the process lifetime. LVGL keeps the image-source pointer
 * (it does not copy the string), so this MUST be used for lv_img_set_src() /
 * lv_obj_set_style_bg_img_src(). Returns NULL if the image was not found.
 */
const char *asset_persist_path(const char *name, int w, int h);

/* Create an image object filling w x h and push it to the background. */
lv_obj_t *asset_create_bg_image(lv_obj_t *parent, const char *name, int w, int h,
                                const char *scale_mode, int opacity);

/* Parse "#RRGGBB"; returns fallback on error. */
lv_color_t asset_parse_color(const char *hex, lv_color_t fallback);

/* True when a LittleFS VFS path exists. */
bool asset_vfs_exists(const char *vfs_path);

/*
 * Copy every image/icon referenced by the current config (and settings) from
 * the SD card into the device (LittleFS) if it is missing there. Returns the
 * number of files copied. Safe to call repeatedly.
 */
int asset_sync_used(void);

/* Drop the cached directory listings (call after the FS changes, e.g. sync). */
void asset_dir_cache_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* MODIPAD_UI_ASSETS_H */
