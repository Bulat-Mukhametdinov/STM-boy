/* SPDX-License-Identifier: MIT */

#ifndef TIMEBASE_H
#define TIMEBASE_H

#include <stdint.h>

void timebase_init(void);
uint16_t millis16(void);

static inline uint8_t elapsed16(uint16_t now, uint16_t start, uint16_t delay_ms)
{
    return (uint16_t)(now - start) >= delay_ms;
}

#endif
