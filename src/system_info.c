/*
 * system_info.c - Runtime system information for the About page.
 *
 * Two backends:
 *   - device (ESP32-S3 / ESP-IDF): real hardware data
 *   - host  (HOST_BUILD, PC simulators): best-effort data from the OS
 *
 * All memory/filesystem values are stored in KB.
 */
#include "system_info.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* FreeRTOS task registry (shared by both backends)                    */
/* ------------------------------------------------------------------ */
#define SYSINFO_TASK_MAX 16
typedef struct {
    char name[16];
    void *handle;
    uint32_t stack_bytes;
} sysinfo_task_t;
static sysinfo_task_t s_tasks[SYSINFO_TASK_MAX];
static int s_task_count = 0;

void system_info_register_task(const char *name, void *handle, uint32_t stack_bytes)
{
    if (name == NULL || handle == NULL || s_task_count >= SYSINFO_TASK_MAX) {
        return;
    }
    strncpy(s_tasks[s_task_count].name, name, sizeof(s_tasks[0].name) - 1);
    s_tasks[s_task_count].name[sizeof(s_tasks[0].name) - 1] = '\0';
    s_tasks[s_task_count].handle = handle;
    s_tasks[s_task_count].stack_bytes = stack_bytes;
    s_task_count++;
}

int system_info_task_count(void)
{
    return s_task_count;
}

const char *system_info_task_name(int index)
{
    if (index < 0 || index >= s_task_count) {
        return "";
    }
    return s_tasks[index].name;
}

uint32_t system_info_task_stack_total(int index)
{
    if (index < 0 || index >= s_task_count) {
        return 0;
    }
    return s_tasks[index].stack_bytes;
}

#ifdef HOST_BUILD
uint32_t system_info_task_stack_free(int index)
{
    (void)index;
    return 0;
}
#else
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
uint32_t system_info_task_stack_free(int index)
{
    if (index < 0 || index >= s_task_count) {
        return 0;
    }
    return (uint32_t)uxTaskGetStackHighWaterMark((TaskHandle_t)s_tasks[index].handle) * sizeof(StackType_t);
}
#endif

#ifdef HOST_BUILD
/* ------------------------------------------------------------------ */
/* Host / simulator backend                                            */
/* ------------------------------------------------------------------ */
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static uint32_t clamp_u32(uint64_t v)
{
    return (v > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)v;
}

#if defined(_WIN32)
static DWORD s_start_tick;
static bool s_started = false;
#endif

void system_info_init(void)
{
#if defined(_WIN32)
    if (!s_started) {
        s_start_tick = GetTickCount();
        s_started = true;
    }
#endif
}

void system_info_get_mac(uint8_t mac[6], bool bluetooth)
{
    (void)bluetooth;
    memset(mac, 0, 6); /* no real radio on the host */
}

void system_info_collect(system_info_t *info)
{
    memset(info, 0, sizeof(*info));
    info->chip_model = "Host PC";
    info->idf_version = "n/a (simulator)";
    info->temperature_c = 0.0f;

#if defined(_WIN32)
    MEMORYSTATUSEX ms;
    memset(&ms, 0, sizeof(ms));
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        info->sram_total = clamp_u32(ms.ullTotalPhys / 1024ull);
        info->sram_free = clamp_u32(ms.ullAvailPhys / 1024ull);
        info->heap_total = info->sram_total;
        info->heap_free = info->sram_free;
    }

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    info->cores = (uint32_t)si.dwNumberOfProcessors;

    ULARGE_INTEGER free_bytes, total_bytes;
    if (GetDiskFreeSpaceExA(".", &free_bytes, &total_bytes, NULL)) {
        info->fs_total = clamp_u32(total_bytes.QuadPart / 1024ull);
        info->fs_free = clamp_u32(free_bytes.QuadPart / 1024ull);
    }

    if (!s_started) {
        system_info_init();
    }
    info->uptime_seconds = (GetTickCount() - s_start_tick) / 1000u;
#endif

    info->reset_reason = -1;
    info->build_date = __DATE__;
    info->build_time = __TIME__;
    info->sd_fs_type = "-";
    info->wifi_reconnects = 0;
}

void system_info_print(const system_info_t *info)
{
    printf("[SYSINFO] chip=%s #/%d, temp=%.1f C, uptime=%us\n",
           info->chip_model, (int)info->cores, info->temperature_c,
           (unsigned)info->uptime_seconds);
    printf("[SYSINFO] SRAM %u/%u KB, PSRAM %u/%u KB, Heap %u/%u KB, FS %u/%u KB\n",
           (unsigned)info->sram_free, (unsigned)info->sram_total,
           (unsigned)info->psram_free, (unsigned)info->psram_total,
           (unsigned)info->heap_free, (unsigned)info->heap_total,
           (unsigned)info->fs_free, (unsigned)info->fs_total);
}

#else
/* ------------------------------------------------------------------ */
/* ESP32-S3 / ESP-IDF backend                                          */
/* ------------------------------------------------------------------ */
#include "config.h"
#include "driver/temperature_sensor.h"
#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sd_card.h"
#include "wifi_manager.h"

static const char *TAG = "SYSINFO";

static temperature_sensor_handle_t s_temp_sensor = NULL;

void system_info_init(void)
{
    if (s_temp_sensor != NULL) {
        return;
    }
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    esp_err_t err = temperature_sensor_install(&cfg, &s_temp_sensor);
    if (err == ESP_OK) {
        err = temperature_sensor_enable(s_temp_sensor);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Temperature sensor unavailable (%s)", esp_err_to_name(err));
        s_temp_sensor = NULL;
    }
}

void system_info_get_mac(uint8_t mac[6], bool bluetooth)
{
    esp_read_mac(mac, bluetooth ? ESP_MAC_BT : ESP_MAC_WIFI_STA);
}

void system_info_collect(system_info_t *info)
{
    memset(info, 0, sizeof(*info));

    /* Memory (KB) */
    info->sram_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL) / 1024u;
    info->sram_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024u;
    info->psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024u;
    info->psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024u;
    info->heap_total = heap_caps_get_total_size(MALLOC_CAP_DEFAULT) / 1024u;
    info->heap_free = heap_caps_get_free_size(MALLOC_CAP_DEFAULT) / 1024u;

    /* Filesystem (KB) */
    size_t fs_total = 0;
    size_t fs_used = 0;
    if (esp_littlefs_info(LFS_PARTITION_LABEL, &fs_total, &fs_used) == ESP_OK) {
        info->fs_total = (uint32_t)(fs_total / 1024u);
        info->fs_free = (uint32_t)((fs_total - fs_used) / 1024u);
    }

    /* Temperature */
    if (s_temp_sensor != NULL) {
        float celsius = 0.0f;
        if (temperature_sensor_get_celsius(s_temp_sensor, &celsius) == ESP_OK) {
            info->temperature_c = celsius;
        }
    }

    /* Clocks */
#if defined(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ)
    info->cpu_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
#endif
#if defined(CONFIG_ESPTOOLPY_FLASHFREQ_80M)
    info->flash_freq_mhz = 80;
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_40M)
    info->flash_freq_mhz = 40;
#elif defined(CONFIG_ESPTOOLPY_FLASHFREQ_20M)
    info->flash_freq_mhz = 20;
#endif

    /* Chip */
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    switch (chip.model) {
    case CHIP_ESP32S3: info->chip_model = "ESP32-S3"; break;
    case CHIP_ESP32S2: info->chip_model = "ESP32-S2"; break;
    case CHIP_ESP32: info->chip_model = "ESP32"; break;
    default: info->chip_model = "Unknown"; break;
    }
    info->chip_revision = chip.revision;
    info->cores = chip.cores;
    info->idf_version = esp_get_idf_version();

    /* Uptime */
    info->uptime_seconds = (uint32_t)(esp_timer_get_time() / 1000000);

    /* MACs */
    esp_read_mac(info->wifi_mac, ESP_MAC_WIFI_STA);
    esp_read_mac(info->ble_mac, ESP_MAC_BT);

    /* Reset reason + build stamp */
    info->reset_reason = (int)esp_reset_reason();
    info->build_date = __DATE__;
    info->build_time = __TIME__;

    /* SD card */
    sd_card_info(&info->sd_present, &info->sd_total_kb, &info->sd_free_kb);
    info->sd_fs_type = sd_card_fs_type();

    /* Wi-Fi signal + link quality (0 when not associated) */
    info->wifi_rssi = wifi_manager_rssi();
    info->wifi_reconnects = wifi_manager_reconnect_count();
}

void system_info_print(const system_info_t *info)
{
    ESP_LOGI(TAG, "=== System Information ===");
    ESP_LOGI(TAG, "Chip: %s (rev %u, %u cores)", info->chip_model,
             (unsigned)info->chip_revision, (unsigned)info->cores);
    ESP_LOGI(TAG, "Temperature: %.1f C", info->temperature_c);
    ESP_LOGI(TAG, "SRAM: %u/%u KB free", (unsigned)info->sram_free,
             (unsigned)info->sram_total);
    ESP_LOGI(TAG, "PSRAM: %u/%u KB free", (unsigned)info->psram_free,
             (unsigned)info->psram_total);
    ESP_LOGI(TAG, "Heap: %u/%u KB free", (unsigned)info->heap_free,
             (unsigned)info->heap_total);
    ESP_LOGI(TAG, "FS: %u/%u KB free", (unsigned)info->fs_free,
             (unsigned)info->fs_total);
    ESP_LOGI(TAG, "CPU: %u MHz, Flash: %u MHz", (unsigned)info->cpu_freq_mhz,
             (unsigned)info->flash_freq_mhz);
    ESP_LOGI(TAG, "IDF: %s", info->idf_version);
    ESP_LOGI(TAG, "Uptime: %u seconds", (unsigned)info->uptime_seconds);
    ESP_LOGI(TAG, "WiFi MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             info->wifi_mac[0], info->wifi_mac[1], info->wifi_mac[2],
             info->wifi_mac[3], info->wifi_mac[4], info->wifi_mac[5]);
}

#endif /* HOST_BUILD */
