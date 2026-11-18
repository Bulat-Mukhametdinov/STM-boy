#ifndef OC_H
#define OC_H

/* Overclock / underclock sweep (block B, plan section E): journal in the W25Q OC log
   (0xFE0000-0xFEFFFF, 32-byte records), IWDG supervision, boot-time recovery. */

#include <stdbool.h>
#include <stdint.h>

void oc_boot_check(void);                 /* after w25q_init + cmd_capture_reset_cause */
void oc_wdg_service(void);                /* from sys_service(): feeds the IWDG while the sweep is healthy */
void oc_cmd(int argc, char **argv);       /* oc log [clear] | oc stable | oc status */

/* Frequencies (MHz) the sweep marked stable (latest record per frequency wins), ascending. */
uint32_t oc_stable_list(uint16_t *out, uint32_t max);

/* Last demo scene statistics (Src/ui/t_demo.c), used for the LCD check. */
extern uint32_t g_demo_fps100;
extern uint32_t g_demo_frames;

#endif
