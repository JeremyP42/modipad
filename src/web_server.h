/*
 * web_server.h - WiFi access point + HTTP configuration server.
 */
#ifndef MODIPAD_WEB_SERVER_H
#define MODIPAD_WEB_SERVER_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the HTTP server; the WiFi interface is owned by wifi_manager. */
esp_err_t web_server_start_http(void);

void web_server_loop(void);
bool web_server_running(void);

#ifdef __cplusplus
}
#endif

#endif /* MODIPAD_WEB_SERVER_H */
