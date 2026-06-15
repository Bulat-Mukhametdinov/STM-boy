/* SPDX-License-Identifier: MIT */

#ifndef POWER_FSM_H
#define POWER_FSM_H

#include <stdint.h>

typedef uint8_t power_state_t;

enum {
    ST_OFF_SLEEP = 0,

    ST_OFF_PRESS_DEBOUNCE,
    ST_POWER_ON_HOLD,
    ST_POWER_ENABLE,
    ST_POWER_STABILIZE,
    ST_RUN_WAIT_RELEASE,

    ST_RUNNING,
    ST_RUN_PRESS_DEBOUNCE,
    ST_RUN_HOLDING,
    ST_SHUTDOWN_ARMED,

    ST_GRACEFUL_SHUTDOWN_REQ,
    ST_WAIT_OFF_ACK,
    ST_POWER_OFF,
    ST_OFF_LOCKOUT,

    ST_HARD_RESET_CUT,
    ST_HARD_RESET_WAIT_RELEASE
};

void power_fsm_init(uint16_t now_ms);
void power_fsm_update(uint16_t now_ms);
power_state_t power_fsm_state(void);
uint8_t power_fsm_may_sleep(void);

#endif
