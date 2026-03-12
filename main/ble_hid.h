#ifndef BLE_HID_H
#define BLE_HID_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Report ID 1: Keyboard — standard 8-byte report */
typedef struct __attribute__((packed)) {
    uint8_t modifiers;      /* ctrl/shift/alt/gui bitmask */
    uint8_t reserved;
    uint8_t keycodes[6];    /* up to 6 simultaneous keys */
} ble_hid_kb_report_t;

/* Report ID 2: Gamepad — 2 buttons + X/Y axes */
typedef struct __attribute__((packed)) {
    uint8_t  buttons;       /* bit 0 = A, bit 1 = B */
    uint16_t x;             /* 0–4095 */
    uint16_t y;             /* 0–4095 */
} ble_hid_gp_report_t;

esp_err_t ble_hid_init(const char *device_name);
bool      ble_hid_connected(void);
esp_err_t ble_hid_send_keyboard(const ble_hid_kb_report_t *report);
esp_err_t ble_hid_send_gamepad(const ble_hid_gp_report_t *report);

/* Connection interval control for power management */
esp_err_t ble_hid_conn_fast(void);   /* ~15ms interval — active input */
esp_err_t ble_hid_conn_slow(void);   /* ~1000ms interval — idle/sleep */

#endif
