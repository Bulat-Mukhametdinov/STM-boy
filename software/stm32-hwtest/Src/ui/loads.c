/* Load generators for power.current / battlog. */

#include "loads.h"
#include "audio.h"
#include "backlight.h"
#include "bench.h"
#include "board.h"
#include "font.h"
#include "lcd.h"
#include "timebase.h"
#include "w25q.h"
#include "stm32f4xx_hal.h"

#define S3_TC (1u << 27)

static const load_def_t s_defs[] = {
    {2, "S2 WFI idle", "mA", true, false},
    {3, "S3 BL 10%", "mA", false, false},
    {4, "S4 BL 25%", "mA", false, false},
    {5, "S5 BL 50%", "mA", false, false},
    {6, "S6 BL 75%", "mA", false, false},
    {7, "S7 BL 100%", "mA", false, false},
    {8, "S8 CPU max loop", "mA", true, false},
    {9, "S9 LCD DMA max fps", "mA", false, true},
    {10, "S10 amp on, silent", "mA", false, false},
    {12, "S12 max: BL100+LCD+CPU", "mA", false, true},  /* battlog ls=12 (fast discharge); not in g_load_order */
    {11, "S11 amp 1kHz -6dBFS", "mA", false, false},
    {13, "S13 W25Q cont read", "mA", true, false},
    {14, "S14 W25Q erase/prog", "mA", true, false},
    {15, "S15 game mix", "mA", false, true},
};

const uint8_t g_load_order[] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 14, 15, 0};

const load_def_t *load_def(int id)
{
    for (unsigned i = 0; i < sizeof(s_defs) / sizeof(s_defs[0]); i++) {
        if (s_defs[i].id == id) {
            return &s_defs[i];
        }
    }
    return NULL;
}

static int s_id;
static bool s_dma_busy;
static uint32_t s_next_us;
static uint32_t s_addr;
static uint32_t s_sector;
static uint32_t s_rng = 0x1234567u;
static uint32_t s_sink;

#define FB      (g_arena)                 /* 160x128 RGB565 */
#define RDBUF   (g_arena + 40960)         /* 4 KB */
#define FB_MAX  (160u * 128u)
#define S14_BASE 0xF80000u                /* scratch, outside the battlog + handshake areas */
#define S14_SECTORS 16u

static inline uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

static void burn_us(uint32_t us)
{
    uint32_t t0 = time_us();
    uint32_t x = s_rng;
    while ((uint32_t)(time_us() - t0) < us) {
        for (int k = 0; k < 64; k++) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            s_sink += x * 2654435761u;
        }
    }
    s_rng = x;
}

static uint32_t frame_px(void) { return (uint32_t)lcd_width() * lcd_height(); }

static void frame_start(void)
{
    lcd_stream_window(0, 0, (uint16_t)(lcd_width() - 1u), (uint16_t)(lcd_height() - 1u));
    spi1_dff(true);
    dma3_start(FB, frame_px(), true, true, false);
    s_dma_busy = true;
}

static void frame_finish(void)
{
    if (s_dma_busy) {
        dma3_finish();
        lcd_end();
        s_dma_busy = false;
    }
}

/* Frame-buffer index of landscape pixel (x, y): frames are streamed with lcd_stream_window(), which
   expects column-major data in native scan mode and row-major otherwise. */
static uint32_t fb_idx(uint32_t x, uint32_t y)
{
    return lcd_native() ? x * lcd_height() + y : y * lcd_width() + x;
}

void load_progress(uint32_t px, uint32_t of)
{
    uint32_t w = lcd_width();
    uint32_t fill = of ? (px * w) / of : 0;
    for (uint32_t y = 0; y < 6u; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint32_t i = fb_idx(x, y);
            FB[i * 2u] = (x < fill) ? 0xE0 : 0x08;
            FB[i * 2u + 1u] = (x < fill) ? 0x07 : 0x42;
        }
    }
}

void load_fb_text(int16_t y, const char *str, uint16_t fg)
{
    uint16_t *px = (uint16_t *)(void *)FB;
    uint32_t w = lcd_width(), h = lcd_height();
    if (y < 0 || (uint32_t)y + 8u > h) {
        return;
    }
    for (uint32_t r = 0; r < 8u; r++) {
        for (uint32_t x = 0; x < w; x++) {
            px[fb_idx(x, (uint32_t)y + r)] = 0;
        }
    }
    uint32_t cx = 2;
    for (; *str && cx + 6u <= w; str++, cx += 6u) {
        const uint8_t *g = font5x7_glyph(*str);
        for (uint32_t c = 0; c < 5u; c++) {
            for (uint32_t r = 0; r < 7u; r++) {
                if ((g[c] >> r) & 1u) {
                    px[fb_idx(cx + c, (uint32_t)y + r)] = fg;
                }
            }
        }
    }
}

void load_begin(int id)
{
    s_id = id;
    s_dma_busy = false;
    s_addr = 0;
    s_sector = 0;
    const load_def_t *d = load_def(id);
    if (!d) {
        return;
    }
    static const uint32_t pct[] = {10, 25, 50, 75, 100};
    if (id >= 3 && id <= 7) {
        backlight_set_ppm(pct[id - 3] * 10000u);
    } else if (id == 12) {
        backlight_full();
    } else if (id == 9 || id == 15) {
        backlight_set_ppm(500000u);
    } else {
        backlight_off();
    }
    if (d->owns_lcd) {
        uint32_t *p = (uint32_t *)(void *)FB;
        for (uint32_t i = 0; i < FB_MAX / 2u; i++) {
            p[i] = rnd();
        }
        load_progress(0, 1);
        s_next_us = time_us();
    }
    if (id == 14) {
        for (uint32_t i = 0; i < 4096u; i++) {
            RDBUF[i] = (uint8_t)(i * 7u + 3u);
        }
    }
    if (id == 10 || id == 11) {                     /* amp states: backlight off (above) */
        audio_init();
        if (id == 10) {
            audio_pwm_start(1, true);               /* 375 kHz / 8-bit carrier, 50 % = silence */
        } else {
            audio_engine_start(AUD_ENG_B, 22050u, AUD_DITHER_OFF);
            audio_voice_tone(0, AUD_GEN_SINE, 1000.0f, -6.0f);
        }
        audio_amp_on(20);
    }
}

void load_step(void)
{
    switch (s_id) {
    case 8:
        burn_us(1000);
        break;
    case 9:
        if (!s_dma_busy) {
            frame_start();
        } else if (DMA2->LISR & S3_TC) {
            frame_finish();
        } else {
            __WFI();
        }
        break;
    case 12:                                        /* like S9, but burn CPU instead of WFI */
        if (!s_dma_busy) {
            frame_start();
        } else if (DMA2->LISR & S3_TC) {
            frame_finish();
        } else {
            burn_us(200);
        }
        break;
    case 13:
        w25q_read_ex(s_addr, RDBUF, 4096, true);
        s_addr = (s_addr + 4096u) & 0x1FFFFFu;
        break;
    case 14: {
        uint32_t base = S14_BASE + s_sector * 4096u;
        w25q_erase_sector(base);
        for (uint32_t p = 0; p < 16u; p++) {
            w25q_program(base + p * 256u, RDBUF + p * 256u, 256);
        }
        s_sector = (s_sector + 1u) % S14_SECTORS;
        break;
    }
    case 15: {
        uint32_t now = time_us();
        if ((int32_t)(now - s_next_us) >= 0) {
            if ((int32_t)(now - s_next_us) > 100000) {
                s_next_us = now;
            }
            s_next_us += 33333u;                    /* 30 fps */
            frame_finish();
            frame_start();
            burn_us(16000);                         /* ~50 % CPU while the frame streams */
            w25q_read_ex(s_addr, RDBUF, 2048, true);
            s_addr = (s_addr + 2048u) & 0x1FFFFFu;
        } else {
            __WFI();
        }
        break;
    }
    default:
        __WFI();                                    /* S2..S7: idle, woken by SysTick */
        break;
    }
}

void load_end(void)
{
    if (s_id == 10 || s_id == 11) {
        audio_amp_off(20);
        audio_shutdown();                           /* engine off, PA8 input, AMP_SD low */
    }
    frame_finish();
    s_id = 0;
    backlight_full();
}
