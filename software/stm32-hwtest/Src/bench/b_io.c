/* bench.w25q (read-only), bench.audio (software mixer cost), bench.all + budget. */

#include "bench.h"
#include "cmd.h"
#include "lcd.h"
#include "log.h"
#include "timebase.h"
#include "w25q.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static volatile uint32_t g_sink;

/* ================================================================ bench.w25q */

static uint32_t s_wa, s_wlen;
static bool s_wfast, s_wdma;

static void k_w25q_read(uint32_t n)
{
    while (n--) {
        if (s_wdma) {
            g_sink = (uint32_t)w25q_read_dma(s_wa, g_arena, s_wlen, s_wfast);
        } else {
            g_sink = (uint32_t)w25q_read_ex(s_wa, g_arena, s_wlen, s_wfast);
        }
    }
}

static uint32_t s_lfsr = 0xACE1u;
static uint32_t next_rand(void)
{
    s_lfsr = s_lfsr * 1664525u + 1013904223u;
    return s_lfsr;
}

static void k_w25q_random(uint32_t n)
{
    while (n--) {
        uint32_t a = (next_rand() >> 8) & 0x7FFF00u; /* anywhere in the first 8 MB, 256-aligned */
        if (s_wdma) {
            g_sink = (uint32_t)w25q_read_dma(a, g_arena, s_wlen, true);
        } else {
            g_sink = (uint32_t)w25q_read_ex(a, g_arena, s_wlen, true);
        }
    }
}

static uint32_t s_conc_w, s_conc_l;
static void k_concurrent(uint32_t n)
{
    (void)n;
    /* LCD frame (DMA2 S3, from the upper arena) + 32 KB W25Q read (DMA1 S0/S5, into the lower arena) */
    lcd_stream_window(0, 0, 159, 127);
    spi1_dff(true);
    uint32_t t0 = DWT->CYCCNT;
    w25q_dma_start(0x100000u, g_arena, 32768u, true);
    dma3_start(g_arena + 40960, 20480, true, true, false);
    bool dw = false, dl = false;
    uint32_t spin = 0;
    while ((!dw || !dl) && ++spin < 20000000u) {
        if (!dw && w25q_dma_done()) {
            s_conc_w = DWT->CYCCNT - t0;
            dw = true;
        }
        if (!dl && (DMA2->LISR & (1u << 27))) {
            s_conc_l = DWT->CYCCNT - t0;
            dl = true;
        }
    }
    w25q_dma_finish();
    dma3_finish();
    lcd_end();
}

suite_verdict_t suite_bench_w25q(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (!w25q_present()) {
        res_printf("{\"t\":\"test\",\"suite\":\"bench.w25q\",\"name\":\"present\",\"pass\":false}");
        return SUITE_FAIL;
    }
    const uint32_t hz0 = w25q_spi_hz();
    static const uint16_t divs[3] = {8, 4, 2};
    uint32_t best_kBps = 0;
    char ex[80], nm[48];
    bench_stat_t s;
    for (int d = 0; d < 3; d++) {
        w25q_set_div(divs[d]);
        uint32_t hz = w25q_spi_hz();
        snprintf(ex, sizeof(ex), ",\"spi_hz\":%" PRIu32, hz);
        for (int dma = 0; dma < 2; dma++) {
            for (int fast = 0; fast < 2; fast++) {
                for (int big = 0; big < 2; big++) {
                    s_wa = 0x100000u;
                    s_wlen = big ? 65536u : 4096u;
                    s_wfast = fast;
                    s_wdma = dma;
                    s = bench_measure_irq(k_w25q_read, 1);
                    snprintf(nm, sizeof(nm), "read_%s_%s_%uk_%" PRIu32 "mhz", dma ? "dma" : "poll",
                             fast ? "0B" : "03", big ? 64u : 4u, hz / 1000000u);
                    bench_emit("bench.w25q", nm, s, 0, NULL, s_wlen, "flash", ex);
                    uint32_t kBps = s.cyc_min ? (uint32_t)(((uint64_t)s_wlen * (SystemCoreClock / 1000u)) / s.cyc_min) : 0u;
                    if (dma && big && kBps > best_kBps) best_kBps = kBps;
                    sys_service();
                    if (abort_requested()) {
                        w25q_set_div(4);
                        return SUITE_ABORT;
                    }
                }
            }
        }
        if (divs[d] <= 4) {
            for (int dma = 0; dma < 2; dma++) {
                for (int k = 0; k < 2; k++) {
                    s_wlen = k ? 256u : 32u;
                    s_wdma = dma;
                    s = bench_measure_irq(k_w25q_random, 32);
                    uint32_t rps = s.cyc_min ? (uint32_t)(((uint64_t)32u * SystemCoreClock) / s.cyc_min) : 0u;
                    snprintf(ex, sizeof(ex), ",\"spi_hz\":%" PRIu32 ",\"reads_per_s\":%" PRIu32, hz, rps);
                    snprintf(nm, sizeof(nm), "random_%uB_%s_%" PRIu32 "mhz", (unsigned)s_wlen, dma ? "dma" : "poll",
                             hz / 1000000u);
                    bench_emit("bench.w25q", nm, s, 32, "read", 0, "flash", ex);
                    sys_service();
                }
            }
        }
    }
    /* concurrent W25Q DMA + LCD DMA (both at 24 MHz) */
    const uint16_t ldiv0 = lcd_cfg.spi_div;
    w25q_set_div(2);
    lcd_set_spi_div(4);
    s_wa = 0x100000u;
    s_wlen = 32768u;
    s_wfast = true;
    s_wdma = true;
    bench_stat_t w_alone = bench_measure_irq(k_w25q_read, 1);
    lcd_stream_window(0, 0, 159, 127);
    spi1_dff(true);
    uint32_t t0 = DWT->CYCCNT;
    dma3_start(g_arena + 40960, 20480, true, true, false);
    dma3_finish();
    uint32_t l_alone = DWT->CYCCNT - t0;
    lcd_end();
    s = bench_measure_irq(k_concurrent, 1);
    snprintf(ex, sizeof(ex), ",\"w25q_alone_us\":%" PRIu32 ",\"lcd_alone_us\":%" PRIu32 ",\"w25q_conc_us\":%" PRIu32
             ",\"lcd_conc_us\":%" PRIu32,
             cyc_to_us(w_alone.cyc_min), cyc_to_us(l_alone), cyc_to_us(s_conc_w), cyc_to_us(s_conc_l));
    bench_emit("bench.w25q", "concurrent_w25q32k_lcdframe_24mhz", s, 0, NULL, 0, "flash", ex);
    uint32_t conc_kBps = s_conc_w ? (uint32_t)(((uint64_t)32768u * (SystemCoreClock / 1000u)) / s_conc_w) : 0u;
    bench_note("w25q.best_kBps", best_kBps);
    bench_note("w25q.conc_kBps", conc_kBps);
    lcd_set_spi_div(ldiv0);
    w25q_set_div((uint16_t)(48000000u / hz0));
    return SUITE_PASS;
}

/* ================================================================ bench.audio */

typedef struct {
    uint8_t type;   /* 0 pcm8, 1 pcm16, 2 square, 3 sine, 4 noise */
    uint8_t vol;    /* 0..255 */
    uint32_t pos;   /* 16.16 (pcm) or 32-bit phase (synth) */
    uint32_t step;
} achan_t;

#define s_pcm8 ((int8_t *)(void *)(g_arena + 4096))
#define s_pcm16 ((int16_t *)(void *)(g_arena + 8192))
static int16_t s_sin[256];
static achan_t s_ch[8];
static uint32_t s_nch;
static int16_t *s_aout;
static uint32_t s_noise = 1;

static void mix_block(uint32_t n)
{
    int16_t *out = s_aout;
    const uint32_t nch = s_nch;
    while (n--) {
        int32_t acc = 0;
        for (uint32_t c = 0; c < nch; c++) {
            achan_t *ch = &s_ch[c];
            int32_t s;
            switch (ch->type) {
            case 0: s = (int32_t)s_pcm8[(ch->pos >> 16) & 4095u] << 8; break;
            case 1: s = s_pcm16[(ch->pos >> 16) & 4095u]; break;
            case 2: s = (ch->pos & 0x80000000u) ? 12000 : -12000; break;
            case 3: s = s_sin[ch->pos >> 24]; break;
            default:
                s_noise ^= s_noise << 13;
                s_noise ^= s_noise >> 17;
                s_noise ^= s_noise << 5;
                s = (int32_t)(int16_t)(s_noise >> 16) >> 1;
                break;
            }
            ch->pos += ch->step;
            acc += (s * (int32_t)ch->vol) >> 8;
        }
        *out++ = (int16_t)__SSAT(acc, 16);
    }
}

suite_verdict_t suite_bench_audio(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    int16_t *obuf = (int16_t *)(void *)g_arena;
    s_aout = obuf;
    uint32_t x = 5;
    for (int i = 0; i < 4096; i++) {
        x = x * 1664525u + 1013904223u;
        s_pcm8[i] = (int8_t)(x >> 24);
        s_pcm16[i] = (int16_t)(x >> 16);
    }
    for (int i = 0; i < 256; i++) {
        s_sin[i] = (int16_t)(12000.0f * (float)(i < 128 ? (i * (256 - 2 * i)) : (-(i - 128) * (256 - 2 * (i - 128)))) / 8192.0f);
    }
    static const char *const tn[3] = {"pcm8", "pcm16", "synth"};
    static const uint32_t rates[4] = {8000, 11025, 16000, 22050};
    uint32_t worst4 = 0;
    for (int t = 0; t < 3; t++) {
        for (uint32_t nch = 1; nch <= 8; nch <<= 1) {
            for (uint32_t c = 0; c < nch; c++) {
                s_ch[c].type = (t == 0) ? 0u : (t == 1) ? 1u : (uint8_t)(2u + (c % 3u));
                s_ch[c].vol = (uint8_t)(255u - 20u * c);
                s_ch[c].pos = c * 12345u;
                s_ch[c].step = (t < 2) ? (0x10000u + c * 777u) : (0x01000000u + c * 0x00123456u);
            }
            s_nch = nch;
            bench_stat_t s = bench_measure(mix_block, 256);
            char ex[160], a[4][12], nm[24];
            uint32_t pct[4];
            for (int r = 0; r < 4; r++) {
                pct[r] = (uint32_t)(((uint64_t)s.cyc_min * rates[r] * 10000u) / (256u * (uint64_t)SystemCoreClock));
                fmt2(a[r], sizeof(a[r]), pct[r]);
            }
            snprintf(ex, sizeof(ex), ",\"cpu8k\":%s,\"cpu11k\":%s,\"cpu16k\":%s,\"cpu22k\":%s", a[0], a[1], a[2], a[3]);
            snprintf(nm, sizeof(nm), "mix_%s_%uch", tn[t], (unsigned)nch);
            bench_emit("bench.audio", nm, s, 256, "sample", 0, "flash", ex);
            if (nch == 4 && pct[3] > worst4) worst4 = pct[3];
            if (nch == 4) {
                bench_note_max("audio.4ch_cyc_sample_x100", (uint32_t)(((uint64_t)s.cyc_min * 100u) / 256u));
            }
            sys_service();
            if (abort_requested()) return SUITE_ABORT;
        }
    }
    bench_note("audio.4ch22k_x100", worst4);
    return SUITE_PASS;
}

/* ================================================================ bench.all + budget */

static uint32_t note(const char *k)
{
    uint32_t v = 0;
    bench_note_get(k, &v);
    return v;
}

static void emit_budget(void)
{
    const uint32_t clk = SystemCoreClock;
    char a[12], b[12], c[12], d[12], e[12];
    uint32_t f30 = clk / 30u, f60 = clk / 60u;
    uint32_t conv = note("gfx.conv_cyc_pipelined");
    uint32_t aud_x100 = note("audio.4ch22k_x100"); /* percent * 100 */
    uint32_t aud30 = (uint32_t)(((uint64_t)f30 * aud_x100) / 10000u);
    uint32_t aud60 = (uint32_t)(((uint64_t)f60 * aud_x100) / 10000u);
    uint32_t hk_ms = note("sys.systick_cyc"); /* cycles per 1 ms tick */
    uint32_t hk30 = hk_ms * 1000u / 30u, hk60 = hk_ms * 1000u / 60u;
    int32_t rem30 = (int32_t)f30 - (int32_t)conv - (int32_t)aud30 - (int32_t)hk30;
    int32_t rem60 = (int32_t)f60 - (int32_t)conv - (int32_t)aud60 - (int32_t)hk60;
    uint32_t kBps = note("w25q.best_kBps");
    res_printf("{\"t\":\"budget\",\"suite\":\"bench.all\",\"clk\":%" PRIu32 ",\"frame_cyc_30\":%" PRIu32
               ",\"frame_cyc_60\":%" PRIu32 ",\"best_spi_frame_dma_us\":%" PRIu32 ",\"present_pipelined_48mhz_us\":%" PRIu32
               ",\"conv_cyc\":%" PRIu32 ",\"conv_pct_30\":%s,\"conv_pct_60\":%s,\"audio_4ch_22k_pct\":%s"
               ",\"audio_cyc_30\":%" PRIu32 ",\"audio_cyc_60\":%" PRIu32 ",\"hk_cyc_per_ms\":%" PRIu32
               ",\"hk_cyc_30\":%" PRIu32 ",\"hk_cyc_60\":%" PRIu32 ",\"rem_cyc_30\":%" PRId32 ",\"rem_cyc_60\":%" PRId32
               ",\"rem_pct_30\":%s,\"rem_pct_60\":%s}",
               clk, f30, f60, note("lcd.best_dma_us"), note("gfx.present48_us"), conv,
               fmt2(a, sizeof(a), (uint32_t)(((uint64_t)conv * 10000u) / f30)),
               fmt2(b, sizeof(b), (uint32_t)(((uint64_t)conv * 10000u) / f60)), fmt2(c, sizeof(c), aud_x100), aud30, aud60,
               hk_ms, hk30, hk60, rem30, rem60,
               fmt2(d, sizeof(d), rem30 > 0 ? (uint32_t)(((uint64_t)rem30 * 10000u) / f30) : 0u),
               fmt2(e, sizeof(e), rem60 > 0 ? (uint32_t)(((uint64_t)rem60 * 10000u) / f60) : 0u));
    res_printf("{\"t\":\"budget\",\"suite\":\"bench.all\",\"part\":2,\"w25q_best_kBps\":%" PRIu32 ",\"w25q_conc_kBps\":%" PRIu32
               ",\"w25q_kb_frame_30\":%" PRIu32 ",\"w25q_kb_frame_60\":%" PRIu32 ",\"free_ram\":%" PRIu32
               ",\"free_ram_excl_arena\":%" PRIu32 ",\"usb_tx_kBps\":%" PRIu32 ",\"boot_ms\":%" PRIu32
               ",\"sprites16_key_per_frame_60\":%" PRIu32 ",\"tilemap_cyc\":%" PRIu32 "}",
               kBps, note("w25q.conc_kBps"), kBps / 30u, kBps / 60u, note("ram.free_incl_arena"),
               note("ram.free_excl_arena"), note("usb.tx_kBps"), note("sys.boot_ms"), note("gfx.spf60_16key"),
               note("gfx.tilemap_cyc"));
}

suite_verdict_t suite_bench_all(int argc, char **argv)
{
    static const struct { const char *n; suite_fn_t f; } seq[] = {
        {"bench.cpu", suite_bench_cpu},   {"bench.mem", suite_bench_mem}, {"bench.flash", suite_bench_flash},
        {"bench.irq", suite_bench_irq},   {"bench.lcd", suite_bench_lcd}, {"bench.gfx", suite_bench_gfx},
        {"bench.w25q", suite_bench_w25q}, {"bench.audio", suite_bench_audio}, {"bench.sys", suite_bench_sys},
    };
    bench_notes_reset();
    suite_verdict_t v = SUITE_PASS;
    for (unsigned i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
        res_printf("{\"t\":\"progress\",\"suite\":\"bench.all\",\"name\":\"%s\",\"step\":%u,\"of\":%u}", seq[i].n, i + 1u,
                   (unsigned)(sizeof(seq) / sizeof(seq[0])));
        suite_verdict_t r = seq[i].f(argc, argv);
        if (r == SUITE_ABORT) return SUITE_ABORT;
        if (r == SUITE_FAIL) v = SUITE_FAIL;
        sys_service();
    }
    emit_budget();
    return v;
}
