#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "driver/uart.h"

#define APP_DEVICE_NAME            "Belt Controller"
#define APP_POLL_PERIOD_MS         5      /* Fast poll — GPIO 0 ISR unreliable (strapping pin) */
#define APP_IDLE_TIMEOUT_MS        30000     /* 30s no input → idle (LEDs off, slave latency, light sleep) */
#define APP_DEEP_SLEEP_TIMEOUT_MS  1800000  /* 30 min idle → deep sleep (BLE disconnects, ~108μA) */
#define APP_PROFILE_TOGGLE_MS      2000   /* hold both buttons to switch */

/* Joystick thresholds for keyboard arrow mapping (0–4095 range, center ~2048) */
#define JOY_LEFT_THRESHOLD         1024
#define JOY_RIGHT_THRESHOLD        3072

/* Chain joystick hardware */
#define CHAIN_JOYSTICK_DEVICE_ID   1
#define CHAIN_JOYSTICK_UART_PORT   UART_NUM_1

/* USB HID keycodes for keyboard profile */
#define KEY_ENTER   0x28
#define KEY_ESCAPE  0x29
#define KEY_RIGHT   0x4F
#define KEY_LEFT    0x50

#endif
