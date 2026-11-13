#ifndef CLOCK_H
#define CLOCK_H

#include <stdbool.h>
#include <stdint.h>

/* 96 MHz preset (same as stm32-bringup): HSE 8 MHz, PLLM 4, PLLN 96, PLLP 2,
   PLLQ 4 (USB 48 MHz), FLASH_LATENCY_3, AHB /1, APB1 /2 (48 MHz),
   APB2 /1 (96 MHz). */

typedef enum {
    CLOCK_OK = 0,
    CLOCK_ERR_HSE,      /* HSE did not start; running on HSI 16 MHz */
    CLOCK_ERR_PLL,      /* PLL did not lock; running on HSI 16 MHz */
    CLOCK_ERR_SWITCH,
} clock_status_t;

clock_status_t clock_init_96mhz(void);
clock_status_t clock_last_status(void);
const char *clock_status_str(clock_status_t s);

/* Live readings for selftest/info */
bool clock_hse_ready(void);
bool clock_sysclk_is_pll(void);
bool clock_pll_src_is_hse(void);
uint32_t clock_pclk1_hz(void);
uint32_t clock_pclk2_hz(void);
uint32_t clock_tim_apb1_hz(void);   /* timer kernel clock on APB1 (x2 when APB1 prescaled) */

#endif
