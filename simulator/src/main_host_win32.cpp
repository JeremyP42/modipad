/*
 * main_host.cpp - PC simulator for the ModiPAD UI.
 *
 * Pure Win32 (GDI) window, no SDL. Renders the REAL project UI code
 * (ui_renderer, splash_screen, status_bar, settings_page, gesture_handler,
 * ui_loader, ui_assets) with LVGL 8.4.
 *
 * Mouse = touch. Drag = swipe.
 */
#include <windows.h>
#include <windowsx.h>
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

#define HOR_RES 480
#define VER_RES 320

static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t s_buf1[HOR_RES * 40];
static lv_color_t s_buf2[HOR_RES * 40];
static HWND s_hwnd = NULL;
static uint32_t s_fb[HOR_RES * VER_RES];
static bool s_pressed = false;
static int s_mx = 0, s_my = 0, s_press_x = 0, s_press_y = 0;
static DWORD s_last_tick = 0;

/* --- LVGL flush: RGB565 -> 32-bit framebuffer -> window --- */
static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *cp)
{
    int w = area->x2 - area->x1 + 1;
    for (int y = area->y1; y <= area->y2; y++) {
        for (int x = area->x1; x <= area->x2; x++) {
            lv_color_t c = cp[(y - area->y1) * w + (x - area->x1)];
            s_fb[y * HOR_RES + x] = lv_color_to32(c) & 0xFFFFFFu;
        }
    }
    InvalidateRect(s_hwnd, NULL, FALSE);
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
            uint32_t c = s_fb[y * HOR_RES + x];
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

/* --- drive the real tabview using the public UI helpers --- */
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

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        BITMAPINFO bmi = {0};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = HOR_RES;
        bmi.bmiHeader.biHeight = -VER_RES;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        SetDIBitsToDevice(hdc, 0, 0, HOR_RES, VER_RES, 0, 0, 0, VER_RES,
                          s_fb, &bmi, DIB_RGB_COLORS);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
        s_pressed = true;
        s_press_x = s_mx = GET_X_LPARAM(l);
        s_press_y = s_my = GET_Y_LPARAM(l);
        return 0;
    case WM_LBUTTONUP: {
        s_pressed = false;
        int dx = GET_X_LPARAM(l) - s_press_x;
        int dy = GET_Y_LPARAM(l) - s_press_y;
        if (abs(dx) > 60 && abs(dx) > abs(dy)) {
            do_swipe(dx > 0 ? LV_DIR_RIGHT : LV_DIR_LEFT);
        } else if (abs(dy) > 60) {
            do_swipe(dy > 0 ? LV_DIR_BOTTOM : LV_DIR_TOP);
        }
        return 0;
    }
    case WM_MOUSEMOVE:
        s_mx = GET_X_LPARAM(l);
        s_my = GET_Y_LPARAM(l);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(h, m, w, l);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== ModiPAD Host Simulator (480x320) ===\n");

    int autoexit_ms = getenv("SIM_AUTOEXIT_MS") ? atoi(getenv("SIM_AUTOEXIT_MS")) : 0;

    /* ANSI window API on purpose: the title is plain ASCII and the stock MinGW
     * gcc here mis-encodes the *W wide-string ABI, which truncated the title to
     * its first character. ANSI keeps it identical to the SDL2 build. */
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "ModiPADSim";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc = {0, 0, HOR_RES, VER_RES};
    AdjustWindowRect(&rc, style, FALSE);
    s_hwnd = CreateWindowA(wc.lpszClassName, "ModiPAD Simulator",
                           style, CW_USEDEFAULT, CW_USEDEFAULT,
                           rc.right - rc.left, rc.bottom - rc.top,
                           NULL, NULL, wc.hInstance, NULL);
    ShowWindow(s_hwnd, SW_SHOW);

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
    ui_loader_init();                 /* mounts (shim) + loads ./datadevice/config.json */
    i18n_init();
    const AppSettings *settings = get_settings();

    set_splash_language(i18n_get_language() == LANG_RU);
    show_splash_screen();
    for (int i = 0; i < 5; i++) {
        update_progress(20 * (i + 1), (splash_status_t)(i % SPLASH_STATUS_COUNT));
        lv_timer_handler();
        Sleep(120);
    }
    close_splash_screen();

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

    s_last_tick = GetTickCount();
    DWORD start_tick = s_last_tick;
    MSG msg;
    bool running = true;
    while (running) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        DWORD now = GetTickCount();
        lv_tick_inc(now - s_last_tick);
        s_last_tick = now;
        lv_timer_handler();
        Sleep(5);

        if (autoexit_ms > 0 && (int)(now - start_tick) > autoexit_ms) {
            printf("[SIM] auto-exit after %d ms\n", autoexit_ms);
            running = false;
        }
    }
    if (getenv("SIM_DUMP_BMP") != NULL) {
        dump_bmp(getenv("SIM_DUMP_BMP"));
    }
    return 0;
}
