/*
 * splash_screen.cpp - Boot splash with progress and (optional) multilingual text.
 */
#include "splash_screen.h"

#include "config.h"
#include "esp_bsp.h"
#include "esp_log.h"
#include "font_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ui_assets.h"
#include "ui_loader.h"

static const char *TAG = "splash";

static lv_obj_t *s_splash = NULL;
static lv_obj_t *s_progress = NULL;
static lv_obj_t *s_status = NULL;
static lv_obj_t *s_percent = NULL;
static lv_obj_t *s_title = NULL;
static lv_obj_t *s_subtitle = NULL;

static const char *kTitle[] = {
    "ModiPAD",
    "ModiPAD",
};

static const char *kSubtitle[] = {
    "Hotkey Keyboard",
    "Клавиатура с горячими клавишами",
};

static const char *kStatus[2][SPLASH_STATUS_COUNT] = {
    { /* English */
      "Initializing...",
      "Loading display...",
      "Loading touch...",
      "Loading configuration...",
      "Loading interface...",
      "Connecting keyboard...",
      "Starting web server...",
      "Ready!" },
    { /* Russian (needs a Cyrillic font to render) */
      "Инициализация...",
      "Загрузка дисплея...",
      "Загрузка тача...",
      "Загрузка конфигурации...",
      "Загрузка интерфейса...",
      "Подключение клавиатуры...",
      "Запуск веб-сервера...",
      "Готово!" },
};

/* 0 = English, 1 = Russian. Default English (Montserrat has no Cyrillic). */
static int s_lang = 0;

void set_splash_language(bool russian)
{
    s_lang = russian ? 1 : 0;
}

static const char *status_text(splash_status_t status)
{
    if (status < 0 || status >= SPLASH_STATUS_COUNT) {
        status = SPLASH_STATUS_INIT;
    }
    return kStatus[s_lang][status];
}

void show_splash_screen(void)
{
    bsp_display_lock(0);

    s_splash = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_splash, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_center(s_splash);
    lv_obj_set_style_radius(s_splash, 0, 0);
    lv_obj_set_style_border_width(s_splash, 0, 0);
    lv_obj_set_style_pad_all(s_splash, 0, 0);
    lv_obj_set_style_bg_color(s_splash, lv_color_hex(0x121212), 0);
    lv_obj_clear_flag(s_splash, LV_OBJ_FLAG_SCROLLABLE);

    /* Early splash: no filesystem and no custom-font access, so it can be shown
     * right after the display is up (before LittleFS/Roboto are ready). The
     * background image and Roboto are applied later by
     * splash_screen_apply_assets(). */
    s_title = lv_label_create(s_splash);
    lv_label_set_text(s_title, kTitle[s_lang]);
    lv_obj_set_style_text_color(s_title, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_title, get_font(18), 0);
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, -70);

    s_subtitle = lv_label_create(s_splash);
    lv_label_set_text(s_subtitle, kSubtitle[s_lang]);
    lv_obj_set_style_text_color(s_subtitle, lv_color_hex(0xcccccc), 0);
    lv_obj_set_style_text_font(s_subtitle, get_font(14), 0);
    lv_obj_align(s_subtitle, LV_ALIGN_CENTER, 0, -35);

    s_status = lv_label_create(s_splash);
    lv_label_set_text(s_status, status_text(SPLASH_STATUS_INIT));
    lv_obj_set_style_text_color(s_status, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(s_status, get_font(14), 0);
    lv_obj_align(s_status, LV_ALIGN_CENTER, 0, 30);

    s_progress = lv_bar_create(s_splash);
    lv_obj_set_size(s_progress, 300, 12);
    lv_obj_align(s_progress, LV_ALIGN_CENTER, 0, 60);
    lv_obj_set_style_bg_color(s_progress, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_progress, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_progress, lv_color_hex(0x00ff88), LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_progress, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(s_progress, 6, LV_PART_INDICATOR);
    lv_bar_set_range(s_progress, 0, 100);
    lv_bar_set_value(s_progress, 0, LV_ANIM_OFF);

    s_percent = lv_label_create(s_splash);
    lv_label_set_text(s_percent, "0%");
    lv_obj_set_style_text_color(s_percent, lv_color_hex(0x888888), 0);
    lv_obj_align(s_percent, LV_ALIGN_CENTER, 160, 60);

    /* Draw the splash immediately so the user sees it from the first moment. */
    lv_refr_now(NULL);

    bsp_display_unlock();
}

/* Apply the "rich" splash assets once LittleFS and the Roboto fonts are ready:
 * the configured background image, the real font and the saved language. */
void splash_screen_apply_assets(void)
{
    if (s_splash == NULL) {
        return;
    }

    bsp_display_lock(0);

    const AppSettings *settings = get_settings();
    if (settings != NULL) {
        asset_create_bg_image(s_splash, settings->splash_bg, LCD_WIDTH, LCD_HEIGHT, "cover", 100);
    }

    lv_obj_set_style_text_font(s_title, get_font(18), 0);
    lv_obj_set_style_text_font(s_subtitle, get_font(14), 0);
    lv_obj_set_style_text_font(s_status, get_font(14), 0);
    lv_label_set_text(s_title, kTitle[s_lang]);
    lv_label_set_text(s_subtitle, kSubtitle[s_lang]);
    lv_label_set_text(s_status, status_text(SPLASH_STATUS_INIT));

    lv_refr_now(NULL);
    bsp_display_unlock();
}

void update_progress(int percent, splash_status_t status)
{
    if (s_splash == NULL) {
        return;
    }
    if (percent < 0) {
        percent = 0;
    }
    if (percent > 100) {
        percent = 100;
    }

    bsp_display_lock(0);

    lv_bar_set_value(s_progress, percent, LV_ANIM_OFF);
    lv_label_set_text(s_status, status_text(status));
    lv_label_set_text_fmt(s_percent, "%d%%", percent);

    lv_color_t color;
    if (percent < 30) {
        color = lv_color_hex(0xff4444);
    } else if (percent < 70) {
        color = lv_color_hex(0xffaa00);
    } else {
        color = lv_color_hex(0x00ff88);
    }
    lv_obj_set_style_bg_color(s_progress, color, LV_PART_INDICATOR);

    /* Draw this step synchronously: otherwise the LVGL refresh coalesces fast
     * updates and intermediate percentages are never shown (only the last). */
    lv_refr_now(NULL);

    bsp_display_unlock();

    ESP_LOGI(TAG, "%d%% - %s", percent, status_text(status));
    vTaskDelay(pdMS_TO_TICKS(60));
}

void close_splash_screen(void)
{
    if (s_splash == NULL) {
        return;
    }

    bsp_display_lock(0);
    lv_obj_del(s_splash);
    s_splash = NULL;
    bsp_display_unlock();
}
