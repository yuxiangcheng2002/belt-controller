#include "dualkey_hw.h"

#include "board_pins.h"

#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "ws2812_encoder.h"

static rmt_channel_handle_t s_led_chan;
static rmt_encoder_handle_t s_led_encoder;
static uint8_t s_led_pixels[DUALKEY_LED_COUNT * 3];

static esp_err_t dualkey_refresh(void)
{
    if (s_led_chan == NULL || s_led_encoder == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
        .flags.eot_level = 0,
    };

    ESP_RETURN_ON_ERROR(
        rmt_transmit(s_led_chan, s_led_encoder, s_led_pixels, sizeof(s_led_pixels), &tx_config),
        "dualkey_hw", "Failed to transmit LED pixels");
    return rmt_tx_wait_all_done(s_led_chan, -1);
}

esp_err_t dualkey_hw_init(void)
{
    const gpio_config_t input_config = {
        .pin_bit_mask = (1ULL << DUALKEY_BUTTON_A_GPIO) | (1ULL << DUALKEY_BUTTON_B_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_RETURN_ON_ERROR(gpio_config(&input_config), "dualkey_hw", "Failed to init button GPIOs");
    /* LED power pin is open-drain, active-low on Chain DualKey */
    ESP_RETURN_ON_ERROR(gpio_set_direction(DUALKEY_WS2812_POWER_GPIO, GPIO_MODE_OUTPUT_OD), "dualkey_hw",
                        "Failed to init LED power GPIO");
    ESP_RETURN_ON_ERROR(gpio_set_level(DUALKEY_WS2812_POWER_GPIO, 0), "dualkey_hw", "Failed to enable LED power");

    const rmt_tx_channel_config_t tx_config = {
        .gpio_num = DUALKEY_WS2812_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .trans_queue_depth = 1,
        .flags = {
            .invert_out = false,
            .with_dma = false,
        },
    };
    const ws2812_encoder_config_t encoder_config = {
        .resolution_hz = tx_config.resolution_hz,
    };

    ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_config, &s_led_chan), "dualkey_hw",
                        "Failed to init LED RMT channel");
    ESP_RETURN_ON_ERROR(ws2812_new_encoder(&encoder_config, &s_led_encoder), "dualkey_hw",
                        "Failed to init LED encoder");
    ESP_RETURN_ON_ERROR(rmt_enable(s_led_chan), "dualkey_hw", "Failed to enable LED RMT channel");

    return dualkey_set_rgb(0, 0, 0, true);
}

dualkey_buttons_t dualkey_read_buttons(void)
{
    dualkey_buttons_t state = {
        .button_a = gpio_get_level(DUALKEY_BUTTON_A_GPIO) == 0,
        .button_b = gpio_get_level(DUALKEY_BUTTON_B_GPIO) == 0,
    };

    return state;
}

esp_err_t dualkey_set_led(int index, uint8_t r, uint8_t g, uint8_t b)
{
    if (s_led_chan == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (index < 0 || index >= DUALKEY_LED_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t pixel = (size_t)index * 3;
    s_led_pixels[pixel + 0] = g;
    s_led_pixels[pixel + 1] = r;
    s_led_pixels[pixel + 2] = b;
    return dualkey_refresh();
}

esp_err_t dualkey_set_leds(uint8_t r0, uint8_t g0, uint8_t b0,
                           uint8_t r1, uint8_t g1, uint8_t b1)
{
    if (s_led_chan == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_led_pixels[0] = g0;
    s_led_pixels[1] = r0;
    s_led_pixels[2] = b0;
    s_led_pixels[3] = g1;
    s_led_pixels[4] = r1;
    s_led_pixels[5] = b1;
    return dualkey_refresh();
}

esp_err_t dualkey_set_rgb(uint8_t r, uint8_t g, uint8_t b, bool enabled)
{
    if (s_led_chan == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_ERROR_CHECK(gpio_set_level(DUALKEY_WS2812_POWER_GPIO, enabled ? 0 : 1));
    for (int i = 0; i < DUALKEY_LED_COUNT; i++) {
        s_led_pixels[i * 3 + 0] = g;
        s_led_pixels[i * 3 + 1] = r;
        s_led_pixels[i * 3 + 2] = b;
    }
    return dualkey_refresh();
}

esp_err_t dualkey_led_power(bool enabled)
{
    return gpio_set_level(DUALKEY_WS2812_POWER_GPIO, enabled ? 0 : 1);
}
