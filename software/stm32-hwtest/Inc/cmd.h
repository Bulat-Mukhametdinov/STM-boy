#ifndef CMD_H
#define CMD_H

#include <stdbool.h>
#include <stdint.h>

/* Host command dispatcher (PROTOCOL.md). */

void cmd_init(void);

/* Main loop: process all queued host lines with the full command set. */
void cmd_poll(void);

/* Keep the system alive from inside long-running work (suites, prompts, UI
   waits): drains CDC TX, handles OFF_REQ, sends @HELLO, and answers a
   restricted command set (ping, info, abort, dfu, reset, suites, help);
   everything else gets "@ERR <cmd> busy". */
void sys_service(void);

/* Busy state (reported by `info`) and abort handling for suites. */
void busy_begin(const char *suite);
void busy_end(void);
const char *busy_name(void);        /* "idle" when nothing runs */
bool abort_requested(void);         /* host `abort` (ISR fast path or parsed) */
void abort_request(void);           /* local abort (e.g. UI) */

/* Emits `<prefix>{info json}` (prefix "@R " for info, "@HELLO " for hello). */
void cmd_emit_info(const char *prefix);

/* Reset cause captured at boot (decoded RCC->CSR). */
void cmd_capture_reset_cause(void);
const char *cmd_reset_cause(void);

/* W25Q read-only sub-commands read/crc/scan (Src/ui/t_flash.c); true = handled. */
bool w25q_cmd_ext(int argc, char **argv);

#endif
