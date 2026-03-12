#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "app_config.h"
#include "ble_midi.h"
#include "board_pins.h"
#include "chain_joystick.h"
#include "dualkey_hw.h"

static const char *TAG = "app";

typedef enum {
    PROFILE_NOTES,
    PROFILE_CC,
} profile_t;

typedef struct {
    bool button_a;
    bool button_b;
    bool joy_left;
    bool joy_right;
    bool joy_up;
    bool joy_down;
} midi_note_state_t;

typedef struct {
    uint8_t button_a;
    uint8_t button_b;
    uint8_t joy_x;
    uint8_t joy_y;
} midi_cc_state_t;

static RTC_DATA_ATTR profile_t s_saved_profile = PROFILE_NOTES;

static uint32_t uptime_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static uint8_t midi_scale_7bit(uint16_t raw)
{
    if (raw > JOY_AXIS_MAX) {
        raw = JOY_AXIS_MAX;
    }

    return (uint8_t)((raw * 127U + (JOY_AXIS_MAX / 2U)) / JOY_AXIS_MAX);
}

static void enter_deep_sleep(void)
{
    ESP_LOGI(TAG, "Entering deep sleep");

    esp_sleep_enable_ext1_wakeup_io(
        (1ULL << DUALKEY_BUTTON_A_GPIO) | (1ULL << DUALKEY_BUTTON_B_GPIO),
        ESP_EXT1_WAKEUP_ANY_LOW);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    esp_deep_sleep_start();
}

static void build_note_state(midi_note_state_t *state,
                             const dualkey_buttons_t *buttons,
                             const chain_joystick_sample_t *joy,
                             bool has_joystick)
{
    memset(state, 0, sizeof(*state));
    state->button_a = buttons->button_a;
    state->button_b = buttons->button_b;

    if (has_joystick) {
        state->joy_left = joy->x_raw < JOY_LEFT_THRESHOLD;
        state->joy_right = joy->x_raw > JOY_RIGHT_THRESHOLD;
        state->joy_up = joy->y_raw < JOY_UP_THRESHOLD;
        state->joy_down = joy->y_raw > JOY_DOWN_THRESHOLD;
    }
}

static void build_cc_state(midi_cc_state_t *state,
                           const dualkey_buttons_t *buttons,
                           const chain_joystick_sample_t *joy,
                           bool has_joystick)
{
    state->button_a = buttons->button_a ? 127 : 0;
    state->button_b = buttons->button_b ? 127 : 0;
    state->joy_x = has_joystick ? midi_scale_7bit(joy->x_raw) : 64;
    state->joy_y = has_joystick ? (uint8_t)(127 - midi_scale_7bit(joy->y_raw)) : 64;
}

static void send_note_transition(bool previous, bool current, uint8_t note)
{
    if (previous == current) {
        return;
    }

    ble_midi_send_note(APP_MIDI_CHANNEL, note,
                       current ? APP_MIDI_NOTE_VELOCITY : 0,
                       current);
}

static void send_note_changes(const midi_note_state_t *previous,
                              const midi_note_state_t *current)
{
    send_note_transition(previous->button_a, current->button_a, APP_MIDI_NOTE_BUTTON_A);
    send_note_transition(previous->button_b, current->button_b, APP_MIDI_NOTE_BUTTON_B);
    send_note_transition(previous->joy_left, current->joy_left, APP_MIDI_NOTE_LEFT);
    send_note_transition(previous->joy_right, current->joy_right, APP_MIDI_NOTE_RIGHT);
    send_note_transition(previous->joy_up, current->joy_up, APP_MIDI_NOTE_UP);
    send_note_transition(previous->joy_down, current->joy_down, APP_MIDI_NOTE_DOWN);
}

static void release_notes(midi_note_state_t *state)
{
    static const midi_note_state_t empty = {0};
    send_note_changes(state, &empty);
    *state = empty;
}

static void send_cc_if_changed(uint8_t previous, uint8_t current, uint8_t controller)
{
    if (previous == current) {
        return;
    }

    ble_midi_send_control_change(APP_MIDI_CHANNEL, controller, current);
}

static void send_cc_changes(const midi_cc_state_t *previous,
                            const midi_cc_state_t *current)
{
    send_cc_if_changed(previous->button_a, current->button_a, APP_MIDI_CC_BUTTON_A);
    send_cc_if_changed(previous->button_b, current->button_b, APP_MIDI_CC_BUTTON_B);
    send_cc_if_changed(previous->joy_x, current->joy_x, APP_MIDI_CC_JOY_X);
    send_cc_if_changed(previous->joy_y, current->joy_y, APP_MIDI_CC_JOY_Y);
}

static void reset_ccs(midi_cc_state_t *state)
{
    const midi_cc_state_t neutral = {
        .button_a = 0,
        .button_b = 0,
        .joy_x = 64,
        .joy_y = 64,
    };

    send_cc_changes(state, &neutral);
    *state = neutral;
}

/* ---- Reactive LED system ---- */

#define LED_BRIGHT     255
#define LED_DIM        20
#define LED_FADE_RATE  6
#define BREATH_PERIOD  3000
#define BREATH_AMP     12
#define PINK_GREEN_DIV 32
#define PINK_BLUE_DIV  10

static uint8_t sin8(uint8_t x)
{
    uint8_t y = (x < 128) ? x : (255 - x);
    uint16_t s = (uint16_t)y * y;
    return (uint8_t)(s >> 6);
}

static uint8_t s_fade_a = LED_DIM;
static uint8_t s_fade_b = LED_DIM;

static void update_leds(profile_t profile, const dualkey_buttons_t *buttons,
                        uint32_t now_ms)
{
    if (buttons->button_a) {
        s_fade_a = LED_BRIGHT;
    } else if (s_fade_a > LED_DIM) {
        uint8_t decay = (s_fade_a > LED_FADE_RATE) ? LED_FADE_RATE : s_fade_a;
        s_fade_a -= decay;
        if (s_fade_a < LED_DIM) {
            s_fade_a = LED_DIM;
        }
    }

    if (buttons->button_b) {
        s_fade_b = LED_BRIGHT;
    } else if (s_fade_b > LED_DIM) {
        uint8_t decay = (s_fade_b > LED_FADE_RATE) ? LED_FADE_RATE : s_fade_b;
        s_fade_b -= decay;
        if (s_fade_b < LED_DIM) {
            s_fade_b = LED_DIM;
        }
    }

    uint8_t phase = (uint8_t)((now_ms % BREATH_PERIOD) * 256 / BREATH_PERIOD);
    uint8_t breath = (uint8_t)((uint16_t)sin8(phase) * BREATH_AMP / 255);

    uint8_t a_val = (s_fade_a == LED_DIM) ? (LED_DIM + breath) : s_fade_a;
    uint8_t b_val = (s_fade_b == LED_DIM) ? (LED_DIM + breath) : s_fade_b;

    uint8_t a_r = 0;
    uint8_t a_g = 0;
    uint8_t a_b = 0;
    uint8_t b_r = 0;
    uint8_t b_g = 0;
    uint8_t b_b = 0;

    if (profile == PROFILE_NOTES) {
        a_b = a_val;
        b_b = b_val;
    } else {
        a_r = a_val;
        b_r = b_val;
        a_g = a_val / PINK_GREEN_DIV;
        b_g = b_val / PINK_GREEN_DIV;
        a_b = a_val / PINK_BLUE_DIV;
        b_b = b_val / PINK_BLUE_DIV;
    }

    dualkey_set_leds(b_r, b_g, b_b, a_r, a_g, a_b);
}

static void apply_profile_color(profile_t profile, chain_joystick_t *joystick,
                                bool has_joystick)
{
    update_leds(profile, &(dualkey_buttons_t){0}, uptime_ms());

    if (!has_joystick) {
        return;
    }

    chain_joystick_set_rgb(joystick,
                           profile == PROFILE_CC ? 64 : 0,
                           profile == PROFILE_CC ? 2 : 0,
                           profile == PROFILE_NOTES ? 64 : 6);
}

static void clear_active_profile(profile_t profile,
                                 midi_note_state_t *last_notes,
                                 midi_cc_state_t *last_ccs)
{
    if (profile == PROFILE_NOTES) {
        release_notes(last_notes);
    } else {
        reset_ccs(last_ccs);
    }
}

static void controller_task(void *arg)
{
    chain_joystick_t joystick = {0};
    chain_joystick_sample_t joy_sample = {0};
    bool has_joystick = false;

    midi_note_state_t last_notes = {0};
    midi_cc_state_t last_ccs = {
        .button_a = 0,
        .button_b = 0,
        .joy_x = 64,
        .joy_y = 64,
    };

    ESP_ERROR_CHECK(dualkey_hw_init());
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

    profile_t profile = s_saved_profile;
    uint32_t last_activity_ms = uptime_ms();
    uint32_t both_pressed_since = 0;
    uint32_t last_led_ms = 0;
    uint32_t last_joy_read_ms = 0;
    bool toggle_pending = false;

    dualkey_set_rgb(64, 64, 64, true);
    vTaskDelay(pdMS_TO_TICKS(300));
    apply_profile_color(profile, &joystick, has_joystick);

    if (has_joystick) {
        chain_joystick_set_brightness(&joystick, 20);
    }

    ESP_LOGI(TAG, "Starting in %s profile, joystick=%s",
             profile == PROFILE_NOTES ? "notes" : "cc",
             has_joystick ? "YES" : "NO");

    while (true) {
        uint32_t now = uptime_ms();
        dualkey_buttons_t buttons = dualkey_read_buttons();
        bool both = buttons.button_a && buttons.button_b;

        if (has_joystick && (now - last_joy_read_ms) >= 20) {
            chain_joystick_read_raw(&joystick, &joy_sample);
            last_joy_read_ms = now;
        }

        if (both && both_pressed_since == 0) {
            both_pressed_since = now;
            toggle_pending = false;
        }

        if (both && !toggle_pending &&
            (now - both_pressed_since) >= APP_PROFILE_TOGGLE_MS) {
            toggle_pending = true;
            ESP_LOGI(TAG, "Profile switch armed; release buttons to confirm");
        }

        if (!both) {
            if (both_pressed_since != 0 && toggle_pending) {
                clear_active_profile(profile, &last_notes, &last_ccs);

                profile = (profile == PROFILE_NOTES) ? PROFILE_CC : PROFILE_NOTES;
                s_saved_profile = profile;

                ESP_LOGI(TAG, "Profile → %s", profile == PROFILE_NOTES ? "notes" : "cc");

                dualkey_set_rgb(96, 96, 96, true);
                vTaskDelay(pdMS_TO_TICKS(200));
                apply_profile_color(profile, &joystick, has_joystick);
                last_activity_ms = uptime_ms();
            }

            both_pressed_since = 0;
            toggle_pending = false;
        } else {
            last_activity_ms = now;
            vTaskDelay(pdMS_TO_TICKS(APP_POLL_PERIOD_MS));
            continue;
        }

        bool changed = false;

        if (profile == PROFILE_NOTES) {
            midi_note_state_t notes;
            build_note_state(&notes, &buttons, &joy_sample, has_joystick);
            changed = memcmp(&notes, &last_notes, sizeof(notes)) != 0;
            if (changed && ble_midi_connected()) {
                send_note_changes(&last_notes, &notes);
            }
            last_notes = notes;
        } else {
            midi_cc_state_t ccs;
            build_cc_state(&ccs, &buttons, &joy_sample, has_joystick);
            changed = memcmp(&ccs, &last_ccs, sizeof(ccs)) != 0;
            if (changed && ble_midi_connected()) {
                send_cc_changes(&last_ccs, &ccs);
            }
            last_ccs = ccs;
        }

        if (changed) {
            last_activity_ms = now;
        }

        if ((now - last_activity_ms) >= APP_IDLE_TIMEOUT_MS) {
            clear_active_profile(profile, &last_notes, &last_ccs);

            dualkey_led_power(false);
            if (has_joystick) {
                chain_joystick_set_brightness(&joystick, 0);
            }

            ESP_LOGI(TAG, "Idle — LEDs off, waiting for button");

            uint32_t idle_started_ms = uptime_ms();
            while (true) {
                vTaskDelay(pdMS_TO_TICKS(APP_IDLE_POLL_MS));

                dualkey_buttons_t wake_buttons = dualkey_read_buttons();
                if (wake_buttons.button_a || wake_buttons.button_b) {
                    break;
                }

                if ((uptime_ms() - idle_started_ms) >= APP_DEEP_SLEEP_TIMEOUT_MS) {
                    dualkey_led_power(false);
                    enter_deep_sleep();
                }
            }

            last_activity_ms = uptime_ms();
            dualkey_led_power(true);
            s_fade_a = LED_DIM;
            s_fade_b = LED_DIM;
            both_pressed_since = 0;
            toggle_pending = false;

            ESP_LOGI(TAG, "Wake → active");
            apply_profile_color(profile, &joystick, has_joystick);
            if (has_joystick) {
                chain_joystick_set_brightness(&joystick, 20);
            }
            continue;
        }

        if ((now - last_led_ms) >= 30) {
            update_leds(profile, &buttons, now);
            last_led_ms = now;
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

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause == ESP_SLEEP_WAKEUP_EXT1) {
        ESP_LOGI(TAG, "Woke from deep sleep (button press)");
    } else if (cause != ESP_SLEEP_WAKEUP_UNDEFINED) {
        ESP_LOGI(TAG, "Woke from sleep (cause=%d)", (int)cause);
    }

    const esp_pm_config_t pm_config = {
        .max_freq_mhz = 160,
        .min_freq_mhz = 80,
        .light_sleep_enable = false,
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    ESP_ERROR_CHECK(ble_midi_init(APP_DEVICE_NAME));

    xTaskCreate(controller_task, "controller", 8192, NULL, 5, NULL);
}
