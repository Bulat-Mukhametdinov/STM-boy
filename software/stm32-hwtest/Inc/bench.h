#ifndef BENCH_H
#define BENCH_H

/* P5 benchmark harness: DWT CYCCNT, 1 warm-up + 7 reps, min/median.
   Every result is `@R {"t":"bench","suite","name","cyc_min","cyc_med","us","unit",
   "per_unit"[,"mbps"],"clk","ws","art","opt","loc"[,extras]}`. */

#include "suites.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BENCH_REPS 7

/* Defeat dead-code elimination / reassociation: the value is "modified" by an
   empty asm statement, so it must exist in a register at this point. */
#define OPAQUE(x) __asm volatile("" : "+r"(x))
#define MEMBAR()  __asm volatile("" ::: "memory")

typedef void (*bench_fn_t)(uint32_t n);

typedef struct {
    uint32_t cyc_min;
    uint32_t cyc_med;
} bench_stat_t;

/* Runs fn(n) once as warm-up then BENCH_REPS times with IRQs masked per rep
   (kernels must stay below ~20 ms). The empty-call overhead is subtracted. */
bench_stat_t bench_measure(bench_fn_t fn, uint32_t n);
/* Same, but IRQs stay enabled (kernels that need interrupts, e.g. DMA ISRs); min/median reject IRQ noise. */
bench_stat_t bench_measure_irq(bench_fn_t fn, uint32_t n);

/* Emits one bench result. units = how many `unit`s one call of the kernel did
   (0 = none); bytes = payload bytes per call (0 = none -> no mbps). `extra` = pre-formatted
   `,"k":v` fragment or NULL. */
void bench_emit(const char *suite, const char *name, bench_stat_t st, uint32_t units, const char *unit,
                uint32_t bytes, const char *loc, const char *extra);
/* Same with caller-supplied cycles (not from bench_measure). */
void bench_emit_cyc(const char *suite, const char *name, uint32_t cyc_min, uint32_t cyc_med, uint32_t units,
                    const char *unit, uint32_t bytes, const char *loc, const char *extra);

/* "x.yy" formatter: value*100 as integer; returns buf. */
const char *fmt2(char *buf, size_t n, uint32_t x100);
uint32_t cyc_to_us(uint32_t cyc);

/* Table-driven kernels. */
typedef struct {
    const char *name;
    bench_fn_t fn;
    uint32_t n;        /* argument passed to fn */
    uint32_t units;    /* units of work per call */
    const char *unit;
    uint32_t bytes;
} bench_case_t;

/* Runs all cases; returns false if aborted. */
bool bench_run_cases(const char *suite, const bench_case_t *c, int count, const char *loc);

/* Results shared with bench.all / budget. */
void bench_note(const char *key, uint32_t value);
bool bench_note_get(const char *key, uint32_t *value);
void bench_note_min(const char *key, uint32_t value);
void bench_note_max(const char *key, uint32_t value);
void bench_notes_reset(void);

/* DMA2 stream 7 mem2mem helpers (b_mem.c). */
void dma_m2m_copy(void *dst, const void *src, uint32_t bytes);    /* blocking, 32-bit beats, bytes % 4 == 0 */
void dma_m2m_copy_b(void *dst, const void *src, uint32_t bytes);  /* blocking, byte beats */
void dma_m2m_fill(void *dst, uint32_t word, uint32_t bytes);      /* PINC = 0 source */

/* SPI1 / DMA2 S3 helpers (b_lcd.c) */
void spi1_wait_idle(void);
void spi1_dff(bool w16);
void dma3_start(const void *src, uint32_t items, bool w16, bool minc, bool irq);
void dma3_finish(void);

extern uint8_t g_arena[];
#define ARENA_BYTES (72u * 1024u)

/* Suites (registered in suites.c) */
suite_verdict_t suite_bench_cpu(int argc, char **argv);
suite_verdict_t suite_bench_mem(int argc, char **argv);
suite_verdict_t suite_bench_flash(int argc, char **argv);
suite_verdict_t suite_bench_irq(int argc, char **argv);
suite_verdict_t suite_bench_lcd(int argc, char **argv);
suite_verdict_t suite_bench_gfx(int argc, char **argv);
suite_verdict_t suite_bench_w25q(int argc, char **argv);
suite_verdict_t suite_bench_audio(int argc, char **argv);
suite_verdict_t suite_bench_sys(int argc, char **argv);
suite_verdict_t suite_bench_all(int argc, char **argv);

/* P3 suites (Src/ui/t_w25q.c, t_mem.c) */
suite_verdict_t suite_w25q_info(int argc, char **argv);
suite_verdict_t suite_w25q_readcrc(int argc, char **argv);
suite_verdict_t suite_w25q_scratch(int argc, char **argv);
suite_verdict_t suite_w25q_sector(int argc, char **argv);
suite_verdict_t suite_w25q_pd(int argc, char **argv);
suite_verdict_t suite_ram_march(int argc, char **argv);
suite_verdict_t suite_flash_check(int argc, char **argv);

/* USB bench command (cmd.c): usbbench rx|tx <bytes> */
void usbbench_cmd(int argc, char **argv);

#endif
