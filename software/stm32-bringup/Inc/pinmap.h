#ifndef __PINMAP_H
#define __PINMAP_H

#include "stm32f4xx_hal.h"

/* Display (SPI1) */
#define DISP_CS_PORT   GPIOA
#define DISP_CS_PIN    GPIO_PIN_4
#define DISP_SCK_PORT  GPIOA
#define DISP_SCK_PIN   GPIO_PIN_5
#define DISP_MOSI_PORT GPIOA
#define DISP_MOSI_PIN  GPIO_PIN_7
#define DISP_DC_PORT   GPIOB
#define DISP_DC_PIN    GPIO_PIN_0
#define DISP_RST_PORT  GPIOB
#define DISP_RST_PIN   GPIO_PIN_1
#define DISP_BLK_PORT  GPIOA
#define DISP_BLK_PIN   GPIO_PIN_6

/* Power monitoring.
   NOTE: physical board has BAT_STAT and CHRG_STAT swapped vs the original
   pinout doc — BAT_STAT is actually on PA1 (ADC1_IN1), CHRG_STAT on PA0. */
#define BAT_STAT_PORT  GPIOA
#define BAT_STAT_PIN   GPIO_PIN_1   /* ADC1_IN1 */
#define CHRG_STAT_PORT GPIOA
#define CHRG_STAT_PIN  GPIO_PIN_0
#define VBUS_SNS_PORT  GPIOA
#define VBUS_SNS_PIN   GPIO_PIN_9

/* Buttons (all active-low, internal pull-up) */
#define BTN_A_PORT     GPIOB
#define BTN_A_PIN      GPIO_PIN_13
#define BTN_B_PORT     GPIOB
#define BTN_B_PIN      GPIO_PIN_14
#define BTN_X_PORT     GPIOB
#define BTN_X_PIN      GPIO_PIN_12
#define BTN_Y_PORT     GPIOB
#define BTN_Y_PIN      GPIO_PIN_15
#define BTN_UP_PORT    GPIOB
#define BTN_UP_PIN     GPIO_PIN_9
#define BTN_DOWN_PORT  GPIOB
#define BTN_DOWN_PIN   GPIO_PIN_8
#define BTN_LEFT_PORT  GPIOB
#define BTN_LEFT_PIN   GPIO_PIN_7
#define BTN_RIGHT_PORT GPIOB
#define BTN_RIGHT_PIN  GPIO_PIN_6
#define BTN_CE_PORT    GPIOA
#define BTN_CE_PIN     GPIO_PIN_10

/* Speaker */
#define AMP_SD_PORT    GPIOB
#define AMP_SD_PIN     GPIO_PIN_2
/* AUDIO_PWM = PA8 = TIM1_CH1 */

/* ON/OFF controller (ATTINY13A) handshake */
#define OFF_REQ_PORT   GPIOA
#define OFF_REQ_PIN    GPIO_PIN_2   /* input: ATTINY requests shutdown, active-low */
#define OFF_ACK_PORT   GPIOA
#define OFF_ACK_PIN    GPIO_PIN_3   /* output: MCU confirms shutdown to ATTINY */

#endif
