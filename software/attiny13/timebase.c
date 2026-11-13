/* SPDX-License-Identifier: MIT */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include "config.h"
#include "timebase.h"

static volatile uint16_t g_ms = 0;

void timebase_init(void)
{
    /* Timer0 CTC mode, compare match A every approximately 1 ms. */
    TCCR0A = (1 << WGM01);
    TCCR0B = 0;
    OCR0A = TIMER0_OCR0A_VALUE;
    TCNT0 = 0;

    TIMSK0 |= (1 << OCIE0A);

    /* Start Timer0 with the prescaler OCR0A was computed for (config.h).
       Bug fixed 2026-11-13: this used to hard-code /64 while OCR0A assumed
       /8, so every ATtiny interval ran 8x long (0.5 s POWER hold -> ~4 s,
       2.5 s OFF_ACK timeout -> ~20 s, 8 s hard reset -> ~64 s). */
#if TIMER0_PRESCALER == 8UL
    TCCR0B = (1 << CS01);
#elif TIMER0_PRESCALER == 64UL
    TCCR0B = (1 << CS01) | (1 << CS00);
#else
#error "unsupported TIMER0_PRESCALER (use 8 or 64)"
#endif
}

ISR(TIM0_COMPA_vect)
{
    g_ms++;
}

uint16_t millis16(void)
{
    uint16_t value;
    uint8_t sreg = SREG;

    cli();
    value = g_ms;
    SREG = sreg;

    return value;
}
