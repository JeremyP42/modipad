#pragma once
#include <stdio.h>

#define ESP_LOGI(tag, fmt, ...) printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) printf("[D][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGV(tag, fmt, ...) printf("[V][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOG_BUFFER_HEX(tag, buf, len) ((void)0)
#define esp_log_level_set(tag, level) ((void)0)
