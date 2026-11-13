/* bench.flash (ART / wait states / RAM execution), bench.irq (NVIC latency), bench.sys
   (CDC, WFI, boot time, ADC, CRC unit, entropy) and the `usbbench` command. */

#include "bench.h"
#include "board.h"
#include "clock.h"
#include "cmd.h"
#include "crc.h"
#include "log.h"
#include "noinit.h"
#include "timebase.h"
#include "usb.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile uint32_t g_sink;

/* ================================================================ bench.flash */

#define REP4(s)   s s s s
#define REP16(s)  REP4(REP4(s))
#define REP64(s)  REP4(REP16(s))
#define REP200(s) REP64(s) REP64(s) REP64(s) REP4(REP4(REP4(s))) REP4(REP4(s)) REP4(s)

/* data: words of the image itself (flash), so the D-cache / flash wait states matter */
#define TBL ((const uint32_t *)(FLASH_BASE + 0x1000u))

static inline __attribute__((always_inline)) uint32_t body_small(uint32_t n)
{
    const uint32_t *t = TBL;
    uint32_t x = 1, acc = 0;
    while (n--) {
        x = x * 1664525u + 1013904223u;
        acc += t[x >> 24];
        if (x & 1u) {
            acc ^= x;
        } else {
            acc += x >> 3;
        }
        OPAQUE(acc);
    }
    return acc;
}

/* ~200 straight-line blocks (~3 KB of code): exceeds the 1 KB instruction cache */
static inline __attribute__((always_inline)) uint32_t body_big(uint32_t n)
{
    const uint32_t *t = TBL;
    uint32_t x = 1, acc = 0;
    while (n--) {
        REP200(x = x * 1664525u + t[(x >> 20) & 255u]; OPAQUE(x); acc += x;)
    }
    return acc ^ x;
}

#define RAMF __attribute__((section(".RamFunc"), noinline))
#define FLASHF __attribute__((noinline))
FLASHF static void k_small_flash(uint32_t n) { g_sink = body_small(n); }
FLASHF static void k_big_flash(uint32_t n) { g_sink = body_big(n); }
RAMF static void k_small_ram(uint32_t n) { g_sink = body_small(n); }
RAMF static void k_big_ram(uint32_t n) { g_sink = body_big(n); }

static bool set_acr(uint32_t ws, uint32_t flags)
{
    /* caches off + reset, then latency, then enables */
    uint32_t cur = FLASH->ACR;
    if (ws > (cur & FLASH_ACR_LATENCY)) {
        FLASH->ACR = (cur & ~FLASH_ACR_LATENCY) | ws;
    }
    FLASH->ACR &= ~(FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_PRFTEN);
    FLASH->ACR |= FLASH_ACR_ICRST | FLASH_ACR_DCRST;
    FLASH->ACR &= ~(FLASH_ACR_ICRST | FLASH_ACR_DCRST);
    FLASH->ACR = ws | flags;
    return (FLASH->ACR & FLASH_ACR_LATENCY) == ws;
}

suite_verdict_t suite_bench_flash(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const uint32_t saved = FLASH->ACR;
    static const struct { const char *name; uint32_t ws; uint32_t fl; } cfg[] = {
        {"full", 3, FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_PRFTEN},
        {"icache_only", 3, FLASH_ACR_ICEN},
        {"dcache_only", 3, FLASH_ACR_DCEN},
        {"prefetch_only", 3, FLASH_ACR_PRFTEN},
        {"i+p", 3, FLASH_ACR_ICEN | FLASH_ACR_PRFTEN},
        {"art_off", 3, 0},
        {"full", 4, FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_PRFTEN},
        {"art_off", 4, 0},
        {"full", 5, FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_PRFTEN},
        {"art_off", 5, 0},
        {"full", 7, FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_PRFTEN},
        {"art_off", 7, 0},
    };
    static const struct { const char *name; bench_fn_t fn; uint32_t n; uint32_t units; const char *loc; } k[] = {
        {"small_loop", k_small_flash, 256, 256, "flash"},
        {"big_code", k_big_flash, 4, 4 * 200, "flash"},
        {"small_loop", k_small_ram, 256, 256, "ram"},
        {"big_code", k_big_ram, 4, 4 * 200, "ram"},
    };
    suite_verdict_t v = SUITE_PASS;
    for (unsigned c = 0; c < sizeof(cfg) / sizeof(cfg[0]); c++) {
        if (!set_acr(cfg[c].ws, cfg[c].fl)) {
            res_printf("{\"t\":\"test\",\"suite\":\"bench.flash\",\"name\":\"acr_%u\",\"pass\":false}", c);
            v = SUITE_FAIL;
            continue;
        }
        for (unsigned i = 0; i < 4; i++) {
            bench_stat_t s = bench_measure(k[i].fn, k[i].n);
            char nm[48];
            snprintf(nm, sizeof(nm), "%s_%s_ws%u_%s", k[i].name, k[i].loc, (unsigned)cfg[c].ws, cfg[c].name);
            bench_emit("bench.flash", nm, s, k[i].units, k[i].units == k[i].n ? "iter" : "block", 0, k[i].loc, NULL);
            if (abort_requested()) {
                set_acr(saved & FLASH_ACR_LATENCY, saved & ~FLASH_ACR_LATENCY);
                return SUITE_ABORT;
            }
        }
    }
    FLASH->ACR = saved;
    if ((FLASH->ACR & FLASH_ACR_LATENCY) != (saved & FLASH_ACR_LATENCY)) {
        v = SUITE_FAIL;
    }
    set_acr(saved & FLASH_ACR_LATENCY, saved & (FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_PRFTEN));
    return v;
}

/* ================================================================ bench.irq */

static volatile uint32_t s_t1, s_t2;
static volatile uint32_t s_irq_mode; /* 0 = entry, 1 = tail-chain source */

void SPI5_IRQHandler(void)
{
    uint32_t t = DWT->CYCCNT;
    s_t1 = t;
    if (s_irq_mode == 1u) {
        NVIC->STIR = SPI4_IRQn;
        __DSB();
    }
}

void SPI4_IRQHandler(void) { s_t2 = DWT->CYCCNT; }

void EXTI1_IRQHandler(void)
{
    s_t1 = DWT->CYCCNT;
    EXTI->PR = EXTI_PR_PR1;
}

static void sort_u32(uint32_t *v, int n)
{
    for (int i = 1; i < n; i++) {
        uint32_t k = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > k) {
            v[j + 1] = v[j];
            j--;
        }
        v[j + 1] = k;
    }
}

#define NIRQ 33

static void emit_samples(const char *suite, const char *name, uint32_t *v, int n, const char *extra)
{
    sort_u32(v, n);
    bench_emit_cyc(suite, name, v[0], v[n / 2], 0, NULL, 0, "flash", extra);
}

static void irq_entry_run(const char *name, uint32_t irq_mode)
{
    uint32_t v[NIRQ];
    s_irq_mode = irq_mode;
    for (int i = 0; i < NIRQ + 1; i++) {
        __DSB();
        __ISB();
        uint32_t t0 = DWT->CYCCNT;
        NVIC->STIR = SPI5_IRQn;
        __DSB();
        __ISB();
        MEMBAR();
        uint32_t d = (irq_mode == 1u) ? (s_t2 - s_t1) : (s_t1 - t0);
        if (i > 0) {
            v[i - 1] = d;
        }
        sys_service();
    }
    emit_samples("bench.irq", name, v, NIRQ, NULL);
}

suite_verdict_t suite_bench_irq(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    NVIC_SetPriority(SPI5_IRQn, 0);
    NVIC_SetPriority(SPI4_IRQn, 0);
    NVIC_SetPriority(EXTI1_IRQn, 0);
    NVIC_EnableIRQ(SPI5_IRQn);
    NVIC_EnableIRQ(SPI4_IRQn);
    const uint32_t ctrl0 = __get_CONTROL();
    const uint32_t fpccr0 = FPU->FPCCR;

    __set_CONTROL(__get_CONTROL() & ~4u); /* FPCA = 0: basic frame */
    __ISB();
    irq_entry_run("stir_entry_fpca0", 0);

    __asm volatile("vmov s0, s0"); /* FPCA = 1: extended frame, lazy stacking (LSPEN=1) */
    irq_entry_run("stir_entry_fpca1_lazy", 0);

    FPU->FPCCR &= ~FPU_FPCCR_LSPEN_Msk; /* eager FP context save (S0-S15, FPSCR) */
    __asm volatile("vmov s0, s0");
    irq_entry_run("stir_entry_fpca1_eager", 0);
    FPU->FPCCR = fpccr0;

    __set_CONTROL(__get_CONTROL() & ~4u);
    __ISB();
    irq_entry_run("stir_tailchain_fpca0", 1); /* SPI5 handler -> pends SPI4; delta handler-to-handler incl. exit/tail-chain */

    /* EXTI SWIER -> handler */
    uint32_t v[NIRQ];
    NVIC_EnableIRQ(EXTI1_IRQn);
    EXTI->IMR |= EXTI_IMR_MR1;
    for (int i = 0; i < NIRQ + 1; i++) {
        s_t1 = 0;
        uint32_t t0 = DWT->CYCCNT;
        EXTI->SWIER = EXTI_SWIER_SWIER1;
        __DSB();
        __ISB();
        for (uint32_t w = 0; w < 1000u && s_t1 == 0u; w++) {
        }
        MEMBAR();
        if (i > 0) {
            v[i - 1] = s_t1 - t0;
        }
        sys_service();
    }
    EXTI->IMR &= ~EXTI_IMR_MR1;
    NVIC_DisableIRQ(EXTI1_IRQn);
    emit_samples("bench.irq", "exti_swier_entry", v, NIRQ, NULL);
    NVIC_DisableIRQ(SPI5_IRQn);
    NVIC_DisableIRQ(SPI4_IRQn);
    __set_CONTROL(ctrl0);
    FPU->FPCCR = fpccr0;
    return SUITE_PASS;
}

/* ================================================================ USB / bench.sys */

/* Sends a @B frame of `bytes` (pattern, multiple of 256) and times the full drain. */
static bool usb_tx_frame(uint32_t bytes, uint32_t *us)
{
    static uint8_t blk[256];
    for (int i = 0; i < 256; i++) blk[i] = (uint8_t)i;
    uint32_t crc = 0;
    for (uint32_t i = 0; i < bytes / 256u; i++) {
        crc = crc32_zlib(crc, blk, 256);
        if ((i & 63u) == 0u) sys_service();
    }
    char hdr[40];
    int n = snprintf(hdr, sizeof(hdr), "@B %" PRIu32 " %08" PRIX32 "\r\n", bytes, crc);
    log_flush(500);
    uint32_t t0 = time_us();
    bool ok = log_write_blocking(hdr, (size_t)n, 2000u);
    static uint8_t chunk[2048];
    for (int i = 0; i < 8; i++) memcpy(&chunk[i * 256], blk, 256);
    for (uint32_t sent = 0; ok && sent < bytes; sent += sizeof(chunk)) {
        uint32_t k = (bytes - sent < sizeof(chunk)) ? bytes - sent : (uint32_t)sizeof(chunk);
        ok = log_write_blocking(chunk, k, 2000u);
    }
    log_flush(3000);
    *us = time_us() - t0;
    return ok;
}

void usbbench_cmd(int argc, char **argv)
{
    uint32_t bytes = (argc >= 3) ? (uint32_t)strtoul(argv[2], NULL, 0) : 0u;
    if (argc < 3 || bytes < 256u || bytes > (1u << 20) || (bytes % 256u)) {
        proto_err("usbbench", "usage: usbbench rx|tx <bytes multiple of 256, <= 1 MB>");
        return;
    }
    if (strcmp(argv[1], "tx") == 0) {
        busy_begin("usbbench");
        uint32_t us = 0;
        bool ok = usb_tx_frame(bytes, &us);
        busy_end();
        res_printf("{\"t\":\"usbbench\",\"dir\":\"tx\",\"bytes\":%" PRIu32 ",\"us\":%" PRIu32 ",\"kBps\":%" PRIu32
                   ",\"ok\":%s}",
                   bytes, us, us ? (uint32_t)(((uint64_t)bytes * 1000u) / us) : 0u, ok ? "true" : "false");
        proto_ok("usbbench tx");
    } else if (strcmp(argv[1], "rx") == 0) {
        busy_begin("usbbench");
        g_usb_rx_bytes = 0;
        g_usb_rx_sink = true;
        res_printf("{\"t\":\"usbbench\",\"dir\":\"rx\",\"state\":\"ready\",\"bytes\":%" PRIu32 "}", bytes);
        log_flush(200);
        uint32_t t_first = 0, t_last = 0, c_first = 0, last_cnt = 0, t_prog = HAL_GetTick();
        for (;;) {
            uint32_t c = g_usb_rx_bytes;
            if (c && !t_first) {
                t_first = time_us();
                c_first = c;
            }
            if (c >= bytes) {
                t_last = time_us();
                break;
            }
            if (c != last_cnt) {
                last_cnt = c;
                t_prog = HAL_GetTick();
            } else if (HAL_GetTick() - t_prog > (t_first ? 1500u : 8000u)) {
                break;
            }
            log_poll();
        }
        g_usb_rx_sink = false;
        busy_end();
        uint32_t got = g_usb_rx_bytes;
        uint32_t us = t_last - t_first;
        uint32_t b = (got >= bytes) ? bytes - c_first : 0u;
        res_printf("{\"t\":\"usbbench\",\"dir\":\"rx\",\"bytes\":%" PRIu32 ",\"got\":%" PRIu32 ",\"us\":%" PRIu32
                   ",\"kBps\":%" PRIu32 ",\"ok\":%s}",
                   bytes, got, us, (us && b) ? (uint32_t)(((uint64_t)b * 1000u) / us) : 0u,
                   got >= bytes ? "true" : "false");
        proto_ok("usbbench rx");
    } else {
        proto_err("usbbench", "usage: usbbench rx|tx <bytes>");
    }
}

/* ---- WFI wake latency (TIM5 compare at the CPU clock) ---- */

static volatile uint32_t s_wake_cyc;
static volatile bool s_wake_flag;

void TIM5_IRQHandler(void)
{
    uint32_t now = TIM5->CNT;
    s_wake_cyc = now - TIM5->CCR1;
    TIM5->DIER = 0;
    TIM5->SR = 0;
    s_wake_flag = true;
}

static void wake_run(const char *name, bool wfi)
{
    uint32_t v[NIRQ];
    for (int i = 0; i < NIRQ + 1; i++) {
        s_wake_flag = false;
        TIM5->SR = 0;
        TIM5->CCR1 = TIM5->CNT + 19200u; /* 200 us at 96 MHz */
        TIM5->DIER = TIM_DIER_CC1IE;
        while (!s_wake_flag) {
            if (wfi) {
                __WFI();
            }
        }
        if (i > 0) v[i - 1] = s_wake_cyc;
    }
    emit_samples("bench.sys", name, v, NIRQ, NULL);
}

/* ---- ADC ---- */

static uint32_t adc_convert_cycles(uint32_t smp, uint32_t *val)
{
    ADC1->SMPR1 = (smp & 7u) << 21; /* channel 17 */
    ADC1->SQR3 = 17;
    uint32_t t0 = DWT->CYCCNT;
    ADC1->CR2 |= ADC_CR2_SWSTART;
    uint32_t n = 0;
    while (!(ADC1->SR & ADC_SR_EOC) && ++n < 100000u) {
    }
    uint32_t t1 = DWT->CYCCNT;
    *val = ADC1->DR;
    return t1 - t0;
}

static uint32_t isqrt(uint32_t x)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > x) b >>= 2;
    while (b) {
        if (x >= r + b) {
            x -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}

suite_verdict_t suite_bench_sys(int argc, char **argv)
{
    static const uint32_t smp_cyc[8] = {3, 15, 28, 56, 84, 112, 144, 480};
    char extra[160];
    uint32_t us;

    /* 1. CDC TX */
    if (usb_configured() && usb_dtr()) {
        bool ok = usb_tx_frame(128u * 1024u, &us);
        snprintf(extra, sizeof(extra), ",\"bytes\":131072,\"kBps\":%" PRIu32 ",\"ok\":%s",
                 us ? (uint32_t)((131072ull * 1000u) / us) : 0u, ok ? "true" : "false");
        bench_emit_cyc("bench.sys", "cdc_tx", us * (SystemCoreClock / 1000000u), 0, 0, NULL, 131072, "flash", extra);
        bench_note("usb.tx_kBps", us ? (uint32_t)((131072ull * 1000u) / us) : 0u);
    } else {
        res_printf("{\"t\":\"bench\",\"suite\":\"bench.sys\",\"name\":\"cdc_tx\",\"skipped\":\"no host terminal (DTR low)\"}");
    }
    res_printf("{\"t\":\"bench\",\"suite\":\"bench.sys\",\"name\":\"cdc_rx\",\"note\":\"host driven: hwtest.py usbbench\"}");

    /* 2. WFI wake latency */
    RCC->APB1ENR |= RCC_APB1ENR_TIM5EN;
    (void)RCC->APB1ENR;
    TIM5->CR1 = 0;
    TIM5->PSC = 0; /* APB1 timer clock = 96 MHz = CPU clock */
    TIM5->ARR = 0xFFFFFFFFu;
    TIM5->EGR = TIM_EGR_UG;
    TIM5->CR1 = TIM_CR1_CEN;
    NVIC_SetPriority(TIM5_IRQn, 0);
    NVIC_EnableIRQ(TIM5_IRQn);
    wake_run("wake_busy_spin", false);
    wake_run("wake_wfi", true);
    NVIC_DisableIRQ(TIM5_IRQn);

    /* 3. boot to first pixel (HAL tick at the first frame, from .noinit) */
    res_printf("{\"t\":\"bench\",\"suite\":\"bench.sys\",\"name\":\"boot_first_pixel\",\"ms\":%" PRIu32
               ",\"note\":\"HAL tick since HAL_Init() incl. LCD init (~0.6 s); excludes reset/DFU-exit before main\"}",
               g_noinit.boot_first_pixel_ms);
    bench_note("sys.boot_ms", g_noinit.boot_first_pixel_ms);

    /* 4. ADC conversion time (Vrefint, ADCCLK = PCLK2/4) */
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    (void)RCC->APB2ENR;
    ADC->CCR = (1u << ADC_CCR_ADCPRE_Pos) | ADC_CCR_TSVREFE;
    ADC1->CR2 = ADC_CR2_ADON;
    delay_us(20);
    uint32_t adc_hz = clock_pclk2_hz() / 4u;
    for (int s = 0; s < 8; s++) {
        uint32_t best = 0xFFFFFFFFu, val = 0;
        for (int i = 0; i < 16; i++) {
            uint32_t c = adc_convert_cycles((uint32_t)s, &val);
            if (c < best) best = c;
        }
        char nm[24];
        snprintf(nm, sizeof(nm), "adc_conv_smp%u", (unsigned)smp_cyc[s]);
        uint32_t theory_ns = (uint32_t)(((uint64_t)(smp_cyc[s] + 12u) * 1000000000ull) / adc_hz);
        snprintf(extra, sizeof(extra), ",\"adc_hz\":%" PRIu32 ",\"raw\":%" PRIu32 ",\"theory_ns\":%" PRIu32,
                 adc_hz, val, theory_ns);
        bench_emit_cyc("bench.sys", nm, best, best, 0, NULL, 0, "flash", extra);
        if (s == 0) bench_note("sys.adc_conv_ns_min", (uint32_t)(((uint64_t)best * 1000u) / (SystemCoreClock / 1000000u)));
    }

    /* 5. entropy: ADC LSB (Vrefint, 3-cycle sampling) */
    {
        uint32_t ones = 0, same = 0, prev = 2, val;
        const uint32_t N = 4096;
        for (uint32_t i = 0; i < N; i++) {
            adc_convert_cycles(0, &val);
            uint32_t b = val & 1u;
            ones += b;
            if (b == prev) same++;
            prev = b;
        }
        snprintf(extra, sizeof(extra), ",\"bits\":%" PRIu32 ",\"ones\":%" PRIu32 ",\"same_as_prev\":%" PRIu32, N, ones, same);
        res_printf("{\"t\":\"bench\",\"suite\":\"bench.sys\",\"name\":\"entropy_adc_lsb\"%s}", extra);
    }
    ADC1->CR2 = 0;
    ADC->CCR = 0;
    RCC->APB2ENR &= ~RCC_APB2ENR_ADC1EN;

    /* 6. entropy / jitter: LSI (RC oscillator) period measured with the CPU clock via TIM5_CH4 */
    {
        RCC->CSR |= RCC_CSR_LSION;
        uint32_t n = 0;
        while (!(RCC->CSR & RCC_CSR_LSIRDY) && ++n < 2000000u) {
        }
        TIM5->CR1 = 0;
        TIM5->DIER = 0;
        TIM5->OR = (1u << 6); /* TI4_RMP = 01: LSI -> TI4 */
        TIM5->CCMR2 = (1u << 8); /* CC4S = 01 */
        TIM5->CCER = TIM_CCER_CC4E;
        TIM5->EGR = TIM_EGR_UG;
        TIM5->CR1 = TIM_CR1_CEN;
        enum { NC = 512 };
        uint32_t *cap = (uint32_t *)(void *)g_arena;
        int got = 0;
        uint32_t t0 = HAL_GetTick();
        TIM5->SR = 0;
        while (got < NC + 1 && (HAL_GetTick() - t0) < 200u) {
            if (TIM5->SR & TIM_SR_CC4IF) {
                cap[got++] = TIM5->CCR4;
            }
        }
        uint64_t sum = 0;
        uint32_t lsb = 0;
        for (int i = 1; i < got; i++) {
            uint32_t d = cap[i] - cap[i - 1];
            sum += d;
            lsb += d & 1u;
        }
        uint32_t mean = got > 1 ? (uint32_t)(sum / (uint32_t)(got - 1)) : 0u;
        uint64_t var = 0;
        for (int i = 1; i < got; i++) {
            int32_t e = (int32_t)(cap[i] - cap[i - 1]) - (int32_t)mean;
            var += (uint64_t)((int64_t)e * e);
        }
        uint32_t sd_x100 = got > 2 ? isqrt((uint32_t)(var * 10000u / (uint32_t)(got - 1))) : 0u;
        char sd[16];
        snprintf(extra, sizeof(extra), ",\"captures\":%d,\"lsi_hz\":%" PRIu32 ",\"mean_cyc\":%" PRIu32
                 ",\"sd_cyc\":%s,\"lsb_ones\":%" PRIu32,
                 got - 1, mean ? (uint32_t)(SystemCoreClock / mean) : 0u, mean, fmt2(sd, sizeof(sd), sd_x100), lsb);
        res_printf("{\"t\":\"bench\",\"suite\":\"bench.sys\",\"name\":\"entropy_lsi_jitter\"%s}", extra);
        TIM5->CR1 = 0;
        TIM5->CCER = 0;
        TIM5->OR = 0;
        RCC->APB1ENR &= ~RCC_APB1ENR_TIM5EN;
        RCC->CSR &= ~RCC_CSR_LSION;
    }

    /* 7. CRC unit and software CRC32 */
    {
        const uint32_t *w = (const uint32_t *)(void *)g_arena;
        uint32_t t0 = DWT->CYCCNT;
        uint32_t c = crc_hw_words(w, 4096); /* 16 KB */
        uint32_t cyc = DWT->CYCCNT - t0;
        g_sink = c;
        bench_emit_cyc("bench.sys", "crc_hw_16k", cyc, cyc, 4096, "word", 16384, "flash", NULL);
        t0 = DWT->CYCCNT;
        c = crc32_zlib(0, g_arena, 1024);
        cyc = DWT->CYCCNT - t0;
        g_sink = c;
        bench_emit_cyc("bench.sys", "crc32_sw_1k", cyc, cyc, 1024, "byte", 1024, "flash", NULL);
    }

    /* 8. housekeeping: SysTick tick (HAL_IncTick + input scan hook) */
    {
        uint32_t best = 0xFFFFFFFFu;
        for (int r = 0; r < 8; r++) {
            uint32_t pm = __get_PRIMASK();
            __disable_irq();
            uint32_t t0 = DWT->CYCCNT;
            HAL_IncTick();
            timebase_systick_hook();
            uint32_t d = DWT->CYCCNT - t0;
            if (!pm) __enable_irq();
            if (d < best) best = d;
        }
        bench_emit_cyc("bench.sys", "systick_body", best, best, 0, NULL, 0, "flash", NULL);
        bench_note("sys.systick_cyc", best + 30u); /* + exception entry/exit */
    }

    if (suite_arg_u32(argc, argv, "stop", 0) != 0u) {
        res_printf("{\"t\":\"bench\",\"suite\":\"bench.sys\",\"name\":\"stop_mode\",\"skipped\":\"not implemented: STOP "
                   "would drop USB and needs a clock re-init; measure with SWD attached\"}");
    }
    return SUITE_PASS;
}
