#ifndef __POWER_H
#define __POWER_H

#include "stm32f4xx_hal.h"

void Power_Init(void);
/* Call frequently from the main loop: feeds the rolling battery-sample window */
void Power_Poll(void);
/* Windowed-average voltage on the ADC pin (BAT_STAT / PA1), before divider math */
uint16_t Power_ReadPinMillivolts(void);
uint16_t Power_ReadBatteryMillivolts(void);
uint8_t  Power_ReadBatteryPercent(void);
uint8_t  Power_IsCharging(void);
uint8_t  Power_IsUsbConnected(void);
/* Heuristic: true only if the reading falls inside a plausible single-cell
   LiPo range. Out-of-range (e.g. battery unplugged, divider floating) -> false. */
uint8_t  Power_IsBatteryPresent(void);

#endif
