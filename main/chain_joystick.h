#ifndef CHAIN_JOYSTICK_H
#define CHAIN_JOYSTICK_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/uart.h"
#include "esp_err.h"

typedef struct {
    uart_port_t uart_port;
    uint8_t device_id;
    bool present;
} chain_joystick_t;

typedef struct {
    uint16_t x_raw;
    uint16_t y_raw;
} chain_joystick_sample_t;

esp_err_t chain_joystick_init(chain_joystick_t *ctx, uart_port_t uart_port, int tx_gpio, int rx_gpio, uint8_t device_id);
esp_err_t chain_joystick_probe(chain_joystick_t *ctx);
esp_err_t chain_joystick_read_raw(chain_joystick_t *ctx, chain_joystick_sample_t *sample);
esp_err_t chain_joystick_set_brightness(chain_joystick_t *ctx, uint8_t brightness);
esp_err_t chain_joystick_set_rgb(chain_joystick_t *ctx, uint8_t r, uint8_t g, uint8_t b);
esp_err_t chain_joystick_deinit(chain_joystick_t *ctx);

#endif
