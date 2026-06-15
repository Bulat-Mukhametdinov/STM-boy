/* SPDX-License-Identifier: MIT */

#ifndef INPUTS_H
#define INPUTS_H

#include <stdint.h>

typedef uint8_t fsm_inputs_t;

#define INPUT_PWR_PRESSED      (1u << 0)
#define INPUT_PWR_RELEASED     (1u << 1)
#define INPUT_OFF_ACK_ACTIVE   (1u << 2)

void inputs_init(uint16_t now_ms);
void inputs_update(uint16_t now_ms);

fsm_inputs_t inputs_get(void);

#endif
