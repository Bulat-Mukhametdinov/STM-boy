/* P5 benchmark harness + shared result notes. */

#include "bench.h"
#include "cmd.h"
#include "fw_id.h"
#include "log.h"
#include "timebase.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static uint32_t s_overhead;      /* empty-kernel cycles of one measurement */
static bool s_overhead_valid;

static void k_empty(uint32_t n) { (void)n; }

static void sort7(uint32_t *v)
{
    for (int i = 1; i < BENCH_REPS; i++) {
        uint32_t k = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > k) {
            v[j + 1] = v[j];
            j--;
        }
        v[j + 1] = k;
    }
}

static bench_stat_t measure(bench_fn_t fn, uint32_t n, bool mask)
{
    uint32_t r[BENCH_REPS];
    for (int i = -1; i < BENCH_REPS; i++) { /* i = -1: warm-up */
        uint32_t pm = __get_PRIMASK();
        if (mask) {
            __disable_irq();
        }
        __DSB();
        __ISB();
        uint32_t t0 = DWT->CYCCNT;
        fn(n);
        MEMBAR();
        uint32_t t1 = DWT->CYCCNT;
        if (mask && !pm) {
            __enable_irq();
        }
        if (i >= 0) {
            r[i] = t1 - t0;
        }
        sys_service();
    }
    sort7(r);
    bench_stat_t s = {r[0], r[BENCH_REPS / 2]};
    return s;
}

static void ensure_overhead(void)
{
    if (!s_overhead_valid) {
        bench_stat_t e = measure(k_empty, 0, true);
        s_overhead = e.cyc_min;
        s_overhead_valid = true;
    }
}

bench_stat_t bench_measure(bench_fn_t fn, uint32_t n)
{
    ensure_overhead();
    bench_stat_t s = measure(fn, n, true);
    s.cyc_min = (s.cyc_min > s_overhead) ? s.cyc_min - s_overhead : 0u;
    s.cyc_med = (s.cyc_med > s_overhead) ? s.cyc_med - s_overhead : 0u;
    return s;
}

bench_stat_t bench_measure_irq(bench_fn_t fn, uint32_t n)
{
    ensure_overhead();
    bench_stat_t s = measure(fn, n, false);
    s.cyc_min = (s.cyc_min > s_overhead) ? s.cyc_min - s_overhead : 0u;
    s.cyc_med = (s.cyc_med > s_overhead) ? s.cyc_med - s_overhead : 0u;
    return s;
}

uint32_t cyc_to_us(uint32_t cyc) { return (uint32_t)(((uint64_t)cyc * 1000000u) / SystemCoreClock); }

const char *fmt2(char *buf, size_t n, uint32_t x100)
{
    snprintf(buf, n, "%" PRIu32 ".%02" PRIu32, x100 / 100u, x100 % 100u);
    return buf;
}

static void art_str(char *b, size_t n)
{
    uint32_t a = FLASH->ACR;
    snprintf(b, n, "I%uD%uP%u", (a & FLASH_ACR_ICEN) ? 1u : 0u, (a & FLASH_ACR_DCEN) ? 1u : 0u,
             (a & FLASH_ACR_PRFTEN) ? 1u : 0u);
}

void bench_emit_cyc(const char *suite, const char *name, uint32_t cyc_min, uint32_t cyc_med, uint32_t units,
                    const char *unit, uint32_t bytes, const char *loc, const char *extra)
{
    char art[12], pu[16], mb[16], pj[56], mj[40];
    art_str(art, sizeof(art));
    pj[0] = '\0';
    mj[0] = '\0';
    if (units && unit) {
        uint32_t x100 = (uint32_t)(((uint64_t)cyc_min * 100u + units / 2u) / units);
        snprintf(pj, sizeof(pj), ",\"unit\":\"%s\",\"per_unit\":%s", unit, fmt2(pu, sizeof(pu), x100));
    }
    if (bytes && cyc_min) {
        /* MB/s = bytes * clk / cyc / 1e6 (MB = 1e6 bytes), 2 decimals */
        uint32_t x100 = (uint32_t)(((uint64_t)bytes * SystemCoreClock) / ((uint64_t)cyc_min * 10000u));
        snprintf(mj, sizeof(mj), ",\"mbps\":%s", fmt2(mb, sizeof(mb), x100));
    }
    res_printf("{\"t\":\"bench\",\"suite\":\"%s\",\"name\":\"%s\",\"cyc_min\":%" PRIu32 ",\"cyc_med\":%" PRIu32
               ",\"us\":%" PRIu32 "%s%s,\"clk\":%" PRIu32 ",\"ws\":%" PRIu32 ",\"art\":\"%s\",\"opt\":\"%s\","
               "\"loc\":\"%s\"%s}",
               suite, name, cyc_min, cyc_med, cyc_to_us(cyc_min), pj, mj, SystemCoreClock,
               (uint32_t)(FLASH->ACR & FLASH_ACR_LATENCY), art, g_fw_opt, loc ? loc : "flash",
               extra ? extra : "");
}

void bench_emit(const char *suite, const char *name, bench_stat_t st, uint32_t units, const char *unit,
                uint32_t bytes, const char *loc, const char *extra)
{
    bench_emit_cyc(suite, name, st.cyc_min, st.cyc_med, units, unit, bytes, loc, extra);
}

bool bench_run_cases(const char *suite, const bench_case_t *c, int count, const char *loc)
{
    for (int i = 0; i < count; i++) {
        bench_stat_t s = bench_measure(c[i].fn, c[i].n);
        bench_emit(suite, c[i].name, s, c[i].units, c[i].unit, c[i].bytes, loc, NULL);
        sys_service();
        if (abort_requested()) {
            return false;
        }
    }
    return true;
}

/* ---------------------------------------------------------------- notes */

#define MAX_NOTES 48
static struct { const char *key; uint32_t v; } s_notes[MAX_NOTES];
static int s_nnotes;

void bench_notes_reset(void) { s_nnotes = 0; }

static int note_find(const char *key)
{
    for (int i = 0; i < s_nnotes; i++) {
        if (strcmp(s_notes[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

void bench_note(const char *key, uint32_t value)
{
    int i = note_find(key);
    if (i < 0 && s_nnotes < MAX_NOTES) {
        i = s_nnotes++;
        s_notes[i].key = key;
    }
    if (i >= 0) {
        s_notes[i].v = value;
    }
}

bool bench_note_get(const char *key, uint32_t *value)
{
    int i = note_find(key);
    if (i < 0) {
        return false;
    }
    *value = s_notes[i].v;
    return true;
}

void bench_note_min(const char *key, uint32_t value)
{
    uint32_t o;
    if (!bench_note_get(key, &o) || value < o) {
        bench_note(key, value);
    }
}

void bench_note_max(const char *key, uint32_t value)
{
    uint32_t o;
    if (!bench_note_get(key, &o) || value > o) {
        bench_note(key, value);
    }
}
