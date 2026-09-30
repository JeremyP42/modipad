/*
 * obs_client.c - Minimal OBS WebSocket v5 client (see obs_client.h).
 *
 * Implemented directly on top of the lwIP sockets already used by the project
 * (esp_http_server / esp_wifi). mbedTLS provides SHA-256 + Base64 for the
 * OBS authentication challenge. All network I/O happens on a dedicated task so
 * the LVGL/touch loop is never blocked.
 */
#include "obs_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "config.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i18n.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"
#include "system_info.h"
#include "toast.h"
#include "ui_loader.h"
#include "wifi_manager.h"

static const char *TAG = "obs_client";

static volatile bool s_connected = false;
static volatile int s_state = OBS_STATE_UNKNOWN;
static volatile int s_check = OBS_CHECK_NONE;
static volatile bool s_check_req = false;
/* Pending recording command: 0 = none, 1 = toggle, 2 = start, 3 = stop. */
static volatile int s_cmd = 0;

static int s_sock = -1;
static char s_host[64] = "";
static int s_port = 4455;
static char s_password[64] = "";

/* ------------------------------------------------------------------ */
/* Config                                                             */
/* ------------------------------------------------------------------ */
static void obs_load_config(void)
{
    cJSON *cfg = get_config();
    cJSON *obs = cfg ? cJSON_GetObjectItemCaseSensitive(cfg, "obs") : NULL;
    if (!cJSON_IsObject(obs)) {
        return;
    }

    cJSON *host = cJSON_GetObjectItemCaseSensitive(obs, "host");
    cJSON *port = cJSON_GetObjectItemCaseSensitive(obs, "port");
    cJSON *pass = cJSON_GetObjectItemCaseSensitive(obs, "password");

    if (cJSON_IsString(host)) {
        strncpy(s_host, host->valuestring, sizeof(s_host) - 1);
        s_host[sizeof(s_host) - 1] = '\0';
    }
    if (cJSON_IsNumber(port) && port->valueint > 0) {
        s_port = port->valueint;
    }
    if (cJSON_IsString(pass)) {
        strncpy(s_password, pass->valuestring, sizeof(s_password) - 1);
        s_password[sizeof(s_password) - 1] = '\0';
    }
}

void obs_client_get_config(char *host, unsigned host_len, int *port,
                           char *password, unsigned pwd_len)
{
    if (host && host_len) {
        strncpy(host, s_host, host_len - 1);
        host[host_len - 1] = '\0';
    }
    if (port) {
        *port = s_port;
    }
    if (password && pwd_len) {
        strncpy(password, s_password, pwd_len - 1);
        password[pwd_len - 1] = '\0';
    }
}

void obs_client_set_config(const char *host, int port, const char *password)
{
    if (host) {
        strncpy(s_host, host, sizeof(s_host) - 1);
        s_host[sizeof(s_host) - 1] = '\0';
    }
    if (port > 0) {
        s_port = port;
    }
    if (password) {
        strncpy(s_password, password, sizeof(s_password) - 1);
        s_password[sizeof(s_password) - 1] = '\0';
    }

    cJSON *cfg = get_config();
    if (cfg == NULL) {
        return;
    }
    cJSON *obs = cJSON_GetObjectItemCaseSensitive(cfg, "obs");
    if (obs == NULL) {
        obs = cJSON_AddObjectToObject(cfg, "obs");
    }
    if (obs == NULL) {
        return;
    }
    cJSON_DeleteItemFromObject(obs, "host");
    cJSON_DeleteItemFromObject(obs, "port");
    cJSON_DeleteItemFromObject(obs, "password");
    cJSON_AddStringToObject(obs, "host", s_host);
    cJSON_AddNumberToObject(obs, "port", s_port);
    cJSON_AddStringToObject(obs, "password", s_password);
    save_config();

    /* Force a reconnect with the new endpoint. */
    s_check_req = true;
    s_connected = false;
}

/* ------------------------------------------------------------------ */
/* Crypto helpers                                                     */
/* ------------------------------------------------------------------ */
static void sha256_bin(const char *in, unsigned char out[32])
{
    mbedtls_sha256((const unsigned char *)in, strlen(in), out, 0);
}

static void b64_encode(const unsigned char *in, size_t in_len, char *out, size_t out_len)
{
    size_t olen = 0;
    if (mbedtls_base64_encode((unsigned char *)out, out_len, &olen, in, in_len) != 0) {
        out[0] = '\0';
        return;
    }
    out[olen] = '\0';
}

/* OBS v5: base64(sha256(base64(sha256(password + salt)) + challenge)). */
static void compute_auth(const char *password, const char *salt,
                         const char *challenge, char *out, size_t out_len)
{
    char cat[192];
    snprintf(cat, sizeof(cat), "%s%s", password ? password : "", salt ? salt : "");

    unsigned char h1[32];
    sha256_bin(cat, h1);
    char b1[64];
    b64_encode(h1, sizeof(h1), b1, sizeof(b1));

    char cat2[256];
    snprintf(cat2, sizeof(cat2), "%s%s", b1, challenge ? challenge : "");

    unsigned char h2[32];
    sha256_bin(cat2, h2);
    b64_encode(h2, sizeof(h2), out, out_len);
}

static const char *json_string(cJSON *obj, const char *key)
{
    cJSON *item = obj ? cJSON_GetObjectItemCaseSensitive(obj, key) : NULL;
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

/* ------------------------------------------------------------------ */
/* WebSocket framing                                                  */
/* ------------------------------------------------------------------ */
static int recv_all(int fd, void *buf, size_t len)
{
    size_t got = 0;
    while (got < len) {
        int r = recv(fd, (char *)buf + got, len - got, 0);
        if (r <= 0) {
            return -1;
        }
        got += (size_t)r;
    }
    return (int)got;
}

static int ws_send_frame(int fd, unsigned char opcode, const char *payload)
{
    size_t len = strlen(payload);
    unsigned char hdr[14];
    int hl = 0;

    hdr[hl++] = (unsigned char)(0x80 | (opcode & 0x0F)); /* FIN + opcode */
    if (len < 126) {
        hdr[hl++] = (unsigned char)(0x80 | len);
    } else if (len <= 0xFFFF) {
        hdr[hl++] = 0x80 | 126;
        hdr[hl++] = (unsigned char)((len >> 8) & 0xFF);
        hdr[hl++] = (unsigned char)(len & 0xFF);
    } else {
        return -1;
    }

    unsigned char mask[4];
    for (int i = 0; i < 4; i++) {
        mask[i] = (unsigned char)(esp_random() & 0xFF);
    }
    memcpy(hdr + hl, mask, 4);
    hl += 4;

    if (send(fd, hdr, hl, 0) != hl) {
        return -1;
    }

    unsigned char buf[256];
    for (size_t off = 0; off < len;) {
        size_t n = len - off;
        if (n > sizeof(buf)) {
            n = sizeof(buf);
        }
        for (size_t i = 0; i < n; i++) {
            buf[i] = (unsigned char)(payload[off + i] ^ mask[(off + i) & 3]);
        }
        if (send(fd, buf, n, 0) != (int)n) {
            return -1;
        }
        off += n;
    }
    return 0;
}

static int ws_send_text(int fd, const char *payload)
{
    return ws_send_frame(fd, 0x01, payload);
}

/* Receive one frame; returns the opcode and null-terminates `out`. */
static int ws_recv_frame(int fd, char *out, size_t out_len)
{
    unsigned char b0 = 0, b1 = 0;
    if (recv_all(fd, &b0, 1) != 1 || recv_all(fd, &b1, 1) != 1) {
        return -1;
    }

    int opcode = b0 & 0x0F;
    bool masked = (b1 & 0x80) != 0;
    unsigned long long len = (unsigned long long)(b1 & 0x7F);

    if (len == 126) {
        unsigned char e[2];
        if (recv_all(fd, e, 2) != 2) {
            return -1;
        }
        len = ((unsigned long long)e[0] << 8) | e[1];
    } else if (len == 127) {
        unsigned char e[8];
        if (recv_all(fd, e, 8) != 8) {
            return -1;
        }
        len = 0;
        for (int i = 0; i < 8; i++) {
            len = (len << 8) | e[i];
        }
    }

    unsigned char mask[4] = {0, 0, 0, 0};
    if (masked && recv_all(fd, mask, 4) != 4) {
        return -1;
    }

    size_t cap = (len < out_len - 1) ? (size_t)len : out_len - 1;
    size_t got = 0;
    unsigned char chunk[1024];
    while (got < len) {
        size_t want = len - got;
        if (want > sizeof(chunk)) {
            want = sizeof(chunk);
        }
        if (recv_all(fd, chunk, want) != (int)want) {
            return -1;
        }
        for (size_t i = 0; i < want; i++) {
            unsigned char c = masked ? (unsigned char)(chunk[i] ^ mask[(got + i) & 3]) : chunk[i];
            if (got + i < cap) {
                out[got + i] = (char)c;
            }
        }
        got += want;
    }
    out[cap] = '\0';
    return opcode;
}

static void ws_send_pong(int fd, const char *payload)
{
    (void)ws_send_frame(fd, 0x0A, payload ? payload : "");
}

/* ------------------------------------------------------------------ */
/* OBS protocol                                                       */
/* ------------------------------------------------------------------ */
static int ws_connect_obs(void)
{
    if (s_host[0] == '\0') {
        return -1;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%d", s_port);

    struct addrinfo *res = NULL;
    if (getaddrinfo(s_host, portstr, &hints, &res) != 0 || res == NULL) {
        ESP_LOGW(TAG, "Cannot resolve OBS host \"%s\"", s_host);
        return -1;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        return -1;
    }

    struct timeval tv = {.tv_sec = 3, .tv_usec = 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(fd, res->ai_addr, res->ai_addrlen) != 0) {
        close(fd);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);

    unsigned char nonce[16];
    for (int i = 0; i < 16; i++) {
        nonce[i] = (unsigned char)(esp_random() & 0xFF);
    }
    char key[32];
    b64_encode(nonce, sizeof(nonce), key, sizeof(key));

    char req[512];
    snprintf(req, sizeof(req),
             "GET / HTTP/1.1\r\n"
             "Host: %s:%d\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: %s\r\n"
             "Sec-WebSocket-Version: 13\r\n\r\n",
             s_host, s_port, key);

    if (send(fd, req, strlen(req), 0) != (int)strlen(req)) {
        close(fd);
        return -1;
    }

    /* Read the HTTP response byte-by-byte so we never consume the first
     * WebSocket frame (OBS sends the Hello immediately after the 101). */
    char resp[1024];
    int total = 0;
    resp[0] = '\0';
    while (total < (int)sizeof(resp) - 1) {
        char ch;
        if (recv(fd, &ch, 1, 0) != 1) {
            close(fd);
            return -1;
        }
        resp[total++] = ch;
        resp[total] = '\0';
        if (total >= 4 && resp[total - 4] == '\r' && resp[total - 3] == '\n' &&
            resp[total - 2] == '\r' && resp[total - 1] == '\n') {
            break;
        }
    }
    if (strstr(resp, "101") == NULL) {
        close(fd);
        return -1;
    }

    return fd;
}

static int obs_identify(int fd)
{
    char buf[4096];
    if (ws_recv_frame(fd, buf, sizeof(buf)) != 0x01) {
        return -1;
    }

    cJSON *hello = cJSON_Parse(buf);
    cJSON *d = hello ? cJSON_GetObjectItemCaseSensitive(hello, "d") : NULL;
    cJSON *auth = d ? cJSON_GetObjectItemCaseSensitive(d, "authentication") : NULL;

    char auth_str[128] = "";
    if (cJSON_IsObject(auth)) {
        const char *challenge = json_string(auth, "challenge");
        const char *salt = json_string(auth, "salt");
        if (s_password[0] == '\0') {
            ESP_LOGW(TAG, "OBS requires a password but none is configured");
            cJSON_Delete(hello);
            return -1;
        }
        compute_auth(s_password, salt, challenge, auth_str, sizeof(auth_str));
    }
    cJSON_Delete(hello);

    char identify[320];
    if (auth_str[0] != '\0') {
        snprintf(identify, sizeof(identify),
                 "{\"op\":1,\"d\":{\"rpcVersion\":1,\"eventSubscriptions\":0,\"authentication\":\"%s\"}}",
                 auth_str);
    } else {
        snprintf(identify, sizeof(identify),
                 "{\"op\":1,\"d\":{\"rpcVersion\":1,\"eventSubscriptions\":0}}");
    }

    if (ws_send_text(fd, identify) != 0) {
        return -1;
    }

    if (ws_recv_frame(fd, buf, sizeof(buf)) != 0x01) {
        return -1;
    }
    cJSON *reply = cJSON_Parse(buf);
    cJSON *op = reply ? cJSON_GetObjectItemCaseSensitive(reply, "op") : NULL;
    bool ok = cJSON_IsNumber(op) && op->valueint == 2; /* Identified */
    cJSON_Delete(reply);
    return ok ? 0 : -1;
}

static int obs_open(void)
{
    int fd = ws_connect_obs();
    if (fd < 0) {
        return -1;
    }
    if (obs_identify(fd) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void obs_close(void)
{
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }
    s_connected = false;
}

static int obs_request(int fd, const char *type, char *out, size_t out_len)
{
    char req[192];
    snprintf(req, sizeof(req),
             "{\"op\":6,\"d\":{\"requestType\":\"%s\",\"requestId\":\"mpad\"}}", type);
    if (ws_send_text(fd, req) != 0) {
        return -1;
    }

    for (int i = 0; i < 12; i++) {
        int op = ws_recv_frame(fd, out, out_len);
        if (op < 0) {
            return -1;
        }
        if (op == 0x09) { /* ping */
            ws_send_pong(fd, out);
            continue;
        }
        if (op == 0x08) { /* close */
            return -1;
        }
        if (op != 0x01) {
            continue;
        }
        cJSON *j = cJSON_Parse(out);
        cJSON *opj = j ? cJSON_GetObjectItemCaseSensitive(j, "op") : NULL;
        int opv = cJSON_IsNumber(opj) ? opj->valueint : -1;
        cJSON_Delete(j);
        if (opv == 7) { /* RequestResponse */
            return 0;
        }
    }
    return -1;
}

static bool parse_output_active(const char *json)
{
    bool active = false;
    cJSON *j = cJSON_Parse(json);
    if (j != NULL) {
        cJSON *d = cJSON_GetObjectItemCaseSensitive(j, "d");
        cJSON *rd = d ? cJSON_GetObjectItemCaseSensitive(d, "responseData") : NULL;
        cJSON *oa = rd ? cJSON_GetObjectItemCaseSensitive(rd, "outputActive") : NULL;
        if (cJSON_IsBool(oa)) {
            active = cJSON_IsTrue(oa);
        }
        cJSON_Delete(j);
    }
    return active;
}

static void obs_refresh_state(void)
{
    if (!s_connected) {
        return;
    }
    char buf[1024];
    if (obs_request(s_sock, "GetRecordStatus", buf, sizeof(buf)) == 0) {
        s_state = parse_output_active(buf) ? OBS_STATE_RECORDING : OBS_STATE_IDLE;
    } else {
        obs_close();
        s_state = OBS_STATE_UNKNOWN;
    }
}

/* ------------------------------------------------------------------ */
/* Worker task                                                        */
/* ------------------------------------------------------------------ */
static void obs_worker(void *arg)
{
    (void)arg;
    int64_t last_poll = 0;
    int64_t last_connect = 0;

    while (1) {
        int64_t now = esp_timer_get_time();

        if (s_check_req) {
            s_check_req = false;
            s_check = OBS_CHECK_PENDING;
            obs_close();
            int fd = obs_open();
            if (fd < 0) {
                s_check = OBS_CHECK_FAIL;
                s_state = OBS_STATE_UNKNOWN;
            } else {
                s_sock = fd;
                s_connected = true;
                s_check = OBS_CHECK_OK;
                last_connect = now;
                obs_refresh_state();
            }
        }

        if (!s_connected && (now - last_connect) > 5000000) {
            last_connect = now;
            /* Only bother when we actually have a router (STA) network; with
             * WiFi off there is no point trying to reach the OBS host. */
            if (wifi_manager_mode() == WIFI_APP_STA && wifi_manager_connected()) {
                int fd = obs_open();
                if (fd >= 0) {
                    s_sock = fd;
                    s_connected = true;
                    s_state = OBS_STATE_UNKNOWN;
                    obs_refresh_state();
                }
            }
        }

        if (s_cmd != 0) {
            int cmd = s_cmd;
            s_cmd = 0;
            if (!s_connected) {
                toast_show(tr("obs_no_link"));
            } else {
                char buf[1024];
                bool ok = true;
                if (cmd == 1) {
                    if (obs_request(s_sock, "GetRecordStatus", buf, sizeof(buf)) == 0) {
                        bool active = parse_output_active(buf);
                        ok = (obs_request(s_sock, active ? "StopRecord" : "StartRecord",
                                          buf, sizeof(buf)) == 0);
                    } else {
                        ok = false;
                    }
                } else if (cmd == 2) {
                    ok = (obs_request(s_sock, "StartRecord", buf, sizeof(buf)) == 0);
                } else {
                    ok = (obs_request(s_sock, "StopRecord", buf, sizeof(buf)) == 0);
                }
                if (ok) {
                    obs_refresh_state();
                } else {
                    obs_close();
                    s_state = OBS_STATE_UNKNOWN;
                    toast_show(tr("obs_no_link"));
                }
            }
        }

        if (s_connected && (now - last_poll) > 3000000) {
            last_poll = now;
            obs_refresh_state();
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */
void obs_client_init(void)
{
    obs_load_config();

    static bool started = false;
    if (started) {
        return;
    }
    started = true;
    TaskHandle_t h = NULL;
    /* Pin to core 0 with the rest of the network stack (UI/LVGL is on core 1). */
    xTaskCreatePinnedToCore(obs_worker, "obs_client", 6144, NULL, 4, &h, 0);
    system_info_register_task("obs_client", h, 6144);
    ESP_LOGI(TAG, "OBS client ready (host=\"%s\" port=%d)", s_host, s_port);
}

void obs_client_loop(void)
{
    /* The worker task owns all I/O. */
}

void obs_client_command(const char *command)
{
    if (command == NULL) {
        return;
    }

    if (strcasecmp(command, "CHECK") == 0) {
        obs_client_request_check();
        return;
    }

    int cmd = 1; /* TOGGLE by default */
    if (strcasecmp(command, "START_REC") == 0 || strcasecmp(command, "START") == 0) {
        cmd = 2;
    } else if (strcasecmp(command, "STOP_REC") == 0 || strcasecmp(command, "STOP") == 0) {
        cmd = 3;
    }

    if (!s_connected) {
        toast_show(tr("obs_no_link"));
    }
    s_cmd = cmd;
}

void obs_client_request_check(void)
{
    s_check = OBS_CHECK_PENDING;
    s_check_req = true;
}

obs_check_result_t obs_client_check_result(void)
{
    return (obs_check_result_t)s_check;
}

obs_state_t obs_client_get_state(void)
{
    return (obs_state_t)s_state;
}

