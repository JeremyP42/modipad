#pragma once
#include <stdio.h>
#include <stdint.h>

static inline void esp_restart(void) { printf("[HOST] esp_restart\n"); }
static inline uint32_t esp_get_free_heap_size(void) { return 1000000; }
static inline uint32_t esp_get_minimum_free_heap_size(void) { return 1000000; }
