/* SPDX-License-Identifier: MIT */

#include <stdint.h>
#include "config.h"
#include "timebase.h"
#include "hw.h"
#include "inputs.h"
#include "power_fsm.h"

static power_state_t state;

static uint16_t state_enter_ms;
static uint16_t pwr_press_start_ms;

static void set_state(power_state_t next, uint16_t now_ms)
{
    if (state == next) {
        return;
    }

    state = next;
    state_enter_ms = now_ms;

    switch (state) {
    case ST_OFF_SLEEP:
        hw_off_req_inactive();
        hw_en_off();
        break;

    case ST_POWER_ENABLE:
        hw_off_req_inactive();
        hw_en_on();
        break;

    case ST_GRACEFUL_SHUTDOWN_REQ:
        hw_off_req_active();
        break;

    case ST_POWER_OFF:
        hw_off_req_inactive();
        hw_en_off();
        break;

    case ST_HARD_RESET_CUT:
        hw_off_req_inactive();
        hw_en_off();
        break;

    default:
        break;
    }
}

void power_fsm_init(uint16_t now_ms)
{
    state = ST_POWER_OFF;
    state_enter_ms = now_ms;
    pwr_press_start_ms = now_ms;

    set_state(ST_OFF_SLEEP, now_ms);
}

void power_fsm_update(uint16_t now_ms)
{
    fsm_inputs_t in = inputs_get();

    switch (state) {
    case ST_OFF_SLEEP:
        if (hw_pwr_raw_pressed()) {
            set_state(ST_OFF_PRESS_DEBOUNCE, now_ms);
        }
        break;

    case ST_OFF_PRESS_DEBOUNCE:
        if (in & INPUT_PWR_RELEASED) {
            set_state(ST_OFF_SLEEP, now_ms);
        } else if (in & INPUT_PWR_PRESSED) {
            pwr_press_start_ms = now_ms;
            set_state(ST_POWER_ON_HOLD, now_ms);
        }
        break;

    case ST_POWER_ON_HOLD:
        if (in & INPUT_PWR_RELEASED) {
            set_state(ST_OFF_SLEEP, now_ms);
        } else if (elapsed16(now_ms, pwr_press_start_ms, T_POWER_ON_HOLD_MS)) {
            set_state(ST_POWER_ENABLE, now_ms);
        }
        break;

    case ST_POWER_ENABLE:
        set_state(ST_POWER_STABILIZE, now_ms);
        break;

    case ST_POWER_STABILIZE:
        if (elapsed16(now_ms, state_enter_ms, T_POWER_STABLE_MS)) {
            if (in & INPUT_PWR_PRESSED) {
                set_state(ST_RUN_WAIT_RELEASE, now_ms);
            } else {
                set_state(ST_RUNNING, now_ms);
            }
        }
        break;

    case ST_RUN_WAIT_RELEASE:
        if (in & INPUT_PWR_RELEASED) {
            set_state(ST_RUNNING, now_ms);
        }
        break;

    case ST_RUNNING:
        if (hw_pwr_raw_pressed()) {
            set_state(ST_RUN_PRESS_DEBOUNCE, now_ms);
        }
        break;

    case ST_RUN_PRESS_DEBOUNCE:
        if (in & INPUT_PWR_RELEASED) {
            set_state(ST_RUNNING, now_ms);
        } else if (in & INPUT_PWR_PRESSED) {
            pwr_press_start_ms = now_ms;
            set_state(ST_RUN_HOLDING, now_ms);
        }
        break;

    case ST_RUN_HOLDING:
        if (in & INPUT_PWR_RELEASED) {
            set_state(ST_RUNNING, now_ms);
        } else if (elapsed16(now_ms, pwr_press_start_ms, T_SHUT_HOLD_MS)) {
            set_state(ST_SHUTDOWN_ARMED, now_ms);
        }
        break;

    case ST_SHUTDOWN_ARMED:
        if (in & INPUT_PWR_RELEASED) {
            set_state(ST_GRACEFUL_SHUTDOWN_REQ, now_ms);
        } else if (elapsed16(now_ms, pwr_press_start_ms, T_HARD_RESET_MS)) {
            set_state(ST_HARD_RESET_CUT, now_ms);
        }
        break;

    case ST_GRACEFUL_SHUTDOWN_REQ:
        set_state(ST_WAIT_OFF_ACK, now_ms);
        break;

    case ST_WAIT_OFF_ACK:
        if (in & INPUT_OFF_ACK_ACTIVE) {
            set_state(ST_POWER_OFF, now_ms);
        } else if (elapsed16(now_ms, state_enter_ms, T_OFF_ACK_TIMEOUT_MS)) {
            set_state(ST_POWER_OFF, now_ms);
        }
        break;

    case ST_POWER_OFF:
        set_state(ST_OFF_LOCKOUT, now_ms);
        break;

    case ST_OFF_LOCKOUT:
        if ((in & INPUT_PWR_RELEASED) && elapsed16(now_ms, state_enter_ms, T_OFF_LOCKOUT_MS)) {
            set_state(ST_OFF_SLEEP, now_ms);
        }
        break;

    case ST_HARD_RESET_CUT:
        if (elapsed16(now_ms, state_enter_ms, T_RESET_CUT_MS)) {
            set_state(ST_HARD_RESET_WAIT_RELEASE, now_ms);
        }
        break;

    case ST_HARD_RESET_WAIT_RELEASE:
        if (in & INPUT_PWR_RELEASED) {
            set_state(ST_POWER_ENABLE, now_ms);
        }
        break;

    default:
        set_state(ST_POWER_OFF, now_ms);
        break;
    }
}

power_state_t power_fsm_state(void)
{
    return state;
}

uint8_t power_fsm_may_sleep(void)
{
    return state == ST_OFF_SLEEP;
}
