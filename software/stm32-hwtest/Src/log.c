#include "log.h"
#include "usb.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define FULL_WAIT_MS 50u  /* how long a writer may wait for space when the host is listening */

static char s_ring[LOG_RING_SIZE];
static uint32_t s_head; /* write index (free running) */
static uint32_t s_tail; /* read index (free running) */
static uint32_t s_drops;

void log_init(void)
{
    s_head = s_tail = 0;
    s_drops = 0;
}

uint32_t log_pending(void) { return s_head - s_tail; }
uint32_t log_drops(void) { return s_drops; }

void log_poll(void)
{
    uint32_t pending = s_head - s_tail;
    if (pending == 0u || usb_tx_busy() || !usb_configured()) {
        return;
    }
    /* Largest contiguous chunk, capped at the USB staging buffer. */
    uint32_t off = s_tail & (LOG_RING_SIZE - 1u);
    uint32_t chunk = LOG_RING_SIZE - off;
    if (chunk > pending) chunk = pending;
    if (chunk > usb_tx_max()) chunk = usb_tx_max();
    if (usb_tx((const uint8_t *)&s_ring[off], (uint16_t)chunk)) {
        s_tail += chunk;
    }
}

void log_flush(uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();
    while ((s_head != s_tail || usb_tx_busy()) && usb_configured() &&
           (uint32_t)(HAL_GetTick() - t0) < timeout_ms) {
        log_poll();
    }
}

static bool reserve(size_t n)
{
    if (n > LOG_RING_SIZE) {
        return false;
    }
    if ((LOG_RING_SIZE - (s_head - s_tail)) < n) {
        /* Ring full. Only wait if somebody is plausibly reading. */
        if (usb_configured() && usb_dtr()) {
            uint32_t t0 = HAL_GetTick();
            while ((LOG_RING_SIZE - (s_head - s_tail)) < n &&
                   (uint32_t)(HAL_GetTick() - t0) < FULL_WAIT_MS) {
                log_poll();
            }
        }
    }
    return (LOG_RING_SIZE - (s_head - s_tail)) >= n;
}

static void put(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        s_ring[(s_head + i) & (LOG_RING_SIZE - 1u)] = s[i];
    }
    s_head += (uint32_t)n;
}

/* All-or-nothing write of up to three parts (one protocol line is never split). */
static bool write3(const char *a, size_t an, const char *b, size_t bn, const char *c, size_t cn)
{
    size_t n = an + bn + cn;
    if (n == 0u) {
        return true;
    }
    if (!reserve(n)) {
        s_drops++;
        return false;
    }
    put(a, an);
    put(b, bn);
    put(c, cn);
    log_poll();
    return true;
}

bool log_write(const char *s, size_t n)
{
    return write3(s, n, NULL, 0, NULL, 0);
}

void log_puts(const char *s)
{
    log_write(s, strlen(s));
}

static void vwrite(const char *prefix, const char *fmt, va_list ap, const char *suffix)
{
    char buf[512];
    int r = vsnprintf(buf, sizeof(buf), fmt, ap);
    size_t n = (r < 0) ? 0u : ((size_t)r >= sizeof(buf) ? sizeof(buf) - 1u : (size_t)r);
    write3(prefix ? prefix : "", prefix ? strlen(prefix) : 0u, buf, n,
           suffix ? suffix : "", suffix ? strlen(suffix) : 0u);
}

void log_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vwrite(NULL, fmt, ap, NULL);
    va_end(ap);
}

void res_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vwrite("@R ", fmt, ap, "\r\n");
    va_end(ap);
}

void proto_ok(const char *cmd) { log_printf("@OK %s\r\n", cmd); }
void proto_err(const char *cmd, const char *msg) { log_printf("@ERR %s %s\r\n", cmd, msg); }
void proto_done(const char *suite, const char *verdict) { log_printf("@DONE %s %s\r\n", suite, verdict); }

/* ---------------------------------------------------------------- JSON builder */

static void jb_put(jb_t *j, const char *s, size_t n)
{
    if (j->len + n + 2u >= sizeof(j->buf)) { /* keep room for "}" + NUL */
        j->overflow = true;
        return;
    }
    memcpy(&j->buf[j->len], s, n);
    j->len += n;
    j->buf[j->len] = '\0';
}

static void jb_puts(jb_t *j, const char *s) { jb_put(j, s, strlen(s)); }

static void jb_escaped(jb_t *j, const char *s)
{
    jb_put(j, "\"", 1);
    for (; *s; s++) {
        char c = *s;
        if (c == '"' || c == '\\') {
            char e[2] = {'\\', c};
            jb_put(j, e, 2);
        } else if ((unsigned char)c < 0x20u) {
            char e[8];
            int n = snprintf(e, sizeof(e), "\\u%04x", (unsigned)(unsigned char)c);
            jb_put(j, e, (size_t)n);
        } else {
            jb_put(j, &c, 1);
        }
    }
    jb_put(j, "\"", 1);
}

static void jb_key(jb_t *j, const char *key)
{
    if (!j->first) {
        jb_put(j, ",", 1);
    }
    j->first = false;
    jb_escaped(j, key);
    jb_put(j, ":", 1);
}

void jb_begin(jb_t *j, const char *type)
{
    j->len = 0;
    j->first = true;
    j->overflow = false;
    j->buf[0] = '\0';
    jb_put(j, "{", 1);
    jb_str(j, "t", type);
}

void jb_str(jb_t *j, const char *key, const char *val)
{
    jb_key(j, key);
    jb_escaped(j, val ? val : "");
}

void jb_u32(jb_t *j, const char *key, uint32_t v)
{
    char b[16];
    int n = snprintf(b, sizeof(b), "%" PRIu32, v);
    jb_key(j, key);
    jb_put(j, b, (size_t)n);
}

void jb_i32(jb_t *j, const char *key, int32_t v)
{
    char b[16];
    int n = snprintf(b, sizeof(b), "%" PRId32, v);
    jb_key(j, key);
    jb_put(j, b, (size_t)n);
}

void jb_bool(jb_t *j, const char *key, bool v)
{
    jb_key(j, key);
    jb_puts(j, v ? "true" : "false");
}

void jb_hex32(jb_t *j, const char *key, uint32_t v)
{
    char b[16];
    snprintf(b, sizeof(b), "%08" PRIX32, v);
    jb_str(j, key, b);
}

void jb_u16_array(jb_t *j, const char *key, const uint16_t *v, size_t n)
{
    char b[8];
    jb_key(j, key);
    jb_put(j, "[", 1);
    for (size_t i = 0; i < n; i++) {
        int k = snprintf(b, sizeof(b), "%s%u", i ? "," : "", (unsigned)v[i]);
        jb_put(j, b, (size_t)k);
    }
    jb_put(j, "]", 1);
}

void jb_raw(jb_t *j, const char *key, const char *json)
{
    jb_key(j, key);
    jb_puts(j, json);
}

void jb_end(jb_t *j)
{
    if (j->overflow) {
        /* Still emit valid JSON: mark truncation. */
        j->len = 0;
        j->buf[0] = '\0';
        j->overflow = false;
        jb_puts(j, "{\"t\":\"error\",\"msg\":\"json overflow\"");
    }
    j->buf[j->len++] = '}';
    j->buf[j->len] = '\0';
}

void jb_emit(jb_t *j, const char *prefix)
{
    write3(prefix ? prefix : "", prefix ? strlen(prefix) : 0u, j->buf, j->len, "\r\n", 2);
}
