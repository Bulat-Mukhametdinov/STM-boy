#ifndef LOWPOWER_H
#define LOWPOWER_H

/* Stop-mode helpers (P2): RTC (LSI) wake-up timer, OFF_REQ EXTI2 edge,
   optional VBUS (PA9, EXTI9) wake, and the peripheral sleep/wake sequence
   (backlight off, LCD SLPIN, W25Q power-down, USB D+ detach). */

#include <stdbool.h>
#include <stdint.h>

#define LP_WAKE_TIMER  1u
#define LP_WAKE_OFFREQ 2u
#define LP_WAKE_VBUS   4u

/* RTC clocked from LSI (~1 Hz ck_spre). lp_rtc_start() zeroes the counter. */
void lp_rtc_start(void);
uint32_t lp_rtc_seconds(void);         /* seconds since lp_rtc_start() (wraps at 24 h) */

/* PA2 falling edge log (EXTI2): lp_off_req_edge_ms/us are valid when lp_off_req_seen. */
void lp_off_req_arm(void);
extern volatile bool lp_off_req_seen;
extern volatile uint32_t lp_off_req_edge_us;   /* TIM2 timestamp */
extern volatile uint32_t lp_off_req_edge_ms;   /* HAL tick */

void lp_peripherals_sleep(void);       /* backlight off, LCD sleep, W25Q power-down, USB detach */
void lp_peripherals_wake(void);        /* reverse; backlight stays off */

/* Enter Stop until a wake source in `mask` fires (timeout_s = 0: no timer).
   Returns the LP_WAKE_* bit(s) that woke the core; clocks are restored. VBUS
   wake reconfigures EXTI9 to PA9 for the duration (restored on return). */
uint32_t lp_stop(uint32_t timeout_s, uint32_t mask);

/* Timing of the last lp_stop() (power.sleep): entry_us = lp_stop() entry -> just before WFI (RTC/EXTI setup);
   clk_cyc = DWT cycles from the first instruction after the wake-up to the end of clock_init_96mhz() (HSE start +
   PLL lock; the core runs from HSI 16 MHz for nearly all of it, so us ~ clk_cyc / 16); cause = LP_WAKE_*. */
typedef struct {
    uint32_t entry_us;
    uint32_t clk_cyc;
    uint32_t cause;
} lp_stop_info_t;
extern lp_stop_info_t lp_last_stop;

#endif
