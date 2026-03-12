#include <string.h>

#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
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

/* Survives deep sleep — chip reboots but RTC memory persists */
static RTC_DATA_ATTR profile_t s_saved_profile = PROFILE_KEYBOARD;

static TaskHandle_t s_controller_task;

static uint32_t uptime_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

/* GPIO ISR — wakes controller task from idle sleep */
static void IRAM_ATTR button_isr(void *arg)
{
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_controller_task, &woken);
    portYIELD_FROM_ISR(woken);
}

static void enter_deep_sleep(void)
{
    ESP_LOGI(TAG, "Entering deep sleep");

    /* EXT1 wakeup on either button (active-low → wake on ANY_LOW).
       Both GPIOs 0 and 17 are RTC-capable on ESP32-S3. */
    esp_sleep_enable_ext1_wakeup_io(
        (1ULL << DUALKEY_BUTTON_A_GPIO) | (1ULL << DUALKEY_BUTTON_B_GPIO),
        ESP_EXT1_WAKEUP_ANY_LOW);

    /* Keep RTC peripherals powered so internal pull-ups stay active */
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    esp_deep_sleep_start();
    /* Never returns — chip reboots on wake */
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
        report->keycodes[idx++] = KEY_LEFT;
    }
    if (buttons->button_b && idx < 6) {
        report->keycodes[idx++] = KEY_RIGHT;
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

/* ---- Reactive LED system ----
   Bright flash on press, smooth exponential fade on release,
   gentle breathing when idle. Matches M5Stack stock firmware feel. */

#define LED_BRIGHT     255   /* Peak brightness on press */
#define LED_DIM        20    /* Base idle brightness */
#define LED_FADE_RATE  6     /* Fade per 5ms tick: ~60ms to half brightness */
#define BREATH_PERIOD  3000  /* Breathing cycle in ms */
#define BREATH_AMP     12    /* Breathing amplitude (added to dim) */

/* Simple 8-bit sine approximation (0-255 input → 0-255 output, centered at 128) */
static uint8_t sin8(uint8_t x)
{
    /* Parabolic sine approximation */
    uint8_t y = (x < 128) ? x : (255 - x);
    uint16_t s = (uint16_t)y * y;
    return (uint8_t)(s >> 6);  /* scale to 0-255 range */
}

static uint8_t s_fade_a = LED_DIM;
static uint8_t s_fade_b = LED_DIM;

static void update_leds(profile_t profile, const dualkey_buttons_t *buttons,
                        uint32_t now_ms)
{
    /* On press: snap to bright. On release: exponential decay. */
    if (buttons->button_a) {
        s_fade_a = LED_BRIGHT;
    } else if (s_fade_a > LED_DIM) {
        uint8_t decay = (s_fade_a > LED_FADE_RATE) ? LED_FADE_RATE : s_fade_a;
        s_fade_a -= decay;
        if (s_fade_a < LED_DIM) s_fade_a = LED_DIM;
    }

    if (buttons->button_b) {
        s_fade_b = LED_BRIGHT;
    } else if (s_fade_b > LED_DIM) {
        uint8_t decay = (s_fade_b > LED_FADE_RATE) ? LED_FADE_RATE : s_fade_b;
        s_fade_b -= decay;
        if (s_fade_b < LED_DIM) s_fade_b = LED_DIM;
    }

    /* Breathing modulation on idle brightness */
    uint8_t phase = (uint8_t)((now_ms % BREATH_PERIOD) * 256 / BREATH_PERIOD);
    uint8_t breath = (uint8_t)((uint16_t)sin8(phase) * BREATH_AMP / 255);

    uint8_t a_val = (s_fade_a == LED_DIM) ? (LED_DIM + breath) : s_fade_a;
    uint8_t b_val = (s_fade_b == LED_DIM) ? (LED_DIM + breath) : s_fade_b;

    /* Apply profile color */
    uint8_t a_r = 0, a_g = 0, a_b = 0;
    uint8_t b_r = 0, b_g = 0, b_b = 0;
    if (profile == PROFILE_KEYBOARD) {
        a_b = a_val; b_b = b_val;
    } else {
        a_g = a_val; b_g = b_val;
    }

    /* LED 0 = right key = button B, LED 1 = left key = button A */
    dualkey_set_leds(b_r, b_g, b_b, a_r, a_g, a_b);
}

static void controller_task(void *arg)
{
    chain_joystick_t joystick = {0};
    chain_joystick_sample_t joy_sample = {0};
    bool has_joystick = false;

    /* Hardware init */
    ESP_ERROR_CHECK(dualkey_hw_init());
    /* Try chain joystick on right connector */
    ESP_ERROR_CHECK(chain_joystick_init(&joystick, CHAIN_JOYSTICK_UART_PORT,
                                         DUALKEY_CHAIN_RIGHT_TX_GPIO,
                                         DUALKEY_CHAIN_RIGHT_RX_GPIO,
                                         CHAIN_JOYSTICK_DEVICE_ID));
    if (chain_joystick_probe(&joystick) == ESP_OK) {
        has_joystick = true;
        chain_joystick_set_brightness(&joystick, 20);
        ESP_LOGI(TAG, "Chain joystick detected");
    } else {
        ESP_LOGW(TAG, "Joystick not detected — buttons only");
        chain_joystick_deinit(&joystick);
    }

    /* GPIO ISR for button wake from idle */
    s_controller_task = xTaskGetCurrentTaskHandle();
    gpio_set_intr_type(DUALKEY_BUTTON_A_GPIO, GPIO_INTR_NEGEDGE);
    gpio_set_intr_type(DUALKEY_BUTTON_B_GPIO, GPIO_INTR_NEGEDGE);
    esp_err_t isr_err = gpio_install_isr_service(0);
    if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(isr_err));
    }
    ESP_ERROR_CHECK(gpio_isr_handler_add(DUALKEY_BUTTON_A_GPIO, button_isr, NULL));
    ESP_ERROR_CHECK(gpio_isr_handler_add(DUALKEY_BUTTON_B_GPIO, button_isr, NULL));
    ESP_LOGI(TAG, "Button ISRs installed on GPIO %d and %d",
             DUALKEY_BUTTON_A_GPIO, DUALKEY_BUTTON_B_GPIO);

    /* GPIO wakeup from light sleep (buttons are active-low) */
    gpio_wakeup_enable(DUALKEY_BUTTON_A_GPIO, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable(DUALKEY_BUTTON_B_GPIO, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    /* Restore profile from RTC memory (survives deep sleep) */
    profile_t profile = s_saved_profile;
    uint32_t last_activity_ms = uptime_ms();
    uint32_t both_pressed_since = 0;
    bool toggle_pending = false;

    /* Previous reports for change detection */
    ble_hid_kb_report_t last_kb = {0};
    ble_hid_gp_report_t last_gp = {0};
    uint32_t last_led_ms = 0;  /* Rate-limit LED refresh to ~33Hz */

    /* Startup flash — confirms LEDs work */
    dualkey_set_rgb(64, 64, 64, true);
    vTaskDelay(pdMS_TO_TICKS(300));
    update_leds(profile, &(dualkey_buttons_t){0}, uptime_ms());
    if (has_joystick) {
        chain_joystick_set_brightness(&joystick, 20);
        chain_joystick_set_rgb(&joystick,
                               profile == PROFILE_KEYBOARD ? 0 : 0,
                               profile == PROFILE_KEYBOARD ? 0 : 64,
                               profile == PROFILE_KEYBOARD ? 64 : 0);
    }
    ESP_LOGI(TAG, "Starting in %s profile, joystick=%s",
             profile == PROFILE_KEYBOARD ? "keyboard" : "gamepad",
             has_joystick ? "YES" : "NO");

    uint32_t last_joy_read_ms = 0;

    while (true) {
        uint32_t now = uptime_ms();
        dualkey_buttons_t buttons = dualkey_read_buttons();

        /* Rate-limit joystick UART reads (~50Hz) to avoid blocking */
        if (has_joystick && (now - last_joy_read_ms) >= 20) {
            chain_joystick_read_raw(&joystick, &joy_sample);
            last_joy_read_ms = now;
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
                s_saved_profile = profile;
                toggle_pending = true;
                ESP_LOGI(TAG, "Profile → %s",
                         profile == PROFILE_KEYBOARD ? "keyboard" : "gamepad");

                /* Flash LED to confirm */
                dualkey_set_rgb(96, 96, 96, true);
                vTaskDelay(pdMS_TO_TICKS(200));
                update_leds(profile, &(dualkey_buttons_t){0}, uptime_ms());
                if (has_joystick) {
                    chain_joystick_set_rgb(&joystick,
                                           profile == PROFILE_KEYBOARD ? 0 : 0,
                                           profile == PROFILE_KEYBOARD ? 0 : 64,
                                           profile == PROFILE_KEYBOARD ? 64 : 0);
                }

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
            last_activity_ms = now;
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(APP_POLL_PERIOD_MS));
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

        /* --- Idle check: enter low-power state after timeout --- */
        if ((now - last_activity_ms) >= APP_IDLE_TIMEOUT_MS) {
            /* Release all keys/buttons before sleeping */
            ble_hid_kb_report_t kb_empty = {0};
            ble_hid_gp_report_t gp_empty = {0};
            ble_hid_send_keyboard(&kb_empty);
            ble_hid_send_gamepad(&gp_empty);
            last_kb = kb_empty;
            last_gp = gp_empty;

            /* LEDs off, power rail off */
            dualkey_led_power(false);
            if (has_joystick) {
                chain_joystick_set_brightness(&joystick, 0);
            }

            ESP_LOGI(TAG, "Idle — LEDs off, waiting for button");

            /* Block until button press OR deep sleep timeout.
               BLE stays connected at 15ms interval (no parameter change —
               macOS rejects slave latency changes and drops the connection).
               CPU just blocks on the notification; BLE stack runs in its own task. */
            ulTaskNotifyTake(pdTRUE, 0);  /* drain pending */
            uint32_t got = ulTaskNotifyTake(pdTRUE,
                               pdMS_TO_TICKS(APP_DEEP_SLEEP_TIMEOUT_MS));

            if (got == 0) {
                /* Timed out — no button press for 30 min, enter deep sleep.
                   BLE disconnects. Bonding keys survive in NVS.
                   On wake (button press), chip reboots and reconnects (~1-2s). */
                dualkey_led_power(false);
                enter_deep_sleep();
                /* Never reaches here */
            }

            /* Button press woke us — restore active state */
            last_activity_ms = uptime_ms();
            dualkey_led_power(true);
            s_fade_a = LED_DIM;
            s_fade_b = LED_DIM;
            both_pressed_since = 0;
            toggle_pending = false;

            ESP_LOGI(TAG, "Wake → active");
            update_leds(profile, &(dualkey_buttons_t){0}, uptime_ms());
            continue;
        }

        /* LED feedback (active mode) — rate-limited to ~33Hz to avoid
           starving the interrupt WDT (RMT refresh is slow). */
        if ((now - last_led_ms) >= 30) {
            update_leds(profile, &buttons, now);
            last_led_ms = now;
        }

        /* Wait for next poll or immediate button event */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(APP_POLL_PERIOD_MS));
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

    /* Log wakeup source */
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_EXT1) {
        ESP_LOGI(TAG, "Woke from deep sleep (button press)");
    } else if (cause != ESP_SLEEP_WAKEUP_UNDEFINED) {
        ESP_LOGI(TAG, "Woke from sleep (cause=%d)", (int)cause);
    }

    /* Power management: frequency scaling only for now.
       light_sleep_enable disabled until LED/RMT stability is confirmed. */
    const esp_pm_config_t pm_config = {
        .max_freq_mhz = 160,
        .min_freq_mhz = 80,
        .light_sleep_enable = false,
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    ESP_ERROR_CHECK(ble_hid_init(APP_DEVICE_NAME));

    xTaskCreate(controller_task, "controller", 8192, NULL, 5, NULL);
}
