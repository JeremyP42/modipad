/*
 * main_host.cpp - SDL2 simulator for the ModiPAD UI.
 *
 * Uses SDL2 (local tools/sdl2) directly - no lv_drivers dependency. Renders the
 * REAL project UI code (ui_renderer, splash_screen, status_bar, settings_page,
 * gesture_handler, ui_loader, ui_assets) with LVGL 8.4.
 *
 * Mouse = touch, drag = swipe (handled here via the public tabview helpers).
 */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <lvgl.h>
#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "display_init.h"
#include "gesture_handler.h"
#include "i18n.h"
#include "settings_page.h"
#include "splash_screen.h"
#include "status_bar.h"
#include "ui_loader.h"
#include "ui_renderer.h"
#include "ui_assets.h"
#define HOR_RES 480
#define VER_RES 320

static SDL_Window *s_win = NULL;
static SDL_Renderer *s_ren = NULL;
static SDL_Texture *s_tex = NULL;
static uint32_t s_pixels[HOR_RES * VER_RES];

static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t s_buf1[HOR_RES * 40];
static lv_color_t s_buf2[HOR_RES * 40];

static bool s_pressed = false;
static int s_mx = 0, s_my = 0, s_press_x = 0, s_press_y = 0;

/* --- LVGL flush -> SDL texture --- */
static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *cp)
{
    int w = area->x2 - area->x1 + 1;
    for (int y = area->y1; y <= area->y2; y++) {
        for (int x = area->x1; x <= area->x2; x++) {
            s_pixels[y * HOR_RES + x] = lv_color_to32(cp[(y - area->y1) * w + (x - area->x1)]) & 0xFFFFFFu;
        }
    }
    SDL_UpdateTexture(s_tex, NULL, s_pixels, HOR_RES * (int)sizeof(uint32_t));
    SDL_RenderClear(s_ren);
    SDL_RenderCopy(s_ren, s_tex, NULL, NULL);
    SDL_RenderPresent(s_ren);
    lv_disp_flush_ready(drv);
}

static void mouse_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    data->point.x = s_mx;
    data->point.y = s_my;
    data->state = s_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* Debug helper: write the framebuffer to a 24-bit BMP. */
static void dump_bmp(const char *path)
{
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) return;
    int row = (HOR_RES * 3 + 3) & ~3;
    int filesize = 54 + row * VER_RES;
    unsigned char hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    *(int *)(hdr + 2) = filesize;
    *(int *)(hdr + 10) = 54;
    *(int *)(hdr + 14) = 40;
    *(int *)(hdr + 18) = HOR_RES;
    *(int *)(hdr + 22) = VER_RES;
    *(short *)(hdr + 26) = 1;
    *(short *)(hdr + 28) = 24;
    fwrite(hdr, 1, 54, fp);
    unsigned char *line = (unsigned char *)malloc(row);
    for (int y = VER_RES - 1; y >= 0; y--) {
        for (int x = 0; x < HOR_RES; x++) {
            uint32_t c = s_pixels[y * HOR_RES + x];
            line[x * 3 + 0] = (unsigned char)(c & 0xFF);
            line[x * 3 + 1] = (unsigned char)((c >> 8) & 0xFF);
            line[x * 3 + 2] = (unsigned char)((c >> 16) & 0xFF);
        }
        memset(line + HOR_RES * 3, 0, row - HOR_RES * 3);
        fwrite(line, 1, row, fp);
    }
    free(line);
    fclose(fp);
    printf("[SIM] wrote %s\n", path);
}

/* Debug helper: number of distinct colours in a framebuffer region. */
static int count_colors(int x0, int y0, int x1, int y1)
{
    static uint32_t seen[256];
    int n = 0;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (x < 0 || y < 0 || x >= HOR_RES || y >= VER_RES) continue;
            uint32_t c = s_pixels[y * HOR_RES + x] & 0xFFFFFFu;
            bool f = false;
            for (int i = 0; i < n; i++) {
                if (seen[i] == c) { f = true; break; }
            }
            if (!f && n < 256) seen[n++] = c;
        }
    }
    return n;
}

static void do_swipe(lv_dir_t dir)
{
    lv_obj_t *tv = get_ui_tabview();
    int count = get_ui_tab_count();
    if (tv == NULL || count < 1) {
        return;
    }
    if (is_settings_page_active()) {
        printf("[SIM] swipe disabled on settings\n");
        return;
    }
    int act = (int)lv_tabview_get_tab_act(tv);
    printf("[SIM] swipe dir=%d (page %d/%d)\n", (int)dir, act, count);

    switch (dir) {
    case LV_DIR_LEFT:   ui_show_page((act + 1) % count); break;
    case LV_DIR_RIGHT:  ui_show_page((act - 1 + count) % count); break;
    case LV_DIR_BOTTOM: ui_show_page(0); break;
    case LV_DIR_TOP:    toggle_brightness(); break;
    default: break;
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== ModiPAD SDL2 Simulator (480x320) ===\n");

    int autoexit_ms = 0;
    {
        const char *ae = getenv("SIM_AUTOEXIT_MS");
        if (ae != NULL && ae[0] != '\0') {
            autoexit_ms = atoi(ae);
        }
    }

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    s_win = SDL_CreateWindow("ModiPAD Simulator",
                             SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             HOR_RES, VER_RES, 0);
    if (s_win == NULL) {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    s_ren = SDL_CreateRenderer(s_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (s_ren == NULL) {
        s_ren = SDL_CreateRenderer(s_win, -1, SDL_RENDERER_SOFTWARE);
    }
    s_tex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, HOR_RES, VER_RES);
    SDL_SetTextureBlendMode(s_tex, SDL_BLENDMODE_NONE);

    lv_init();
    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, HOR_RES * 40);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = HOR_RES;
    disp_drv.ver_res = VER_RES;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = mouse_read_cb;
    lv_indev_drv_register(&indev_drv);

    /* === REAL project code === */
    ui_loader_init();
    i18n_init();
    const AppSettings *settings = get_settings();

    set_splash_language(i18n_get_language() == LANG_RU);
    show_splash_screen();
    for (int i = 0; i < 5; i++) {
        update_progress(20 * (i + 1), (splash_status_t)(i % SPLASH_STATUS_COUNT));
        lv_timer_handler();
        SDL_Delay(120);
    }
    close_splash_screen();
    if (getenv("SIM_DUMP_SPLASH") != NULL) {
        dump_bmp(getenv("SIM_DUMP_SPLASH"));
    }

    create_ui();
    create_settings_page(get_ui_tabview());
    init_gestures();
    create_status_bar(lv_scr_act());
    update_status_bar(ui_page_name(0));
    if (settings != NULL) {
        set_brightness(settings->brightness);
    }
    lv_timer_handler();

    if (getenv("SIM_SETTINGS_SUB") != NULL) {
        /* Screenshot tooling: open Settings, optionally a sub-page by index. */
        go_to_settings();
        int sub = atoi(getenv("SIM_SETTINGS_SUB"));
        if (sub >= 0) {
            settings_debug_show_sub(sub);
        }
        lv_timer_handler();
    } else if (getenv("SIM_TAB") != NULL) {
        ui_show_page(atoi(getenv("SIM_TAB")));
        lv_timer_handler();
    }

    Uint32 last = SDL_GetTicks();
    Uint32 start = last;
    bool running = true;
    if (autoexit_ms > 0) {
        printf("[SIM] running (auto-exit after %d ms)\n", autoexit_ms);
    } else {
        printf("[SIM] running - close the window to exit\n");
    }
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                s_pressed = true;
                s_press_x = s_mx = e.button.x;
                s_press_y = s_my = e.button.y;
            } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                s_pressed = false;
                int dx = e.button.x - s_press_x;
                int dy = e.button.y - s_press_y;
                if (abs(dx) > 60 && abs(dx) > abs(dy)) {
                    do_swipe(dx > 0 ? LV_DIR_RIGHT : LV_DIR_LEFT);
                } else if (abs(dy) > 60) {
                    do_swipe(dy > 0 ? LV_DIR_BOTTOM : LV_DIR_TOP);
                }
            } else if (e.type == SDL_MOUSEMOTION) {
                s_mx = e.motion.x;
                s_my = e.motion.y;
            }
        }

        Uint32 now = SDL_GetTicks();
        lv_tick_inc(now - last);
        last = now;
        lv_timer_handler();
        SDL_Delay(5);

        static bool probed = false;
        if (!probed && (int)(now - start) > 500) {
            probed = true;
            if (getenv("SIM_PROBE") != NULL) {
                printf("[SIM] colors in buttons region: %d, corner patch: %d\n",
                       count_colors(20, 40, 115, 170), count_colors(18, 38, 42, 70));
                char ip[192];
                bool ok = asset_resolve_image("gradient_ocean_blue.png", 105, 137, ip, sizeof(ip));
                lv_img_header_t hdr = {0, 0, 0};
                lv_res_t r = LV_RES_INV;
                if (ok) {
                    r = lv_img_decoder_get_info(ip, &hdr);
                }
                printf("[SIM] resolve=%d fs_ready=%d decode=%d w=%d h=%d path=%s\n",
                       (int)ok, (int)lv_fs_is_ready('S'), (int)r, (int)hdr.w, (int)hdr.h, ip);

                lv_fs_file_t f;
                lv_res_t fr = lv_fs_open(&f, ip, LV_FS_MODE_RD);
                printf("[SIM] lv_fs_open=%d\n", (int)fr);
                if (fr == LV_RES_OK) {
                    lv_fs_close(&f);
                }
                lv_img_decoder_dsc_t dsc;
                lv_res_t orr = lv_img_decoder_open(&dsc, ip, lv_color_black(), 0);
                printf("[SIM] decoder_open=%d\n", (int)orr);
                if (orr == LV_RES_OK) {
                    lv_img_decoder_close(&dsc);
                }
                const char *pp = asset_persist_path("gradient_ocean_blue.png", 105, 137);
                lv_img_decoder_dsc_t d2;
                lv_res_t r2 = (pp != NULL) ? lv_img_decoder_open(&d2, pp, lv_color_black(), 0) : LV_RES_INV;
                printf("[SIM] persist_open=%d path=%s\n", (int)r2, pp ? pp : "(null)");
                if (r2 == LV_RES_OK) {
                    lv_img_decoder_close(&d2);
                }
                lv_obj_t *scr = lv_scr_act();
                lv_obj_set_style_bg_color(scr, lv_color_hex(0xFF0000), 0);
                lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
                lv_obj_invalidate(scr);
                lv_timer_handler();
                printf("[SIM] scr bg_opa=%d color=%06X\n",
                       (int)lv_obj_get_style_bg_opa(scr, 0),
                       (unsigned)lv_obj_get_style_bg_color(scr, 0).full & 0xFFFFFF);
                lv_obj_t *t = lv_img_create(lv_scr_act());
                lv_img_set_src(t, ip);
                printf("[SIM] test img size=%dx%d\n",
                       (int)lv_obj_get_width(t), (int)lv_obj_get_height(t));
                lv_obj_del(t);
            }
        }

        if (autoexit_ms > 0 && (int)(now - start) > autoexit_ms) {
            printf("[SIM] auto-exit after %d ms\n", autoexit_ms);
            running = false;
        }
    }

    if (getenv("SIM_DUMP_BMP") != NULL) {
        dump_bmp(getenv("SIM_DUMP_BMP"));
    }

    SDL_DestroyTexture(s_tex);
    SDL_DestroyRenderer(s_ren);
    SDL_DestroyWindow(s_win);
    SDL_Quit();
    return 0;
}
