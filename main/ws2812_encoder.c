#include "ws2812_encoder.h"

#include <stdlib.h>

#include "esp_check.h"

static const char *TAG = "ws2812_encoder";

typedef struct {
    rmt_encoder_t base;
    rmt_encoder_handle_t bytes_encoder;
    rmt_encoder_handle_t copy_encoder;
    rmt_symbol_word_t reset_code;
    int state;
} ws2812_encoder_t;

RMT_ENCODER_FUNC_ATTR
static size_t ws2812_encode(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                            const void *primary_data, size_t data_size,
                            rmt_encode_state_t *ret_state)
{
    ws2812_encoder_t *strip_encoder = __containerof(encoder, ws2812_encoder_t, base);
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    rmt_encode_state_t state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;

    switch (strip_encoder->state) {
    case 0:
        encoded_symbols += strip_encoder->bytes_encoder->encode(
            strip_encoder->bytes_encoder, channel, primary_data, data_size, &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            strip_encoder->state = 1;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
        /* falls through */
    case 1:
        encoded_symbols += strip_encoder->copy_encoder->encode(
            strip_encoder->copy_encoder, channel, &strip_encoder->reset_code,
            sizeof(strip_encoder->reset_code), &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            strip_encoder->state = RMT_ENCODING_RESET;
            state |= RMT_ENCODING_COMPLETE;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            state |= RMT_ENCODING_MEM_FULL;
            goto out;
        }
        break;
    default:
        state |= RMT_ENCODING_COMPLETE;
        break;
    }

out:
    *ret_state = state;
    return encoded_symbols;
}

static esp_err_t ws2812_del_encoder(rmt_encoder_t *encoder)
{
    ws2812_encoder_t *strip_encoder = __containerof(encoder, ws2812_encoder_t, base);
    ESP_RETURN_ON_ERROR(rmt_del_encoder(strip_encoder->bytes_encoder), TAG, "delete bytes encoder failed");
    ESP_RETURN_ON_ERROR(rmt_del_encoder(strip_encoder->copy_encoder), TAG, "delete copy encoder failed");
    free(strip_encoder);
    return ESP_OK;
}

RMT_ENCODER_FUNC_ATTR
static esp_err_t ws2812_reset_encoder(rmt_encoder_t *encoder)
{
    ws2812_encoder_t *strip_encoder = __containerof(encoder, ws2812_encoder_t, base);
    ESP_RETURN_ON_ERROR(rmt_encoder_reset(strip_encoder->bytes_encoder), TAG, "reset bytes encoder failed");
    ESP_RETURN_ON_ERROR(rmt_encoder_reset(strip_encoder->copy_encoder), TAG, "reset copy encoder failed");
    strip_encoder->state = RMT_ENCODING_RESET;
    return ESP_OK;
}

esp_err_t ws2812_new_encoder(const ws2812_encoder_config_t *config,
                             rmt_encoder_handle_t *ret_encoder)
{
    esp_err_t ret = ESP_OK;
    ws2812_encoder_t *strip_encoder = NULL;

    ESP_GOTO_ON_FALSE(config && ret_encoder, ESP_ERR_INVALID_ARG, err, TAG, "invalid argument");

    strip_encoder = rmt_alloc_encoder_mem(sizeof(*strip_encoder));
    ESP_GOTO_ON_FALSE(strip_encoder, ESP_ERR_NO_MEM, err, TAG, "no mem for ws2812 encoder");

    strip_encoder->base.encode = ws2812_encode;
    strip_encoder->base.del = ws2812_del_encoder;
    strip_encoder->base.reset = ws2812_reset_encoder;

    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = config->resolution_hz * 3 / 10000000,
            .level1 = 0,
            .duration1 = config->resolution_hz * 9 / 10000000,
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = config->resolution_hz * 9 / 10000000,
            .level1 = 0,
            .duration1 = config->resolution_hz * 3 / 10000000,
        },
        .flags.msb_first = 1,
    };
    ESP_GOTO_ON_ERROR(rmt_new_bytes_encoder(&bytes_encoder_config, &strip_encoder->bytes_encoder),
                      err, TAG, "create bytes encoder failed");

    rmt_copy_encoder_config_t copy_encoder_config = {};
    ESP_GOTO_ON_ERROR(rmt_new_copy_encoder(&copy_encoder_config, &strip_encoder->copy_encoder),
                      err, TAG, "create copy encoder failed");

    uint32_t reset_ticks = config->resolution_hz / 1000000 * 50 / 2;
    strip_encoder->reset_code = (rmt_symbol_word_t) {
        .level0 = 0,
        .duration0 = reset_ticks,
        .level1 = 0,
        .duration1 = reset_ticks,
    };

    *ret_encoder = &strip_encoder->base;
    return ESP_OK;

err:
    if (strip_encoder) {
        if (strip_encoder->bytes_encoder) {
            rmt_del_encoder(strip_encoder->bytes_encoder);
        }
        if (strip_encoder->copy_encoder) {
            rmt_del_encoder(strip_encoder->copy_encoder);
        }
        free(strip_encoder);
    }
    return ret;
}
