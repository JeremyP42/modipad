/*
 * keyboard_manager.h - Keyboard layer (Bluetooth LE HID).
 *
 * The rest of the application only talks to this module; it forwards HID
 * reports to the BLE HID profile.
 */
#ifndef MODIPAD_KEYBOARD_MANAGER_H
#define MODIPAD_KEYBOARD_MANAGER_H

#include <stdbool.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise the BLE keyboard transport. When `enable_ble` is false the BLE
 * stack is not brought up (saving internal RAM) and can be enabled later from
 * the settings UI. */
void init_keyboard(bool enable_ble);

/* Enable/disable the BLE transport at runtime (also works when it was skipped
 * at boot: the stack is initialised lazily on first enable). */
void keyboard_set_ble_enabled(bool enabled);
bool keyboard_ble_enabled(void);

/* True when the BLE transport has an attached host. */
bool is_keyboard_connected(void);

/* Send a hotkey combination such as "CTRL+C" or "CTRL+SHIFT+S". */
void send_hotkey(const char *keys);

/* Type a plain ASCII string. */
void send_text(const char *text);

/*
 * Send a BLE HID Consumer Control (multimedia) command such as "PLAY_PAUSE",
 * "NEXT", "PREV", "VOL_UP", "VOL_DOWN" or "MUTE" (numeric usage ids are also
 * accepted). Only the BLE-connected host is targeted; returns false and shows
 * a toast when nothing is connected over BLE.
 */
bool send_multimedia(const char *command);

/* Poll transports (call periodically). */
void keyboard_loop(void);

/* Called whenever the BLE connection state changes. */
void keyboard_set_connection_cb(void (*cb)(bool ble_connected));

#ifdef __cplusplus
}
#endif

#endif /* MODIPAD_KEYBOARD_MANAGER_H */
