/* SPDX-License-Identifier: MIT */

#ifndef CONFIG_H
#define CONFIG_H

#ifndef F_CPU
#define F_CPU 1200000UL
#endif

#include <avr/io.h>
#include <stdint.h>

/*
 * Logic levels.
 * PWR button: active low, because the button shorts the pin to GND.
 * OFF_REQ: active low. ATTiny pulls the line low to request shutdown.
 * OFF_ACK: default active high. Change to 0 if your STM32 drives ACK low.
 */
#define OFF_ACK_ACTIVE_LEVEL 1u

/* Pin assignment for ATTINY13A. */
#define PIN_PWR      PB0
#define PIN_OFF_ACK  PB2
#define PIN_OFF_REQ  PB3
#define PIN_EN       PB4

/* Timing constants, in milliseconds. */
#define T_DEBOUNCE_MS         20u
#define T_POWER_ON_HOLD_MS   100u
#define T_POWER_STABLE_MS    100u

#define T_SHUT_HOLD_MS      500u
#define T_HARD_RESET_MS     8000u

#define T_OFF_ACK_TIMEOUT_MS 2500u
#define T_RESET_CUT_MS       300u
#define T_OFF_LOCKOUT_MS     300u

/*
 * Timer0 configuration. Prescaler 64 is used for the system tick.
 *
 * Keep the intermediate value uncast so the preprocessor can validate it.
 * OCR0A is programmed to (ticks_per_ms - 1) in CTC mode.
 */
#ifndef TIMER0_PRESCALER
#define TIMER0_PRESCALER 8UL
#endif
#define TIMER0_TICKS_PER_MS \
    ((F_CPU + (TIMER0_PRESCALER * 500UL)) / (TIMER0_PRESCALER * 1000UL))

#if TIMER0_TICKS_PER_MS < 1UL
#error "F_CPU is too low for Timer0 1 ms tick with prescaler 64"
#endif

#if TIMER0_TICKS_PER_MS > 256UL
#error "F_CPU is too high for Timer0 1 ms tick with prescaler 64"
#endif

#define TIMER0_OCR0A_VALUE ((uint8_t)(TIMER0_TICKS_PER_MS - 1UL))

/*
 * The 16-bit millisecond counter supports wrap-safe elapsed checks for delays
 * shorter than half its range.
 */
#if T_HARD_RESET_MS > 32767u || T_OFF_ACK_TIMEOUT_MS > 32767u
#error "Timing constants must stay below 32768 ms for elapsed16()"
#endif

#endif
