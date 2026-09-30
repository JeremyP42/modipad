/*
 * macro_player.c - Background macro executor (see macro_player.h).
 */
#include "macro_player.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "keyboard_manager.h"

static const char *TAG = "macro_player";

#define MACRO_QUEUE_LEN 8

typedef struct {
    char *json;
} macro_job_t;

static QueueHandle_t s_queue = NULL;
static TaskHandle_t s_task = NULL;

static void macro_step_delay(int ms)
{
    if (ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
}

static void macro_run(const char *json)
{
    cJSON *steps = cJSON_Parse(json);
    if (!cJSON_IsArray(steps)) {
        ESP_LOGW(TAG, "Macro is not an array, ignoring");
        cJSON_Delete(steps);
        return;
    }

    cJSON *step = NULL;
    cJSON_ArrayForEach(step, steps) {
        const cJSON *type = cJSON_GetObjectItemCaseSensitive(step, "type");
        const cJSON *value = cJSON_GetObjectItemCaseSensitive(step, "value");
        const cJSON *delay = cJSON_GetObjectItemCaseSensitive(step, "delay_ms");

        const char *t = cJSON_IsString(type) ? type->valuestring : "";
        const char *v = cJSON_IsString(value) ? value->valuestring : NULL;
        int delay_ms = cJSON_IsNumber(delay) ? delay->valueint : 0;

        if (strcmp(t, "key") == 0) {
            if (v != NULL) {
                send_hotkey(v);
            }
        } else if (strcmp(t, "text") == 0) {
            if (v != NULL) {
                send_text(v);
            }
        } else if (strcmp(t, "multimedia") == 0) {
            if (v != NULL) {
                send_multimedia(v);
            }
        } else if (strcmp(t, "delay") == 0) {
            if (cJSON_IsNumber(value)) {
                delay_ms = value->valueint;
            }
            macro_step_delay(delay_ms);
            continue;
        } else {
            ESP_LOGW(TAG, "Unknown step type \"%s\"", t);
        }

        macro_step_delay(delay_ms);
    }

    cJSON_Delete(steps);
}

static void macro_task(void *arg)
{
    (void)arg;
    macro_job_t job;
    while (1) {
        if (xQueueReceive(s_queue, &job, portMAX_DELAY) == pdTRUE) {
            macro_run(job.json);
            free(job.json);
        }
    }
}

void macro_player_init(void)
{
    if (s_queue != NULL) {
        return;
    }
    s_queue = xQueueCreate(MACRO_QUEUE_LEN, sizeof(macro_job_t));
    if (s_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create macro queue");
        return;
    }
    xTaskCreate(macro_task, "macro_player", 4096, NULL, 4, &s_task);
    ESP_LOGI(TAG, "Macro player ready (queue %d)", MACRO_QUEUE_LEN);
}

bool macro_play_json(const char *steps_json)
{
    if (steps_json == NULL || steps_json[0] == '\0') {
        return false;
    }

    if (s_queue == NULL) {
        /* Not initialised (e.g. early boot): run synchronously as a fallback. */
        macro_run(steps_json);
        return true;
    }

    macro_job_t job;
    job.json = strdup(steps_json);
    if (job.json == NULL) {
        return false;
    }
    if (xQueueSend(s_queue, &job, 0) != pdTRUE) {
        free(job.json);
        ESP_LOGW(TAG, "Macro queue full, aborting");
        return false;
    }
    return true;
}
