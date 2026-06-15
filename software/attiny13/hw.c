/* SPDX-License-Identifier: MIT */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <stdint.h>
#include "config.h"
#include "hw.h"

void hw_init(void)
{
    /* Disable unused analog hardware to save current. */
    ADCSRA &= ~(1 << ADEN);
    ACSR |= (1 << ACD);

    /* Make sure internal pull-ups are globally enabled. */
#if defined(PUD)
    MCUCR &= ~(1 << PUD);
#endif

    /* PWR input, internal pull-up enabled. Button shorts to GND. */
    DDRB &= ~(1 << PIN_PWR);
    PORTB |= (1 << PIN_PWR);

    /* OFF_ACK input. No pull-up by default, because ACK must not look active
     * while the main MCU power domain is off. Add external bias if needed. */
    DDRB &= ~(1 << PIN_OFF_ACK);
    PORTB &= ~(1 << PIN_OFF_ACK);

    /* EN output, initially off. */
    hw_en_off();

    /* OFF_REQ inactive: Hi-Z. */
    hw_off_req_inactive();

    /* Pin-change interrupt on PWR to wake from power-down sleep. */
    GIMSK |= (1 << PCIE);
    PCMSK |= (1 << PCINT0);
}

uint8_t hw_pwr_raw_released(void)
{
    return (PINB & (1 << PIN_PWR)) ? 1u : 0u;
}

uint8_t hw_pwr_raw_pressed(void)
{
    return hw_pwr_raw_released() ? 0u : 1u;
}

uint8_t hw_off_ack_raw_active(void)
{
    uint8_t high = (PINB & (1 << PIN_OFF_ACK)) ? 1u : 0u;

#if OFF_ACK_ACTIVE_LEVEL
    return high;
#else
    return high ? 0u : 1u;
#endif
}

void hw_en_on(void)
{
    DDRB |= (1 << PIN_EN);
    PORTB |= (1 << PIN_EN);
}

void hw_en_off(void)
{
    DDRB |= (1 << PIN_EN);
    PORTB &= ~(1 << PIN_EN);
}

void hw_off_req_active(void)
{
    /* OFF_REQ active low: drive PB3 low. */
    PORTB &= ~(1 << PIN_OFF_REQ);
    DDRB |= (1 << PIN_OFF_REQ);
}

void hw_off_req_inactive(void)
{
    /* Release OFF_REQ to Hi-Z. The STM32-side pull-up to 3V3 makes it inactive. */
    PORTB &= ~(1 << PIN_OFF_REQ);
    DDRB &= ~(1 << PIN_OFF_REQ);
}

void hw_sleep_until_pwr_change(void)
{
    /*
     * The test and sleep-enable sequence is atomic to avoid missing a
     * pin-change interrupt between checking PB0 and executing SLEEP.
     */
    cli();

    if (hw_pwr_raw_pressed()) {
        sei();
        return;
    }

    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();

#if defined(BODS) && defined(BODSE)
    sleep_bod_disable();
#endif

    sei();
    sleep_cpu();

    sleep_disable();
}

EMPTY_INTERRUPT(PCINT0_vect)
