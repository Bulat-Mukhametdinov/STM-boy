#include "gfx.h"
#include "font.h"
#include "lcd.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define GFX_MAX_SCALE 4

void gfx_fill(uint16_t color)
{
    lcd_fill_rect(0, 0, (int16_t)lcd_width(), (int16_t)lcd_height(), color);
}

void gfx_rect_fill(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    lcd_fill_rect(x, y, w, h, color);
}

void gfx_hline(int16_t x, int16_t y, int16_t w, uint16_t color)
{
    lcd_fill_rect(x, y, w, 1, color);
}

void gfx_vline(int16_t x, int16_t y, int16_t h, uint16_t color)
{
    lcd_fill_rect(x, y, 1, h, color);
}

void gfx_pixel(int16_t x, int16_t y, uint16_t color)
{
    lcd_fill_rect(x, y, 1, 1, color);
}

void gfx_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    gfx_hline(x, y, w, color);
    gfx_hline(x, (int16_t)(y + h - 1), w, color);
    gfx_vline(x, y, h, color);
    gfx_vline((int16_t)(x + w - 1), y, h, color);
}

int16_t gfx_char(int16_t x, int16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale)
{
    uint16_t line[GFX_CHAR_W * GFX_MAX_SCALE];

    if (scale < 1u) scale = 1u;
    if (scale > GFX_MAX_SCALE) scale = GFX_MAX_SCALE;
    int16_t cw = (int16_t)(GFX_CHAR_W * scale);
    int16_t ch = (int16_t)(GFX_CHAR_H * scale);

    /* Only whole cells are drawn (simple clipping). */
    if (x < 0 || y < 0 || (x + cw) > (int16_t)lcd_width() || (y + ch) > (int16_t)lcd_height()) {
        return cw;
    }

    const uint8_t *g = font5x7_glyph(c);
    if (lcd_native()) {
        /* native scan order: stream column by column (one glyph column = one native line) */
        uint16_t col[GFX_CHAR_H * GFX_MAX_SCALE];
        lcd_stream_window((uint16_t)x, (uint16_t)y, (uint16_t)(x + cw - 1), (uint16_t)(y + ch - 1));
        for (int16_t cx = 0; cx < GFX_CHAR_W; cx++) {
            for (int16_t row = 0; row < GFX_CHAR_H; row++) {
                bool on = (cx < FONT_W) && (row < FONT_H) && ((g[cx] >> row) & 1u);
                uint16_t px = on ? fg : bg;
                for (uint8_t s = 0; s < scale; s++) {
                    col[row * scale + s] = px;
                }
            }
            for (uint8_t s = 0; s < scale; s++) {
                lcd_push_pixels(col, (uint32_t)ch);
            }
        }
        lcd_end();
        return cw;
    }
    lcd_set_window((uint16_t)x, (uint16_t)y, (uint16_t)(x + cw - 1), (uint16_t)(y + ch - 1));
    for (int16_t row = 0; row < GFX_CHAR_H; row++) {
        for (int16_t col = 0; col < GFX_CHAR_W; col++) {
            bool on = (col < FONT_W) && (row < FONT_H) && ((g[col] >> row) & 1u);
            uint16_t px = on ? fg : bg;
            for (uint8_t s = 0; s < scale; s++) {
                line[col * scale + s] = px;
            }
        }
        for (uint8_t s = 0; s < scale; s++) {
            lcd_push_pixels(line, (uint32_t)cw);
        }
    }
    lcd_end();
    return cw;
}

int16_t gfx_text(int16_t x, int16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale)
{
    while (*s) {
        x = (int16_t)(x + gfx_char(x, y, *s++, fg, bg, scale));
    }
    return x;
}

int16_t gfx_textf(int16_t x, int16_t y, uint16_t fg, uint16_t bg, uint8_t scale, const char *fmt, ...)
{
    char buf[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return gfx_text(x, y, buf, fg, bg, scale);
}

void gfx_text_pad(int16_t x, int16_t y, const char *s, uint16_t fg, uint16_t bg, uint8_t scale,
                  int16_t width_px)
{
    int16_t end = gfx_text(x, y, s, fg, bg, scale);
    int16_t rest = (int16_t)(x + width_px - end);
    if (rest > 0) {
        lcd_fill_rect(end, y, rest, (int16_t)(GFX_CHAR_H * (scale ? scale : 1)), bg);
    }
}

int16_t gfx_text_wrap(int16_t x, int16_t y, int16_t w, const char *s, uint16_t fg, uint16_t bg,
                      uint8_t scale)
{
    int16_t cw = (int16_t)(GFX_CHAR_W * scale);
    int16_t chh = (int16_t)(GFX_CHAR_H * scale);
    int16_t per_line = (int16_t)(w / cw);
    char line[40];

    if (per_line < 1) {
        return y;
    }
    if (per_line > (int16_t)sizeof(line) - 1) {
        per_line = (int16_t)sizeof(line) - 1;
    }
    while (*s) {
        /* take up to per_line chars, break at last space if the word continues */
        int16_t n = 0, brk = -1;
        while (s[n] && s[n] != '\n' && n < per_line) {
            if (s[n] == ' ') brk = n;
            n++;
        }
        int16_t take = n;
        if (s[n] && s[n] != '\n' && s[n] != ' ' && brk > 0) {
            take = brk;
        }
        memcpy(line, s, (size_t)take);
        line[take] = '\0';
        gfx_text_pad(x, y, line, fg, bg, scale, w);
        y = (int16_t)(y + chh);
        s += take;
        while (*s == ' ' || *s == '\n') {
            s++;
        }
        if (y + chh > (int16_t)lcd_height()) {
            break;
        }
    }
    return y;
}
