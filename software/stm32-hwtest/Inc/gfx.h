#ifndef GFX_H
#define GFX_H

#include <stdint.h>

/* RGB565 colours */
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))
#define COLOR_BLACK    0x0000u
#define COLOR_WHITE    0xFFFFu
#define COLOR_RED      0xF800u
#define COLOR_GREEN    0x07E0u
#define COLOR_BLUE     0x001Fu
#define COLOR_YELLOW   0xFFE0u
#define COLOR_CYAN     0x07FFu
#define COLOR_MAGENTA  0xF81Fu
#define COLOR_ORANGE   0xFD20u
#define COLOR_GRAY     0x8410u
#define COLOR_DARKGRAY 0x4208u
#define COLOR_NAVY     0x0010u

/* Text cell = 6x8 pixels per scale unit (5x7 glyph + 1 px spacing). */
#define GFX_CHAR_W 6
#define GFX_CHAR_H 8

void gfx_fill(uint16_t color);
void gfx_rect_fill(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void gfx_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color); /* 1 px outline */
void gfx_hline(int16_t x, int16_t y, int16_t w, uint16_t color);
void gfx_vline(int16_t x, int16_t y, int16_t h, uint16_t color);
void gfx_pixel(int16_t x, int16_t y, uint16_t color);

/* Draws one char cell (background included). Returns the cell width. */
int16_t gfx_char(int16_t x, int16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale);
/* Returns the x after the last char. '\n' is not interpreted. */
int16_t gfx_text(int16_t x, int16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale);
int16_t gfx_textf(int16_t x, int16_t y, uint16_t fg, uint16_t bg, uint8_t scale, const char *fmt, ...)
    __attribute__((format(printf, 6, 7)));
/* Text padded with background up to `width_px` (clears stale chars). */
void gfx_text_pad(int16_t x, int16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale,
                  int16_t width_px);
/* Word-wrapping text box; returns the y below the last line. */
int16_t gfx_text_wrap(int16_t x, int16_t y, int16_t w, const char *s, uint16_t fg, uint16_t bg,
                      uint8_t scale);

#endif
