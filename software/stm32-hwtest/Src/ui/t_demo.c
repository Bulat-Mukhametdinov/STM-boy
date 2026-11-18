/* demo: real-time scenes on the recommended game pipeline.

   Pipeline: two 160x128 8bpp framebuffers (2 x 20 KB, COLUMN-major: fb[x * 128 + y], so a
   landscape column is the panel's native scan line) + a 256-entry RGB565 palette. present =
   8-column chunks (1024 px) are converted through the palette into two ping-pong DMA buffers
   and streamed to the panel in native scan order (lcd_stream_window) by SPI1 TX DMA at 48 MHz
   (DMA2 S3). The TC interrupt starts the next chunk and converts the one after it, so the whole
   present runs in the background while the main loop renders the next frame into the other
   framebuffer. Arena layout (72 KB): FB0 0, FB1 20480, palette 40960 (512), PP0/PP1 41472
   (2 x 2048), textures 45568 (4 x 4096) -> 61952 B used.

   demo [auto=1] [t=<s per scene, auto>] [scene=0..2] [fps=<cap, 0 = unlimited>]
   Scenes: cubes (flat/Lambert triangles), raycaster (Wolfenstein style), raytracer (80x64, 2x).
   Keys: LEFT/RIGHT scene (cube scene) or X/Y scene (all), A overlay on/off, CENTER fps cap
   0/60/30, B exit. Raycaster/raytracer: joystick walks/turns/looks. */

#include "suites.h"
#include "bench.h"
#include "cmd.h"
#include "font.h"
#include "gamepipe.h"
#include "gfx.h"
#include "input.h"
#include "clock.h"
#include "lcd.h"
#include "oc.h"
#include "log.h"
#include "timebase.h"
#include "ui.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define FW 160
#define FH 128
#define NPX (FW * FH)
#define CH_COLS 8
#define CH_PX (CH_COLS * FH)
#define NCH (FW / CH_COLS)

#define FBUF(i) (g_arena + (uint32_t)(i) * NPX)
#define PAL     ((uint16_t *)(void *)(g_arena + 2u * NPX))
#define PP0     ((uint16_t *)(void *)(g_arena + 2u * NPX + 512u))
#define PP1     ((uint16_t *)(void *)(g_arena + 2u * NPX + 512u + 2048u))
#define TEX     (g_arena + 2u * NPX + 512u + 4096u)   /* 4 x 64x64, column-major */

#define S3_ALL (0x3Du << 22)
#define S3_TC  (1u << 27)

#define IDX_BLACK 254
#define IDX_WHITE 255

static inline float fsqrt(float x)
{
    float r;
    __asm volatile("vsqrt.f32 %0, %1" : "=t"(r) : "t"(x));
    return r;
}

static inline void set_pal(int i, int r, int g, int b)
{
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    PAL[i] = RGB565(r, g, b);
}

/* ---------------------------------------------------------------- present engine */

static const uint8_t *s_pfb;
static volatile uint32_t s_pk, s_pdone;
static bool s_pending;

static inline void conv_chunk(uint32_t k, uint16_t *dst)
{
    const uint32_t *sw = (const uint32_t *)(const void *)(s_pfb + k * CH_PX);
    uint32_t *dw = (uint32_t *)(void *)dst;
    const uint16_t *pal = PAL;
    for (uint32_t i = 0; i < CH_PX / 4u; i++) {
        uint32_t w = sw[i];
        uint32_t p0 = pal[w & 255u], p1 = pal[(w >> 8) & 255u], p2 = pal[(w >> 16) & 255u], p3 = pal[w >> 24];
        dw[2u * i] = p0 | (p1 << 16);
        dw[2u * i + 1u] = p2 | (p3 << 16);
    }
}

volatile uint32_t g_gp_dma_err;   /* DMA2 S3 TEIF/DMEIF seen (game pipeline test) */
volatile uint32_t g_gp_dma_fe;    /* DMA2 S3 FEIF seen */

static inline void s3_count_err(uint32_t lisr)
{
    if (lisr & ((1u << 25) | (1u << 24))) g_gp_dma_err++;
    if (lisr & (1u << 22)) g_gp_dma_fe++;
}

static void present_isr(void)
{
    s3_count_err(DMA2->LISR);
    if (DMA2->LISR & S3_TC) {
        DMA2->LIFCR = S3_ALL;
        uint32_t k = s_pk;
        s_pk = k + 1u;
        if (k + 1u < NCH) {
            uint32_t n = 0;
            while ((DMA2_Stream3->CR & DMA_SxCR_EN) && ++n < 1000u) {
            }
            DMA2_Stream3->M0AR = (uint32_t)(((k + 1u) & 1u) ? PP1 : PP0);
            DMA2_Stream3->NDTR = CH_PX;
            DMA2_Stream3->CR |= DMA_SxCR_EN;
            if (k + 2u < NCH) {
                conv_chunk(k + 2u, (k & 1u) ? PP1 : PP0);
            }
        } else {
            s_pdone = 1u;
        }
    } else {
        DMA2->LIFCR = S3_ALL;
    }
}

static void present_start(const uint8_t *fb)
{
    s_pfb = fb;
    s_pk = 0;
    s_pdone = 0;
    lcd_stream_window(0, 0, FW - 1u, FH - 1u);   /* native: column-major stream follows */
    spi1_dff(true);
    conv_chunk(0, PP0);
    conv_chunk(1, PP1);
    dma3_start(PP0, CH_PX, true, true, true);
    s_pending = true;
}

/* Sleeps (WFI) until the DMA chain is done; returns the cycles spent waiting. */
static uint32_t present_wait(void)
{
    uint32_t t0 = time_cycles();
    if (!s_pending) {
        return 0;
    }
    uint32_t spin = 0;
    while (!s_pdone && ++spin < 1000000u) {
        __disable_irq();
        if (!s_pdone) {
            __WFI();
        }
        __enable_irq();
    }
    spi1_wait_idle();
    SPI1->CR2 &= ~SPI_CR2_TXDMAEN;
    DMA2_Stream3->CR &= ~(DMA_SxCR_EN | DMA_SxCR_TCIE);
    s3_count_err(DMA2->LISR);
    DMA2->LIFCR = S3_ALL;
    lcd_end();
    s_pending = false;
    return time_cycles() - t0;
}

/* ---------------------------------------------------------------- framebuffer helpers */

static void fb_text(uint8_t *fb, int x, int y, const char *s, uint8_t fg, uint8_t bg)
{
    for (; *s; s++, x += 6) {
        const uint8_t *g = font5x7_glyph(*s);
        for (int cx = 0; cx < 6; cx++) {
            int px = x + cx;
            if ((unsigned)px >= FW) continue;
            uint8_t col = (cx < 5) ? g[cx] : 0u;
            uint8_t *d = fb + px * FH + y;
            for (int r = 0; r < 8; r++) {
                d[r] = (col >> r) & 1u ? fg : bg;
            }
        }
    }
}

static void fb_box(uint8_t *fb, int x, int y, int w, int h, uint8_t c)
{
    for (int px = x; px < x + w && px < FW; px++) {
        if (px < 0) continue;
        uint8_t *d = fb + px * FH;
        for (int py = y; py < y + h && py < FH; py++) {
            if (py >= 0) d[py] = c;
        }
    }
}

/* ---------------------------------------------------------------- scene A: cubes */

static void pal_cubes(void)
{
    static const uint8_t hue[6][3] = {{230, 60, 60}, {60, 200, 80}, {70, 110, 240},
                                      {230, 200, 50}, {200, 70, 200}, {60, 200, 210}};
    for (int h = 0; h < 6; h++) {
        for (int s = 0; s < 16; s++) {
            int f = 38 + (s * 218) / 15;   /* 0.15 .. 1.0 of 256 */
            set_pal(h * 16 + s, hue[h][0] * f >> 8, hue[h][1] * f >> 8, hue[h][2] * f >> 8);
        }
    }
    for (int i = 96; i < 128; i++) {
        set_pal(i, 6, 8 + (i - 96) / 3, 24 + (i - 96));
    }
    for (int i = 128; i < 254; i++) set_pal(i, 0, 0, 0);
}

static void tri_fill(uint8_t *fb, int x0, int y0, int x1, int y1, int x2, int y2, uint8_t c)
{
    int t;
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    if (x1 > x2) { t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; }
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    if (x2 < 0 || x0 >= FW) return;
    if (x2 == x0) {
        int lo = y0, hi = y0;
        if (y1 < lo) lo = y1;
        if (y2 < lo) lo = y2;
        if (y1 > hi) hi = y1;
        if (y2 > hi) hi = y2;
        if (lo < 0) lo = 0;
        if (hi >= FH) hi = FH - 1;
        if (hi >= lo && x0 >= 0 && x0 < FW) memset(fb + x0 * FH + lo, c, (size_t)(hi - lo + 1));
        return;
    }
    int xa = x0 < 0 ? 0 : x0, xb = x2 >= FW ? FW - 1 : x2;
    for (int x = xa; x <= xb; x++) {
        int ya = y0 + (y2 - y0) * (x - x0) / (x2 - x0);
        int yb;
        if (x < x1 && x1 > x0) {
            yb = y0 + (y1 - y0) * (x - x0) / (x1 - x0);
        } else if (x2 > x1) {
            yb = y1 + (y2 - y1) * (x - x1) / (x2 - x1);
        } else {
            yb = y1;
        }
        int lo = ya < yb ? ya : yb, hi = ya < yb ? yb : ya;
        if (lo < 0) lo = 0;
        if (hi >= FH) hi = FH - 1;
        if (hi >= lo) memset(fb + x * FH + lo, c, (size_t)(hi - lo + 1));
    }
}

static const int8_t k_cv[8][3] = {{-1, -1, -1}, {1, -1, -1}, {-1, 1, -1}, {1, 1, -1},
                                  {-1, -1, 1}, {1, -1, 1}, {-1, 1, 1}, {1, 1, 1}};
static const struct { int8_t n[3]; uint8_t v[4]; } k_cf[6] = {
    {{1, 0, 0}, {1, 3, 7, 5}},  {{-1, 0, 0}, {0, 4, 6, 2}}, {{0, 1, 0}, {2, 3, 7, 6}},
    {{0, -1, 0}, {0, 4, 5, 1}}, {{0, 0, 1}, {4, 5, 7, 6}},  {{0, 0, -1}, {0, 2, 3, 1}},
};

typedef struct { float x, y, z, s; float a, b, c; uint8_t hue; } cube_t;

static void rot_mat(float m[9], float a, float b, float c)
{
    float sa = sinf(a), ca = cosf(a), sb = sinf(b), cb = cosf(b), sc = sinf(c), cc = cosf(c);
    /* R = Rz(c) * Ry(b) * Rx(a) */
    m[0] = cc * cb;  m[1] = cc * sb * sa - sc * ca;  m[2] = cc * sb * ca + sc * sa;
    m[3] = sc * cb;  m[4] = sc * sb * sa + cc * ca;  m[5] = sc * sb * ca - cc * sa;
    m[6] = -sb;      m[7] = cb * sa;                 m[8] = cb * ca;
}

static void render_cubes(uint8_t *fb, uint32_t t_ms, uint16_t held, bool autop)
{
    (void)held;
    (void)autop;
    float t = (float)t_ms * 0.001f;
    memset(fb, 112, NPX);
    cube_t cb[4];
    cb[0] = (cube_t){0.0f, 0.0f, 7.5f, 1.3f, t * 0.9f, t * 1.3f, t * 0.4f, 0};
    for (int i = 1; i < 4; i++) {
        float an = t * 0.7f + (float)i * 2.0944f;
        cb[i] = (cube_t){3.0f * cosf(an), 1.3f * sinf(an * 1.3f), 8.0f + 2.0f * sinf(an), 0.8f,
                         t * (1.1f + 0.3f * (float)i), t * 0.8f, t * (0.5f + 0.2f * (float)i), (uint8_t)i};
    }
    cb[3].hue = 4;
    /* back to front */
    int ord[4] = {0, 1, 2, 3};
    for (int i = 1; i < 4; i++) {
        int k = ord[i], j = i;
        while (j > 0 && cb[ord[j - 1]].z < cb[k].z) { ord[j] = ord[j - 1]; j--; }
        ord[j] = k;
    }
    static const float L[3] = {-0.40f, 0.60f, -0.69f};
    const float f = 110.0f;
    for (int oi = 0; oi < 4; oi++) {
        const cube_t *c = &cb[ord[oi]];
        float m[9];
        rot_mat(m, c->a, c->b, c->c);
        float vx[8], vy[8], vz[8];
        int sx[8], sy[8];
        for (int i = 0; i < 8; i++) {
            float x = k_cv[i][0] * c->s, y = k_cv[i][1] * c->s, z = k_cv[i][2] * c->s;
            vx[i] = m[0] * x + m[1] * y + m[2] * z + c->x;
            vy[i] = m[3] * x + m[4] * y + m[5] * z + c->y;
            vz[i] = m[6] * x + m[7] * y + m[8] * z + c->z;
            float iz = f / vz[i];
            sx[i] = (int)(80.5f + vx[i] * iz);
            sy[i] = (int)(64.5f - vy[i] * iz);
        }
        for (int fi = 0; fi < 6; fi++) {
            float nx = m[0] * k_cf[fi].n[0] + m[1] * k_cf[fi].n[1] + m[2] * k_cf[fi].n[2];
            float ny = m[3] * k_cf[fi].n[0] + m[4] * k_cf[fi].n[1] + m[5] * k_cf[fi].n[2];
            float nz = m[6] * k_cf[fi].n[0] + m[7] * k_cf[fi].n[1] + m[8] * k_cf[fi].n[2];
            const uint8_t *q = k_cf[fi].v;
            /* a face point relative to the eye: its first vertex; visible when facing the camera */
            if (nx * vx[q[0]] + ny * vy[q[0]] + nz * vz[q[0]] >= 0.0f) continue;   /* backface cull */
            float d = nx * L[0] + ny * L[1] + nz * L[2];
            int s = 2 + (d > 0.0f ? (int)(13.0f * d) : 0);
            if (s > 15) s = 15;
            uint8_t col = (uint8_t)(c->hue * 16 + s);
            tri_fill(fb, sx[q[0]], sy[q[0]], sx[q[1]], sy[q[1]], sx[q[2]], sy[q[2]], col);
            tri_fill(fb, sx[q[0]], sy[q[0]], sx[q[2]], sy[q[2]], sx[q[3]], sy[q[3]], col);
        }
    }
}

/* ---------------------------------------------------------------- scene B: raycaster */

static const char *const k_map[16] = {
    "1111111111111111", "1..............1", "1.2222...3333..1", "1.2......3.....1",
    "1.2..44..3..22.1", "1....4.........1", "1.3..4..111.2..1", "1.3.....1...2..1",
    "1.3.....1......1", "1.......1..444.1", "1.22.......4...1", "1.2....333.4.3.1",
    "1.2....3.....3.1", "1......3..22.3.1", "1..............1", "1111111111111111",
};

static float s_px, s_py, s_dx, s_dy;   /* position, direction; plane = 0.66 * perp(dir) */

static inline int map_at(int x, int y)
{
    if ((unsigned)x >= 16u || (unsigned)y >= 16u) return 1;
    char c = k_map[y][x];
    return c == '.' ? 0 : c - '0';
}

static void gen_textures(void)
{
    for (int tI = 0; tI < 4; tI++) {
        uint8_t *tx = TEX + tI * 4096;
        for (int u = 0; u < 64; u++) {
            for (int v = 0; v < 64; v++) {
                uint32_t h = (uint32_t)(u * 73856093u) ^ (uint32_t)(v * 19349663u);
                h = (h ^ (h >> 13)) * 0x5bd1e995u;
                int n = (int)((h >> 20) & 3u);
                int base;
                switch (tI) {
                case 0: { /* brick: 8 px rows, staggered every other row */
                    int row = v >> 3, off = (row & 1) * 16;
                    if ((v & 7) == 0 || ((u + off) & 31) == 0) base = 0;
                    else base = (n == 0) ? 2 : 1;
                    break;
                }
                case 1: { /* stone blocks 16x16 with a bevel */
                    int lu = u & 15, lv = v & 15;
                    if (lu == 0 || lv == 0) base = 3;
                    else if (lu == 1 || lv == 1) base = 5;
                    else base = (n == 0) ? 5 : 4;
                    break;
                }
                case 2: { /* blue checker tiles with bright border */
                    int lu = u & 15, lv = v & 15;
                    if (lu == 0 || lv == 0) base = 8;
                    else base = (((u >> 4) ^ (v >> 4)) & 1) ? 6 : 7;
                    break;
                }
                default: /* green diagonal stripes */
                    base = (((u + v) / 6) & 1) ? 9 : ((n == 0) ? 11 : 10);
                    break;
                }
                tx[u * 64 + v] = (uint8_t)(base * 8);
            }
        }
    }
}

static void pal_ray(void)
{
    static const uint8_t base[12][3] = {{90, 30, 25}, {170, 60, 45}, {215, 110, 80}, {70, 70, 75},
                                        {120, 120, 125}, {175, 175, 180}, {30, 50, 120}, {50, 90, 190},
                                        {150, 190, 255}, {30, 90, 40}, {60, 150, 70}, {120, 200, 110}};
    for (int b = 0; b < 12; b++) {
        for (int s = 0; s < 8; s++) {
            int f = 256 - (s * 200) / 7;   /* shade 0 = full, 7 = 22 % */
            set_pal(b * 8 + s, base[b][0] * f >> 8, base[b][1] * f >> 8, base[b][2] * f >> 8);
        }
    }
    for (int i = 0; i < 8; i++) {   /* ceiling bands 128..135, floor bands 136..143 */
        set_pal(128 + i, 24 + i * 3, 28 + i * 3, 44 + i * 4);
        set_pal(136 + i, 70 - i * 7, 56 - i * 6, 40 - i * 4);
    }
    for (int i = 144; i < 254; i++) set_pal(i, 0, 0, 0);
}

static void ray_enter(void)
{
    gen_textures();
    s_px = 2.5f; s_py = 2.5f; s_dx = 1.0f; s_dy = 0.0f;
}

static void render_ray(uint8_t *fb, uint32_t t_ms, uint16_t held, bool autop)
{
    static uint32_t last_ms;
    float dt = (float)(t_ms - last_ms) * 0.001f;
    last_ms = t_ms;
    if (dt > 0.1f || dt < 0.0f) dt = 0.03f;
    float mv = 0.0f, turn = 0.0f;
    if (autop) {
        mv = 1.5f * dt;
        turn = 0.35f * dt;
        float ax = s_px + s_dx * 0.7f, ay = s_py + s_dy * 0.7f;
        if (map_at((int)ax, (int)ay)) turn = 2.8f * dt;
    } else {
        if (held & BTN_MASK(BTN_UP)) mv = 2.5f * dt;
        if (held & BTN_MASK(BTN_DOWN)) mv = -2.0f * dt;
        if (held & BTN_MASK(BTN_LEFT)) turn = -2.2f * dt;
        if (held & BTN_MASK(BTN_RIGHT)) turn = 2.2f * dt;
    }
    if (turn != 0.0f) {
        float cs = cosf(turn), sn = sinf(turn);
        float ndx = s_dx * cs - s_dy * sn, ndy = s_dx * sn + s_dy * cs;
        s_dx = ndx; s_dy = ndy;
    }
    if (mv != 0.0f) {
        float nx = s_px + s_dx * mv, ny = s_py + s_dy * mv;
        float mx = mv > 0 ? 0.25f : -0.25f;
        if (!map_at((int)(nx + s_dx * mx), (int)s_py)) s_px = nx;
        if (!map_at((int)s_px, (int)(ny + s_dy * mx))) s_py = ny;
    }
    float plx = -s_dy * 0.66f, ply = s_dx * 0.66f;

    for (int x = 0; x < FW; x++) {
        uint8_t *col = fb + x * FH;
        for (int b = 0; b < 8; b++) {   /* ceiling / floor bands */
            memset(col + b * 8, 128 + b, 8);
            memset(col + 64 + b * 8, 136 + (7 - b), 8);
        }
        float cam = (float)(2 * x) / (float)FW - 1.0f;
        float rdx = s_dx + plx * cam, rdy = s_dy + ply * cam;
        int mx = (int)s_px, my = (int)s_py;
        float ddx = rdx == 0.0f ? 1e30f : fabsf(1.0f / rdx);
        float ddy = rdy == 0.0f ? 1e30f : fabsf(1.0f / rdy);
        int stx, sty;
        float sdx, sdy;
        if (rdx < 0) { stx = -1; sdx = (s_px - (float)mx) * ddx; } else { stx = 1; sdx = ((float)mx + 1.0f - s_px) * ddx; }
        if (rdy < 0) { sty = -1; sdy = (s_py - (float)my) * ddy; } else { sty = 1; sdy = ((float)my + 1.0f - s_py) * ddy; }
        int side = 0, hit = 0;
        for (int i = 0; i < 24 && !hit; i++) {
            if (sdx < sdy) { sdx += ddx; mx += stx; side = 0; }
            else { sdy += ddy; my += sty; side = 1; }
            hit = map_at(mx, my);
        }
        if (!hit) continue;
        float perp = side == 0 ? sdx - ddx : sdy - ddy;
        if (perp < 0.05f) perp = 0.05f;
        int lh = (int)((float)FH / perp);
        int y0 = FH / 2 - lh / 2, y1 = y0 + lh;
        float wx = side == 0 ? s_py + perp * rdy : s_px + perp * rdx;
        wx -= floorf(wx);
        int u = (int)(wx * 64.0f);
        if ((side == 0 && rdx > 0) || (side == 1 && rdy < 0)) u = 63 - u;
        int shade = (int)(perp * 0.7f) + side;
        if (shade > 7) shade = 7;
        const uint8_t *tc = TEX + ((hit - 1) & 3) * 4096 + u * 64;
        uint32_t step = (uint32_t)((64u << 16) / (uint32_t)(lh ? lh : 1));
        uint32_t pos = 0;
        int ys = y0, ye = y1;
        if (ys < 0) { pos = (uint32_t)(-ys) * step; ys = 0; }
        if (ye > FH) ye = FH;
        for (int y = ys; y < ye; y++) {
            col[y] = (uint8_t)(tc[(pos >> 16) & 63u] + shade);
            pos += step;
        }
    }
}

/* ---------------------------------------------------------------- scene C: ray tracer */

typedef struct { float x, y, z, r2; uint8_t base; } sph_t;

static void pal_rt(void)
{
    static const uint8_t base[6][3] = {{235, 235, 225}, {40, 60, 150}, {220, 50, 45}, {50, 190, 80},
                                       {70, 120, 235}, {90, 140, 230}};
    for (int b = 0; b < 6; b++) {
        for (int s = 0; s < 16; s++) {
            int t = s * 256 / 15, r, g, bl;
            if (b == 5) {   /* sky gradient: shade 0 = horizon */
                r = 120 + s * 6; g = 160 + s * 4; bl = 220 + s * 2;
            } else if (t <= 192) {
                r = base[b][0] * t / 192; g = base[b][1] * t / 192; bl = base[b][2] * t / 192;
            } else {
                int w = (t - 192) * 256 / 64;   /* toward white: specular */
                r = base[b][0] + (255 - base[b][0]) * w * 7 / (10 * 256);
                g = base[b][1] + (255 - base[b][1]) * w * 7 / (10 * 256);
                bl = base[b][2] + (255 - base[b][2]) * w * 7 / (10 * 256);
            }
            set_pal(b * 16 + s, r, g, bl);
        }
    }
    for (int i = 96; i < 254; i++) set_pal(i, 0, 0, 0);
}

static float s_yaw, s_pitch;

/* returns nearest hit t > eps for sphere (o, d unit), or -1 */
static inline float sph_hit(const sph_t *s, float ox, float oy, float oz, float dx, float dy, float dz)
{
    float lx = s->x - ox, ly = s->y - oy, lz = s->z - oz;
    float tca = lx * dx + ly * dy + lz * dz;
    float d2 = lx * lx + ly * ly + lz * lz - tca * tca;
    float th2 = s->r2 - d2;
    if (th2 <= 0.0f) return -1.0f;
    float t = tca - fsqrt(th2);
    return t > 0.001f ? t : -1.0f;
}

static void render_rt(uint8_t *fb, uint32_t t_ms, uint16_t held, bool autop)
{
    static uint32_t last_ms;
    float dt = (float)(t_ms - last_ms) * 0.001f;
    last_ms = t_ms;
    if (dt > 0.1f || dt < 0.0f) dt = 0.03f;
    float tt = (float)t_ms * 0.001f;
    if (autop) {
        s_yaw = 0.5f * sinf(tt * 0.5f);
        s_pitch = -0.12f + 0.06f * sinf(tt * 0.9f);
    } else {
        if (held & BTN_MASK(BTN_LEFT)) s_yaw -= 1.2f * dt;
        if (held & BTN_MASK(BTN_RIGHT)) s_yaw += 1.2f * dt;
        if (held & BTN_MASK(BTN_UP)) s_pitch += 0.8f * dt;
        if (held & BTN_MASK(BTN_DOWN)) s_pitch -= 0.8f * dt;
        if (s_pitch > 0.6f) s_pitch = 0.6f;
        if (s_pitch < -0.6f) s_pitch = -0.6f;
    }
    sph_t sp[3] = {
        {-1.7f, -0.1f + 0.35f * sinf(tt * 1.7f), 5.0f, 0.81f, 2},
        {0.2f, -0.45f, 4.0f + 0.9f * sinf(tt * 0.8f), 0.30f, 3},
        {1.9f, 0.05f + 0.3f * sinf(tt * 1.3f + 1.0f), 5.6f, 1.0f, 4},
    };
    float lx = 3.0f * cosf(tt * 0.9f), ly = 4.0f, lz = 2.0f + 3.0f * sinf(tt * 0.9f);
    float cy = cosf(s_yaw), sy = sinf(s_yaw), cp = cosf(s_pitch), spn = sinf(s_pitch);
    /* camera basis: forward, right, up */
    float fx = sy * cp, fy = spn, fz = cy * cp;
    float rx = cy, rz = -sy;
    float ux = -sy * spn, uy = cp, uz = -cy * spn;
    const float ox = 0.0f, oy = 0.8f, oz = -1.0f;
    const float plane_y = -1.0f;
    for (int px = 0; px < 80; px++) {
        float u = ((float)px - 39.5f) * (1.0f / 40.0f);
        for (int py = 0; py < 64; py++) {
            float v = (31.5f - (float)py) * (1.0f / 40.0f);
            float dx = fx + rx * u + ux * v, dy = fy + uy * v, dz = fz + rz * u + uz * v;
            float il = 1.0f / fsqrt(dx * dx + dy * dy + dz * dz);
            dx *= il; dy *= il; dz *= il;
            float tbest = 1e9f;
            int hi = -1;
            for (int i = 0; i < 3; i++) {
                float t = sph_hit(&sp[i], ox, oy, oz, dx, dy, dz);
                if (t > 0.0f && t < tbest) { tbest = t; hi = i; }
            }
            bool plane = false;
            if (dy < -0.0001f) {
                float t = (plane_y - oy) / dy;
                if (t > 0.0f && t < tbest) { tbest = t; plane = true; hi = -1; }
            }
            uint8_t c;
            if (hi < 0 && !plane) {
                int s = (int)(7.0f + 9.0f * dy * 2.5f);
                c = (uint8_t)(5 * 16 + (s < 0 ? 0 : s > 15 ? 15 : s));
            } else {
                float hx = ox + dx * tbest, hy = oy + dy * tbest, hz = oz + dz * tbest;
                float nx, ny, nz;
                int base;
                if (plane) {
                    nx = 0.0f; ny = 1.0f; nz = 0.0f;
                    base = (((int)floorf(hx) + (int)floorf(hz)) & 1) ? 1 : 0;
                } else {
                    nx = hx - sp[hi].x; ny = hy - sp[hi].y; nz = hz - sp[hi].z;
                    float in = 1.0f / fsqrt(nx * nx + ny * ny + nz * nz);
                    nx *= in; ny *= in; nz *= in;
                    base = sp[hi].base;
                }
                float tx = lx - hx, ty = ly - hy, tz = lz - hz;
                float il2 = 1.0f / fsqrt(tx * tx + ty * ty + tz * tz);
                float ldist = 1.0f / il2;
                tx *= il2; ty *= il2; tz *= il2;
                float diff = nx * tx + ny * ty + nz * tz;
                float inten;
                if (diff <= 0.0f) {
                    inten = 0.12f;
                } else {
                    bool sh = false;
                    for (int i = 0; i < 3; i++) {
                        if (i == hi) continue;
                        float t = sph_hit(&sp[i], hx + nx * 0.002f, hy + ny * 0.002f, hz + nz * 0.002f, tx, ty, tz);
                        if (t > 0.0f && t < ldist) { sh = true; break; }
                    }
                    inten = sh ? 0.14f : 0.14f + 0.78f * diff;
                    if (!sh && !plane) {   /* specular, Blinn n.h ^ 16 */
                        float hxv = tx - dx, hyv = ty - dy, hzv = tz - dz;
                        float ih = 1.0f / fsqrt(hxv * hxv + hyv * hyv + hzv * hzv);
                        float nh = (nx * hxv + ny * hyv + nz * hzv) * ih;
                        if (nh > 0.0f) {
                            nh *= nh; nh *= nh; nh *= nh; nh *= nh;
                            inten += 0.45f * nh;
                        }
                    }
                }
                if (plane) {   /* fade far floor into the horizon */
                    float fade = tbest * 0.045f;
                    inten *= fade > 0.7f ? 0.3f : 1.0f - fade;
                }
                int s = (int)(inten * 15.0f);
                c = (uint8_t)(base * 16 + (s < 0 ? 0 : s > 15 ? 15 : s));
            }
            uint16_t c2 = (uint16_t)(c * 0x0101u);
            uint16_t *d0 = (uint16_t *)(void *)(fb + (2 * px) * FH + 2 * py);
            d0[0] = c2;
            d0[FH / 2] = c2;   /* next column: +128 bytes */
        }
    }
}

/* ---------------------------------------------------------------- scenes + driver */

typedef struct {
    const char *name;
    void (*enter)(void);
    void (*render)(uint8_t *fb, uint32_t t_ms, uint16_t held, bool autop);
} scene_t;

static void cubes_enter(void) { pal_cubes(); }
static void ray_enter_all(void) { pal_ray(); ray_enter(); }
static void rt_enter(void) { pal_rt(); s_yaw = 0.0f; s_pitch = -0.1f; }

static const scene_t k_scenes[] = {
    {"cubes", cubes_enter, render_cubes},
    {"raycaster", ray_enter_all, render_ray},
    {"raytracer", rt_enter, render_rt},
};
#define N_SCENES ((int)(sizeof(k_scenes) / sizeof(k_scenes[0])))

typedef struct {
    uint32_t frames;
    uint64_t c_render, c_wait, c_pace, c_frame;
} stats_t;

uint32_t g_demo_fps100, g_demo_frames;

static void emit_stats(const scene_t *sc, const stats_t *st, uint32_t cap)
{
    if (st->frames == 0u) return;
    uint32_t mhz = SystemCoreClock / 1000000u;
    uint32_t cpms = SystemCoreClock / 1000u;
    uint64_t busy_den = st->c_frame ? st->c_frame : 1u;
    uint32_t fps100 = (uint32_t)((uint64_t)st->frames * 100u * SystemCoreClock / busy_den);
    g_demo_fps100 = fps100;
    g_demo_frames = st->frames;
    uint32_t r100 = (uint32_t)(st->c_render * 100u / st->frames / cpms);
    uint32_t w100 = (uint32_t)(st->c_wait * 100u / st->frames / cpms);
    uint32_t f100 = (uint32_t)(st->c_frame * 100u / st->frames / cpms);
    uint32_t cpu100 = (uint32_t)((st->c_frame - st->c_wait - st->c_pace) * 10000u / busy_den);
    char a[16], b[16], c[16], d[16], e[16];
    res_printf("{\"t\":\"bench\",\"suite\":\"demo\",\"name\":\"%s\",\"fps\":%s,\"render_ms\":%s,\"present_ms\":%s,"
               "\"frame_ms\":%s,\"cpu_pct\":%s,\"frames\":%" PRIu32 ",\"sysclk_mhz\":%" PRIu32 ",\"fps_cap\":%" PRIu32
               ",\"spi_mhz\":%" PRIu32 "}",
               sc->name, fmt2(a, sizeof(a), fps100), fmt2(b, sizeof(b), r100), fmt2(c, sizeof(c), w100),
               fmt2(d, sizeof(d), f100), fmt2(e, sizeof(e), cpu100), st->frames, mhz, cap, lcd_spi_hz() / 1000000u);
}

/* Clock list for CENTER+UP/DOWN: fixed profiles plus every frequency the OC sweep marked stable. */
static uint32_t demo_clock_list(uint16_t *out, uint32_t max)
{
    static const uint16_t base[] = {48, 72, 84, 96, 120, 144, 168, 192};
    uint32_t n = 0;
    for (uint32_t i = 0; i < sizeof(base) / sizeof(base[0]); i++) out[n++] = base[i];
    uint16_t st[16];
    uint32_t m = oc_stable_list(st, 16);
    for (uint32_t i = 0; i < m && n < max; i++) {
        if (st[i] > 192u) out[n++] = st[i];
    }
    return n;
}

suite_verdict_t suite_demo(int argc, char **argv)
{
    bool aut = suite_arg_u32(argc, argv, "auto", 0u) != 0u;
    if (!aut && !suite_user_start("demo",
                                  "Real-time demos. LEFT/RIGHT or X/Y scene, A overlay, CENTER fps cap, B exit. "
                                  "A=start B=skip",
                                  argc, argv)) {
        return SUITE_ABORT;
    }
    uint32_t auto_ms = suite_arg_u32(argc, argv, "t", 5u) * 1000u;
    int scene = (int)suite_arg_u32(argc, argv, "scene", 0u) % N_SCENES;
    uint32_t cap_fps = suite_arg_u32(argc, argv, "fps", 0u);

    const uint16_t div0 = lcd_cfg.spi_div;
    const uint32_t speed0 = lcd_gpio_speed();
    const bool f16_0 = lcd_frame16();
    bool nat0 = lcd_set_native(true);
    if (speed0 < 2u) lcd_set_gpio_speed(2);
    lcd_apply_spi_div(clock_lcd_div_for_hz(48000000u));   /* 48 MHz where the bus allows */
    lcd_set_frame16(false);
    lcd_fill_rect(0, 0, FW, FH, COLOR_BLACK);

    g_dma3_hook = present_isr;
    NVIC_SetPriority(DMA2_Stream3_IRQn, 2);
    NVIC_EnableIRQ(DMA2_Stream3_IRQn);
    s_pending = false;
    input_flush();

    suite_verdict_t v = SUITE_PASS;
    int back = 0;
    bool overlay = true, scene_new = true, done = false;
    bool center_used = false;
    uint16_t clk_list[24];
    uint32_t clk_n = demo_clock_list(clk_list, 24);
    int clk_i = 0;
    for (uint32_t i = 0; i < clk_n; i++) {
        if (clk_list[i] == SystemCoreClock / 1000000u) clk_i = (int)i;
    }
    uint32_t cap_modes[3] = {0, 60, 30};
    int cap_i = cap_fps == 60u ? 1 : (cap_fps == 30u ? 2 : 0);
    if (cap_fps != 0u && cap_fps != 60u && cap_fps != 30u) cap_modes[0] = cap_fps;   /* custom value as mode 0 */
    stats_t st = {0}, win = {0};
    uint32_t scene_t0 = 0, run_t0 = HAL_GetTick(), win_t0 = 0;
    char l1[32], l2[32], l3[40], l4[32];
    l1[0] = l2[0] = l3[0] = l4[0] = '\0';
    uint32_t frame_t0 = time_cycles();
    uint32_t us_t0 = time_us();

    while (!done) {
        const scene_t *sc = &k_scenes[scene];
        if (scene_new) {
            present_wait();
            sc->enter();
            PAL[IDX_BLACK] = RGB565(0, 0, 40);   /* overlay box + text colours (indices 254/255 were never set) */
            PAL[IDX_WHITE] = COLOR_WHITE;
            memset(&st, 0, sizeof(st));
            memset(&win, 0, sizeof(win));
            scene_t0 = HAL_GetTick();
            win_t0 = scene_t0;
            scene_new = false;
            frame_t0 = time_cycles();
            us_t0 = time_us();
        }
        uint32_t cap = cap_modes[cap_i];
        uint32_t tnow = HAL_GetTick();
        uint16_t held = aut ? 0u : input_held();
        if (held & BTN_MASK(BTN_CENTER)) held &= (uint16_t)~(BTN_MASK(BTN_UP) | BTN_MASK(BTN_DOWN));

        uint32_t c0 = time_cycles();
        uint8_t *fb = FBUF(back);
        sc->render(fb, tnow - run_t0, held, aut);
        if (overlay) {
            /* dark box (2 px margin) behind the text so it reads on any scene */
            int bw = 0;
            const char *const ls[4] = {l1, l2, l3, l4};
            for (int i = 0; i < 4; i++) {
                int w = (int)strlen(ls[i]) * 6;
                if (w > bw) bw = w;
            }
            fb_box(fb, 0, 0, bw + 4, 4 * 8 + 3, IDX_BLACK);
            for (int i = 0; i < 4; i++) {
                fb_text(fb, 2, 2 + 8 * i, ls[i], IDX_WHITE, IDX_BLACK);
            }
        }
        uint32_t c1 = time_cycles();
        uint32_t cw = present_wait();
        present_start(fb);
        back ^= 1;
        sys_service();
        if (abort_requested()) {
            v = SUITE_ABORT;
            break;
        }
        uint32_t cp = 0;
        if (cap) {
            uint32_t period = 1000000u / cap;
            uint32_t p0 = time_cycles();
            while ((uint32_t)(time_us() - us_t0) < period) {
                sys_service();
            }
            cp = time_cycles() - p0;
        }
        us_t0 = time_us();
        uint32_t c2 = time_cycles();
        uint32_t fcyc = c2 - frame_t0;
        frame_t0 = c2;
        st.frames++; st.c_render += c1 - c0; st.c_wait += cw; st.c_pace += cp; st.c_frame += fcyc;
        win.frames++; win.c_render += c1 - c0; win.c_wait += cw; win.c_pace += cp; win.c_frame += fcyc;

        uint32_t now = HAL_GetTick();
        if (now - win_t0 >= 500u && win.frames) {
            uint32_t cpms = SystemCoreClock / 1000u;
            uint32_t fps10 = (uint32_t)((uint64_t)win.frames * 10u * SystemCoreClock / win.c_frame);
            uint32_t r10 = (uint32_t)(win.c_render * 10u / win.frames / cpms);
            uint32_t w10 = (uint32_t)(win.c_wait * 10u / win.frames / cpms);
            uint32_t cpu = (uint32_t)((win.c_frame - win.c_wait - win.c_pace) * 100u / win.c_frame);
            snprintf(l1, sizeof(l1), "%s %uMHz", sc->name, (unsigned)(SystemCoreClock / 1000000u));
            snprintf(l2, sizeof(l2), "%u.%u fps%s", (unsigned)(fps10 / 10u), (unsigned)(fps10 % 10u),
                     cap ? (cap == 60u ? " cap60" : (cap == 30u ? " cap30" : " cap")) : "");
            snprintf(l3, sizeof(l3), "R%u.%u P%u.%ums", (unsigned)(r10 / 10u), (unsigned)(r10 % 10u),
                     (unsigned)(w10 / 10u), (unsigned)(w10 % 10u));
            snprintf(l4, sizeof(l4), "CPU %u%%", (unsigned)cpu);
            memset(&win, 0, sizeof(win));
            win_t0 = now;
        }

        if (aut) {
            if (now - scene_t0 >= auto_ms) {
                emit_stats(sc, &st, cap);
                st.frames = 0;
                if (suite_arg_u32(argc, argv, "scene", 99u) != 99u || ++scene >= N_SCENES) {
                    done = true;   /* scene=N runs only that one */
                } else {
                    scene_new = true;
                }
            }
            continue;
        }
        input_event_t ev;
        while (input_pop(&ev)) {
            if (!ev.pressed) {
                if (ev.button == BTN_CENTER && !center_used) cap_i = (cap_i + 1) % 3;   /* tap = fps cap */
                continue;
            }
            if (ev.button == BTN_CENTER) {
                center_used = false;
                continue;
            }
            if ((ev.button == BTN_UP || ev.button == BTN_DOWN) && (input_held() & BTN_MASK(BTN_CENTER))) {
                center_used = true;   /* CENTER held + UP/DOWN: next/previous clock profile */
                int ni = clk_i + (ev.button == BTN_UP ? 1 : -1);
                if (ni >= 0 && ni < (int)clk_n) {
                    present_wait();
                    clk_i = ni;
                    (void)clock_set_mhz(clk_list[clk_i], NULL);
                    scene_new = true;
                }
                continue;
            }
            int ns = scene;
            switch (ev.button) {
            case BTN_LEFT:   if (scene == 0) ns = (scene + N_SCENES - 1) % N_SCENES; break;
            case BTN_RIGHT:  if (scene == 0) ns = (scene + 1) % N_SCENES; break;
            case BTN_X:      ns = (scene + 1) % N_SCENES; break;
            case BTN_Y:      ns = (scene + N_SCENES - 1) % N_SCENES; break;
            case BTN_A:      overlay = !overlay; break;
            case BTN_B:      done = true; break;
            default: break;
            }
            if (ns != scene) {
                if (now - scene_t0 > 1000u) emit_stats(sc, &st, cap);
                st.frames = 0;
                scene = ns;
                scene_new = true;
            }
        }
        if (done && st.frames && now - scene_t0 > 1000u) emit_stats(sc, &st, cap);
    }

    present_wait();
    NVIC_DisableIRQ(DMA2_Stream3_IRQn);
    g_dma3_hook = NULL;
    lcd_set_frame16(f16_0);
    if (SystemCoreClock != 96000000u) {
        (void)clock_set_mhz(96u, NULL);   /* the demo clock control always returns to the boot profile */
    }
    lcd_apply_spi_div(div0);
    lcd_set_native(nat0);
    lcd_fill_rect(0, 0, FW, FH, COLOR_BLACK);
    return v;
}

/* ---------------------------------------------------------------- game pipeline API (gamepipe.h) */

static struct {
    uint16_t div;
    uint32_t speed;
    bool f16, nat, open;
} s_gp;

void gp_open(void)
{
    s_gp.div = lcd_cfg.spi_div;
    s_gp.speed = lcd_gpio_speed();
    s_gp.f16 = lcd_frame16();
    s_gp.nat = lcd_set_native(true);
    if (s_gp.speed < 2u) lcd_set_gpio_speed(2);
    lcd_apply_spi_div(clock_lcd_div_for_hz(48000000u));
    lcd_set_frame16(false);
    lcd_fill_rect(0, 0, FW, FH, COLOR_BLACK);
    g_dma3_hook = present_isr;
    NVIC_SetPriority(DMA2_Stream3_IRQn, 2);
    NVIC_EnableIRQ(DMA2_Stream3_IRQn);
    s_pending = false;
    s_gp.open = true;
}

void gp_close(void)
{
    if (!s_gp.open) return;
    present_wait();
    NVIC_DisableIRQ(DMA2_Stream3_IRQn);
    g_dma3_hook = NULL;
    lcd_set_frame16(s_gp.f16);
    lcd_set_gpio_speed(s_gp.speed);
    lcd_apply_spi_div(s_gp.div);
    lcd_set_native(s_gp.nat);
    lcd_fill_rect(0, 0, FW, FH, COLOR_BLACK);
    s_gp.open = false;
}

uint8_t *gp_fb(int i) { return FBUF(i & 1); }
uint16_t *gp_pal(void) { return PAL; }
void gp_start(const uint8_t *fb) { present_start(fb); }
uint32_t gp_wait(void) { return present_wait(); }
void gp_text(uint8_t *fb, int x, int y, const char *s, uint8_t fg, uint8_t bg) { fb_text(fb, x, y, s, fg, bg); }
void gp_box(uint8_t *fb, int x, int y, int w, int h, uint8_t c) { fb_box(fb, x, y, w, h, c); }

void gp_game_enter(void)
{
    pal_ray();
    ray_enter();
    PAL[IDX_BLACK] = RGB565(0, 0, 40);
    PAL[IDX_WHITE] = COLOR_WHITE;
}

void gp_game_render(uint8_t *fb, uint32_t t_ms) { render_ray(fb, t_ms, 0u, true); }
