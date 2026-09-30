/*
 * splash_screen.h - Boot splash with progress and (optional) multilingual text.
 *
 * NOTE: the on-device font is LVGL Montserrat, which has no Cyrillic glyphs, so
 * the Russian strings render as blanks on the LCD. The API is bilingual, but the
 * device defaults to English until a Cyrillic font is added (see datadevice/fonts).
 */
#ifndef MODIPAD_SPLASH_SCREEN_H
#define MODIPAD_SPLASH_SCREEN_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SPLASH_STATUS_INIT = 0,
    SPLASH_STATUS_DISPLAY,
    SPLASH_STATUS_TOUCH,
    SPLASH_STATUS_CONFIG,
    SPLASH_STATUS_UI,
    SPLASH_STATUS_KEYBOARD,
    SPLASH_STATUS_WEBSERVER,
    SPLASH_STATUS_READY,
    SPLASH_STATUS_COUNT
} splash_status_t;

void set_splash_language(bool russian);
void show_splash_screen(void);
/* Apply the background image + Roboto font once LittleFS/fonts are ready. */
void splash_screen_apply_assets(void);
void update_progress(int percent, splash_status_t status);
void close_splash_screen(void);

#ifdef __cplusplus
}
#endif

#endif /* MODIPAD_SPLASH_SCREEN_H */
