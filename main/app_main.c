#include <string.h>

#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_config.h"
#include "board_pins.h"
#include "ble_hid.h"
#include "chain_joystick.h"
#include "dualkey_hw.h"

static const char *TAG = "app";

typedef enum {
    PROFILE_KEYBOARD,
    PROFILE_GAMEPAD,
} profile_t;

static uint32_t uptime_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

/* Build keyboard report from current inputs */
static void build_kb_report(ble_hid_kb_report_t *report,
                             const dualkey_buttons_t *buttons,
                             const chain_joystick_sample_t *joy,
                             bool has_joystick)
{
    memset(report, 0, sizeof(*report));
    int idx = 0;

    if (buttons->button_a && idx < 6) {
        report->keycodes[idx++] = KEY_ENTER;
    }
    if (buttons->button_b && idx < 6) {
        report->keycodes[idx++] = KEY_ESCAPE;
    }
    if (has_joystick) {
        if (joy->x_raw < JOY_LEFT_THRESHOLD && idx < 6) {
            report->keycodes[idx++] = KEY_LEFT;
        } else if (joy->x_raw > JOY_RIGHT_THRESHOLD && idx < 6) {
            report->keycodes[idx++] = KEY_RIGHT;
        }
    }
}

/* Build gamepad report from current inputs.
   Without joystick: buttons map to L/R (x-axis extremes). */
static void build_gp_report(ble_hid_gp_report_t *report,
                              const dualkey_buttons_t *buttons,
                              const chain_joystick_sample_t *joy,
                              bool has_joystick)
{
    report->buttons = (uint8_t)((buttons->button_a ? 0x01 : 0x00) |
                                 (buttons->button_b ? 0x02 : 0x00));
    if (has_joystick) {
        report->x = joy->x_raw;
        report->y = joy->y_raw;
    } else {
        /* A = left, B = right, neither = center */
        report->x = buttons->button_a ? 0 : (buttons->button_b ? 4095 : 2048);
        report->y = 2048;
    }
}

/* Per-button LED feedback.
   LED 0 = right key (button B), LED 1 = left key (button A).
   Profile color: blue = keyboard, green = gamepad.
   Pressed button LED goes bright, unpressed stays dim. */
static void update_leds(profile_t profile, const dualkey_buttons_t *buttons, bool idle)
{
    uint8_t dim = 16, bright = 96;
    uint8_t a_r = 0, a_g = 0, a_b = 0;
    uint8_t b_r = 0, b_g = 0, b_b = 0;

    if (!idle) {
        uint8_t a_val = buttons->button_a ? bright : dim;
        uint8_t b_val = buttons->button_b ? bright : dim;
        if (profile == PROFILE_KEYBOARD) {
            a_b = a_val;
            b_b = b_val;
        } else {
            a_g = a_val;
            b_g = b_val;
        }
    }

    /* LED 1 = left key = button A, LED 0 = right key = button B */
    dualkey_set_led(DUALKEY_LED_LEFT, a_r, a_g, a_b);
    dualkey_set_led(DUALKEY_LED_RIGHT, b_r, b_g, b_b);
}

static void controller_task(void *arg)
{
    chain_joystick_t joystick = {0};
    chain_joystick_sample_t joy_sample = {0};
    bool has_joystick = false;

    /* Probe joystick — deinit UART if absent to save power */
    ESP_ERROR_CHECK(dualkey_hw_init());
    ESP_ERROR_CHECK(chain_joystick_init(&joystick,
                                         CHAIN_JOYSTICK_UART_PORT,
                                         DUALKEY_CHAIN_LEFT_TX_GPIO,
                                         DUALKEY_CHAIN_LEFT_RX_GPIO,
                                         CHAIN_JOYSTICK_DEVICE_ID));

    if (chain_joystick_probe(&joystick) == ESP_OK) {
        has_joystick = true;
        chain_joystick_set_brightness(&joystick, 20);
    } else {
        ESP_LOGW(TAG, "Joystick not detected — deiniting UART for low power");
        chain_joystick_deinit(&joystick);
    }

    profile_t profile = PROFILE_KEYBOARD;
    uint32_t last_activity_ms = uptime_ms();
    uint32_t both_pressed_since = 0;
    bool toggle_pending = false;

    /* Previous reports for change detection */
    ble_hid_kb_report_t last_kb = {0};
    ble_hid_gp_report_t last_gp = {0};

    update_leds(profile, &(dualkey_buttons_t){0}, false);
    ESP_LOGI(TAG, "Starting in keyboard profile");

    while (true) {
        uint32_t now = uptime_ms();
        dualkey_buttons_t buttons = dualkey_read_buttons();

        if (has_joystick) {
            chain_joystick_read_raw(&joystick, &joy_sample);
        }

        /* --- Profile toggle: hold both buttons for 2s --- */
        bool both = buttons.button_a && buttons.button_b;

        if (both && !toggle_pending) {
            if (both_pressed_since == 0) {
                both_pressed_since = now;
            }
            if ((now - both_pressed_since) >= APP_PROFILE_TOGGLE_MS) {
                profile = (profile == PROFILE_KEYBOARD)
                              ? PROFILE_GAMEPAD : PROFILE_KEYBOARD;
                toggle_pending = true;
                ESP_LOGI(TAG, "Profile → %s",
                         profile == PROFILE_KEYBOARD ? "keyboard" : "gamepad");

                /* Flash LED to confirm */
                dualkey_set_rgb(96, 96, 96, true);
                vTaskDelay(pdMS_TO_TICKS(200));
                update_leds(profile, &(dualkey_buttons_t){0}, false);

                /* Send empty reports to release any held keys/buttons */
                ble_hid_kb_report_t kb_empty = {0};
                ble_hid_gp_report_t gp_empty = {0};
                ble_hid_send_keyboard(&kb_empty);
                ble_hid_send_gamepad(&gp_empty);
                last_kb = kb_empty;
                last_gp = gp_empty;
            }
        }
        if (!both) {
            both_pressed_since = 0;
            toggle_pending = false;
        }

        /* Suppress input while both buttons held (toggle gesture) */
        if (both) {
            vTaskDelay(pdMS_TO_TICKS(APP_POLL_PERIOD_MS));
            continue;
        }

        /* --- Build and send report for active profile --- */
        bool changed = false;

        if (profile == PROFILE_KEYBOARD) {
            ble_hid_kb_report_t kb;
            build_kb_report(&kb, &buttons, &joy_sample, has_joystick);
            changed = memcmp(&kb, &last_kb, sizeof(kb)) != 0;
            if (changed && ble_hid_connected()) {
                ble_hid_send_keyboard(&kb);
                last_kb = kb;
            }
        } else {
            ble_hid_gp_report_t gp;
            build_gp_report(&gp, &buttons, &joy_sample, has_joystick);
            changed = memcmp(&gp, &last_gp, sizeof(gp)) != 0;
            if (changed && ble_hid_connected()) {
                ble_hid_send_gamepad(&gp);
                last_gp = gp;
            }
        }

        if (changed) {
            last_activity_ms = now;
        }

        /* LED feedback */
        bool idle = (now - last_activity_ms) >= APP_IDLE_LED_TIMEOUT_MS;
        update_leds(profile, &buttons, idle);

        if (has_joystick) {
            if (idle) {
                chain_joystick_set_brightness(&joystick, 0);
            } else {
                chain_joystick_set_brightness(&joystick, 20);
                uint8_t jr = 0, jg = 0, jb = 0;
                if (profile == PROFILE_KEYBOARD) {
                    jb = 64;
                } else {
                    jg = 64;
                }
                chain_joystick_set_rgb(&joystick, jr, jg, jb);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(APP_POLL_PERIOD_MS));
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(ble_hid_init(APP_DEVICE_NAME));

    xTaskCreate(controller_task, "controller", 8192, NULL, 5, NULL);
}
