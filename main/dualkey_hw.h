#ifndef DUALKEY_HW_H
#define DUALKEY_HW_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    bool button_a;
    bool button_b;
} dualkey_buttons_t;

esp_err_t dualkey_hw_init(void);
dualkey_buttons_t dualkey_read_buttons(void);
esp_err_t dualkey_set_rgb(uint8_t r, uint8_t g, uint8_t b, bool enabled);

#endif
