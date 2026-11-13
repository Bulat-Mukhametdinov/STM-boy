#ifndef TIMEBASE_H
#define TIMEBASE_H

#include "stm32f4xx.h"
#include <stdint.h>

/* Time sources:
 *  - SysTick 1 kHz: HAL tick (HAL_GetTick, ms) + input scan hook (priority 1)
 *  - TIM2: free-running 32-bit counter at 1 MHz (us), wraps after ~71 min
 *  - DWT CYCCNT: CPU cycle counter (wraps after ~44.7 s at 96 MHz)
 * Call timebase_init() after every SYSCLK change. */

void timebase_init(void);

static inline uint32_t time_us(void) { return TIM2->CNT; }
static inline uint32_t time_cycles(void) { return DWT->CYCCNT; }
uint32_t time_ms(void);

void delay_us(uint32_t us);

/* 1 kHz hook invoked from SysTick_Handler (input scan, deferred actions). */
void timebase_systick_hook(void);

#endif
