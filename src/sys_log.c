/*
 * sys_log.c - In-RAM ring buffer of recent log lines.
 *
 * esp_log_set_vprintf() is used to tee every log line into a RAM ring buffer
 * (PSRAM on device) while still forwarding it to the normal console. The web
 * UI fetches it through GET /api/log, so problems can be diagnosed without a
 * serial cable.
 */
#include "sys_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifndef HOST_BUILD
#include "esp_heap_caps.h"
#endif

static char (*s_lines)[SYS_LOG_LINE_LEN] = NULL;
static int s_head = 0;   /* next write slot */
static int s_count = 0;  /* valid lines */
static SemaphoreHandle_t s_mutex = NULL;
static vprintf_like_t s_prev = NULL;

static int sys_log_vprintf(const char *fmt, va_list args)
{
    if (s_lines != NULL && s_mutex != NULL && xSemaphoreTake(s_mutex, 0) == pdTRUE) {
        char buf[SYS_LOG_LINE_LEN];
        va_list copy;
        va_copy(copy, args);
        int n = vsnprintf(buf, sizeof(buf), fmt, copy);
        va_end(copy);

        int l = (n > 0) ? n : 0;
        if (l > (int)sizeof(buf) - 1) {
            l = (int)sizeof(buf) - 1;
        }
        while (l > 0 && (buf[l - 1] == '\n' || buf[l - 1] == '\r')) {
            l--;
        }
        buf[l] = '\0';
        memcpy(s_lines[s_head], buf, (size_t)l + 1);
        s_head = (s_head + 1) % SYS_LOG_LINES;
        if (s_count < SYS_LOG_LINES) {
            s_count++;
        }
        xSemaphoreGive(s_mutex);
    }

    if (s_prev != NULL) {
        return s_prev(fmt, args);
    }
    return vprintf(fmt, args);
}

void sys_log_init(void)
{
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
    }
    if (s_lines == NULL) {
#ifdef HOST_BUILD
        s_lines = malloc(SYS_LOG_BYTES);
#else
        s_lines = heap_caps_malloc(SYS_LOG_BYTES, MALLOC_CAP_SPIRAM);
        if (s_lines == NULL) {
            s_lines = malloc(SYS_LOG_BYTES);
        }
#endif
    }
    s_prev = esp_log_set_vprintf(sys_log_vprintf);
}

size_t sys_log_get(char *out, size_t len)
{
    if (out == NULL || len == 0) {
        return 0;
    }
    out[0] = '\0';
    if (s_lines == NULL || s_mutex == NULL) {
        return 0;
    }

    size_t off = 0;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        int start = (s_count < SYS_LOG_LINES) ? 0 : s_head;
        for (int i = 0; i < s_count; i++) {
            const char *line = s_lines[(start + i) % SYS_LOG_LINES];
            size_t l = strlen(line);
            if (off + l + 2 >= len) {
                break;
            }
            memcpy(out + off, line, l);
            off += l;
            out[off++] = '\n';
        }
        out[off] = '\0';
        xSemaphoreGive(s_mutex);
    }
    return off;
}
