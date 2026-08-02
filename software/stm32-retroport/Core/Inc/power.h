#ifndef POWER_H
#define POWER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Real battery divider: VOUT - R12(33k) - BAT_STAT - R13(82k) - GND.
 * HIGH = top resistor to VOUT (33k), LOW = bottom resistor to GND (82k). */
#ifndef POWER_BATTERY_DIVIDER_HIGH_OHMS
#define POWER_BATTERY_DIVIDER_HIGH_OHMS 33000u
#endif

#ifndef POWER_BATTERY_DIVIDER_LOW_OHMS
#define POWER_BATTERY_DIVIDER_LOW_OHMS 82000u
#endif

#ifndef POWER_BATTERY_EMPTY_MV
#define POWER_BATTERY_EMPTY_MV 3300u
#endif

#ifndef POWER_BATTERY_FULL_MV
#define POWER_BATTERY_FULL_MV 4200u
#endif

void Power_Init(void);
void Power_Task(void);

uint8_t Power_GetBatteryPercent(void);
uint16_t Power_GetBatteryMillivolts(void);
bool Power_IsCharging(void);
bool Power_IsUsbPresent(void);
bool Power_IsOffRequested(void);
/* Debounced, boot-safe edge detect: true once when the ATTINY asserts OFF_REQ. */
bool Power_PollShutdown(void);
void Power_SetOffAck(bool acknowledged);

#ifdef __cplusplus
}
#endif

#endif /* POWER_H */
