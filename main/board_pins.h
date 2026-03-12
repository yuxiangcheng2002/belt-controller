#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include "driver/gpio.h"

/* Buttons (mechanical blue switches, active-low) */
#define DUALKEY_BUTTON_A_GPIO GPIO_NUM_0   /* Left key */
#define DUALKEY_BUTTON_B_GPIO GPIO_NUM_17  /* Right key */

/* WS2812B LEDs (open-drain power enable, active-low) */
#define DUALKEY_WS2812_GPIO       GPIO_NUM_21
#define DUALKEY_WS2812_POWER_GPIO GPIO_NUM_40

/* Chain bus — RIGHT connector (UART1) */
#define DUALKEY_CHAIN_RIGHT_TX_GPIO GPIO_NUM_48
#define DUALKEY_CHAIN_RIGHT_RX_GPIO GPIO_NUM_47

/* Chain bus — LEFT connector (UART2) */
#define DUALKEY_CHAIN_LEFT_TX_GPIO  GPIO_NUM_6
#define DUALKEY_CHAIN_LEFT_RX_GPIO  GPIO_NUM_5

/* 3-position slide switch (read via ADC) */
#define DUALKEY_SW_BLE_GPIO  GPIO_NUM_8  /* ADC1_CH7 — high when switch right */
#define DUALKEY_SW_RAIN_GPIO GPIO_NUM_7  /* ADC1_CH6 — high when switch left */

/* Battery / charge monitoring (ADC) */
#define DUALKEY_VBAT_GPIO    GPIO_NUM_10 /* ADC1_CH9, divider ratio 1.51 */
#define DUALKEY_CHARGE_GPIO  GPIO_NUM_9  /* ADC1_CH8 */
#define DUALKEY_VBUS_GPIO    GPIO_NUM_2  /* ADC1_CH1, USB VBUS detect */

#endif
