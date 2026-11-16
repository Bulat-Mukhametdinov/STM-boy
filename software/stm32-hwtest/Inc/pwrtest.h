#ifndef PWRTEST_H
#define PWRTEST_H

/* P2 power suites (Src/ui/t_power.c, t_current.c, t_battlog.c) */

#include "suites.h"
#include "ui.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* host-supplied values for an open numeric entry screen */
typedef struct {
    volatile bool pending;
    volatile int32_t v[2];
} pwr_host_t;

/* Joystick number entry (UP/DOWN +-step_s, LEFT/RIGHT +-step_b, CENTER next field, A accept/next, B skip).
   Values are shown as v/div with `dec` decimals. Emits @P + closes it with @A. */
ui_answer_t pwr_num_entry(const char *id, const char *title, const char *l0, const char *l1, int nf,
                          int32_t v[2], int32_t step_s, int32_t step_b, int32_t lo, int32_t hi,
                          int32_t div, int dec, void (*live)(char *buf, size_t n), pwr_host_t *host,
                          uint32_t timeout_ms);

/* Loaded Vbat + die temp recorded at the end of a power.current state (RAM; `cur list`). */
void pwr_state_vbat_set(unsigned state, uint32_t mv, int tdc);

/* Per-state current in 0.1 mA (RAM; falls back to hwcal i_state_ma * 10). */
uint32_t pwr_state_x10(unsigned state);
void pwr_set_state_x10(unsigned state, uint32_t x10);   /* also updates g_hwcal.i_state_ma */
/* A reading entered via the host `cur` command while power.current waits for `state`. */
extern pwr_host_t g_cur_host;
extern volatile int g_cur_wait_state;

/* Host commands handled in any busy state: `adccal ...`, `cur <state> <mA>` */
bool pwr_cmd(int argc, char **argv);

/* battlog (t_battlog.c): command `battlog ...`; boot check */
void battlog_cmd(int argc, char **argv, bool restricted);
void battlog_boot_check(void);

suite_verdict_t suite_power_info(int argc, char **argv);
suite_verdict_t suite_power_adccal(int argc, char **argv);
suite_verdict_t suite_power_handshake(int argc, char **argv);
suite_verdict_t suite_power_current(int argc, char **argv);
suite_verdict_t suite_power_rint(int argc, char **argv);
suite_verdict_t suite_battlog(int argc, char **argv);

#endif
