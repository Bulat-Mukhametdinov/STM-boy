#ifndef ST7735_H
#define ST7735_H

#include <stddef.h>
#include <stdint.h>

#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

HAL_StatusTypeDef ST7735_Init(void);
void ST7735_SetBacklight(uint8_t enabled);
HAL_StatusTypeDef ST7735_WritePixels(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                                     const uint8_t *pixels, size_t length);
HAL_StatusTypeDef ST7735_FillScreen(uint16_t color);
HAL_StatusTypeDef ST7735_FillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color);
SPI_HandleTypeDef *ST7735_GetSpiHandle(void);

#ifdef __cplusplus
}
#endif

#endif
