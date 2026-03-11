#ifndef DUALKEY_HW_H
#define DUALKEY_HW_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* LED indices: hardware has 2 WS2812 LEDs, one per button */
#define DUALKEY_LED_RIGHT  0
#define DUALKEY_LED_LEFT   1
#define DUALKEY_LED_COUNT  2

typedef struct {
    bool button_a;
    bool button_b;
} dualkey_buttons_t;

esp_err_t dualkey_hw_init(void);
dualkey_buttons_t dualkey_read_buttons(void);

/* Set a single LED by index (0 = right key, 1 = left key) */
esp_err_t dualkey_set_led(int index, uint8_t r, uint8_t g, uint8_t b);

/* Set both LEDs to the same color */
esp_err_t dualkey_set_rgb(uint8_t r, uint8_t g, uint8_t b, bool enabled);

/* Control LED power rail (for sleep/wake) */
esp_err_t dualkey_led_power(bool enabled);

#endif
