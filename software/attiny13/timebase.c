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

    /* Start Timer0, prescaler 64. */
    TCCR0B = (1 << CS01) | (1 << CS00);
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
