/* bench.mem: memcpy / memset implementations vs DMA2 stream 7 mem2mem. */

#include "bench.h"

/* the byte/word loops must stay loops (GCC would turn them into memcpy/memset calls) */
#pragma GCC optimize("no-tree-loop-distribute-patterns")
#include "cmd.h"
#include "log.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------- DMA2 S7 mem2mem */

#define S7_ALL (0x3Du << 22) /* HIFCR/HISR bits of stream 7 */
#define S7_TC  (1u << 27)

static volatile uint32_t s_fill_word;

static void m2m(void *dst, const void *src, uint32_t items, uint32_t szcode, bool pinc)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
    (void)RCC->AHB1ENR;
    DMA2_Stream7->CR &= ~DMA_SxCR_EN;
    uint32_t t = 0;
    while ((DMA2_Stream7->CR & DMA_SxCR_EN) && ++t < 100000u) {
    }
    DMA2->HIFCR = S7_ALL;
    DMA2_Stream7->PAR = (uint32_t)src;
    DMA2_Stream7->M0AR = (uint32_t)dst;
    DMA2_Stream7->NDTR = items;
    DMA2_Stream7->FCR = DMA_SxFCR_DMDIS | 3u;
    DMA2_Stream7->CR = (2u << DMA_SxCR_DIR_Pos) | DMA_SxCR_MINC | (pinc ? DMA_SxCR_PINC : 0u) |
                       (szcode << DMA_SxCR_PSIZE_Pos) | (szcode << DMA_SxCR_MSIZE_Pos) | (2u << DMA_SxCR_PL_Pos);
    DMA2_Stream7->CR |= DMA_SxCR_EN;
    t = 0;
    while (!(DMA2->HISR & S7_TC) && ++t < 10000000u) {
    }
}

void dma_m2m_copy(void *dst, const void *src, uint32_t bytes) { m2m(dst, src, bytes / 4u, 2u, true); }
void dma_m2m_copy_b(void *dst, const void *src, uint32_t bytes) { m2m(dst, src, bytes, 0u, true); }
void dma_m2m_fill(void *dst, uint32_t word, uint32_t bytes)
{
    s_fill_word = word;
    m2m(dst, (const void *)&s_fill_word, bytes / 4u, 2u, false);
}

/* ---------------------------------------------------------------- CPU kernels */

static uint8_t *s_src, *s_dst;
static uint32_t s_size;
typedef struct { uint32_t v; } __attribute__((packed, aligned(1))) u32u_t;

static void c_newlib(uint32_t n) { while (n--) { memcpy(s_dst, s_src, s_size); MEMBAR(); } }
static void c_bytes(uint32_t n)
{
    while (n--) {
        const uint8_t *s = s_src;
        uint8_t *d = s_dst;
        for (uint32_t i = 0; i < s_size; i++) { d[i] = s[i]; }
        MEMBAR();
    }
}
static void c_word(uint32_t n)
{
    while (n--) {
        const uint32_t *s = (const uint32_t *)(void *)s_src;
        uint32_t *d = (uint32_t *)(void *)s_dst;
        for (uint32_t i = 0; i < s_size / 4u; i++) { d[i] = s[i]; }
        MEMBAR();
    }
}
static void c_word_unal(uint32_t n)
{
    while (n--) {
        const u32u_t *s = (const u32u_t *)(void *)s_src;
        u32u_t *d = (u32u_t *)(void *)s_dst;
        for (uint32_t i = 0; i < s_size / 4u; i++) { d[i].v = s[i].v; }
        MEMBAR();
    }
}
static void c_ldm(uint32_t n)
{
    while (n--) {
        const uint32_t *s = (const uint32_t *)(void *)s_src;
        uint32_t *d = (uint32_t *)(void *)s_dst;
        uint32_t cnt = s_size / 32u;
        __asm volatile("1: ldmia %0!, {r3-r10}\n"
                       "   stmia %1!, {r3-r10}\n"
                       "   subs  %2, %2, #1\n"
                       "   bne   1b\n"
                       : "+r"(s), "+r"(d), "+r"(cnt)
                       :
                       : "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "memory", "cc");
    }
}
static void c_dma_w(uint32_t n) { while (n--) { dma_m2m_copy(s_dst, s_src, s_size); } }
static void c_dma_b(uint32_t n) { while (n--) { dma_m2m_copy_b(s_dst, s_src, s_size); } }

static void s_newlib(uint32_t n) { while (n--) { memset(s_dst, 0xA5, s_size); MEMBAR(); } }
static void s_bytes(uint32_t n)
{
    while (n--) {
        uint8_t *d = s_dst;
        for (uint32_t i = 0; i < s_size; i++) { d[i] = 0xA5; }
        MEMBAR();
    }
}
static void s_word(uint32_t n)
{
    while (n--) {
        uint32_t *d = (uint32_t *)(void *)s_dst;
        for (uint32_t i = 0; i < s_size / 4u; i++) { d[i] = 0xA5A5A5A5u; }
        MEMBAR();
    }
}
static void s_stm(uint32_t n)
{
    while (n--) {
        uint32_t *d = (uint32_t *)(void *)s_dst;
        uint32_t cnt = s_size / 32u;
        register uint32_t r3 __asm("r3") = 0xA5A5A5A5u, r4 __asm("r4") = r3, r5 __asm("r5") = r3,
                          r6 __asm("r6") = r3, r7 __asm("r7") = r3, r8 __asm("r8") = r3,
                          r9 __asm("r9") = r3, r10 __asm("r10") = r3;
        __asm volatile("1: stmia %0!, {r3-r10}\n"
                       "   subs  %1, %1, #1\n"
                       "   bne   1b\n"
                       : "+r"(d), "+r"(cnt)
                       : "r"(r3), "r"(r4), "r"(r5), "r"(r6), "r"(r7), "r"(r8), "r"(r9), "r"(r10)
                       : "memory", "cc");
    }
}
static void s_dma(uint32_t n) { while (n--) { dma_m2m_fill(s_dst, 0xA5A5A5A5u, s_size); } }

typedef struct {
    const char *name;
    bench_fn_t fn;
    bool aligned_only;
} impl_t;

static uint32_t run_one(const char *prefix, const impl_t *im, uint32_t size, bool aligned, uint32_t soff,
                        uint32_t doff, bool emit)
{
    s_src = g_arena + (aligned ? 0u : soff);
    s_dst = g_arena + 32768u + (aligned ? 0u : doff);
    s_size = size;
    uint32_t reps = 16384u / size;
    if (reps < 1u) reps = 1u;
    if (reps > 64u) reps = 64u;
    bench_stat_t st = bench_measure(im->fn, reps);
    if (emit) {
        char nm[48];
        snprintf(nm, sizeof(nm), "%s_%s_%" PRIu32 "_%s", prefix, im->name, size, aligned ? "al" : "unal");
        bench_emit("bench.mem", nm, st, reps, "copy", size * reps, "flash", NULL);
    }
    return st.cyc_min / reps;
}

suite_verdict_t suite_bench_mem(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    for (uint32_t i = 0; i < ARENA_BYTES; i++) {
        g_arena[i] = (uint8_t)(i * 7u);
    }
    static const impl_t cp[] = {
        {"newlib", c_newlib, false}, {"bytes", c_bytes, false}, {"word", c_word, true},
        {"wordunal", c_word_unal, false}, {"ldmstm", c_ldm, true}, {"dma32", c_dma_w, true}, {"dma8", c_dma_b, false},
    };
    static const impl_t ms[] = {
        {"newlib", s_newlib, false}, {"bytes", s_bytes, false}, {"word", s_word, true},
        {"stm", s_stm, true}, {"dma32", s_dma, true},
    };
    static const uint32_t sizes[] = {64, 1024, 16384};
    for (int a = 1; a >= 0; a--) {
        bool al = a != 0;
        for (unsigned si = 0; si < 3; si++) {
            for (unsigned k = 0; k < sizeof(cp) / sizeof(cp[0]); k++) {
                if (!al && (cp[k].aligned_only || (cp[k].fn == c_dma_w))) continue;
                if (al && cp[k].fn == c_dma_b) continue;
                run_one("memcpy", &cp[k], sizes[si], al, 1, 2, true);
                sys_service();
                if (abort_requested()) return SUITE_ABORT;
            }
            for (unsigned k = 0; k < sizeof(ms) / sizeof(ms[0]); k++) {
                if (!al && (ms[k].aligned_only)) continue;
                run_one("memset", &ms[k], sizes[si], al, 1, 2, true);
                sys_service();
                if (abort_requested()) return SUITE_ABORT;
            }
        }
    }
    /* crossover: smallest size (aligned) at which DMA beats the best CPU method */
    for (int pass = 0; pass < 2; pass++) {
        const char *what = pass ? "memset" : "memcpy";
        uint32_t cross = 0, cross_nl = 0, nl = 0;
        uint32_t mb_cpu = 0, mb_dma = 0, mbs_size = 16384;
        for (uint32_t size = 16; size <= 16384u; size <<= 1) {
            uint32_t best = 0xFFFFFFFFu, dma;
            if (!pass) {
                uint32_t a = run_one("x", &cp[0], size, true, 0, 0, false);
                uint32_t b = run_one("x", &cp[2], size, true, 0, 0, false);
                uint32_t c = (size >= 32u) ? run_one("x", &cp[4], size, true, 0, 0, false) : 0xFFFFFFFFu;
                nl = a;
                best = a < b ? a : b;
                best = best < c ? best : c;
                dma = run_one("x", &cp[5], size, true, 0, 0, false);
            } else {
                uint32_t a = run_one("x", &ms[0], size, true, 0, 0, false);
                uint32_t b = run_one("x", &ms[2], size, true, 0, 0, false);
                uint32_t c = (size >= 32u) ? run_one("x", &ms[3], size, true, 0, 0, false) : 0xFFFFFFFFu;
                nl = a;
                best = a < b ? a : b;
                best = best < c ? best : c;
                dma = run_one("x", &ms[4], size, true, 0, 0, false);
            }
            if (cross == 0u && dma < best) cross = size;
            if (cross_nl == 0u && dma < nl) cross_nl = size;
            if (size == mbs_size) {
                mb_cpu = best;
                mb_dma = dma;
            }
            sys_service();
            if (abort_requested()) return SUITE_ABORT;
        }
        res_printf("{\"t\":\"bench\",\"suite\":\"bench.mem\",\"name\":\"%s_dma_crossover\",\"crossover_bytes\":%" PRIu32
                   ",\"crossover_vs_newlib_bytes\":%" PRIu32 ",\"cyc16k_best_cpu\":%" PRIu32 ",\"cyc16k_dma\":%" PRIu32 ",\"clk\":%" PRIu32 "}",
                   what, cross, cross_nl, mb_cpu, mb_dma, SystemCoreClock);
        if (!pass) bench_note("mem.copy_cross", cross);
        else bench_note("mem.set_cross", cross);
        if (!pass) {
            uint32_t mbps = mb_dma ? (uint32_t)(((uint64_t)16384u * (SystemCoreClock / 1000u)) / mb_dma / 1000u) : 0u;
            bench_note("mem.dma_copy_mbps", mbps);
        }
    }
    return SUITE_PASS;
}
