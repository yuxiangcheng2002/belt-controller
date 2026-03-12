#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "driver/uart.h"

#define APP_DEVICE_NAME            "Belt MIDI Controller"
#define APP_POLL_PERIOD_MS         5      /* Fast poll — GPIO 0 ISR unreliable (strapping pin) */
#define APP_IDLE_TIMEOUT_MS        30000  /* 30s no input → idle (LEDs off, BLE stays connected) */
#define APP_IDLE_POLL_MS           20     /* Idle wait uses polling instead of GPIO0 ISR */
#define APP_DEEP_SLEEP_TIMEOUT_MS  1800000  /* 30 min idle → deep sleep (BLE disconnects, ~108μA) */
#define APP_PROFILE_TOGGLE_MS      2000   /* hold both buttons to switch */

/* Joystick thresholds for directional note triggering / CC scaling */
#define JOY_LEFT_THRESHOLD         1024
#define JOY_RIGHT_THRESHOLD        3072
#define JOY_UP_THRESHOLD           1024
#define JOY_DOWN_THRESHOLD         3072
#define JOY_AXIS_MAX               4095

/* BLE MIDI mapping */
#define APP_MIDI_CHANNEL           0      /* MIDI channel 1 */
#define APP_MIDI_NOTE_VELOCITY     100
#define APP_MIDI_NOTE_BUTTON_A     60
#define APP_MIDI_NOTE_BUTTON_B     62
#define APP_MIDI_NOTE_LEFT         64
#define APP_MIDI_NOTE_RIGHT        65
#define APP_MIDI_NOTE_UP           67
#define APP_MIDI_NOTE_DOWN         69
#define APP_MIDI_CC_BUTTON_A       20
#define APP_MIDI_CC_BUTTON_B       21
#define APP_MIDI_CC_JOY_X          1
#define APP_MIDI_CC_JOY_Y          74

/* Chain joystick hardware */
#define CHAIN_JOYSTICK_DEVICE_ID   1
#define CHAIN_JOYSTICK_UART_PORT   UART_NUM_1

#endif
