#ifndef LCD_H
#define LCD_H

#include <stdbool.h>
#include <stdint.h>

/* ST7735S on SPI1, register-level, polled, 8-bit frames, write-only
   (BIDIMODE/BIDIOE: MOSI only; PA6 is the backlight, not MISO).
   Physical GDDRAM is 132 x 162. All geometry is runtime-configurable so that
   P1 can calibrate it; defaults = stm32-bringup values (landscape 160x128,
   MADCTL 0xA0 = MY|MV, RGB, x offset 3, y offset 1). */

#define LCD_GDDRAM_W 132u
#define LCD_GDDRAM_H 162u

#define LCD_MADCTL_MY  0x80u
#define LCD_MADCTL_MX  0x40u
#define LCD_MADCTL_MV  0x20u
#define LCD_MADCTL_ML  0x10u
#define LCD_MADCTL_BGR 0x08u

typedef struct {
    uint8_t madctl;    /* MY/MX/MV/ML bits; the BGR bit is taken from `bgr` */
    uint8_t xoff;      /* added to CASET (x) */
    uint8_t yoff;      /* added to RASET (y) */
    uint8_t bgr;       /* 1 = set MADCTL.BGR */
    uint8_t invert;    /* 1 = INVON */
    uint8_t gamma_id;  /* 0 = controller default, 1 = stm32-retroport E0/E1 table */
    uint16_t spi_div;  /* SPI1 prescaler: 2,4,...,256 of PCLK2 (8 => 12 MHz at 96 MHz) */
} lcd_cfg_t;

extern lcd_cfg_t lcd_cfg;

void lcd_cfg_defaults(lcd_cfg_t *c);

void lcd_init(void);                 /* full reset + init + clear all 132x162 */
void lcd_apply_cfg(void);            /* re-send MADCTL / INV / gamma / SPI speed */
void lcd_set_spi_div(uint16_t div);
uint32_t lcd_spi_hz(void);
uint16_t lcd_width(void);
uint16_t lcd_height(void);
void lcd_backlight(bool on);

/* Low-level streaming: lcd_set_window() leaves the controller in RAMWR with
   CS asserted; push pixels; lcd_end() waits for the bus and releases CS. */
void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void lcd_push_pixels(const uint16_t *px, uint32_t n);   /* RGB565, host order */
void lcd_push_color(uint16_t color, uint32_t n);
void lcd_end(void);

void lcd_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void lcd_clear_gddram(uint16_t color);  /* whole 132x162 RAM, ignores offsets */

#endif
