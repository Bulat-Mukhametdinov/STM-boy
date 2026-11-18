/* stream.video: synthetic animation stored in W25Q 0xF80000.., streamed by DMA into the 8bpp framebuffer.

   Flash layout (all inside the block-C scratch range 0xF80000-0xFDBFFF):
     0xF80000  header sector (vhdr_t: magic, geometry, per-frame HW-CRC of every frame, header CRC)
     0xF81000  16 x 8bpp frames, 160x128 column-major = 20480 B each   (0xF81000-0xFD0FFF)
     0xFD1000  16 x 1bpp frames, 160x128 column-major bits = 2560 B each (0xFD1000-0xFDAFFF)
   The region is only rewritten when the header or any frame CRC does not match (re-generated procedurally: plasma /
   rotating checkerboard, 16 frames = seamless loop).

   Playback = the demo's present pipeline (DMA2 S3, 48 MHz SPI1) while the NEXT frame is fetched from the W25Q by
   DMA1 (directly into the framebuffer for 8bpp; into a 2.5 KB buffer + bit expansion for 1bpp). Arena: pipeline
   0..61951 (gamepipe.h), expansion LUT 61952 (2 KB), raw 1bpp frame 64000 (2560 B). */

#include "suites.h"
#include "bench.h"
#include "cmd.h"
#include "crc.h"
#include "gamepipe.h"
#include "gfx.h"
#include "input.h"
#include "lcd.h"
#include "log.h"
#include "timebase.h"
#include "ui.h"
#include "w25q.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define NF 16u
#define SZ8 20480u
#define SZ1 2560u
#define HDR_ADDR 0xF80000u
#define BASE8 0xF81000u
#define BASE1 0xFD1000u
#define END_ADDR 0xFDB000u           /* first byte after the used range; the block-C limit is 0xFDBFFF */
#define VMAGIC 0x31444956u           /* "VID1" */

typedef struct {
    uint32_t magic, ver, n8, sz8, base8, n1, sz1, base1;
    uint32_t crc8[NF], crc1[NF];
    uint32_t hcrc;                   /* CRC-32 (zlib) of everything before */
} vhdr_t;

#define LUT1 ((uint32_t *)(void *)(g_arena + 61952u))   /* 256 x 8 B */
#define RAW1 (g_arena + 64000u)
static uint8_t s_sin[256];

static void sin_init(void)
{
    for (int i = 0; i < 256; i++) {
        s_sin[i] = (uint8_t)(128.0f + 127.0f * sinf((float)i * (6.2831853f / 256.0f)));
    }
}

static void gen8(uint8_t *fb, uint32_t f)
{
    uint32_t t = f * 16u;
    for (uint32_t x = 0; x < 160u; x++) {
        uint32_t a = s_sin[(x * 3u + t) & 255u];
        uint8_t *col = fb + x * 128u;
        for (uint32_t y = 0; y < 128u; y++) {
            uint32_t b = s_sin[(y * 4u + 2u * t) & 255u];
            uint32_t c = s_sin[((x + y) * 2u + 3u * t) & 255u];
            uint32_t q = ((a + b + c) / 3u + t) & 255u;
            col[y] = (uint8_t)((q * 254u) >> 8);       /* 0..253: 254/255 stay the overlay colours */
        }
    }
}

static void gen1(uint8_t *raw, uint32_t f)
{
    int p = (int)(f * 4u);                              /* 16 frames = a quarter turn: the checkerboard repeats */
    int c = (int)s_sin[(p + 64) & 255] - 128, s = (int)s_sin[p & 255] - 128;
    for (int x = 0; x < 160; x++) {
        for (int y8 = 0; y8 < 16; y8++) {
            uint32_t byte = 0;
            for (int k = 0; k < 8; k++) {
                int y = y8 * 8 + k;
                int dx = x - 80, dy = y - 64;
                int u = (dx * c - dy * s) >> 7, v = (dx * s + dy * c) >> 7;
                if (((u >> 4) ^ (v >> 4)) & 1) byte |= 1u << k;
            }
            raw[x * 16 + y8] = (uint8_t)byte;
        }
    }
}

static void pal_plasma(void)
{
    uint16_t *pal = gp_pal();
    for (int i = 0; i < 254; i++) {
        float a = (float)i * (6.2831853f / 254.0f);
        int r = (int)(127.5f * (1.0f + sinf(a))), g = (int)(127.5f * (1.0f + sinf(a + 2.0944f))),
            b = (int)(127.5f * (1.0f + sinf(a + 4.1888f)));
        pal[i] = RGB565(r, g, b);
    }
    pal[GP_IDX_BLACK] = COLOR_BLACK;
    pal[GP_IDX_WHITE] = COLOR_WHITE;
}

static void pal_mono(void)
{
    uint16_t *pal = gp_pal();
    pal[0] = COLOR_BLACK;
    pal[1] = COLOR_WHITE;
    pal[GP_IDX_BLACK] = COLOR_BLACK;
    pal[GP_IDX_WHITE] = COLOR_WHITE;
}

static void lut1_init(void)
{
    for (uint32_t b = 0; b < 256u; b++) {
        uint32_t lo = 0, hi = 0;
        for (uint32_t k = 0; k < 4u; k++) {
            lo |= ((b >> k) & 1u) << (8u * k);
            hi |= ((b >> (k + 4u)) & 1u) << (8u * k);
        }
        LUT1[2u * b] = lo;
        LUT1[2u * b + 1u] = hi;
    }
}

static void expand1(uint8_t *fb, const uint8_t *raw)
{
    uint32_t *d = (uint32_t *)(void *)fb;
    for (uint32_t i = 0; i < SZ1; i++) {
        uint32_t b = raw[i];
        d[2u * i] = LUT1[2u * b];
        d[2u * i + 1u] = LUT1[2u * b + 1u];
    }
}

static bool dma_read_wait(uint32_t addr, void *buf, uint32_t n)
{
    if (!w25q_dma_start(addr, buf, n, true)) return false;
    uint32_t t0 = time_us();
    while (!w25q_dma_done() && (uint32_t)(time_us() - t0) < 200000u) {
    }
    bool ok = w25q_dma_done();
    w25q_dma_finish();
    return ok;
}

/* true when the stored animation is complete and every frame CRC matches */
static bool region_valid(vhdr_t *h)
{
    if (w25q_read(HDR_ADDR, h, sizeof(*h)) != W25Q_OK) return false;
    if (h->magic != VMAGIC || h->ver != 1u || h->n8 != NF || h->sz8 != SZ8 || h->base8 != BASE8 || h->n1 != NF ||
        h->sz1 != SZ1 || h->base1 != BASE1 ||
        h->hcrc != crc32_zlib(0, h, sizeof(*h) - 4u)) {
        return false;
    }
    for (uint32_t f = 0; f < NF; f++) {
        if (!dma_read_wait(BASE8 + f * SZ8, g_arena, SZ8) ||
            crc_hw_words((const uint32_t *)(const void *)g_arena, SZ8 / 4u) != h->crc8[f]) {
            return false;
        }
        if (!dma_read_wait(BASE1 + f * SZ1, g_arena, SZ1) ||
            crc_hw_words((const uint32_t *)(const void *)g_arena, SZ1 / 4u) != h->crc1[f]) {
            return false;
        }
        sys_service();
    }
    return true;
}

static bool region_write(vhdr_t *h, uint32_t *erase_ms, uint32_t *prog_ms)
{
    uint32_t t0 = HAL_GetTick();
    w25q_set_div((uint16_t)(HAL_RCC_GetPCLK1Freq() / w25q_spi_hz()));   /* clean SPI3 state after DMA reads */
    for (uint32_t a = HDR_ADDR; a < END_ADDR; a += 4096u) {
        /* w25q_erase_sector() was seen returning before the erase finished; erase_cmd waits on BUSY */
        if (w25q_erase_cmd(0x20u, a, NULL) != W25Q_OK) return false;
        sys_service();
        if (abort_requested()) return false;
    }
    *erase_ms = HAL_GetTick() - t0;
    t0 = HAL_GetTick();
    memset(h, 0xFF, sizeof(*h));
    h->magic = VMAGIC;
    h->ver = 1;
    h->n8 = NF; h->sz8 = SZ8; h->base8 = BASE8; h->n1 = NF; h->sz1 = SZ1; h->base1 = BASE1;
    for (uint32_t f = 0; f < NF; f++) {
        gen8(g_arena, f);
        h->crc8[f] = crc_hw_words((const uint32_t *)(const void *)g_arena, SZ8 / 4u);
        if (w25q_program(BASE8 + f * SZ8, g_arena, SZ8) != W25Q_OK) return false;
        sys_service();
    }
    for (uint32_t f = 0; f < NF; f++) {
        gen1(g_arena, f);
        h->crc1[f] = crc_hw_words((const uint32_t *)(const void *)g_arena, SZ1 / 4u);
        if (w25q_program(BASE1 + f * SZ1, g_arena, SZ1) != W25Q_OK) return false;
        sys_service();
    }
    h->hcrc = crc32_zlib(0, h, sizeof(*h) - 4u);
    if (w25q_program(HDR_ADDR, h, sizeof(*h)) != W25Q_OK) return false;
    *prog_ms = HAL_GetTick() - t0;
    return true;
}

typedef struct {
    const char *name;
    bool one_bpp;
    uint32_t spi3_hz;
} vphase_t;

static const vphase_t k_ph[] = {
    {"8bpp_24MHz", false, 24000000u},
    {"8bpp_12MHz", false, 12000000u},
    {"1bpp_12MHz", true, 12000000u},
    {"1bpp_24MHz", true, 24000000u},
};

static bool run_phase(const vphase_t *ph, uint32_t secs, uint32_t pclk1, uint32_t *w25_err)
{
    uint32_t hz = ph->spi3_hz;
    w25q_set_div((uint16_t)(pclk1 / hz));
    uint32_t frame_bytes = ph->one_bpp ? SZ1 : SZ8;
    uint32_t base = ph->one_bpp ? BASE1 : BASE8;
    uint8_t *raw = ph->one_bpp ? RAW1 : NULL;
    if (ph->one_bpp) pal_mono(); else pal_plasma();

    /* W25Q alone: ms per frame at this SPI rate */
    uint32_t ta = time_us();
    for (uint32_t i = 0; i < 8u; i++) {
        if (!dma_read_wait(base + (i % NF) * frame_bytes, ph->one_bpp ? (void *)RAW1 : (void *)gp_fb(0), frame_bytes)) {
            (*w25_err)++;
        }
    }
    uint32_t alone_us = (time_us() - ta) / 8u;

    int cur = 0;
    uint32_t f = 0;
    if (ph->one_bpp) {
        dma_read_wait(base, raw, frame_bytes);
        expand1(gp_fb(0), raw);
    } else {
        dma_read_wait(base, gp_fb(0), frame_bytes);
    }
    gp_start(gp_fb(0));
    uint32_t t_begin = time_us(), t_win = t_begin, n_win = 0, frames = 0, fps_show = 0;
    uint32_t cyc_cpu = 0, c_loop0 = time_cycles();
    uint32_t t_end = t_begin + secs * 1000000u;
    char label[24];
    while ((int32_t)(t_end - time_us()) > 0) {
        int nxt = cur ^ 1;
        f++;
        uint32_t fi = f % NF;
        bool ok = ph->one_bpp ? w25q_dma_start(base + fi * frame_bytes, raw, frame_bytes, true)
                              : w25q_dma_start(base + fi * frame_bytes, gp_fb(nxt), frame_bytes, true);
        if (!ok) (*w25_err)++;
        uint32_t tw = time_us();
        while (!w25q_dma_done() && (uint32_t)(time_us() - tw) < 100000u) {
        }
        if (!w25q_dma_done()) (*w25_err)++;
        uint32_t l = DMA1->LISR, h = DMA1->HISR;
        if ((l & ((1u << 2) | (1u << 3))) || (h & ((1u << 8) | (1u << 9)))) (*w25_err)++;
        w25q_dma_finish();
        uint32_t cw = gp_wait();
        uint32_t c1 = time_cycles();
        if (ph->one_bpp) expand1(gp_fb(nxt), raw);
        snprintf(label, sizeof(label), "%s %ufps", ph->one_bpp ? "1bpp" : "8bpp", (unsigned)fps_show);
        gp_box(gp_fb(nxt), 0, 0, (int)strlen(label) * 6 + 4, 11, GP_IDX_BLACK);
        gp_text(gp_fb(nxt), 2, 2, label, GP_IDX_WHITE, GP_IDX_BLACK);
        cyc_cpu += time_cycles() - c1;
        (void)cw;
        gp_start(gp_fb(nxt));
        cur = nxt;
        frames++;
        n_win++;
        sys_service();
        uint32_t now = time_us();
        if ((uint32_t)(now - t_win) >= 500000u) {
            fps_show = (uint32_t)((uint64_t)n_win * 1000000u / (now - t_win));
            t_win = now;
            n_win = 0;
        }
        if (abort_requested()) break;
    }
    uint32_t t_fin = time_us();
    gp_wait();
    (void)c_loop0;
    uint32_t el_us = t_fin - t_begin;
    char a[16], b[16], c[16], d[16];
    uint32_t fps100 = el_us ? (uint32_t)((uint64_t)frames * 100000000u / el_us) : 0u;
    uint32_t mbps100 = el_us ? (uint32_t)((uint64_t)frames * frame_bytes * 100u / el_us) : 0u;   /* MB/s x100 (bytes/us) */
    uint32_t alone_mbps100 = alone_us ? (uint32_t)((uint64_t)frame_bytes * 100u / alone_us) : 0u;
    res_printf("{\"t\":\"video\",\"suite\":\"stream.video\",\"name\":\"%s\",\"fmt\":\"%s\",\"frame_bytes\":%" PRIu32
               ",\"w25q_hz\":%" PRIu32 ",\"frames\":%" PRIu32 ",\"s\":%" PRIu32 ",\"fps\":%s,\"frame_ms\":%s"
               ",\"w25q_MBps_streamed\":%s,\"w25q_alone_us\":%" PRIu32 ",\"w25q_alone_MBps\":%s"
               ",\"cpu_ms_per_frame_x100\":%" PRIu32 ",\"w25q_err\":%" PRIu32 "}",
               ph->name, ph->one_bpp ? "1bpp" : "8bpp", frame_bytes, w25q_spi_hz(), frames, el_us / 1000000u,
               fmt2(a, sizeof(a), fps100), fmt2(b, sizeof(b), frames ? (uint32_t)((uint64_t)el_us / frames / 10u) : 0u),
               fmt2(c, sizeof(c), mbps100), alone_us, fmt2(d, sizeof(d), alone_mbps100), frames ? (uint32_t)((uint64_t)cyc_cpu * 100u / (SystemCoreClock / 1000u) / frames) : 0u,
               *w25_err);
    return true;
}

suite_verdict_t suite_stream_video(int argc, char **argv)
{
    uint32_t secs = suite_arg_u32(argc, argv, "s", 4u);
    bool force = suite_arg_u32(argc, argv, "regen", 0u) != 0u;
    if (!w25q_present()) {
        res_printf("{\"t\":\"error\",\"suite\":\"stream.video\",\"why\":\"no W25Q\"}");
        return SUITE_FAIL;
    }
    sin_init();
    vhdr_t *h = (vhdr_t *)(void *)(g_arena + 66560u);   /* 168 B, outside the frame/pipeline areas */
    bool valid = !force && region_valid(h);
    uint32_t erase_ms = 0, prog_ms = 0;
    ui_con_line(COLOR_GRAY, valid ? "animation valid" : "writing animation...");
    if (!valid) {
        if (!region_write(h, &erase_ms, &prog_ms)) {
            res_printf("{\"t\":\"error\",\"suite\":\"stream.video\",\"why\":\"write failed or aborted\"}");
            return abort_requested() ? SUITE_ABORT : SUITE_FAIL;
        }
        valid = region_valid(h);
    }
    res_printf("{\"t\":\"video_store\",\"suite\":\"stream.video\",\"valid\":%s,\"rewritten\":%s,\"erase_ms\":%" PRIu32
               ",\"program_ms\":%" PRIu32 ",\"range\":\"F80000-FDAFFF\",\"frames8\":%u,\"frames1\":%u}",
               valid ? "true" : "false", erase_ms || prog_ms ? "true" : "false", erase_ms, prog_ms, (unsigned)NF,
               (unsigned)NF);
    if (!valid) return SUITE_FAIL;

    const uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
    const uint32_t hz0 = w25q_spi_hz();
    lut1_init();
    gp_open();
    uint32_t w25_err = 0;
    suite_verdict_t v = SUITE_PASS;
    for (unsigned i = 0; i < sizeof(k_ph) / sizeof(k_ph[0]); i++) {
        if (k_ph[i].spi3_hz > pclk1 / 2u) continue;
        run_phase(&k_ph[i], secs, pclk1, &w25_err);
        if (abort_requested()) {
            v = SUITE_ABORT;
            break;
        }
    }
    gp_close();
    w25q_set_div((uint16_t)(pclk1 / hz0));
    ui_con_begin("stream.video");
    ui_con_line(w25_err ? COLOR_RED : COLOR_GREEN, "%s w25q err %u", w25_err ? "FAIL" : "PASS", (unsigned)w25_err);
    if (v == SUITE_ABORT) return v;
    return w25_err ? SUITE_FAIL : SUITE_PASS;
}
