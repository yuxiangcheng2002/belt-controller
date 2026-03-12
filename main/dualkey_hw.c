#include "dualkey_hw.h"

#include "board_pins.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "led_strip.h"

static led_strip_handle_t s_led_strip;

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

    const led_strip_config_t strip_config = {
        .strip_gpio_num = DUALKEY_WS2812_GPIO,
        .max_leds = DUALKEY_LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .flags = {
            .invert_out = false,
        },
    };
    const led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags = {
            .with_dma = false,
        },
    };

    ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip), "dualkey_hw",
                        "Failed to init LED strip");
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
    if (s_led_strip == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (index < 0 || index >= DUALKEY_LED_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_ERROR_CHECK(led_strip_set_pixel(s_led_strip, index, r, g, b));
    return led_strip_refresh(s_led_strip);
}

esp_err_t dualkey_set_leds(uint8_t r0, uint8_t g0, uint8_t b0,
                           uint8_t r1, uint8_t g1, uint8_t b1)
{
    if (s_led_strip == NULL) return ESP_ERR_INVALID_STATE;
    ESP_ERROR_CHECK(led_strip_set_pixel(s_led_strip, 0, r0, g0, b0));
    ESP_ERROR_CHECK(led_strip_set_pixel(s_led_strip, 1, r1, g1, b1));
    return led_strip_refresh(s_led_strip);
}

esp_err_t dualkey_set_rgb(uint8_t r, uint8_t g, uint8_t b, bool enabled)
{
    if (s_led_strip == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_ERROR_CHECK(gpio_set_level(DUALKEY_WS2812_POWER_GPIO, enabled ? 0 : 1));
    for (int i = 0; i < DUALKEY_LED_COUNT; i++) {
        ESP_ERROR_CHECK(led_strip_set_pixel(s_led_strip, i, r, g, b));
    }
    return led_strip_refresh(s_led_strip);
}

esp_err_t dualkey_led_power(bool enabled)
{
    return gpio_set_level(DUALKEY_WS2812_POWER_GPIO, enabled ? 0 : 1);
}
