#ifndef BOARD_H
#define BOARD_H

/* STM-boy v0.1 pin map (STM32F411CEU6, HSE 8 MHz). Source of truth:
   stm32-bringup/Inc/pinmap.h + docs/pinout.md. */

#include "stm32f4xx.h"
#include <stdbool.h>
#include <stdint.h>

/* Display ST7735S on SPI1 (write-only, AF5) */
#define LCD_SCK_PORT   GPIOA
#define LCD_SCK_PIN    5u
#define LCD_MOSI_PORT  GPIOA
#define LCD_MOSI_PIN   7u
#define LCD_CS_PORT    GPIOA
#define LCD_CS_PIN     4u
#define LCD_DC_PORT    GPIOB
#define LCD_DC_PIN     0u
#define LCD_RST_PORT   GPIOB
#define LCD_RST_PIN    1u
#define LCD_BLK_PORT   GPIOA   /* plain GPIO for now; TIM3_CH1 later (P1) */
#define LCD_BLK_PIN    6u

/* W25Q128 on SPI3 (AF6), CS is a GPIO */
#define FLASH_SCK_PORT  GPIOB
#define FLASH_SCK_PIN   3u
#define FLASH_MISO_PORT GPIOB
#define FLASH_MISO_PIN  4u
#define FLASH_MOSI_PORT GPIOB
#define FLASH_MOSI_PIN  5u
#define FLASH_CS_PORT   GPIOA
#define FLASH_CS_PIN    15u

/* Buttons: active-low, internal pull-ups */
#define BTN_RIGHT_PORT  GPIOB
#define BTN_RIGHT_PIN   6u
#define BTN_LEFT_PORT   GPIOB
#define BTN_LEFT_PIN    7u
#define BTN_DOWN_PORT   GPIOB
#define BTN_DOWN_PIN    8u
#define BTN_UP_PORT     GPIOB
#define BTN_UP_PIN      9u
#define BTN_CENTER_PORT GPIOA
#define BTN_CENTER_PIN  10u
#define BTN_X_PORT      GPIOB
#define BTN_X_PIN       12u
#define BTN_A_PORT      GPIOB
#define BTN_A_PIN       13u
#define BTN_B_PORT      GPIOB
#define BTN_B_PIN       14u
#define BTN_Y_PORT      GPIOB
#define BTN_Y_PIN       15u

/* Audio: PAM8302A shutdown (low = amp off). PA8 = AUDIO_PWM (TIM1_CH1),
   left in its reset state (input) in P0. */
#define AMP_SD_PORT     GPIOB
#define AMP_SD_PIN      2u
#define AUDIO_PWM_PORT  GPIOA
#define AUDIO_PWM_PIN   8u

/* Power: ATtiny13A handshake + charger status */
#define OFF_REQ_PORT    GPIOA   /* input, active-low, external pull-up */
#define OFF_REQ_PIN     2u
#define OFF_ACK_PORT    GPIOA   /* output, active-high ACK */
#define OFF_ACK_PIN     3u
#define CHG_PORT        GPIOA   /* charger CHG, input pull-up */
#define CHG_PIN         0u
#define BAT_SNS_PORT    GPIOA   /* ADC1_IN1 (P2) */
#define BAT_SNS_PIN     1u
#define VBUS_PORT       GPIOA   /* VBUS sense, input pull-down */
#define VBUS_PIN        9u

/* ---- tiny register-level GPIO helpers ---- */
#define GPIO_MODE_IN_   0u
#define GPIO_MODE_OUT_  1u
#define GPIO_MODE_AF_   2u
#define GPIO_MODE_AN_   3u

#define GPIO_PULL_NONE_ 0u
#define GPIO_PULL_UP_   1u
#define GPIO_PULL_DOWN_ 2u

#define GPIO_SPEED_LOW_  0u
#define GPIO_SPEED_VHIGH_ 3u

void gpio_config(GPIO_TypeDef *port, uint32_t pin, uint32_t mode, uint32_t pull,
                 uint32_t speed, uint32_t af);

static inline void gpio_set(GPIO_TypeDef *port, uint32_t pin) { port->BSRR = 1u << pin; }
static inline void gpio_clr(GPIO_TypeDef *port, uint32_t pin) { port->BSRR = 1u << (pin + 16u); }
static inline bool gpio_read(GPIO_TypeDef *port, uint32_t pin) { return (port->IDR >> pin) & 1u; }

/* Early, safe pin state: amp off, OFF_ACK low, CS lines high, inputs. */
void board_init(void);

/* Chip identity */
void board_uid_hex(char out[25]);       /* 96-bit UID as 24 hex chars (word0,word1,word2) */
void board_usb_serial(char out[13]);    /* identical to the USB iSerial string */
uint32_t board_flash_kb(void);

#endif
