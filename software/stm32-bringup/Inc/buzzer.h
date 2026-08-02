#ifndef __BUZZER_H
#define __BUZZER_H

#include "stm32f4xx_hal.h"

void Buzzer_Init(void);
/* Blocking: plays a tone of freq_hz for duration_ms, then silences the PWM output */
void Buzzer_Beep(uint32_t freq_hz, uint32_t duration_ms);

#endif
