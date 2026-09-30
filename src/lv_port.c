/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_interface.h"

#include "lv_port.h"
#include "lvgl.h"
#include "esp_bsp.h"
#include "gesture_handler.h"
#include "system_info.h"

#ifdef ESP_LVGL_PORT_TOUCH_COMPONENT
#include "esp_lcd_touch.h"
#endif

#if (ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(4, 4, 4)) || (ESP_IDF_VERSION == ESP_IDF_VERSION_VAL(5, 0, 0))
#define LVGL_PORT_HANDLE_FLUSH_READY 0
#else
#define LVGL_PORT_HANDLE_FLUSH_READY 1
#endif

static const char *TAG = "LVGL";

/*******************************************************************************
* Types definitions
*******************************************************************************/

typedef struct lvgl_port_ctx_s {
    SemaphoreHandle_t   lvgl_mux;
    esp_timer_handle_t  tick_timer;
    bool                running;
    int                 task_max_sleep_ms;
    int                 task_stack;
} lvgl_port_ctx_t;

typedef struct {
    esp_lcd_panel_io_handle_t io_handle;    /* LCD panel IO handle */
    esp_lcd_panel_handle_t    panel_handle; /* LCD panel handle */
    lv_disp_drv_t             disp_drv;     /* LVGL display driver */

    uint32_t                  trans_size;       /* Maximum size for one transport */
    lv_color_t                *trans_buf_1;     /* Buffer send to driver */
    lv_color_t                *trans_buf_2;     /* Buffer send to driver */
    lv_color_t                *trans_act;       /* Active buffer for sending to driver */
    SemaphoreHandle_t         trans_done_sem;   /* Semaphore for signaling idle transfer */
    lv_disp_rot_t             sw_rotate;        /* Panel software rotation mask */

    lvgl_port_wait_cb         draw_wait_cb;     /* Callback function for drawing */
} lvgl_port_display_ctx_t;

#ifdef ESP_LVGL_PORT_TOUCH_COMPONENT
typedef struct {
    esp_lcd_touch_handle_t  handle;        /* LCD touch IO handle */
    lv_indev_drv_t          indev_drv;     /* LVGL input device driver */
    lvgl_port_wait_cb       touch_wait_cb;  /* Callback function for touch */
} lvgl_port_touch_ctx_t;
#endif

/*******************************************************************************
* Local variables
*******************************************************************************/
static lvgl_port_ctx_t lvgl_port_ctx;
static int lvgl_port_timer_period_ms = 5;

/*******************************************************************************
* Function definitions
*******************************************************************************/
static void lvgl_port_task(void *arg);
static esp_err_t lvgl_port_tick_init(void);
static void lvgl_port_task_deinit(void);

// LVGL callbacks
#if LVGL_PORT_HANDLE_FLUSH_READY
static bool lvgl_port_flush_ready_callback(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx);
#endif
static void lvgl_port_flush_callback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map);
#ifdef ESP_LVGL_PORT_TOUCH_COMPONENT
static void lvgl_port_touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data);
#endif
/*******************************************************************************
* Public API functions
*******************************************************************************/

esp_err_t lvgl_port_init(const lvgl_port_cfg_t *cfg)
{
    esp_err_t ret = ESP_OK;
    ESP_GOTO_ON_FALSE(cfg, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");
    ESP_GOTO_ON_FALSE(cfg->task_affinity < (configNUM_CORES), ESP_ERR_INVALID_ARG, err, TAG, "Bad core number for task! Maximum core number is %d", (configNUM_CORES - 1));

    memset(&lvgl_port_ctx, 0, sizeof(lvgl_port_ctx));

    /* LVGL init */
    lv_init();
    /* Tick init */
    lvgl_port_timer_period_ms = cfg->timer_period_ms;
    ESP_RETURN_ON_ERROR(lvgl_port_tick_init(), TAG, "");
    /* Create task */
    lvgl_port_ctx.task_max_sleep_ms = cfg->task_max_sleep_ms;
    if (lvgl_port_ctx.task_max_sleep_ms == 0) {
        lvgl_port_ctx.task_max_sleep_ms = 500;
    }
    lvgl_port_ctx.task_stack = cfg->task_stack;
    lvgl_port_ctx.lvgl_mux = xSemaphoreCreateRecursiveMutex();
    ESP_GOTO_ON_FALSE(lvgl_port_ctx.lvgl_mux, ESP_ERR_NO_MEM, err, TAG, "Create LVGL mutex fail!");

    BaseType_t res;
    if (cfg->task_affinity < 0) {
        res = xTaskCreate(lvgl_port_task, "LVGL task", cfg->task_stack, NULL, cfg->task_priority, NULL);
    } else {
        res = xTaskCreatePinnedToCore(lvgl_port_task, "LVGL task", cfg->task_stack, NULL, cfg->task_priority, NULL, cfg->task_affinity);
    }
    ESP_GOTO_ON_FALSE(res == pdPASS, ESP_FAIL, err, TAG, "Create LVGL task fail!");

err:
    if (ret != ESP_OK) {
        lvgl_port_deinit();
    }

    return ret;
}

esp_err_t lvgl_port_resume(void)
{
    esp_err_t ret = ESP_ERR_INVALID_STATE;

    if (lvgl_port_ctx.tick_timer != NULL) {
        lv_timer_enable(true);
        ret = esp_timer_start_periodic(lvgl_port_ctx.tick_timer, lvgl_port_timer_period_ms * 1000);
    }

    return ret;
}

esp_err_t lvgl_port_stop(void)
{
    esp_err_t ret = ESP_ERR_INVALID_STATE;

    if (lvgl_port_ctx.tick_timer != NULL) {
        lv_timer_enable(false);
        ret = esp_timer_stop(lvgl_port_ctx.tick_timer);
    }

    return ret;
}

esp_err_t lvgl_port_deinit(void)
{
    /* Stop and delete timer */
    if (lvgl_port_ctx.tick_timer != NULL) {
        esp_timer_stop(lvgl_port_ctx.tick_timer);
        esp_timer_delete(lvgl_port_ctx.tick_timer);
        lvgl_port_ctx.tick_timer = NULL;
    }

    /* Stop running task */
    if (lvgl_port_ctx.running) {
        lvgl_port_ctx.running = false;
    } else {
        lvgl_port_task_deinit();
    }

    return ESP_OK;
}

lv_disp_t *lvgl_port_add_disp(const lvgl_port_display_cfg_t *disp_cfg)
{
    esp_err_t ret = ESP_OK;
    lv_disp_t *disp = NULL;
    lv_color_t *buf1 = NULL;
    lv_color_t *buf2 = NULL;
    lv_color_t *buf3 = NULL;
    SemaphoreHandle_t trans_done_sem = NULL;

    assert(disp_cfg != NULL);
    assert(disp_cfg->io_handle != NULL);
    assert(disp_cfg->panel_handle != NULL);
    assert(disp_cfg->buffer_size > 0);
    assert(disp_cfg->hres > 0);
    assert(disp_cfg->vres > 0);

    /* Display context */
    lvgl_port_display_ctx_t *disp_ctx = malloc(sizeof(lvgl_port_display_ctx_t));
    ESP_GOTO_ON_FALSE(disp_ctx, ESP_ERR_NO_MEM, err, TAG, "Not enough memory for display context allocation!");
    disp_ctx->io_handle = disp_cfg->io_handle;
    disp_ctx->panel_handle = disp_cfg->panel_handle;
    disp_ctx->trans_size = disp_cfg->trans_size;
    disp_ctx->sw_rotate = disp_cfg->sw_rotate;
    disp_ctx->draw_wait_cb = disp_cfg->draw_wait_cb;

    uint32_t buff_caps = MALLOC_CAP_DEFAULT;
    if (disp_cfg->flags.buff_dma) {
        buff_caps = MALLOC_CAP_DMA;
    } else if (disp_cfg->flags.buff_spiram) {
        buff_caps = MALLOC_CAP_SPIRAM;
    }

    /* alloc draw buffers used by LVGL */
    /* it's recommended to choose the size of the draw buffer(s) to be at least 1/10 screen sized */
    buf1 = heap_caps_malloc(disp_cfg->buffer_size * sizeof(lv_color_t), buff_caps);
    ESP_GOTO_ON_FALSE(buf1, ESP_ERR_NO_MEM, err, TAG, "Not enough memory for LVGL buffer (buf1) allocation!");

    if (disp_ctx->trans_size) {

        uint32_t caps = MALLOC_CAP_DMA;

        buf2 = heap_caps_malloc(disp_ctx->trans_size * sizeof(lv_color_t), caps);
        ESP_GOTO_ON_FALSE(buf2, ESP_ERR_NO_MEM, err, TAG, "Not enough memory for buffer(transport) allocation!");
        disp_ctx->trans_buf_1 = buf2;

        buf3 = heap_caps_malloc(disp_ctx->trans_size * sizeof(lv_color_t), caps);
        ESP_GOTO_ON_FALSE(buf3, ESP_ERR_NO_MEM, err, TAG, "Not enough memory for buffer(transport) allocation!");
        disp_ctx->trans_buf_2 = buf3;

        trans_done_sem = xSemaphoreCreateCounting(1, 1);
        ESP_GOTO_ON_FALSE(trans_done_sem, ESP_ERR_NO_MEM, err, TAG, "Failed to create transport counting Semaphore");
        disp_ctx->trans_done_sem = trans_done_sem;
    }

    lv_disp_draw_buf_t *disp_buf = malloc(sizeof(lv_disp_draw_buf_t));
    ESP_GOTO_ON_FALSE(disp_buf, ESP_ERR_NO_MEM, err, TAG, "Not enough memory for LVGL display buffer allocation!");

    /* initialize LVGL draw buffers */
    lv_disp_draw_buf_init(disp_buf, buf1, NULL, disp_cfg->buffer_size);

    ESP_LOGD(TAG, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_ctx->disp_drv);
    disp_ctx->disp_drv.hor_res = disp_cfg->hres;
    disp_ctx->disp_drv.ver_res = disp_cfg->vres;
    disp_ctx->disp_drv.flush_cb = lvgl_port_flush_callback;

    disp_ctx->disp_drv.draw_buf = disp_buf;
    disp_ctx->disp_drv.user_data = disp_ctx;
    /* Keep full_refresh: the AXS15231B QSPI path only sends CASET (no RASET)
     * and streams the frame as sequential chunks (2C/3C "write continue"), so
     * arbitrary partial rectangles are not addressable. Do NOT set this to 0
     * unless the panel driver is taught partial-window addressing first. */
    disp_ctx->disp_drv.full_refresh = 1;

#if LVGL_PORT_HANDLE_FLUSH_READY
    /* Register done callback */
    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = lvgl_port_flush_ready_callback,
    };
    esp_lcd_panel_io_register_event_callbacks(disp_ctx->io_handle, &cbs, &disp_ctx->disp_drv);
#endif

    disp = lv_disp_drv_register(&disp_ctx->disp_drv);

err:
    if (ret != ESP_OK) {
        if (buf1) {
            free(buf1);
        }
        if (buf2) {
            free(buf2);
        }
        if (buf3) {
            free(buf3);
        }
        if (trans_done_sem) {
            vSemaphoreDelete(trans_done_sem);
        }
        if (disp_ctx) {
            free(disp_ctx);
        }
    }

    return disp;
}

esp_err_t lvgl_port_remove_disp(lv_disp_t *disp)
{
    assert(disp);
    lv_disp_drv_t *disp_drv = disp->driver;
    assert(disp_drv);
    lvgl_port_display_ctx_t *disp_ctx = (lvgl_port_display_ctx_t *)disp_drv->user_data;

    lv_disp_remove(disp);

    if (disp_drv) {
        if (disp_drv->draw_buf && disp_drv->draw_buf->buf1) {
            free(disp_drv->draw_buf->buf1);
            disp_drv->draw_buf->buf1 = NULL;
        }
        if (disp_drv->draw_buf && disp_drv->draw_buf->buf2) {
            free(disp_drv->draw_buf->buf2);
            disp_drv->draw_buf->buf2 = NULL;
        }
        if (disp_drv->draw_buf) {
            free(disp_drv->draw_buf);
            disp_drv->draw_buf = NULL;
        }
    }

    free(disp_ctx);

    return ESP_OK;
}

#ifdef ESP_LVGL_PORT_TOUCH_COMPONENT
lv_indev_t *lvgl_port_add_touch(const lvgl_port_touch_cfg_t *touch_cfg)
{
    assert(touch_cfg != NULL);
    assert(touch_cfg->disp != NULL);
    assert(touch_cfg->handle != NULL);

    /* Touch context */
    lvgl_port_touch_ctx_t *touch_ctx = malloc(sizeof(lvgl_port_touch_ctx_t));
    if (touch_ctx == NULL) {
        ESP_LOGE(TAG, "Not enough memory for touch context allocation!");
        return NULL;
    }
    touch_ctx->handle = touch_cfg->handle;
    touch_ctx->touch_wait_cb = touch_cfg->touch_wait_cb;

    /* Register a touchpad input device */
    lv_indev_drv_init(&touch_ctx->indev_drv);
    touch_ctx->indev_drv.type = LV_INDEV_TYPE_POINTER;
    touch_ctx->indev_drv.disp = touch_cfg->disp;
    touch_ctx->indev_drv.read_cb = lvgl_port_touchpad_read;
    touch_ctx->indev_drv.user_data = touch_ctx;
    return lv_indev_drv_register(&touch_ctx->indev_drv);
}

esp_err_t lvgl_port_remove_touch(lv_indev_t *touch)
{
    assert(touch);
    lv_indev_drv_t *indev_drv = touch->driver;
    assert(indev_drv);
    lvgl_port_touch_ctx_t *touch_ctx = (lvgl_port_touch_ctx_t *)indev_drv->user_data;

    /* Remove input device driver */
    lv_indev_delete(touch);

    if (touch_ctx) {
        free(touch_ctx);
    }

    return ESP_OK;
}
#endif

bool lvgl_port_lock(uint32_t timeout_ms)
{
    assert(lvgl_port_ctx.lvgl_mux && "lvgl_port_init must be called first");

    const TickType_t timeout_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_port_ctx.lvgl_mux, timeout_ticks) == pdTRUE;
}

void lvgl_port_unlock(void)
{
    assert(lvgl_port_ctx.lvgl_mux && "lvgl_port_init must be called first");
    xSemaphoreGiveRecursive(lvgl_port_ctx.lvgl_mux);
}

bool lvgl_port_locked(void)
{
    if (lvgl_port_ctx.lvgl_mux == NULL) {
        return false;
    }
    return xSemaphoreGetMutexHolder(lvgl_port_ctx.lvgl_mux) != NULL;
}

/* Diagnostic: last place the LVGL task entered, so the heartbeat can report
 * exactly where it is stuck. 0 = idle, 10..19 = touch read, 20..29 = flush. */
static volatile uint8_t s_lvgl_stage = 0;

/* Diagnostic counters: flushes entered / color-DMA completions / flushes done.
 * If "flush" advances while "done" is stuck, the QSPI color transfer never
 * signalled completion. */
static volatile uint32_t s_stat_flush = 0;
static volatile uint32_t s_stat_done = 0;
static volatile uint32_t s_stat_exit = 0;
static volatile uint32_t s_stat_skip = 0;
static volatile uint32_t s_stat_loops = 0;

/* LVGL-task CPU accounting: microseconds spent inside lv_timer_handler() vs the
 * wall-clock time of the loop (handler + sleep). Used by the status bar. */
static volatile uint64_t s_cpu_busy_us = 0;
static volatile uint64_t s_cpu_total_us = 0;

uint32_t lvgl_port_loops(void)
{
    return s_stat_loops;
}

uint32_t lvgl_port_frame_count(void)
{
    /* One increment per completed flush callback == one rendered frame. */
    return s_stat_exit;
}

void lvgl_port_cpu_stats(uint64_t *busy_us, uint64_t *total_us)
{
    if (busy_us) {
        *busy_us = s_cpu_busy_us;
    }
    if (total_us) {
        *total_us = s_cpu_total_us;
    }
}

uint8_t lvgl_port_stage(void)
{
    return s_lvgl_stage;
}

void lvgl_port_stats(uint32_t *flush, uint32_t *done, uint32_t *exit_cnt, uint32_t *skip)
{
    if (flush) {
        *flush = s_stat_flush;
    }
    if (done) {
        *done = s_stat_done;
    }
    if (exit_cnt) {
        *exit_cnt = s_stat_exit;
    }
    if (skip) {
        *skip = s_stat_skip;
    }
}

void lvgl_port_flush_ready(lv_disp_t *disp)
{
    assert(disp);
    assert(disp->driver);
    lv_disp_flush_ready(disp->driver);
}

/*******************************************************************************
* Private functions
*******************************************************************************/

static void lvgl_port_task(void *arg)
{
    uint32_t task_delay_ms = lvgl_port_ctx.task_max_sleep_ms;

    ESP_LOGI(TAG, "Starting LVGL task");
    system_info_register_task("LVGL", xTaskGetCurrentTaskHandle(), (uint32_t)lvgl_port_ctx.task_stack);
    lvgl_port_ctx.running = true;
    while (lvgl_port_ctx.running) {
        s_stat_loops++; /* liveness: advances unless the task is blocked */
        int64_t loop_t0 = esp_timer_get_time();
        int64_t busy_t0 = loop_t0;
        if (lvgl_port_lock(0)) {
            task_delay_ms = lv_timer_handler();
            lvgl_port_unlock();
        }
        int64_t busy_t1 = esp_timer_get_time();
        if ((task_delay_ms > lvgl_port_ctx.task_max_sleep_ms) || (1 == task_delay_ms)) {
            task_delay_ms = lvgl_port_ctx.task_max_sleep_ms;
        } else if (task_delay_ms < 1) {
            task_delay_ms = 1;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
        int64_t loop_t1 = esp_timer_get_time();
        s_cpu_busy_us += (uint64_t)(busy_t1 - busy_t0);
        s_cpu_total_us += (uint64_t)(loop_t1 - loop_t0);
    }

    lvgl_port_task_deinit();

    /* Close task */
    vTaskDelete(NULL);
}

static void lvgl_port_task_deinit(void)
{
    if (lvgl_port_ctx.lvgl_mux) {
        vSemaphoreDelete(lvgl_port_ctx.lvgl_mux);
    }
    memset(&lvgl_port_ctx, 0, sizeof(lvgl_port_ctx));
#if LV_ENABLE_GC || !LV_MEM_CUSTOM
    /* Deinitialize LVGL */
    lv_deinit();
#endif
}

#if LVGL_PORT_HANDLE_FLUSH_READY
static bool lvgl_port_flush_ready_callback(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    BaseType_t taskAwake = pdFALSE;

    lv_disp_drv_t *disp_drv = (lv_disp_drv_t *)user_ctx;
    assert(disp_drv != NULL);
    lvgl_port_display_ctx_t *disp_ctx = disp_drv->user_data;
    assert(disp_ctx != NULL);

    if (disp_ctx->trans_done_sem) {
        xSemaphoreGiveFromISR(disp_ctx->trans_done_sem, &taskAwake);
    }
    s_stat_done++;

    return false;
}
#endif

static void lvgl_port_flush_callback(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    assert(drv != NULL);
    lvgl_port_display_ctx_t *disp_ctx = (lvgl_port_display_ctx_t *)drv->user_data;
    assert(disp_ctx != NULL);
    s_lvgl_stage = 20; /* flush: enter */
    s_stat_flush++;

    const int x_start = area->x1;
    const int x_end = area->x2;
    const int y_start = area->y1;
    const int y_end = area->y2;
    const int width = x_end - x_start + 1;
    const int height = y_end - y_start + 1;

    lv_color_t *from = color_map;
    lv_color_t *to = NULL;

    if (disp_ctx->trans_size) {
        assert(disp_ctx->trans_buf_1 != NULL);

        int x_draw_start = 0;
        int x_draw_end = 0;
        int y_draw_start = 0;
        int y_draw_end = 0;
        int trans_count = 0;

        disp_ctx->trans_act = disp_ctx->trans_buf_1;
        int rotate = disp_ctx->sw_rotate;

        int x_start_tmp = 0;
        int x_end_tmp = 0;
        int max_width = 0;
        int trans_width = 0;

        int y_start_tmp = 0;
        int y_end_tmp = 0;
        int max_height = 0;
        int trans_height = 0;

        if (LV_DISP_ROT_270 == rotate || LV_DISP_ROT_90 == rotate) {
            max_width = ((disp_ctx->trans_size / height) > width) ? (width) : (disp_ctx->trans_size / height);
            trans_count = width / max_width + (width % max_width ? (1) : (0));

            x_start_tmp = x_start;
            x_end_tmp = x_end;
        } else {
            max_height = ((disp_ctx->trans_size / width) > height) ? (height) : (disp_ctx->trans_size / width);
            trans_count = height / max_height + (height % max_height ? (1) : (0));

            y_start_tmp = y_start;
            y_end_tmp = y_end;
        }

        for (int i = 0; i < trans_count; i++) {

            if (LV_DISP_ROT_90 == rotate) {
                trans_width = (x_end - x_start_tmp + 1) > max_width ? max_width : (x_end - x_start_tmp + 1);
                x_end_tmp = (x_end - x_start_tmp + 1) > max_width ? (x_start_tmp + max_width - 1) : x_end;
            } else if (LV_DISP_ROT_270 == rotate) {
                trans_width = (x_end_tmp - x_start + 1) > max_width ? max_width : (x_end_tmp - x_start + 1);
                x_start_tmp = (x_end_tmp - x_start + 1) > max_width ? (x_end_tmp - trans_width + 1) : x_start;
            } else if (LV_DISP_ROT_NONE == rotate) {
                trans_height = (y_end - y_start_tmp + 1) > max_height ? max_height : (y_end - y_start_tmp + 1);
                y_end_tmp = (y_end - y_start_tmp + 1) > max_height ? (y_start_tmp + max_height - 1) : y_end;
            } else {
                trans_height = (y_end_tmp - y_start + 1) > max_height ? max_height : (y_end_tmp - y_start + 1);
                y_start_tmp = (y_end_tmp - y_start + 1) > max_height ? (y_end_tmp - max_height + 1) : y_start;
            }

            disp_ctx->trans_act = (disp_ctx->trans_act == disp_ctx->trans_buf_1) ? (disp_ctx->trans_buf_2) : (disp_ctx->trans_buf_1);
            to = disp_ctx->trans_act;

            switch (rotate) {
            case LV_DISP_ROT_90: {
                /* Transpose + vertical flip. Process two source pixels per
                 * iteration and advance the destination by 2*h so there is no
                 * per-pixel multiply (the addresses are strength-reduced). */
                const int h = height;
                for (int y = 0; y < h; y++) {
                    const lv_color_t *s = from + (size_t)y * width + x_start_tmp;
                    lv_color_t *d = to + (h - 1 - y);
                    int x = 0;
                    for (; x + 1 < trans_width; x += 2) {
                        d[0] = s[0];
                        d[h] = s[1];
                        s += 2;
                        d += 2 * h;
                    }
                    if (x < trans_width) {
                        *d = *s;
                    }
                }
                x_draw_start = drv->ver_res - y_end - 1;
                x_draw_end = drv->ver_res - y_start - 1;
                y_draw_start = x_start_tmp;
                y_draw_end = x_end_tmp;
                break;
            }
            case LV_DISP_ROT_270: {
                const int h = height;
                for (int y = 0; y < h; y++) {
                    const lv_color_t *s = from + (size_t)y * width + x_start_tmp;
                    lv_color_t *d = to + (trans_width - 1) * h + y;
                    int x = 0;
                    for (; x + 1 < trans_width; x += 2) {
                        d[0] = s[0];
                        d[-h] = s[1];
                        s += 2;
                        d -= 2 * h;
                    }
                    if (x < trans_width) {
                        *d = *s;
                    }
                }
                x_draw_start = y_start;
                x_draw_end = y_end;
                y_draw_start = drv->hor_res - x_end_tmp - 1;
                y_draw_end = drv->hor_res - x_start_tmp - 1;
                break;
            }
            case LV_DISP_ROT_180:
                for (int y = 0; y < trans_height; y++) {
                    const lv_color_t *s = from + (size_t)(y_start_tmp + y) * width;
                    lv_color_t *d = to + (size_t)(trans_height - y - 1) * width;
                    int x = 0;
                    for (; x + 1 < width; x += 2) {
                        d[width - 1 - x] = s[x];
                        d[width - 2 - x] = s[x + 1];
                    }
                    if (x < width) {
                        d[width - 1 - x] = s[x];
                    }
                }
                x_draw_start = drv->hor_res - x_end - 1;
                x_draw_end = drv->hor_res - x_start - 1;
                y_draw_start = drv->ver_res - y_end_tmp - 1;
                y_draw_end = drv->ver_res - y_start_tmp - 1;
                break;
            case LV_DISP_ROT_NONE:
                for (int y = 0; y < trans_height; y++) {
                    memcpy(to + (size_t)y * width,
                           from + (size_t)(y_start_tmp + y) * width,
                           (size_t)width * sizeof(lv_color_t));
                }
                x_draw_start = x_start;
                x_draw_end = x_end;
                y_draw_start = y_start_tmp;
                y_draw_end = y_end_tmp;
                break;
            default:
                break;
            }

            if (0 == i) {
                s_lvgl_stage = 21; /* flush: wait TE edge */
                if (disp_ctx->draw_wait_cb) {
                    disp_ctx->draw_wait_cb(disp_ctx->panel_handle->user_data);
                }
                /* No pre-give of trans_done_sem here: the take below must truly
                 * wait for the previous color transfer. Pre-giving would mask a
                 * lost completion and make the next draw_bitmap() run with a
                 * transaction still in flight, which then hangs forever. */
            }

            /* Wait for the previous color transfer to signal completion. It is
             * CRITICAL to abort the frame here instead of calling
             * esp_lcd_panel_draw_bitmap(): the IDF SPI transport recycles any
             * in-flight transaction with portMAX_DELAY, so if a completion was
             * lost (observed with Bluedroid active on this QSPI panel) calling
             * draw_bitmap() would block the LVGL task forever and freeze the
             * whole UI. Skipping a frame is harmless by comparison. */
            s_lvgl_stage = 22; /* flush: wait DMA done */
            if (xSemaphoreTake(disp_ctx->trans_done_sem, pdMS_TO_TICKS(100)) != pdTRUE) {
                ESP_LOGW(TAG, "flush: DMA done timeout, aborting frame");
                s_stat_skip++;
                break;
            }
            s_lvgl_stage = 23; /* flush: panel draw_bitmap */
            esp_lcd_panel_draw_bitmap(disp_ctx->panel_handle, x_draw_start, y_draw_start, x_draw_end + 1, y_draw_end + 1, to);

            if (LV_DISP_ROT_90 == rotate) {
                x_start_tmp += max_width;
            } else if (LV_DISP_ROT_270 == rotate) {
                x_end_tmp -= max_width;
            } if (LV_DISP_ROT_NONE == rotate) {
                y_start_tmp += max_height;
            } else {
                y_end_tmp -= max_height;
            }
        }
    } else {
        esp_lcd_panel_draw_bitmap(disp_ctx->panel_handle, x_start, y_start, x_end + 1, y_end + 1, color_map);
    }
    s_lvgl_stage = 0; /* flush: done */
    s_stat_exit++;
    lv_disp_flush_ready(drv);
}

#ifdef ESP_LVGL_PORT_TOUCH_COMPONENT
/* The AXS15231B sometimes reports short "up" blips while a finger is still
 * held (intermittent contact). A press is accepted immediately, but a release
 * is only accepted after it stays released for this many reads, so a glitch
 * cannot produce a phantom CLICKED (page switch) mid-hold. */
#define TOUCH_RELEASE_DEBOUNCE_READS 4

static void lvgl_port_touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    assert(indev_drv);
    lvgl_port_touch_ctx_t *touch_ctx = (lvgl_port_touch_ctx_t *)indev_drv->user_data;
    assert(touch_ctx->handle);

    uint16_t touchpad_x[1] = {0};
    uint16_t touchpad_y[1] = {0};
    uint8_t touchpad_cnt = 0;

    /* Read data from touch controller into memory */
    bool touch_int = false;
    if (touch_ctx->touch_wait_cb) {
        touch_int = touch_ctx->touch_wait_cb(touch_ctx->handle->config.user_data);
    }
    if (touch_int) {
        /* A disturbed I2C transfer (EMI, hot-plug) can leave the AXS15231B
         * holding SDA. Without recovery the very next transaction blocks the
         * LVGL task forever, which freezes the whole UI. Count consecutive
         * failures and reset the bus before that happens. */
        static uint32_t s_i2c_err_count = 0;
        s_lvgl_stage = 10; /* touch: read_data */
        esp_err_t rd = esp_lcd_touch_read_data(touch_ctx->handle);
        s_lvgl_stage = 11; /* touch: parse */
        if (rd != ESP_OK) {
            if (++s_i2c_err_count >= 3) {
                ESP_LOGW(TAG, "touch read failed %u times (%s), recovering I2C",
                         (unsigned)s_i2c_err_count, esp_err_to_name(rd));
                bsp_i2c_recover();
                s_i2c_err_count = 0;
            }
        } else {
            s_i2c_err_count = 0;
        }
        /* Read data from touch controller */
        bool raw_pressed = esp_lcd_touch_get_coordinates(touch_ctx->handle, touchpad_x, touchpad_y, NULL, &touchpad_cnt, 1);

        /* Release debounce: a press is immediate, a release must persist. */
        static bool s_deb_pressed = false;
        static uint8_t s_up_reads = 0;
        static uint16_t s_last_x = 0;
        static uint16_t s_last_y = 0;
        if (raw_pressed) {
            s_deb_pressed = true;
            s_up_reads = 0;
            s_last_x = touchpad_x[0];
            s_last_y = touchpad_y[0];
        } else if (s_deb_pressed) {
            if (++s_up_reads >= TOUCH_RELEASE_DEBOUNCE_READS) {
                s_deb_pressed = false;
                s_up_reads = 0;
            }
        }
        bool touchpad_pressed = s_deb_pressed;

        /* If a button action just fired, keep reporting "released" until the
         * finger is actually lifted (input_gate). Otherwise a page switch can
         * trigger the button that lands under the same point on the new page. */
        touchpad_pressed = input_gate_filter(touchpad_pressed);

        if (touchpad_pressed) {
            data->point.x = s_last_x;
            data->point.y = s_last_y;
            data->state = LV_INDEV_STATE_PRESSED;
        } else {
            data->state = LV_INDEV_STATE_RELEASED;
        }
        s_lvgl_stage = 0;
    }
}
#endif

static void lvgl_port_tick_increment(void *arg)
{
    /* Tell LVGL how many milliseconds have elapsed */
    lv_tick_inc(lvgl_port_timer_period_ms);
}

static esp_err_t lvgl_port_tick_init(void)
{
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &lvgl_port_tick_increment,
        .name = "LVGL tick",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&lvgl_tick_timer_args, &lvgl_port_ctx.tick_timer), TAG, "Creating LVGL timer filed!");
    return esp_timer_start_periodic(lvgl_port_ctx.tick_timer, lvgl_port_timer_period_ms * 1000);
}
