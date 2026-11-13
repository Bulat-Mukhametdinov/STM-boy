#ifndef LOG_H
#define LOG_H

/* CDC output: an 8 KB TX ring drained from thread context (main loop and
   wait helpers via log_poll()). Never blocks forever: when the ring is full
   and the host is not reading, whole messages are dropped and counted.
   Thread context only (never call from an ISR). */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LOG_RING_SIZE 8192u   /* power of two */

void log_init(void);
void log_poll(void);                       /* push the next chunk to USB if idle */
void log_flush(uint32_t timeout_ms);       /* drain until empty or timeout */
bool log_write(const char *s, size_t n);   /* all-or-nothing; false = dropped */
void log_puts(const char *s);
/* Binary-safe write that never drops part of the data: waits for ring space
   (pumping USB) and gives up only after `timeout_ms` without progress or when
   USB is gone (returns false; the host then sees a short/CRC-failing frame).
   Used for @B payloads. Do not interleave other output while it runs. */
bool log_write_blocking(const void *data, size_t n, uint32_t timeout_ms);
void log_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
uint32_t log_drops(void);
uint32_t log_pending(void);

/* Machine lines (PROTOCOL.md) */
void res_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2))); /* "@R " + fmt + "\r\n" */
void proto_ok(const char *cmd);                     /* "@OK <cmd>" */
void proto_err(const char *cmd, const char *msg);   /* "@ERR <cmd> <msg>" */
void proto_done(const char *suite, const char *verdict); /* "@DONE <suite> <verdict>" */

/* Small JSON object builder for single-line results. */
typedef struct {
    char buf[1024];
    size_t len;
    bool first;
    bool overflow;
} jb_t;

void jb_begin(jb_t *j, const char *type);   /* {"t":"<type>" */
void jb_str(jb_t *j, const char *key, const char *val);
void jb_u32(jb_t *j, const char *key, uint32_t v);
void jb_i32(jb_t *j, const char *key, int32_t v);
void jb_bool(jb_t *j, const char *key, bool v);
void jb_hex32(jb_t *j, const char *key, uint32_t v);          /* "XXXXXXXX" */
void jb_u16_array(jb_t *j, const char *key, const uint16_t *v, size_t n);
void jb_raw(jb_t *j, const char *key, const char *json);       /* pre-formatted value */
void jb_end(jb_t *j);                       /* closes the object */
void jb_emit(jb_t *j, const char *prefix);  /* prefix + json + CRLF (e.g. "@R ") */

#endif
