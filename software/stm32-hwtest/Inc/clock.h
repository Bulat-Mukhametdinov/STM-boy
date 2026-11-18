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

/* ---- Clock profiles (clock_profile.c): HSE 8 MHz / M 4 (2 MHz) -> VCO = 2*N MHz -> SYSCLK = VCO/P.
   Boot is always 96 MHz (clock_init_96mhz); clock_set_mhz() moves to another SYSCLK at run time
   and re-derives everything that depends on it (clock_apply_derived). */
typedef struct {
    int ws;                 /* -1 = automatic wait states, else forced (0..15) */
    uint32_t apb1_max_hz;   /* 0 = 50 MHz */
    uint32_t apb2_max_hz;   /* 0 = 100 MHz */
} clock_opts_t;

typedef struct {
    uint16_t mhz;
    uint8_t p, q, ws;
    uint16_t n;
    uint8_t apb1_div, apb2_div;
    bool usb_ok;            /* PLLQ output is exactly 48 MHz and HCLK >= 30 MHz */
    bool in_spec;           /* <= 100 MHz, WS >= datasheet rule, APB limits */
} clock_plan_t;

bool clock_plan(uint32_t mhz, const clock_opts_t *o, clock_plan_t *out);
clock_status_t clock_set_mhz(uint32_t mhz, const clock_opts_t *o);   /* o may be NULL; no reset on failure */
void clock_apply_derived(void);
bool clock_usb_ok_now(void);
uint32_t clock_ws_now(void);
uint32_t clock_sysclk_mhz(void);
void clock_lcd_target_set(uint32_t hz);   /* LCD SPI1 rate the profile tries to keep (<=) */
uint16_t clock_lcd_div_for_hz(uint32_t hz);
void clock_emit_info(void);
void clk_cmd(int argc, char **argv);

#endif
