#ifndef GAUGE_FW_H
#define GAUGE_FW_H

#include "gauge.h"
#include <stddef.h>

void gauge_fw_poll(void);                  /* main loop (not inside suites): feeds the gauge every 250 ms */
uint32_t gauge_fw_sig(void);               /* changes whenever the icon needs a redraw */
void gauge_fw_draw_icon(int16_t x, int16_t y, uint16_t bg);   /* 30 x 8 px */
void gauge_fw_json(char *buf, size_t n);   /* {"t":"gauge",...} */

#endif
