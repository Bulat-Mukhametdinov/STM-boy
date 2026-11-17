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
void power_sense_init(void);  /* re-apply CHG/VBUS input config */
bool power_off_req(void);   /* raw OFF_REQ asserted */

/* While inhibited, power_poll() ignores OFF_REQ (used by the pins suite,
   which briefly pulls PA2 down). Releasing re-arms only after PA2 has been
   seen high again. Keep inhibit windows short: a real POWER request is
   handled late (the ATtiny cuts power 2.5 s after OFF_REQ regardless). */
void power_inhibit(bool on);

#endif
