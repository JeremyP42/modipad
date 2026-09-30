/*
 * web_server.cpp - HTTP configuration server.
 *
 * Serves the web configurator from LittleFS and exposes a small JSON API for
 * reading/writing config.json / settings.json and controlling WiFi. The WiFi
 * interface itself is owned by wifi_manager.
 */
#include "web_server.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "config.h"
#include "config_backup.h"
#include "display_init.h"
#include "i18n.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ota_update.h"
#include "system_info.h"
#include "sys_log.h"
#include "ui_assets.h"
#include "ui_loader.h"
#include "wifi_manager.h"

static const char *TAG = "web_server";

static httpd_handle_t s_server = NULL;
static bool s_running = false;

/* ------------------------------------------------------------------ */
/* Static file helpers                                                */
/* ------------------------------------------------------------------ */
/* Stream a file in small chunks: the previous version malloc'ed the whole file
 * (page backgrounds / SD images can be hundreds of KB), which OOM'ed and
 * stalled the server. Chunked sending keeps RAM flat. `cache` is the
 * Cache-Control header value (versioned static files are cached long-term). */
#define CACHE_STATIC "public, max-age=31536000, immutable"
#define CACHE_IMAGE  "public, max-age=604800"
#define CACHE_HTML   "no-cache"
#define CACHE_NONE   "no-store"

static void serve_file(httpd_req_t *req, const char *path, const char *type, const char *cache)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGW(TAG, "404: %s", path);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0) {
        fclose(f);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Bad file");
        return;
    }

    httpd_resp_set_type(req, type);
    httpd_resp_set_hdr(req, "Cache-Control", cache != NULL ? cache : CACHE_NONE);

    char buf[2048];
    long sent = 0;
    while (sent < len) {
        size_t want = (size_t)((len - sent) > (long)sizeof(buf) ? sizeof(buf) : (len - sent));
        size_t rd = fread(buf, 1, want, f);
        if (rd == 0) {
            break;
        }
        if (httpd_resp_send_chunk(req, buf, rd) != ESP_OK) {
            break;
        }
        sent += (long)rd;
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0); /* terminate the chunked response */
}

static esp_err_t save_body_to_file(httpd_req_t *req, const char *path)
{
    int total = req->content_len;
    if (total <= 0 || total > 32768) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body size");
        return ESP_FAIL;
    }

    char *buf = (char *)malloc((size_t)total + 1);
    if (buf == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    int received = 0;
    while (received < total) {
        int ret = httpd_req_recv(req, buf + received, total - received);
        if (ret <= 0) {
            free(buf);
            httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Receive failed");
            return ESP_FAIL;
        }
        received += ret;
    }
    buf[received] = '\0';

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        free(buf);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Cannot write file");
        return ESP_FAIL;
    }
    fwrite(buf, 1, (size_t)received, f);
    fclose(f);
    free(buf);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* HTTP handlers                                                      */
/* ------------------------------------------------------------------ */
static esp_err_t index_handler(httpd_req_t *req)
{
    serve_file(req, WEB_DIR "/index.html", "text/html", CACHE_HTML);
    return ESP_OK;
}

static esp_err_t css_handler(httpd_req_t *req)
{
    serve_file(req, WEB_DIR "/style.css", "text/css", CACHE_STATIC);
    return ESP_OK;
}

static esp_err_t js_handler(httpd_req_t *req)
{
    serve_file(req, WEB_DIR "/app.js", "application/javascript", CACHE_STATIC);
    return ESP_OK;
}

static esp_err_t config_get_handler(httpd_req_t *req)
{
    serve_file(req, CONFIG_FILE, "application/json", CACHE_NONE);
    return ESP_OK;
}

static esp_err_t config_post_handler(httpd_req_t *req)
{
    esp_err_t err = save_body_to_file(req, CONFIG_FILE);
    if (err == ESP_OK) {
        /* Re-parse the uploaded JSON into the in-RAM config. Without this a
         * later device-side save_config() (language change, Wi-Fi configure,
         * ...) would write the stale RAM copy back and discard the upload.
         * The UI/pages are rebuilt on the next reboot (web "Reboot" button). */
        load_config();
        i18n_init(); /* apply settings.language */
    }
    return err;
}

static esp_err_t settings_get_handler(httpd_req_t *req)
{
    serve_file(req, SETTINGS_FILE, "application/json", CACHE_NONE);
    return ESP_OK;
}

static esp_err_t settings_post_handler(httpd_req_t *req)
{
    esp_err_t err = save_body_to_file(req, SETTINGS_FILE);
    if (err == ESP_OK) {
        /* Refresh RAM and apply what can be applied live (brightness). The
         * radio mode / other settings take effect on the next reboot. */
        load_settings(get_settings_mut());
        set_brightness(get_settings()->brightness);
    }
    return err;
}

static void restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(400));
    esp_restart();
}

static esp_err_t reload_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"rebooting\"}");
    xTaskCreate(restart_task, "restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

/* Copy every asset used by the current config from SD into the device FS. */
static esp_err_t fs_sync_handler(httpd_req_t *req)
{
    int copied = asset_sync_used();
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"status\":\"ok\",\"copied\":%d}", copied);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Image library API                                                  */
/* ------------------------------------------------------------------ */
static const char *content_type_for(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (dot == NULL) {
        return "application/octet-stream";
    }
    if (strcasecmp(dot, ".png") == 0) return "image/png";
    if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (strcasecmp(dot, ".gif") == 0) return "image/gif";
    if (strcasecmp(dot, ".webp") == 0) return "image/webp";
    if (strcasecmp(dot, ".css") == 0) return "text/css";
    if (strcasecmp(dot, ".js") == 0) return "application/javascript";
    if (strcasecmp(dot, ".html") == 0) return "text/html";
    if (strcasecmp(dot, ".json") == 0) return "application/json";
    return "application/octet-stream";
}

static bool query_value(httpd_req_t *req, const char *key, char *out, size_t out_len)
{
    size_t qlen = httpd_req_get_url_query_len(req) + 1;
    if (qlen <= 1) {
        return false;
    }
    char *query = (char *)malloc(qlen);
    if (query == NULL) {
        return false;
    }
    bool ok = false;
    if (httpd_req_get_url_query_str(req, query, qlen) == ESP_OK) {
        if (httpd_query_key_value(query, key, out, out_len) == ESP_OK) {
            ok = true;
        }
    }
    free(query);
    return ok;
}

/* Build a sanitised VFS path from a query parameter. A leading "sd:" selects
 * the SD card store (/sdcard/modipad), otherwise the internal LittleFS. */
static bool safe_store_path(httpd_req_t *req, const char *key, char *out, size_t out_len)
{
    char rel[160];
    if (!query_value(req, key, rel, sizeof(rel))) {
        return false;
    }
    while (rel[0] == '/') {
        memmove(rel, rel + 1, strlen(rel));
    }
    if (strstr(rel, "..") != NULL || rel[0] == '\0') {
        return false;
    }
    if (strncmp(rel, "sd:", 3) == 0) {
        snprintf(out, out_len, "/sdcard/modipad/%s", rel + 3);
    } else {
        snprintf(out, out_len, "/littlefs/%s", rel);
    }
    return true;
}

static esp_err_t images_handler(httpd_req_t *req)
{
    if (strstr(req->uri, "..") != NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad path");
        return ESP_OK;
    }
    char path[600];
    snprintf(path, sizeof(path), "/littlefs%s", req->uri);
    serve_file(req, path, content_type_for(path), CACHE_IMAGE);
    return ESP_OK;
}

/* Serve media from the SD card store (/sdcard/modipad). */
static esp_err_t sd_images_handler(httpd_req_t *req)
{
    if (strstr(req->uri, "..") != NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad path");
        return ESP_OK;
    }
    char path[600];
    snprintf(path, sizeof(path), "/sdcard/modipad%s", req->uri + strlen("/sdimages"));
    serve_file(req, path, content_type_for(path), CACHE_IMAGE);
    return ESP_OK;
}

/* Serve the web UI translation files from /littlefs/web/locales. */
static esp_err_t locales_handler(httpd_req_t *req)
{
    if (strstr(req->uri, "..") != NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad path");
        return ESP_OK;
    }
    char path[600];
    snprintf(path, sizeof(path), "/littlefs/web%s", req->uri);
    serve_file(req, path, content_type_for(path), CACHE_STATIC);
    return ESP_OK;
}

static esp_err_t images_list_handler(httpd_req_t *req)
{
    char dir[128] = "images";
    char val[128];
    if (query_value(req, "dir", val, sizeof(val))) {
        while (val[0] == '/') {
            memmove(val, val + 1, strlen(val));
        }
        strncpy(dir, val, sizeof(dir) - 1);
        dir[sizeof(dir) - 1] = '\0';
    }
    if (strstr(dir, "..") != NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad dir");
        return ESP_OK;
    }

    char vfs[192];
    if (strncmp(dir, "sd:", 3) == 0) {
        snprintf(vfs, sizeof(vfs), "/sdcard/modipad/%s", dir + 3);
    } else {
        snprintf(vfs, sizeof(vfs), "/littlefs/%s", dir);
    }

    cJSON *arr = cJSON_CreateArray();
    DIR *d = opendir(vfs);
    if (d != NULL) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (ent->d_name[0] == '.') {
                continue;
            }
            char full[520];
            snprintf(full, sizeof(full), "%s/%s", vfs, ent->d_name);
            bool is_dir = false;
            DIR *sub = opendir(full);
            if (sub != NULL) {
                is_dir = true;
                closedir(sub);
            } else {
                FILE *f = fopen(full, "rb");
                if (f == NULL) {
                    continue;
                }
                fclose(f);
            }
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "name", ent->d_name);
            cJSON_AddBoolToObject(item, "dir", is_dir);
            cJSON_AddItemToArray(arr, item);
        }
        closedir(d);
    }

    ESP_LOGI(TAG, "images?dir=%s (%s) -> %d items", dir, vfs, cJSON_GetArraySize(arr));

    char *out = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr(req, out ? out : "[]");
    if (out) {
        cJSON_free(out);
    }
    return ESP_OK;
}

static esp_err_t upload_handler(httpd_req_t *req)
{
    char vfs[192];
    if (!safe_store_path(req, "path", vfs, sizeof(vfs))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad path");
        return ESP_OK;
    }

    int total = req->content_len;
    if (total <= 0 || total > (2 * 1024 * 1024)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad body size");
        return ESP_OK;
    }

    FILE *f = fopen(vfs, "wb");
    if (f == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Cannot open file");
        return ESP_OK;
    }

    char buf[1024];
    int received = 0;
    while (received < total) {
        int chunk = total - received;
        if (chunk > (int)sizeof(buf)) {
            chunk = (int)sizeof(buf);
        }
        int r = httpd_req_recv(req, buf, chunk);
        if (r <= 0) {
            fclose(f);
            remove(vfs);
            httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Receive failed");
            return ESP_OK;
        }
        fwrite(buf, 1, (size_t)r, f);
        received += r;
    }
    fclose(f);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t delete_handler(httpd_req_t *req)
{
    char vfs[192];
    if (!safe_store_path(req, "path", vfs, sizeof(vfs))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad path");
        return ESP_OK;
    }

    int rc = remove(vfs);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, rc == 0 ? "{\"status\":\"deleted\"}" : "{\"status\":\"error\"}");
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* HTTP server lifecycle (WiFi is owned by wifi_manager)              */
/* ------------------------------------------------------------------ */
static esp_err_t system_get_handler(httpd_req_t *req)
{
    system_info_t si;
    system_info_collect(&si);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_OK;
    }
    cJSON_AddStringToObject(root, "chip", si.chip_model ? si.chip_model : "Unknown");
    cJSON_AddNumberToObject(root, "chip_revision", si.chip_revision);
    cJSON_AddNumberToObject(root, "cores", si.cores);
    cJSON_AddNumberToObject(root, "temperature_c", (double)si.temperature_c);
    cJSON_AddNumberToObject(root, "cpu_freq_mhz", si.cpu_freq_mhz);
    cJSON_AddNumberToObject(root, "flash_freq_mhz", si.flash_freq_mhz);
    cJSON_AddNumberToObject(root, "sram_free", si.sram_free);
    cJSON_AddNumberToObject(root, "sram_total", si.sram_total);
    cJSON_AddNumberToObject(root, "psram_free", si.psram_free);
    cJSON_AddNumberToObject(root, "psram_total", si.psram_total);
    cJSON_AddNumberToObject(root, "heap_free", si.heap_free);
    cJSON_AddNumberToObject(root, "heap_total", si.heap_total);
    cJSON_AddNumberToObject(root, "fs_free", si.fs_free);
    cJSON_AddNumberToObject(root, "fs_total", si.fs_total);
    cJSON_AddNumberToObject(root, "uptime_seconds", si.uptime_seconds);
    cJSON_AddStringToObject(root, "idf_version", si.idf_version ? si.idf_version : "-");
    cJSON_AddNumberToObject(root, "reset_reason", si.reset_reason);
    cJSON_AddStringToObject(root, "build_date", si.build_date ? si.build_date : "-");
    cJSON_AddStringToObject(root, "build_time", si.build_time ? si.build_time : "-");
    cJSON_AddBoolToObject(root, "sd_present", si.sd_present);
    cJSON_AddNumberToObject(root, "sd_total", si.sd_total_kb);
    cJSON_AddNumberToObject(root, "sd_free", si.sd_free_kb);
    cJSON_AddStringToObject(root, "sd_fs", si.sd_fs_type ? si.sd_fs_type : "-");
    cJSON_AddNumberToObject(root, "wifi_rssi", si.wifi_rssi);
    cJSON_AddNumberToObject(root, "wifi_reconnects", si.wifi_reconnects);

    cJSON *tasks = cJSON_AddArrayToObject(root, "tasks");
    int tn = system_info_task_count();
    for (int i = 0; i < tn; i++) {
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "name", system_info_task_name(i));
        cJSON_AddNumberToObject(t, "stack_free", system_info_task_stack_free(i));
        cJSON_AddNumberToObject(t, "stack_total", system_info_task_stack_total(i));
        cJSON_AddItemToArray(tasks, t);
    }

    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             si.wifi_mac[0], si.wifi_mac[1], si.wifi_mac[2],
             si.wifi_mac[3], si.wifi_mac[4], si.wifi_mac[5]);
    cJSON_AddStringToObject(root, "wifi_mac", mac);
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             si.ble_mac[0], si.ble_mac[1], si.ble_mac[2],
             si.ble_mac[3], si.ble_mac[4], si.ble_mac[5]);
    cJSON_AddStringToObject(root, "bt_mac", mac);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (out == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_OK;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, out);
    cJSON_free(out);
    return ESP_OK;
}

/* Recent device log (RAM ring buffer), for the web Diagnostics panel. */
static esp_err_t log_get_handler(httpd_req_t *req)
{
    char *buf = (char *)malloc(SYS_LOG_BYTES + 1);
    if (buf == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_OK;
    }
    size_t n = sys_log_get(buf, SYS_LOG_BYTES + 1);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, buf, n);
    free(buf);
    return ESP_OK;
}

/* ---- SD config backups ---- */
static bool read_json_body(httpd_req_t *req, char *buf, size_t buf_len);

static esp_err_t backup_list_handler(httpd_req_t *req)
{
    static char names[32][64];
    int n = config_backup_list(names, 32);
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON_AddItemToArray(arr, cJSON_CreateString(names[i]));
    }
    char *out = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, out ? out : "[]");
    if (out) {
        cJSON_free(out);
    }
    return ESP_OK;
}

static esp_err_t backup_save_handler(httpd_req_t *req)
{
    char name[64] = "";
    int rc = config_backup_save(name, sizeof(name));
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"status\":\"%s\",\"name\":\"%s\"}",
             rc == 0 ? "ok" : "error", name);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t backup_import_handler(httpd_req_t *req)
{
    char body[128];
    if (!read_json_body(req, body, sizeof(body))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad body");
        return ESP_OK;
    }
    cJSON *j = cJSON_Parse(body);
    cJSON *n = j ? cJSON_GetObjectItemCaseSensitive(j, "name") : NULL;
    bool ok = cJSON_IsString(n) && config_backup_import(n->valuestring);
    cJSON_Delete(j);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, ok ? "{\"status\":\"ok\"}" : "{\"status\":\"error\"}");
    if (ok) {
        xTaskCreate(restart_task, "restart", 2048, NULL, 5, NULL);
    }
    return ESP_OK;
}

/* ---- Firmware update (OTA) ---- */
static esp_err_t ota_status_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, ota_sd_update_present()
                                ? "{\"sd_update\":true}"
                                : "{\"sd_update\":false}");
    return ESP_OK;
}

/* Stream the request body (a .bin image) into the inactive OTA slot. */
static esp_err_t ota_post_handler(httpd_req_t *req)
{
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (part == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_OK;
    }
    int remaining = req->content_len;
    if (remaining <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_OK;
    }

    esp_ota_handle_t h = 0;
    if (esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &h) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota_begin");
        return ESP_OK;
    }

    char buf[2048];
    while (remaining > 0) {
        int chunk = (remaining > (int)sizeof(buf)) ? (int)sizeof(buf) : remaining;
        int r = httpd_req_recv(req, buf, chunk);
        if (r <= 0) {
            esp_ota_abort(h);
            httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "recv");
            return ESP_OK;
        }
        if (esp_ota_write(h, buf, r) != ESP_OK) {
            esp_ota_abort(h);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write");
            return ESP_OK;
        }
        remaining -= r;
    }

    if (esp_ota_end(h) != ESP_OK || esp_ota_set_boot_partition(part) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "end");
        return ESP_OK;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    xTaskCreate(restart_task, "restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

/* Variant B: flash the staged update.bin from the SD card (user-confirmed). */
static esp_err_t ota_sd_handler(httpd_req_t *req)
{
    bool ok = ota_flash_file(OTA_SD_PATH);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, ok ? "{\"status\":\"ok\"}" : "{\"status\":\"error\"}");
    if (ok) {
        xTaskCreate(restart_task, "restart", 2048, NULL, 5, NULL);
    }
    return ESP_OK;
}

/* Available Roboto sizes on the device FS and the SD card (for text buttons). */
static esp_err_t fonts_get_handler(httpd_req_t *req)
{
    int sizes[32];
    int n = 0;
    const char *dirs[2] = { "/littlefs/fonts", "/sdcard/modipad/fonts" };
    for (int d = 0; d < 2; d++) {
        DIR *dir = opendir(dirs[d]);
        if (dir == NULL) {
            continue;
        }
        struct dirent *e;
        while ((e = readdir(dir)) != NULL) {
            /* Only "roboto_<N>.bin" (skip the "_bold" variants: weight is a
             * separate setting, and both files would add duplicate sizes). */
            if (strncmp(e->d_name, "roboto_", 7) != 0 || strstr(e->d_name, "_bold") != NULL) {
                continue;
            }
            int num = 0;
            char ext[8];
            if (sscanf(e->d_name, "roboto_%d.%7s", &num, ext) == 2 &&
                strcmp(ext, "bin") == 0 && num >= 8 && num <= 48) {
                bool seen = false;
                for (int i = 0; i < n; i++) {
                    if (sizes[i] == num) {
                        seen = true;
                    }
                }
                if (!seen && n < 32) {
                    sizes[n++] = num;
                }
            }
        }
        closedir(dir);
    }
    if (n == 0) {
        static const int def[] = {10, 12, 14, 16, 18};
        for (int i = 0; i < 5; i++) {
            sizes[n++] = def[i];
        }
    }
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (sizes[i] > sizes[j]) {
                int t = sizes[i];
                sizes[i] = sizes[j];
                sizes[j] = t;
            }
        }
    }
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(sizes[i]));
    }
    char *out = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, out ? out : "[]");
    if (out) {
        cJSON_free(out);
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* WiFi control API                                                   */
/* ------------------------------------------------------------------ */
/* Read a small JSON request body into `buf` (null-terminated). */
static bool read_json_body(httpd_req_t *req, char *buf, size_t buf_len)
{
    int total = req->content_len;
    if (total <= 0 || (size_t)total >= buf_len) {
        return false;
    }
    int received = 0;
    while (received < total) {
        int ret = httpd_req_recv(req, buf + received, total - received);
        if (ret <= 0) {
            return false;
        }
        received += ret;
    }
    buf[received] = '\0';
    return true;
}

/* WiFi (re)configuration is performed on a helper task a moment later so the
 * HTTP response is flushed before the interface (and the connection) changes. */
typedef struct {
    int kind; /* 0 = apply credentials, 1 = enable/disable */
    int mode;
    bool enabled;
    char ssid[64];
    char pass[64];
} wifi_job_t;

static void wifi_job_task(void *arg)
{
    wifi_job_t *job = (wifi_job_t *)arg;
    vTaskDelay(pdMS_TO_TICKS(350));
    /* Hand the work to the radio worker: it also toggles BLE (mutually
     * exclusive) and keeps the heavy WiFi calls off the LVGL task. */
    if (job->kind == 0) {
        wifi_manager_request_apply(job->mode, job->ssid, job->pass);
    } else if (job->enabled) {
        wifi_manager_request_enable(true);
    } else {
        wifi_manager_request_enable(false);
    }
    free(job);
    vTaskDelete(NULL);
}

static void wifi_spawn_job(wifi_job_t *job)
{
    if (xTaskCreate(wifi_job_task, "wifi_cmd", 4096, job, 4, NULL) != pdPASS) {
        free(job);
    }
}

static esp_err_t wifi_scan_post_handler(httpd_req_t *req)
{
    wifi_manager_request_scan();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"scanning\"}");
    return ESP_OK;
}

static esp_err_t wifi_scan_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_OK;
    }
    cJSON_AddBoolToObject(root, "running", wifi_manager_scan_running());
    cJSON *arr = cJSON_AddArrayToObject(root, "networks");
    int n = wifi_manager_scan_count();
    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", wifi_manager_scan_ssid(i));
        cJSON_AddNumberToObject(item, "rssi", wifi_manager_scan_rssi(i));
        cJSON_AddItemToArray(arr, item);
    }
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, out ? out : "{}");
    if (out) {
        cJSON_free(out);
    }
    return ESP_OK;
}

static esp_err_t wifi_apply_handler(httpd_req_t *req)
{
    char body[512];
    if (!read_json_body(req, body, sizeof(body))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad body");
        return ESP_OK;
    }

    cJSON *j = cJSON_Parse(body);
    if (j == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad JSON");
        return ESP_OK;
    }
    const char *mode = NULL;
    const char *ssid = NULL;
    const char *pass = NULL;
    cJSON *m = cJSON_GetObjectItemCaseSensitive(j, "mode");
    cJSON *s = cJSON_GetObjectItemCaseSensitive(j, "ssid");
    cJSON *p = cJSON_GetObjectItemCaseSensitive(j, "password");
    if (cJSON_IsString(m)) mode = m->valuestring;
    if (cJSON_IsString(s)) ssid = s->valuestring;
    if (cJSON_IsString(p)) pass = p->valuestring;
    int app_mode = (mode != NULL && strcmp(mode, "sta") == 0) ? WIFI_APP_STA : WIFI_APP_AP;

    /* Keep the single radio mode in sync with the requested Wi-Fi mode. */
    AppSettings *st = get_settings_mut();
    st->radio_mode = (app_mode == WIFI_APP_STA) ? RADIO_MODE_STA : RADIO_MODE_AP;
    st->wifi_enabled = true;
    st->ble_enabled = false;
    save_settings(st);

    wifi_job_t *job = (wifi_job_t *)calloc(1, sizeof(wifi_job_t));
    if (job != NULL) {
        job->kind = 0;
        job->mode = app_mode;
        if (ssid != NULL) snprintf(job->ssid, sizeof(job->ssid), "%s", ssid);
        if (pass != NULL) snprintf(job->pass, sizeof(job->pass), "%s", pass);
        wifi_spawn_job(job);
    }
    cJSON_Delete(j);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t wifi_state_handler(httpd_req_t *req)
{
    char body[128];
    if (!read_json_body(req, body, sizeof(body))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad body");
        return ESP_OK;
    }
    cJSON *j = cJSON_Parse(body);
    cJSON *en = j ? cJSON_GetObjectItemCaseSensitive(j, "enabled") : NULL;
    bool enabled = cJSON_IsBool(en) ? cJSON_IsTrue(en) : true;
    cJSON_Delete(j);

    AppSettings *settings = get_settings_mut();
    if (enabled) {
        settings->radio_mode = (wifi_manager_mode() == WIFI_APP_STA) ? RADIO_MODE_STA : RADIO_MODE_AP;
    } else {
        settings->radio_mode = RADIO_MODE_BLE;
    }
    settings->wifi_enabled = (settings->radio_mode != RADIO_MODE_BLE);
    settings->ble_enabled = (settings->radio_mode == RADIO_MODE_BLE);
    save_settings(settings);

    wifi_job_t *job = (wifi_job_t *)calloc(1, sizeof(wifi_job_t));
    if (job != NULL) {
        job->kind = 1;
        job->enabled = enabled;
        wifi_spawn_job(job);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t register_handlers(void)
{
    static const httpd_uri_t uris[] = {
        {.uri = "/",              .method = HTTP_GET,  .handler = index_handler, .user_ctx = NULL},
        {.uri = "/index.html",    .method = HTTP_GET,  .handler = index_handler, .user_ctx = NULL},
        {.uri = "/style.css",     .method = HTTP_GET,  .handler = css_handler, .user_ctx = NULL},
        {.uri = "/app.js",        .method = HTTP_GET,  .handler = js_handler, .user_ctx = NULL},
        {.uri = "/api/config",    .method = HTTP_GET,  .handler = config_get_handler, .user_ctx = NULL},
        {.uri = "/api/config",    .method = HTTP_POST, .handler = config_post_handler, .user_ctx = NULL},
        {.uri = "/api/settings",  .method = HTTP_GET,  .handler = settings_get_handler, .user_ctx = NULL},
        {.uri = "/api/settings",  .method = HTTP_POST, .handler = settings_post_handler, .user_ctx = NULL},
        {.uri = "/api/system",    .method = HTTP_GET,  .handler = system_get_handler, .user_ctx = NULL},
        {.uri = "/api/reload",    .method = HTTP_POST, .handler = reload_handler, .user_ctx = NULL},
        {.uri = "/api/log",       .method = HTTP_GET,  .handler = log_get_handler, .user_ctx = NULL},
        {.uri = "/api/backup/save",   .method = HTTP_POST, .handler = backup_save_handler, .user_ctx = NULL},
        {.uri = "/api/backup/list",   .method = HTTP_GET,  .handler = backup_list_handler, .user_ctx = NULL},
        {.uri = "/api/backup/import", .method = HTTP_POST, .handler = backup_import_handler, .user_ctx = NULL},
        {.uri = "/api/ota",           .method = HTTP_POST, .handler = ota_post_handler, .user_ctx = NULL},
        {.uri = "/api/ota/sd",        .method = HTTP_POST, .handler = ota_sd_handler, .user_ctx = NULL},
        {.uri = "/api/ota/status",    .method = HTTP_GET,  .handler = ota_status_handler, .user_ctx = NULL},
        {.uri = "/api/fonts",         .method = HTTP_GET,  .handler = fonts_get_handler, .user_ctx = NULL},
    {.uri = "/api/fs/sync",   .method = HTTP_POST, .handler = fs_sync_handler, .user_ctx = NULL},
        {.uri = "/images/*",      .method = HTTP_GET,  .handler = images_handler, .user_ctx = NULL},
        {.uri = "/sdimages/*",    .method = HTTP_GET,  .handler = sd_images_handler, .user_ctx = NULL},
        {.uri = "/locales/*",     .method = HTTP_GET,  .handler = locales_handler, .user_ctx = NULL},
        {.uri = "/api/images",    .method = HTTP_GET,  .handler = images_list_handler, .user_ctx = NULL},
        {.uri = "/api/upload",    .method = HTTP_POST, .handler = upload_handler, .user_ctx = NULL},
        {.uri = "/api/delete",    .method = HTTP_POST, .handler = delete_handler, .user_ctx = NULL},
        {.uri = "/api/wifi/scan", .method = HTTP_GET,  .handler = wifi_scan_get_handler, .user_ctx = NULL},
        {.uri = "/api/wifi/scan", .method = HTTP_POST, .handler = wifi_scan_post_handler, .user_ctx = NULL},
        {.uri = "/api/wifi/apply",.method = HTTP_POST, .handler = wifi_apply_handler, .user_ctx = NULL},
        {.uri = "/api/wifi/state",.method = HTTP_POST, .handler = wifi_state_handler, .user_ctx = NULL},
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        esp_err_t ret = httpd_register_uri_handler(s_server, &uris[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register %s: %s", uris[i].uri, esp_err_to_name(ret));
            return ret;
        }
    }
    return ESP_OK;
}

esp_err_t web_server_start_http(void)
{
    if (s_running) {
        return ESP_OK;
    }

    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.max_uri_handlers = 32;
    http_cfg.lru_purge_enable = true;
    http_cfg.uri_match_fn = httpd_uri_match_wildcard;
    /* Keep the web server on core 0 with WiFi/LWIP; LVGL runs on core 1.
     * Bigger stack because serve_file() holds a 2 KB chunk buffer. */
    http_cfg.core_id = 0;
    http_cfg.stack_size = 6144;

    if (httpd_start(&s_server, &http_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return ESP_FAIL;
    }

    if (register_handlers() != ESP_OK) {
        httpd_stop(s_server);
        s_server = NULL;
        return ESP_FAIL;
    }

    s_running = true;
    ESP_LOGI(TAG, "Web server started");
    return ESP_OK;
}



void web_server_loop(void)
{
    /* esp_http_server runs in its own task; nothing to do here. */
}

bool web_server_running(void)
{
    return s_running;
}
