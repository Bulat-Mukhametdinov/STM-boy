/* bench.lcd (SPI1 frame / fill / rect throughput, polling vs DMA) and bench.gfx
   (8bpp framebuffer + RGB565 palette pipeline, sprites, tilemap, glyphs). */

#include "bench.h"
#include "cmd.h"
#include "font.h"
#include "lcd.h"
#include "log.h"
#include "timebase.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define FW 160u
#define FH 128u
#define FRAME_PX (FW * FH)
#define FRAME_BYTES (FRAME_PX * 2u)

#define S3_ALL (0x3Du << 22)
#define S3_TC  (1u << 27)

typedef struct { uint32_t v; } __attribute__((packed, aligned(1))) u32u_t;

/* ---------------------------------------------------------------- SPI1 helpers */

void spi1_wait_idle(void)
{
    uint32_t n = 0;
    while (!(SPI1->SR & SPI_SR_TXE) && ++n < 400000u) {
    }
    n = 0;
    while ((SPI1->SR & SPI_SR_BSY) && ++n < 400000u) {
    }
}

void spi1_dff(bool w16)
{
    if (((SPI1->CR1 & SPI_CR1_DFF) != 0u) == w16) {
        return;
    }
    spi1_wait_idle();
    SPI1->CR1 &= ~SPI_CR1_SPE;
    if (w16) {
        SPI1->CR1 |= SPI_CR1_DFF;
    } else {
        SPI1->CR1 &= ~SPI_CR1_DFF;
    }
    SPI1->CR1 |= SPI_CR1_SPE;
}

static void dma3_off(void)
{
    DMA2_Stream3->CR &= ~(DMA_SxCR_EN | DMA_SxCR_TCIE);
    uint32_t n = 0;
    while ((DMA2_Stream3->CR & DMA_SxCR_EN) && ++n < 100000u) {
    }
    DMA2->LIFCR = S3_ALL;
}

/* Memory -> SPI1 DR. items = number of 8/16-bit beats. */
void dma3_start(const void *src, uint32_t items, bool w16, bool minc, bool irq)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
    (void)RCC->AHB1ENR;
    dma3_off();
    DMA2_Stream3->PAR = (uint32_t)&SPI1->DR;
    DMA2_Stream3->M0AR = (uint32_t)src;
    DMA2_Stream3->NDTR = items;
    DMA2_Stream3->FCR = 0;
    uint32_t sz = w16 ? 1u : 0u;
    DMA2_Stream3->CR = (3u << DMA_SxCR_CHSEL_Pos) | (1u << DMA_SxCR_DIR_Pos) | (minc ? DMA_SxCR_MINC : 0u) |
                       (sz << DMA_SxCR_PSIZE_Pos) | (sz << DMA_SxCR_MSIZE_Pos) | (2u << DMA_SxCR_PL_Pos) |
                       (irq ? DMA_SxCR_TCIE : 0u);
    DMA2_Stream3->CR |= DMA_SxCR_EN;
    SPI1->CR2 |= SPI_CR2_TXDMAEN;
}

void dma3_finish(void)
{
    uint32_t n = 0;
    while (!(DMA2->LISR & S3_TC) && ++n < 20000000u) {
    }
    spi1_wait_idle();
    SPI1->CR2 &= ~SPI_CR2_TXDMAEN;
    dma3_off();
}

/* ================================================================ bench.lcd */

enum { M_POLL8, M_POLL16, M_DMA8, M_DMA16, M_FILL_POLL16, M_FILL_DMA16 };
static uint32_t s_mode;
static uint32_t s_setup_min;       /* cycles to start the DMA (CPU cost), min over reps */
static volatile uint16_t s_fill16 = 0x07E0;
static uint16_t s_rw, s_rh;

static void k_lcd_frame(uint32_t n)
{
    (void)n;
    uint32_t t0 = DWT->CYCCNT;
    lcd_set_window(0, 0, FW - 1u, FH - 1u);
    uint32_t t1 = DWT->CYCCNT;
    (void)t1;
    switch (s_mode) {
    case M_POLL8:
        lcd_set_frame16(false);
        lcd_push_pixels((const uint16_t *)(void *)g_arena, FRAME_PX);
        break;
    case M_POLL16:
        lcd_set_frame16(true);
        lcd_push_pixels((const uint16_t *)(void *)g_arena, FRAME_PX);
        break;
    case M_FILL_POLL16:
        lcd_set_frame16(true);
        lcd_push_color(s_fill16, FRAME_PX);
        break;
    default: {
        bool w16 = (s_mode == M_DMA16 || s_mode == M_FILL_DMA16);
        spi1_dff(w16);
        uint32_t t2 = DWT->CYCCNT;
        if (s_mode == M_FILL_DMA16) {
            dma3_start((const void *)&s_fill16, FRAME_PX, true, false, false);
        } else {
            dma3_start(g_arena, w16 ? FRAME_PX : FRAME_BYTES, w16, true, false);
        }
        uint32_t su = DWT->CYCCNT - t2;
        if (su < s_setup_min) s_setup_min = su;
        dma3_finish();
        break;
    }
    }
    lcd_end();
    lcd_set_frame16(false);
    (void)t0;
}

static void k_lcd_window(uint32_t n)
{
    while (n--) {
        lcd_set_window(0, 0, FW - 1u, FH - 1u);
        lcd_end();
    }
}

static void k_rects(uint32_t n)
{
    uint32_t seed = 1;
    while (n--) {
        seed = seed * 1664525u + 1013904223u;
        int16_t x = (int16_t)((seed >> 8) % (FW - s_rw + 1u));
        int16_t y = (int16_t)((seed >> 20) % (FH - s_rh + 1u));
        uint16_t col = (uint16_t)(seed >> 3);
        if (s_mode == M_FILL_DMA16) {
            lcd_set_window((uint16_t)x, (uint16_t)y, (uint16_t)(x + s_rw - 1), (uint16_t)(y + s_rh - 1));
            spi1_dff(true);
            s_fill16 = col;
            dma3_start((const void *)&s_fill16, (uint32_t)s_rw * s_rh, true, false, false);
            dma3_finish();
            lcd_end();
        } else {
            lcd_set_frame16(true);
            lcd_fill_rect(x, y, (int16_t)s_rw, (int16_t)s_rh, col);
            lcd_set_frame16(false);
        }
    }
}

static void fill_test_pattern(void)
{
    uint16_t *p = (uint16_t *)(void *)g_arena;
    for (uint32_t y = 0; y < FH; y++) {
        for (uint32_t x = 0; x < FW; x++) {
            p[y * FW + x] = (uint16_t)(((x * 31u / FW) << 11) | ((y * 63u / FH) << 5) | ((x + y) & 31u));
        }
    }
}

suite_verdict_t suite_bench_lcd(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const uint16_t div0 = lcd_cfg.spi_div;
    static const uint16_t divs[3] = {8, 4, 2};
    static const char *const modes[] = {"poll8", "poll16", "dma8", "dma16", "fill_poll16", "fill_dma16"};
    fill_test_pattern();
    uint32_t best_dma_us = 0xFFFFFFFFu;
    uint32_t best_poll_us = 0xFFFFFFFFu;
    for (int d = 0; d < 3; d++) {
        lcd_set_spi_div(divs[d]);
        uint32_t hz = lcd_spi_hz();
        uint32_t theory_us = (uint32_t)(((uint64_t)FRAME_BYTES * 8u * 1000000u) / hz);
        /* window-setup cost (3 commands) */
        bench_stat_t w = bench_measure(k_lcd_window, 8);
        char ex[96], nm[40];
        snprintf(ex, sizeof(ex), ",\"spi_hz\":%" PRIu32, hz);
        snprintf(nm, sizeof(nm), "window_setup_%" PRIu32 "mhz", hz / 1000000u);
        bench_emit("bench.lcd", nm, w, 8, "window", 0, "flash", ex);
        if (d == 0) {
            bench_note("lcd.window_cyc", w.cyc_min / 8u);
        }
        for (uint32_t m = 0; m < 6; m++) {
            s_mode = m;
            s_setup_min = 0xFFFFFFFFu;
            bench_stat_t s = bench_measure(k_lcd_frame, 1);
            uint32_t wall_us = cyc_to_us(s.cyc_min);
            uint32_t util = wall_us ? (uint32_t)(((uint64_t)theory_us * 100u) / wall_us) : 0u;
            uint32_t fps_x10 = wall_us ? (uint32_t)(10000000u / wall_us) : 0u;
            bool dma = (m == M_DMA8 || m == M_DMA16 || m == M_FILL_DMA16);
            char f[16];
            snprintf(ex, sizeof(ex), ",\"spi_hz\":%" PRIu32 ",\"wall_us\":%" PRIu32 ",\"fps\":%s,\"util_pct\":%" PRIu32
                     ",\"theory_us\":%" PRIu32 ",\"cpu_cyc\":%" PRIu32,
                     hz, wall_us, fmt2(f, sizeof(f), fps_x10 * 10u), util, theory_us,
                     dma ? s_setup_min : s.cyc_min);
            snprintf(nm, sizeof(nm), "frame_%s_%" PRIu32 "mhz", modes[m], hz / 1000000u);
            bench_emit("bench.lcd", nm, s, 0, NULL, FRAME_BYTES, "flash", ex);
            if (dma && m != M_FILL_DMA16 && wall_us < best_dma_us) best_dma_us = wall_us;
            if (!dma && wall_us < best_poll_us) best_poll_us = wall_us;
            sys_service();
            if (abort_requested()) {
                lcd_set_spi_div(div0);
                return SUITE_ABORT;
            }
        }
        /* rects/s: polling 16-bit vs DMA solid fill */
        static const uint16_t rs[4][2] = {{8, 8}, {16, 16}, {32, 32}, {160, 16}};
        for (int r = 0; r < 4; r++) {
            s_rw = rs[r][0];
            s_rh = rs[r][1];
            for (int mm = 0; mm < 2; mm++) {
                s_mode = mm ? M_FILL_DMA16 : M_POLL16;
                bench_stat_t s = bench_measure(k_rects, 32);
                uint32_t us = cyc_to_us(s.cyc_min);
                uint32_t rps = us ? (uint32_t)(32000000ull / us) : 0u;
                snprintf(ex, sizeof(ex), ",\"spi_hz\":%" PRIu32 ",\"rects_per_s\":%" PRIu32, hz, rps);
                snprintf(nm, sizeof(nm), "rect_%ux%u_%s_%" PRIu32 "mhz", rs[r][0], rs[r][1], mm ? "dma" : "poll16",
                         hz / 1000000u);
                bench_emit("bench.lcd", nm, s, 32, "rect", 0, "flash", ex);
                sys_service();
                if (abort_requested()) {
                    lcd_set_spi_div(div0);
                    return SUITE_ABORT;
                }
            }
        }
    }
    lcd_set_spi_div(div0);
    lcd_set_frame16(false);
    bench_note("lcd.best_dma_us", best_dma_us);
    bench_note("lcd.best_poll_us", best_poll_us);
    return SUITE_PASS;
}

/* ================================================================ bench.gfx */

#define CHUNK_LINES 8u
#define CHUNK_PX (FW * CHUNK_LINES)
#define N_CHUNKS (FH / CHUNK_LINES)

static uint16_t s_pal[256];
#define FB   (g_arena)                       /* 160x128 8bpp, 20480 B */
#define OUTB ((uint16_t *)(void *)(g_arena + 20480))   /* 40960 B */
#define PP0  ((uint16_t *)(void *)(g_arena + 61440))   /* 2560 B */
#define PP1  ((uint16_t *)(void *)(g_arena + 64000))   /* 2560 B */
#define GCACHE (g_arena + 66560)                       /* 95 glyphs * 64 B */

static volatile uint32_t s_gsink;

/* 4-pixel unrolled: one word load, two word stores */
static inline __attribute__((always_inline)) void conv4(const uint8_t *s, uint16_t *d, uint32_t npx)
{
    const uint32_t *sw = (const uint32_t *)(const void *)s;
    uint32_t *dw = (uint32_t *)(void *)d;
    for (uint32_t i = 0; i < npx / 4u; i++) {
        uint32_t w = sw[i];
        uint32_t p0 = s_pal[w & 255u], p1 = s_pal[(w >> 8) & 255u], p2 = s_pal[(w >> 16) & 255u], p3 = s_pal[w >> 24];
        dw[2u * i] = p0 | (p1 << 16);
        dw[2u * i + 1u] = p2 | (p3 << 16);
    }
}

static void k_conv_plain(uint32_t n)
{
    while (n--) {
        const uint8_t *s = FB;
        uint16_t *d = OUTB;
        for (uint32_t i = 0; i < FRAME_PX; i++) {
            d[i] = s_pal[s[i]];
        }
        MEMBAR();
    }
}

static void k_conv_unrolled(uint32_t n)
{
    while (n--) {
        conv4(FB, OUTB, FRAME_PX);
        MEMBAR();
    }
}

/* ---- pipelined present ---- */
static volatile uint32_t s_pp_k, s_pp_done, s_isr_cyc;
static uint32_t s_prefill_cyc, s_isr_min = 0xFFFFFFFFu, s_prefill_min = 0xFFFFFFFFu;

static void conv_chunk(uint32_t k, uint16_t *dst) { conv4(FB + k * CHUNK_PX, dst, CHUNK_PX); }

void DMA2_Stream3_IRQHandler(void)
{
    uint32_t t0 = DWT->CYCCNT;
    if (DMA2->LISR & S3_TC) {
        DMA2->LIFCR = S3_ALL;
        uint32_t k = s_pp_k;
        s_pp_k = k + 1u;
        if (k + 1u < N_CHUNKS) {
            uint32_t n = 0;
            while ((DMA2_Stream3->CR & DMA_SxCR_EN) && ++n < 1000u) {
            }
            DMA2_Stream3->M0AR = (uint32_t)(((k + 1u) & 1u) ? PP1 : PP0);
            DMA2_Stream3->NDTR = CHUNK_PX;
            DMA2_Stream3->CR |= DMA_SxCR_EN;
            if (k + 2u < N_CHUNKS) {
                conv_chunk(k + 2u, (k & 1u) ? PP1 : PP0);
            }
        } else {
            s_pp_done = 1u;
        }
    } else {
        DMA2->LIFCR = S3_ALL;
    }
    s_isr_cyc += DWT->CYCCNT - t0;
}

static void k_present_pipelined(uint32_t n)
{
    (void)n;
    lcd_set_window(0, 0, FW - 1u, FH - 1u);
    spi1_dff(true);
    s_pp_k = 0;
    s_pp_done = 0;
    s_isr_cyc = 0;
    uint32_t t0 = DWT->CYCCNT;
    conv_chunk(0, PP0);
    conv_chunk(1, PP1);
    s_prefill_cyc = DWT->CYCCNT - t0;
    dma3_start(PP0, CHUNK_PX, true, true, true);
    uint32_t spin = 0;
    while (!s_pp_done && ++spin < 50000000u) {
    }
    dma3_off();
    spi1_wait_idle();
    SPI1->CR2 &= ~SPI_CR2_TXDMAEN;
    lcd_end();
    if (s_isr_cyc < s_isr_min) s_isr_min = s_isr_cyc;
    if (s_prefill_cyc < s_prefill_min) s_prefill_min = s_prefill_cyc;
}

static void k_present_whole(uint32_t n)
{
    (void)n;
    conv4(FB, OUTB, FRAME_PX);
    lcd_set_window(0, 0, FW - 1u, FH - 1u);
    spi1_dff(true);
    dma3_start(OUTB, FRAME_PX, true, true, false);
    dma3_finish();
    lcd_end();
}

/* ---- sprites (8bpp, into FB) ---- */
#define s_spr (g_arena + 20480)         /* 1 KB, shares the converter output buffer (sprites run after the present tests) */
static int16_t s_px[64], s_py[64];
static uint32_t s_ss;

static inline __attribute__((always_inline)) void spr_opaque(uint8_t *fb, int x, int y, const uint8_t *sp, int w)
{
    uint8_t *d = fb + y * (int)FW + x;
    for (int r = 0; r < w; r++) {
        for (int c = 0; c < w; c += 4) {
            ((u32u_t *)(void *)(d + c))->v = ((const u32u_t *)(const void *)(sp + c))->v;
        }
        d += FW;
        sp += w;
    }
}

static inline __attribute__((always_inline)) void spr_key(uint8_t *fb, int x, int y, const uint8_t *sp, int w)
{
    uint8_t *d = fb + y * (int)FW + x;
    for (int r = 0; r < w; r++) {
        for (int c = 0; c < w; c++) {
            uint8_t p = sp[c];
            if (p) d[c] = p;
        }
        d += FW;
        sp += w;
    }
}

static inline __attribute__((always_inline)) void spr_flip(uint8_t *fb, int x, int y, const uint8_t *sp, int w)
{
    uint8_t *d = fb + y * (int)FW + x;
    for (int r = 0; r < w; r++) {
        for (int c = 0; c < w; c++) {
            uint8_t p = sp[w - 1 - c];
            if (p) d[c] = p;
        }
        d += FW;
        sp += w;
    }
}

static inline __attribute__((always_inline)) void spr_clip(uint8_t *fb, int x, int y, const uint8_t *sp, int w)
{
    int x0 = x < 0 ? -x : 0, y0 = y < 0 ? -y : 0;
    int x1 = (x + w > (int)FW) ? (int)FW - x : w, y1 = (y + w > (int)FH) ? (int)FH - y : w;
    if (x0 >= x1 || y0 >= y1) return;
    for (int r = y0; r < y1; r++) {
        uint8_t *d = fb + (y + r) * (int)FW + x;
        const uint8_t *s = sp + r * w;
        for (int c = x0; c < x1; c++) {
            uint8_t p = s[c];
            if (p) d[c] = p;
        }
    }
}

#define SPR_KERNEL(name, FN)                                                         \
    static void name(uint32_t n)                                                     \
    {                                                                                \
        uint8_t *fb = FB;                                                            \
        const uint8_t *sp = s_spr;                                                   \
        uint32_t sz = s_ss;                                                          \
        while (n--) {                                                                \
            for (int i = 0; i < 64; i++) {                                           \
                int x = s_px[i], y = s_py[i];                                        \
                if (sz == 8) FN(fb, x, y, sp, 8);                                    \
                else if (sz == 16) FN(fb, x, y, sp, 16);                             \
                else FN(fb, x, y, sp, 32);                                           \
            }                                                                        \
            MEMBAR();                                                                \
        }                                                                            \
    }
SPR_KERNEL(k_spr_opaque, spr_opaque)
SPR_KERNEL(k_spr_key, spr_key)
SPR_KERNEL(k_spr_flip, spr_flip)
SPR_KERNEL(k_spr_clip, spr_clip)

/* ---- tilemap 20x16 of 8x8 tiles with pixel scroll ---- */
#define s_tiles8 (g_arena + 21504)
#define s_map8 (g_arena + 25600)

static void k_tilemap(uint32_t n)
{
    uint32_t sx = 5, sy = 3;
    while (n--) {
        uint32_t cx = sx >> 3, cy = sy >> 3;
        int ox = -(int)(sx & 7u), oy = -(int)(sy & 7u);
        for (uint32_t ty = 0; ty < 17u; ty++) {
            for (uint32_t tx = 0; tx < 21u; tx++) {
                uint32_t idx = s_map8[(((cy + ty) & 63u) << 6) + ((cx + tx) & 63u)] & 63u;
                const uint8_t *sp = &s_tiles8[idx << 6];
                int x = ox + (int)tx * 8, y = oy + (int)ty * 8;
                if (x >= 0 && y >= 0 && x + 8 <= (int)FW && y + 8 <= (int)FH) {
                    spr_opaque(FB, x, y, sp, 8);
                } else {
                    spr_clip(FB, x, y, sp, 8);
                }
            }
        }
        sx += 3;
        sy += 1;
        MEMBAR();
    }
}

/* ---- glyphs ---- */
static void k_glyph_plain(uint32_t n)
{
    static const char txt[] = "The quick brown fox jumps over";
    while (n--) {
        int x = 0;
        for (const char *c = txt; *c; c++) {
            const uint8_t *g = font5x7_glyph(*c);
            uint8_t *d = FB + 20 * (int)FW + x;
            for (int col = 0; col < 5; col++) {
                uint32_t bits = g[col];
                for (int row = 0; row < 7; row++) {
                    if (bits & (1u << row)) d[row * (int)FW + col] = 15;
                }
            }
            x += 6;
        }
        MEMBAR();
    }
}

static void k_glyph_cached(uint32_t n)
{
    static const char txt[] = "The quick brown fox jumps over";
    while (n--) {
        int x = 0;
        for (const char *c = txt; *c; c++) {
            spr_opaque(FB, x, 40, GCACHE + (uint32_t)(*c - 32) * 64u, 8);
            x += 6;
        }
        MEMBAR();
    }
}

static void gfx_prepare_sprites(void)
{
    uint32_t x = 777;
    for (int i = 0; i < 32 * 32; i++) {
        x = x * 1664525u + 1013904223u;
        s_spr[i] = ((x >> 24) & 3u) ? (uint8_t)(x >> 16) | 1u : 0u; /* ~25 % transparent (index 0) */
    }
    for (unsigned i = 0; i < 4096u; i++) {
        x = x * 1664525u + 1013904223u;
        s_tiles8[i] = (uint8_t)(x >> 20) | 1u;
        s_map8[i] = (uint8_t)(x >> 25);
    }
}

static void gfx_prepare(void)
{
    for (int i = 0; i < 256; i++) {
        s_pal[i] = (uint16_t)((((uint32_t)i >> 3) << 11) | (((uint32_t)i >> 2) << 5) | (31u - ((uint32_t)i >> 3)));
    }
    for (uint32_t y = 0; y < FH; y++) {
        for (uint32_t xx = 0; xx < FW; xx++) {
            FB[y * FW + xx] = (uint8_t)(xx + y * 2u);
        }
    }
    /* glyph cache: 8x8 cells, fg 15 / bg 1 */
    for (int ch = 32; ch <= 126; ch++) {
        const uint8_t *g = font5x7_glyph((char)ch);
        uint8_t *d = GCACHE + (uint32_t)(ch - 32) * 64u;
        memset(d, 1, 64);
        for (int col = 0; col < 5; col++) {
            for (int row = 0; row < 7; row++) {
                if (g[col] & (1u << row)) d[row * 8 + col] = 15;
            }
        }
    }
}

extern uint32_t _ebss, _sdata, _estack, _sarena, _earena, end;

suite_verdict_t suite_bench_gfx(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    gfx_prepare();
    const uint16_t div0 = lcd_cfg.spi_div;
    char ex[200], f[16], f2[16];
    bench_stat_t s;

    /* line conversion */
    s = bench_measure(k_conv_plain, 1);
    bench_emit("bench.gfx", "convert_plain", s, FRAME_PX, "px", 0, "flash", NULL);
    s = bench_measure(k_conv_unrolled, 1);
    bench_emit("bench.gfx", "convert_unrolled4", s, FRAME_PX, "px", 0, "flash", NULL);
    bench_note("gfx.convert_cyc_frame", s.cyc_min);

    /* pipelined present vs whole-frame convert + DMA */
    NVIC_SetPriority(DMA2_Stream3_IRQn, 2);
    NVIC_EnableIRQ(DMA2_Stream3_IRQn);
    static const uint16_t divs[3] = {8, 4, 2};
    uint32_t conv_pct60_x100 = 0;
    for (int d = 0; d < 3; d++) {
        lcd_set_spi_div(divs[d]);
        uint32_t hz = lcd_spi_hz();
        s_isr_min = 0xFFFFFFFFu;
        s_prefill_min = 0xFFFFFFFFu;
        s = bench_measure_irq(k_present_pipelined, 1);
        uint32_t wall_us = cyc_to_us(s.cyc_min);
        uint32_t cpu = s_isr_min + s_prefill_min;
        uint32_t pct_x100 = s.cyc_min ? (uint32_t)(((uint64_t)cpu * 10000u) / s.cyc_min) : 0u;
        snprintf(ex, sizeof(ex), ",\"spi_hz\":%" PRIu32 ",\"wall_us\":%" PRIu32 ",\"fps\":%s,\"isr_cyc\":%" PRIu32
                 ",\"prefill_cyc\":%" PRIu32 ",\"cpu_pct\":%s,\"chunks\":%u",
                 hz, wall_us, fmt2(f, sizeof(f), wall_us ? 1000000u * 100u / wall_us : 0u), s_isr_min, s_prefill_min,
                 fmt2(f2, sizeof(f2), pct_x100), (unsigned)N_CHUNKS);
        char nm[48];
        snprintf(nm, sizeof(nm), "present_pipelined_%" PRIu32 "mhz", hz / 1000000u);
        bench_emit("bench.gfx", nm, s, 0, NULL, FRAME_BYTES, "flash", ex);
        if (d == 2) {
            conv_pct60_x100 = (uint32_t)(((uint64_t)cpu * 10000u) / (SystemCoreClock / 60u));
            bench_note("gfx.conv_cyc_pipelined", cpu);
            bench_note("gfx.present48_us", wall_us);
        }
        s = bench_measure_irq(k_present_whole, 1);
        wall_us = cyc_to_us(s.cyc_min);
        snprintf(ex, sizeof(ex), ",\"spi_hz\":%" PRIu32 ",\"wall_us\":%" PRIu32 ",\"fps\":%s", hz, wall_us,
                 fmt2(f, sizeof(f), wall_us ? 1000000u * 100u / wall_us : 0u));
        snprintf(nm, sizeof(nm), "present_whole_%" PRIu32 "mhz", hz / 1000000u);
        bench_emit("bench.gfx", nm, s, 0, NULL, FRAME_BYTES, "flash", ex);
        sys_service();
        if (abort_requested()) {
            NVIC_DisableIRQ(DMA2_Stream3_IRQn);
            lcd_set_spi_div(div0);
            return SUITE_ABORT;
        }
    }
    NVIC_DisableIRQ(DMA2_Stream3_IRQn);
    lcd_set_spi_div(div0);
    bench_note("gfx.conv_pct60_x100", conv_pct60_x100);

    /* sprites */
    gfx_prepare_sprites();
    static const struct { const char *name; bench_fn_t fn; bool clip; } sk[] = {
        {"opaque", k_spr_opaque, false}, {"colorkey", k_spr_key, false},
        {"flip", k_spr_flip, false}, {"clip", k_spr_clip, true},
    };
    uint32_t seed = 99;
    for (uint32_t sz = 8; sz <= 32; sz *= 2) {
        s_ss = sz;
        for (unsigned k = 0; k < 4; k++) {
            for (int i = 0; i < 64; i++) {
                seed = seed * 1664525u + 1013904223u;
                if (sk[k].clip) { /* partly off-screen on every side */
                    s_px[i] = (int16_t)((int)((seed >> 8) % (FW + 2u * sz)) - (int)sz);
                    s_py[i] = (int16_t)((int)((seed >> 20) % (FH + 2u * sz)) - (int)sz);
                } else {
                    s_px[i] = (int16_t)((seed >> 8) % (FW - sz + 1u) & ~3u);
                    s_py[i] = (int16_t)((seed >> 20) % (FH - sz + 1u));
                }
            }
            s = bench_measure(sk[k].fn, 4);
            uint32_t per = s.cyc_min / (4u * 64u);
            snprintf(ex, sizeof(ex), ",\"spf30\":%" PRIu32 ",\"spf60\":%" PRIu32, per ? (SystemCoreClock / 30u) / per : 0u,
                     per ? (SystemCoreClock / 60u) / per : 0u);
            char nm[32];
            snprintf(nm, sizeof(nm), "sprite_%ux%u_%s", (unsigned)sz, (unsigned)sz, sk[k].name);
            bench_emit("bench.gfx", nm, s, 4 * 64, "sprite", 0, "flash", ex);
            if (sz == 16 && k == 1) {
                bench_note("gfx.spf60_16key", per ? (SystemCoreClock / 60u) / per : 0u);
            }
            sys_service();
            if (abort_requested()) return SUITE_ABORT;
        }
    }

    /* tilemap */
    s = bench_measure(k_tilemap, 4);
    snprintf(ex, sizeof(ex), ",\"pct_of_60fps\":%s", fmt2(f, sizeof(f), (uint32_t)(((uint64_t)(s.cyc_min / 4u) * 10000u) / (SystemCoreClock / 60u))));
    bench_emit("bench.gfx", "tilemap_20x16_scroll", s, 4, "frame", 0, "flash", ex);
    bench_note("gfx.tilemap_cyc", s.cyc_min / 4u);

    /* glyphs */
    const uint32_t nchars = 30;
    s = bench_measure(k_glyph_plain, 8);
    snprintf(ex, sizeof(ex), ",\"glyphs_per_ms\":%" PRIu32, s.cyc_min ? (uint32_t)(((uint64_t)8 * nchars * (SystemCoreClock / 1000u)) / s.cyc_min) : 0u);
    bench_emit("bench.gfx", "glyph_5x7_plain", s, 8 * nchars, "glyph", 0, "flash", ex);
    s = bench_measure(k_glyph_cached, 8);
    snprintf(ex, sizeof(ex), ",\"glyphs_per_ms\":%" PRIu32, s.cyc_min ? (uint32_t)(((uint64_t)8 * nchars * (SystemCoreClock / 1000u)) / s.cyc_min) : 0u);
    bench_emit("bench.gfx", "glyph_8x8_cached", s, 8 * nchars, "glyph", 0, "flash", ex);

    /* RAM left */
    uint32_t ram_total = (uint32_t)(uintptr_t)&_estack - 0x20000000u;
    uint32_t used_static = (uint32_t)(uintptr_t)&_ebss - 0x20000000u;
    uint32_t arena = (uint32_t)(uintptr_t)&_earena - (uint32_t)(uintptr_t)&_sarena;
    uint32_t stack_heap = 0x1000u + 0x400u;
    uint32_t free_incl = ram_total - used_static - stack_heap;
    bench_note("ram.free_incl_arena", free_incl);
    bench_note("ram.free_excl_arena", free_incl > arena ? free_incl - arena : 0u);
    res_printf("{\"t\":\"bench\",\"suite\":\"bench.gfx\",\"name\":\"ram_left\",\"ram_total\":%" PRIu32 ",\"static_data_bss\":%"
               PRIu32 ",\"arena\":%" PRIu32 ",\"stack_heap_reserved\":%" PRIu32 ",\"free_incl_arena\":%" PRIu32
               ",\"free_excl_arena\":%" PRIu32 ",\"double_20k_ok\":%s,\"double_40k_ok\":%s}",
               ram_total, used_static, arena, stack_heap, free_incl, free_incl > arena ? free_incl - arena : 0u,
               free_incl >= 2u * 20480u ? "true" : "false", free_incl >= 2u * 40960u ? "true" : "false");
    (void)&_sdata;
    (void)&end;
    return SUITE_PASS;
}
