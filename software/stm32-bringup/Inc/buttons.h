#ifndef __BUTTONS_H
#define __BUTTONS_H

#include "stm32f4xx_hal.h"

typedef enum
{
  BTN_UP = 0,
  BTN_DOWN,
  BTN_LEFT,
  BTN_RIGHT,
  BTN_CE,
  BTN_A,
  BTN_B,
  BTN_X,
  BTN_Y,
  BTN_COUNT
} ButtonId;

extern const char *ButtonNames[BTN_COUNT];

void Buttons_Init(void);
/* Call periodically (e.g. every 5-10 ms) from the main loop */
void Buttons_Scan(void);
uint8_t Buttons_IsPressed(ButtonId id);
uint8_t Buttons_WasPressedEdge(ButtonId id);   /* true once, on press */
uint8_t Buttons_WasReleasedEdge(ButtonId id);  /* true once, on release */

#endif
