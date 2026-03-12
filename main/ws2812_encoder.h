#ifndef WS2812_ENCODER_H
#define WS2812_ENCODER_H

#include <stdint.h>

#include "driver/rmt_encoder.h"
#include "esp_err.h"

typedef struct {
    uint32_t resolution_hz;
} ws2812_encoder_config_t;

esp_err_t ws2812_new_encoder(const ws2812_encoder_config_t *config,
                             rmt_encoder_handle_t *ret_encoder);

#endif
