#ifndef FONT_H
#define FONT_H

#include <stdint.h>

/* 5x7 column-major ASCII font, printable range 32..126 (complete).
   Each glyph = 5 bytes, one per column, bit 0 = top row. */
#define FONT_FIRST 32
#define FONT_LAST  126
#define FONT_W     5
#define FONT_H     7

const uint8_t *font5x7_glyph(char c); /* unknown chars map to '?' */

#endif
