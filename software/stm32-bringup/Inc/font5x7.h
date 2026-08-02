#ifndef __FONT5X7_H
#define __FONT5X7_H

#include "stm32f4xx_hal.h"

/* Returns a pointer to 5 column bytes (bit0=top row .. bit6=bottom row) for
   the given character, or a blank glyph if unsupported. Supports:
   space . - : % 0-9 A B C G H M S T U V (uppercase only) */
const uint8_t *Font5x7_GetGlyph(char c);

#endif
