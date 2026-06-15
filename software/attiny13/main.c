/* SPDX-License-Identifier: MIT */

#include <avr/interrupt.h>
#include <stdint.h>
#include "hw.h"
#include "timebase.h"
#include "inputs.h"
#include "power_fsm.h"

int main(void)
{
    uint16_t now;

    hw_init();
    timebase_init();
    sei();

    now = millis16();
    inputs_init(now);
    power_fsm_init(now);

    for (;;) {
        now = millis16();

        inputs_update(now);
        power_fsm_update(now);

        if (power_fsm_may_sleep()) {
            hw_sleep_until_pwr_change();
        }
    }
}
