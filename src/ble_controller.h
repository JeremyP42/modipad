/*
 * ble_controller.h - Bluetooth LE HID keyboard (Bluedroid + HID profile)
 */
#ifndef MODIPAD_BLE_CONTROLLER_H
#define MODIPAD_BLE_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ble_controller_init(void);

/*
 * Enable or disable the BLE keyboard. Disabling stops advertising and drops
 * any active host connection; enabling (re)starts advertising, lazily bringing
 * up the stack if it was never initialised (so a device with BLE disabled at
 * boot never allocates the Bluedroid controller/HID memory).
 */
esp_err_t ble_controller_set_enabled(bool enabled);

/* True when BLE has been enabled by the user. */
bool ble_controller_enabled(void);

/* True when a host is paired and authenticated. */
bool ble_connected(void);

/* Send a keyboard report: modifier bitmask, key usage id, 1 = press / 0 = release. */
void ble_keyboard_send(uint8_t special_key_mask, uint8_t keyboard_cmd, uint8_t num_key);

/*
 * Send a Consumer Control (multimedia) usage id as a press + release pair,
 * e.g. 0xCD (Play/Pause), 0xB5 (Next), 0xB6 (Prev), 0xE9/0xEA (Vol +/-),
 * 0xE2 (Mute). No-op when no host is connected.
 */
void ble_consumer_send(uint8_t usage);

#ifdef __cplusplus
}
#endif

#endif /* MODIPAD_BLE_CONTROLLER_H */
