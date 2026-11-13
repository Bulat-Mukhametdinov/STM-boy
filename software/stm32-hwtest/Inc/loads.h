#ifndef LOADS_H
#define LOADS_H

/* Repeatable load states used by power.current and battlog (P2/P2b).
   Ids follow the hwcal i_state_ma[] index. Absolute battery current of the
   whole board in that state (not an increment). */

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t id;
    const char *name;     /* short, <= 20 chars */
    const char *range;    /* meter range hint */
    bool dark;            /* backlight off while running */
    bool owns_lcd;        /* frames stream continuously: no text updates while running */
} load_def_t;

const load_def_t *load_def(int id);          /* NULL for undefined / skipped ids */
extern const uint8_t g_load_order[];         /* ids in the order of `power.current`, 0 terminated */

void load_begin(int id);                     /* configure + start (blocks for LCD/backlight setup) */
void load_step(void);                        /* one slice (<= ~35 ms); call between sys_service() */
void load_end(void);                         /* stop, restore idle LCD, backlight full */
void load_progress(uint32_t px, uint32_t of);/* progress bar in owns_lcd states */
void load_fb_text(int16_t y, const char *s, uint16_t fg);   /* text into the frame buffer (owns_lcd states) */

#endif
