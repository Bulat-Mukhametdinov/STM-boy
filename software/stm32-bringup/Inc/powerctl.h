#ifndef __POWERCTL_H
#define __POWERCTL_H

#include "stm32f4xx_hal.h"

void PowerCtl_Init(void);

/* Call frequently from the main loop. Returns 1 exactly once, when a debounced,
   boot-safe shutdown request (OFF_REQ high->low) has been confirmed. */
uint8_t PowerCtl_PollShutdown(void);

/* Drive OFF_ACK to its asserted level, telling the ATTINY to cut system power.
   Call after finishing shutdown housekeeping; power will drop shortly after. */
void PowerCtl_AcknowledgeShutdown(void);

#endif
