#ifndef BLE_MIDI_H
#define BLE_MIDI_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t ble_midi_init(const char *device_name);
bool ble_midi_connected(void);

esp_err_t ble_midi_send_note(uint8_t channel, uint8_t note, uint8_t velocity, bool note_on);
esp_err_t ble_midi_send_control_change(uint8_t channel, uint8_t controller, uint8_t value);

#endif
