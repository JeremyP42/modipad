#pragma once
#include <stdint.h>

typedef int gpio_num_t;
#define GPIO_MODE_INPUT 0
#define GPIO_MODE_OUTPUT 1
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLUP_DISABLE 0

typedef struct { int dummy; } gpio_config_t;

static inline int gpio_config(const gpio_config_t *c) { (void)c; return 0; }
static inline int gpio_set_level(int pin, int level) { (void)pin; (void)level; return 0; }
static inline int gpio_get_level(int pin) { (void)pin; return 1; }
static inline int gpio_set_direction(int pin, int dir) { (void)pin; (void)dir; return 0; }
