/*
 * obs_client.h - OBS Studio control over WiFi (OBS WebSocket v5).
 *
 * The board is a WebSocket *client*: it connects directly to OBS running on a
 * PC on the same network (ws://<host>:<port>). No PC-side agent is required.
 *
 * Configuration lives in config.json:
 *   "obs": { "host": "192.168.1.10", "port": 4455, "password": "" }
 */
#ifndef MODIPAD_OBS_CLIENT_H
#define MODIPAD_OBS_CLIENT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OBS_STATE_UNKNOWN = 0,
    OBS_STATE_IDLE = 1,
    OBS_STATE_RECORDING = 2,
} obs_state_t;

typedef enum {
    OBS_CHECK_NONE = 0,
    OBS_CHECK_PENDING = 1,
    OBS_CHECK_OK = 2,
    OBS_CHECK_FAIL = 3,
} obs_check_result_t;

/* Start the background worker and load the saved config. */
void obs_client_init(void);

/* Periodic tick (called from the app loop; currently a no-op). */
void obs_client_loop(void);

/*
 * Run an OBS command from a button: "TOGGLE_REC", "START_REC", "STOP_REC" or
 * "CHECK". Shows a toast when there is no link.
 */
void obs_client_command(const char *command);

/* Ask the worker to (re)connect and report the result via obs_client_check_result(). */
void obs_client_request_check(void);

/* Latest connection check result. */
obs_check_result_t obs_client_check_result(void);

/* Last known recording state. */
obs_state_t obs_client_get_state(void);

/* Read/write the OBS settings (persisted to config.json). */
void obs_client_get_config(char *host, unsigned host_len,
                           int *port, char *password, unsigned pwd_len);
void obs_client_set_config(const char *host, int port, const char *password);

#ifdef __cplusplus
}
#endif

#endif /* MODIPAD_OBS_CLIENT_H */
