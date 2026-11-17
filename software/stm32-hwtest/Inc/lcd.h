#ifndef LCD_H
#define LCD_H

#include <stdbool.h>
#include <stdint.h>

/* ST7735S on SPI1, register-level, polled, write-only (BIDIMODE/BIDIOE: MOSI
   only; PA6 is the backlight, not MISO). Commands always go out as 8-bit
   frames; pixel data optionally as 16-bit frames (lcd_set_frame16).
   Physical GDDRAM is 132 x 162. All geometry is runtime-configurable so that
   P1 can calibrate it. Defaults: landscape 160x128, MADCTL 0xA0 = MY|MV, RGB,
   x offset 1, y offset 2 (calibrated with display.cal on 2026-11-13; P0 used 3/1 from
   stm32-bringup, which lost the right and top border; display.cal is the source of
   truth). */

#define LCD_GDDRAM_W 132u
#define LCD_GDDRAM_H 162u

#define LCD_MADCTL_MY  0x80u
#define LCD_MADCTL_MX  0x40u
#define LCD_MADCTL_MV  0x20u
#define LCD_MADCTL_ML  0x10u
#define LCD_MADCTL_BGR 0x08u

#define LCD_GAMMA_DEFAULT    0u  /* controller power-on curve (needs a reset to return to) */
#define LCD_GAMMA_RETROPORT  1u  /* stm32-retroport / Adafruit ST7735 "Rcmd3" E0/E1 */
#define LCD_GAMMA_SITRONIX   2u  /* ST7735S sample init (Waveshare 1.8" LCD demo) E0/E1 */
#define LCD_GAMMA_COUNT      3u

typedef struct {
    uint8_t madctl;    /* MY/MX/MV/ML bits; the BGR bit is taken from `bgr` */
    uint8_t xoff;      /* added to CASET (x) */
    uint8_t yoff;      /* added to RASET (y) */
    uint8_t bgr;       /* 1 = set MADCTL.BGR */
    uint8_t invert;    /* 1 = INVON */
    uint8_t gamma_id;  /* LCD_GAMMA_* */
    uint16_t spi_div;  /* SPI1 prescaler: 2,4,...,256 of PCLK2 (8 => 12 MHz at 96 MHz) */
} lcd_cfg_t;

extern lcd_cfg_t lcd_cfg;

void lcd_cfg_defaults(lcd_cfg_t *c);

void lcd_init(void);                 /* full reset + init + clear all 132x162 */
void lcd_apply_cfg(void);            /* re-send MADCTL / INV / gamma / SPI speed (gamma 0 => re-init) */
void lcd_set_spi_div(uint16_t div);
uint32_t lcd_spi_hz(void);
uint16_t lcd_width(void);
uint16_t lcd_height(void);
uint8_t lcd_xoff_max(void);          /* largest x offset that keeps the window inside GDDRAM */
uint8_t lcd_yoff_max(void);
const char *lcd_gamma_name(uint8_t id);

/* Physical position of the 128x160 panel inside the 132x162 GDDRAM, from
   display.cal on the v0.1 board (2026-11-13): portrait MADCTL 0x00 -> x2 y1,
   landscape 0xA0 -> x1 y2. Both slacks are symmetric (2 of 4 on the 132
   axis, 1 of 2 on the 162 axis), so mirroring (MX/MY) does not change them;
   lcd_offsets_for() still derives them generally:
   mirrored axis offset = (GDDRAM - visible) - physical offset. */
#define LCD_PHYS_OFF_132 2u
#define LCD_PHYS_OFF_162 1u
void lcd_offsets_for(uint8_t madctl, uint8_t *xoff, uint8_t *yoff);

/* SPI1 bus variants for the flicker stress test. */
void lcd_set_frmctr1(const uint8_t b[3]);  /* FRMCTR1 RTNA/FPA/BPA (lcd_init restores the default) */
void lcd_set_frame16(bool on);       /* pixel data in 16-bit SPI frames */
bool lcd_frame16(void);
void lcd_set_gpio_speed(uint32_t speed); /* OSPEEDR value for SCK/MOSI (2 high, 3 very high) */
uint32_t lcd_gpio_speed(void);

/* Backlight on/off (full brightness / off), routed through backlight.h. */
void lcd_backlight(bool on);
void lcd_sleep(bool on);             /* DISPOFF+SLPIN / SLPOUT+DISPON (blocks ~130 ms on wake) */

/* Low-level streaming: lcd_set_window() leaves the controller in RAMWR with
   CS asserted; push pixels; lcd_end() waits for the bus and releases CS. */
void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void lcd_push_pixels(const uint16_t *px, uint32_t n);   /* RGB565, host order */
void lcd_push_color(uint16_t color, uint32_t n);
void lcd_end(void);

void lcd_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void lcd_clear_gddram(uint16_t color);  /* whole 132x162 RAM, ignores offsets */

#endif
