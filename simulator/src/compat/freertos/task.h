#pragma once
#include "FreeRTOS.h"
#include <stdint.h>
#include <windows.h>

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

static inline void vTaskDelay(uint32_t ticks) { Sleep(ticks); }

static inline void vTaskDelete(TaskHandle_t handle) { (void)handle; }

static inline BaseType_t xTaskCreate(TaskFunction_t f, const char *name,
                                     uint32_t stack, void *param, int prio,
                                     TaskHandle_t *handle)
{
    (void)f; (void)name; (void)stack; (void)param; (void)prio; (void)handle;
    return 1;
}
