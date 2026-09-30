/*
 * system_info.h - Runtime system information (memory, filesystem, temperature,
 * clocks, versions, uptime, MACs) for the About page.
 *
 * The implementation has two backends:
 *   - device (ESP32-S3 / ESP-IDF): real hardware data
 *   - host  (HOST_BUILD, PC simulators): best-effort data from the OS
 */
#ifndef MODIPAD_SYSTEM_INFO_H
#define MODIPAD_SYSTEM_INFO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Memory (KB) */
    uint32_t sram_total;
    uint32_t sram_free;
    uint32_t psram_total;
    uint32_t psram_free;
    uint32_t heap_total;
    uint32_t heap_free;

    /* Filesystem (KB) */
    uint32_t fs_total;
    uint32_t fs_free;

    /* Temperature */
    float temperature_c;

    /* Clocks */
    uint32_t cpu_freq_mhz;
    uint32_t flash_freq_mhz;

    /* Versions */
    const char *chip_model;
    uint32_t chip_revision;
    uint32_t cores;
    const char *idf_version;

    /* Uptime */
    uint32_t uptime_seconds;

    /* Reset reason (esp_reset_reason_t value; -1 on host/unknown) */
    int reset_reason;

    /* SD card */
    bool sd_present;
    uint32_t sd_total_kb;
    uint32_t sd_free_kb;
    const char *sd_fs_type;

    /* Wi-Fi signal (dBm, 0 when not applicable) and reconnect counter */
    int wifi_rssi;
    int wifi_reconnects;

    /* Build stamp */
    const char *build_date;
    const char *build_time;

    /* MAC */
    uint8_t wifi_mac[6];
    uint8_t ble_mac[6];
} system_info_t;

/* FreeRTOS task stack monitoring (device only; empty on host). Register a task
 * once after creating it (pass the stack size used at creation); the About page
 * shows "free / total" per task. */
void system_info_register_task(const char *name, void *handle, uint32_t stack_bytes);
int system_info_task_count(void);
const char *system_info_task_name(int index);
/* Free stack high-water mark (minimum free ever) in bytes for task `index`. */
uint32_t system_info_task_stack_free(int index);
/* Stack size in bytes the task was created with (0 if unknown / host). */
uint32_t system_info_task_stack_total(int index);

/* Prepare the data source (installs the temperature sensor on device; no-op on host). */
void system_info_init(void);

/* Read the Wi-Fi (bluetooth=false) or Bluetooth (bluetooth=true) MAC address.
 * On the host simulator both are all-zero. */
void system_info_get_mac(uint8_t mac[6], bool bluetooth);

/* Fill `info` with a fresh snapshot. Safe to call repeatedly. */
void system_info_collect(system_info_t *info);

/* Log the snapshot (device: ESP_LOG, host: stdout). */
void system_info_print(const system_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* MODIPAD_SYSTEM_INFO_H */
