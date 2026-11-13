#ifndef POWER_H
#define POWER_H

#include <stdbool.h>
#include <stdint.h>

/* ATtiny13A power handshake (port of stm32-bringup powerctl.c):
   OFF_REQ (PA2) low for >= 30 ms after having been seen high => shutdown:
   amp off, LCD blanked + backlight off, OFF_ACK (PA3) driven high, wait for
   the rail to drop. */

void power_init(void);
/* Call often (main loop / long waits). Performs the shutdown and never
   returns once a debounced request is confirmed. */
void power_poll(void);

bool power_vbus(void);      /* USB VBUS present (PA9 high) */
bool power_charging(void);  /* charger CHG asserted (PA0 low) */
bool power_off_req(void);   /* raw OFF_REQ asserted */

#endif
