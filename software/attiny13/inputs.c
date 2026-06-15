/* SPDX-License-Identifier: MIT */

#include <stdint.h>
#include "config.h"
#include "timebase.h"
#include "hw.h"
#include "inputs.h"

typedef struct {
    uint8_t raw_released;
    uint8_t stable_released;
    uint16_t last_change_ms;
} debounced_button_t;

static debounced_button_t pwr;
static fsm_inputs_t input_flags;

static void rebuild_level_flags(void)
{
    input_flags = hw_off_ack_raw_active() ? INPUT_OFF_ACK_ACTIVE : 0u;

    if (pwr.stable_released) {
        input_flags |= INPUT_PWR_RELEASED;
    } else {
        input_flags |= INPUT_PWR_PRESSED;
    }
}

void inputs_init(uint16_t now_ms)
{
    pwr.raw_released = hw_pwr_raw_released();
    pwr.stable_released = pwr.raw_released;
    pwr.last_change_ms = now_ms;

    rebuild_level_flags();
}

void inputs_update(uint16_t now_ms)
{
    uint8_t raw_pwr = hw_pwr_raw_released();

    if (raw_pwr != pwr.raw_released) {
        pwr.raw_released = raw_pwr;
        pwr.last_change_ms = now_ms;
    }

    if (elapsed16(now_ms, pwr.last_change_ms, T_DEBOUNCE_MS)) {
        pwr.stable_released = pwr.raw_released;
    }

    rebuild_level_flags();
}

fsm_inputs_t inputs_get(void)
{
    return input_flags;
}
