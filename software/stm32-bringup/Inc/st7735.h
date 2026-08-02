#ifndef __ST7735_H
#define __ST7735_H

#include "stm32f4xx_hal.h"

/* Landscape orientation: X runs along the wide (160px) side */
#define ST7735_WIDTH   160
#define ST7735_HEIGHT  128

#define COLOR_BLACK    0x0000
#define COLOR_WHITE    0xFFFF
#define COLOR_RED      0xF800
#define COLOR_GREEN    0x07E0
#define COLOR_BLUE     0x001F
#define COLOR_YELLOW   0xFFE0
#define COLOR_GRAY     0x39C7
#define COLOR_DARKGRAY 0x2104
#define COLOR_ORANGE   0xFC00

void ST7735_Init(void);
void ST7735_FillScreen(uint16_t color);
void ST7735_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ST7735_DrawRectOutline(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ST7735_BacklightOn(void);
void ST7735_DrawChar(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale);
void ST7735_DrawString(uint16_t x, uint16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale);

#endif
