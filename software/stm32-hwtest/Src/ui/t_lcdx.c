/* display.colmod and input.latency (block C).

   display.colmod: COLMOD 0x03 (12 bit, 3 bytes per 2 px), 0x05 (16 bit), 0x06 (18 bit, 3 bytes per px). A full
   160x128 gradient frame is pushed in native (column-major) order by SPI1 TX DMA (DMA2 S3) at 48 MHz, 8-bit frames;
   the frame time is measured, the mode is labelled on screen. [U]: A next mode, B exit. auto=1: t=<s> per mode.

   input.latency [U]: black screen; every EXTI button edge (TIM2 timestamp at ISR entry) immediately fills the whole
   screen white through DMA. Odd presses stage the window first (CASET/RASET/RAMWR sent while idle), even presses
   send it in the reaction path ("cold"). Arena: 0..61439 frame, 61440..63999 label mask. */

#include "suites.h"
#include "bench.h"
#include "clock.h"
#include "cmd.h"
#include "font.h"
#include "gfx.h"
#include "input.h"
#include "lcd.h"
#include "log.h"
#include "timebase.h"
#include "ui.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define FW 160
#define FH 128
#define NPX (FW * FH)
#define MASK (g_arena + 61440u)    /* 160 columns x 16 bytes, bit y%8 of byte y/8 */

typedef struct {
    uint8_t colmod;
    uint8_t bytes_x2;      /* bytes per 2 px */
    const char *label;
    const char *desc;
} cm_t;

static const cm_t k_cm[3] = {
    {0x03, 3, "03 12b 444", "COLMOD 0x03: 12 bit/px (4-4-4), 3 bytes per 2 px"},
    {0x05, 4, "05 16b 565", "COLMOD 0x05: 16 bit/px (5-6-5), 2 bytes per px"},
    {0x06, 6, "06 18b 666", "COLMOD 0x06: 18 bit/px (6-6-6), 3 bytes per px"},
};

static void mask_clear(void) { memset(MASK, 0, 160u * 16u); }

static void mask_text(int x, int y, const char *s, int scale)
{
    for (; *s; s++, x += 6 * scale) {
        const uint8_t *g = font5x7_glyph(*s);
        for (int cx = 0; cx < 5 * scale; cx++) {
            int px = x + cx;
            if ((unsigned)px >= FW) continue;
            uint8_t col = g[cx / scale];
            for (int r = 0; r < 7 * scale; r++) {
                if (!((col >> (r / scale)) & 1u)) continue;
                int py = y + r;
                if ((unsigned)py < FH) MASK[px * 16 + py / 8] |= (uint8_t)(1u << (py % 8));
            }
        }
    }
}

static inline bool mask_get(int x, int y) { return (MASK[x * 16 + y / 8] >> (y % 8)) & 1u; }

static void pat_rgb(int x, int y, uint8_t *r, uint8_t *g, uint8_t *b)
{
    int ramp = x * 255 / (FW - 1);
    *r = *g = *b = 0;
    if (y < 16) {
        /* label band: black background */
        return;
    }
    if (y < 48) {
        *r = *g = *b = (uint8_t)ramp;
    } else if (y < 64) {
        *r = (uint8_t)ramp;
    } else if (y < 80) {
        *g = (uint8_t)ramp;
    } else if (y < 96) {
        *b = (uint8_t)ramp;
    } else {
        /* colour wheel sweep along x, brightness fades with y */
        int h = x * 6 * 256 / FW;          /* 0..1535 */
        int seg = h >> 8, f = h & 255;
        int up = f, down = 255 - f;
        int rr = 0, gg = 0, bb = 0;
        switch (seg) {
        case 0: rr = 255; gg = up; break;
        case 1: rr = down; gg = 255; break;
        case 2: gg = 255; bb = up; break;
        case 3: gg = down; bb = 255; break;
        case 4: bb = 255; rr = up; break;
        default: rr = 255; bb = down; break;
        }
        int v = 255 - (y - 96) * 120 / 32;   /* 255 .. ~135 */
        *r = (uint8_t)(rr * v / 255);
        *g = (uint8_t)(gg * v / 255);
        *b = (uint8_t)(bb * v / 255);
    }
}

/* Builds the whole frame (native order: for x { for y }) in the given format; returns the byte count. */
static uint32_t build_frame(uint8_t *out, const cm_t *m)
{
    uint8_t *o = out;
    for (int x = 0; x < FW; x++) {
        for (int y = 0; y < FH; y++) {
            uint8_t r, g, b;
            if (mask_get(x, y)) {
                r = g = b = 255;
            } else {
                pat_rgb(x, y, &r, &g, &b);
            }
            if (m->colmod == 0x05) {
                *o++ = (uint8_t)((r & 0xF8u) | (g >> 5));
                *o++ = (uint8_t)(((g & 0x1Cu) << 3) | (b >> 3));
            } else if (m->colmod == 0x06) {
                *o++ = r & 0xFCu;
                *o++ = g & 0xFCu;
                *o++ = b & 0xFCu;
            } else {
                static uint8_t r0, g0, b0;
                if ((y & 1) == 0) {
                    r0 = r >> 4;
                    g0 = g >> 4;
                    b0 = b >> 4;
                } else {
                    *o++ = (uint8_t)((r0 << 4) | g0);
                    *o++ = (uint8_t)((b0 << 4) | (r >> 4));
                    *o++ = (uint8_t)(((g >> 4) << 4) | (b >> 4));
                }
            }
        }
    }
    return (uint32_t)(o - out);
}

static uint32_t push_frame(const uint8_t *buf, uint32_t bytes)
{
    lcd_stream_window(0, 0, FW - 1u, FH - 1u);
    spi1_dff(false);
    uint32_t t0 = time_us();
    dma3_start(buf, bytes, false, true, false);
    dma3_finish();
    lcd_end();
    return time_us() - t0;
}

suite_verdict_t suite_display_colmod(int argc, char **argv)
{
    bool aut = suite_arg_u32(argc, argv, "auto", 0u) != 0u;
    if (!aut && !suite_user_start("display.colmod",
                                  "COLMOD 12/16/18 bit gradient: compare banding. A=next mode, B=exit. A=start B=skip",
                                  argc, argv)) {
        return SUITE_ABORT;
    }
    uint32_t hold_ms = suite_arg_u32(argc, argv, "t", 3u) * 1000u;
    uint32_t step_ms = suite_arg_u32(argc, argv, "step", 120u) * 1000u;

    const uint16_t div0 = lcd_cfg.spi_div;
    const uint32_t speed0 = lcd_gpio_speed();
    const bool f16_0 = lcd_frame16();
    bool nat0 = lcd_set_native(true);
    if (speed0 < 2u) lcd_set_gpio_speed(2);
    lcd_apply_spi_div(clock_lcd_div_for_hz(48000000u));
    lcd_set_frame16(false);
    uint32_t spi_mhz = lcd_spi_hz() / 1000000u;

    if (!aut) {
        static const char *const ans[] = {"A=next mode", "B=exit", NULL};
        ui_prompt_emit("colmod", "Compare the gradients (gray ramp rows 16-47). A=next mode B=exit", ans, step_ms);
    }
    suite_verdict_t v = SUITE_PASS;
    const char *end = "done";
    bool done = false;
    int round = 0;
    input_flush();
    while (!done) {
        for (int mi = 0; mi < 3 && !done; mi++) {
            const cm_t *m = &k_cm[mi];
            mask_clear();
            mask_text(2, 1, m->label, 1);
            mask_text(2, 9, "A next  B exit", 1);
            uint32_t bytes = build_frame(g_arena, m);
            lcd_colmod(m->colmod);
            uint32_t tmin = 0xFFFFFFFFu, tsum = 0;
            for (int rep = 0; rep < 5; rep++) {
                uint32_t us = push_frame(g_arena, bytes);
                if (us < tmin) tmin = us;
                tsum += us;
            }
            if (round == 0) {
                char a[16];
                uint32_t avg = tsum / 5u;
                res_printf("{\"t\":\"colmod\",\"suite\":\"display.colmod\",\"name\":\"%s\",\"colmod\":%u,\"bytes\":%" PRIu32
                           ",\"frame_us_min\":%" PRIu32 ",\"frame_us_avg\":%" PRIu32 ",\"fps_max\":%s,\"mbps\":%" PRIu32
                           ",\"spi_mhz\":%" PRIu32 ",\"bits_per_px\":%u}",
                           m->label, (unsigned)m->colmod, bytes, tmin, avg,
                           fmt2(a, sizeof(a), tmin ? (uint32_t)(100000000ull / tmin) : 0u),
                           tmin ? (uint32_t)((uint64_t)bytes * 8u / tmin) : 0u, spi_mhz,
                           (unsigned)(m->colmod == 3 ? 12 : (m->colmod == 5 ? 16 : 18)));
            }
            uint32_t t0 = HAL_GetTick();
            for (;;) {
                sys_service();
                if (abort_requested()) { v = SUITE_ABORT; end = "abort"; done = true; break; }
                if (aut) {
                    if ((uint32_t)(HAL_GetTick() - t0) >= hold_ms) break;
                    continue;
                }
                input_event_t ev;
                if (input_pop(&ev) && ev.pressed) {
                    if (ev.button == BTN_A) break;
                    if (ev.button == BTN_B) { done = true; break; }
                }
                uint32_t waited = HAL_GetTick() - t0;
                if (waited >= step_ms) { end = "timeout"; done = true; break; }
                ui_answer_t ha;
                if (ui_host_answer_take("colmod", &ha)) {   /* host: answer colmod ok = next, fail = exit */
                    if (ha == UI_ANS_OK) break;
                    done = true;
                    break;
                }
            }
        }
        round++;
        if (aut) done = true;
    }
    if (!aut) ui_answer_emit("colmod", end);
    lcd_colmod(0x05);
    lcd_set_frame16(f16_0);
    lcd_set_gpio_speed(speed0);
    lcd_apply_spi_div(div0);
    lcd_set_native(nat0);
    lcd_fill_rect(0, 0, FW, FH, COLOR_BLACK);
    return v;
}

/* ================================================================ input.latency */

static const uint16_t k_white = 0xFFFFu;

static uint32_t med_u32(uint32_t *a, uint32_t n)
{
    for (uint32_t i = 1; i < n; i++) {
        uint32_t k = a[i], j = i;
        while (j > 0 && a[j - 1] > k) { a[j] = a[j - 1]; j--; }
        a[j] = k;
    }
    return a[n / 2u];
}

static void lat_emit(const char *mode, uint32_t *a_start, uint32_t *a_done, uint32_t n)
{
    if (!n) return;
    uint32_t smin = 0xFFFFFFFFu, smax = 0, dmin = 0xFFFFFFFFu, dmax = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (a_start[i] < smin) smin = a_start[i];
        if (a_start[i] > smax) smax = a_start[i];
        if (a_done[i] < dmin) dmin = a_done[i];
        if (a_done[i] > dmax) dmax = a_done[i];
    }
    uint32_t smed = med_u32(a_start, n), dmed = med_u32(a_done, n);
    res_printf("{\"t\":\"latency_stats\",\"suite\":\"input.latency\",\"mode\":\"%s\",\"n\":%" PRIu32
               ",\"edge_to_dma_us\":{\"min\":%" PRIu32 ",\"med\":%" PRIu32 ",\"max\":%" PRIu32
               "},\"edge_to_done_us\":{\"min\":%" PRIu32 ",\"med\":%" PRIu32 ",\"max\":%" PRIu32 "}}",
               mode, n, smin, smed, smax, dmin, dmed, dmax);
}

suite_verdict_t suite_input_latency(int argc, char **argv)
{
    if (!suite_user_start("input.latency",
                          "Button->pixel latency: screen is black; press any button (10x). Film the screen + the "
                          "button at 240 fps. A=start B=skip",
                          argc, argv)) {
        return SUITE_ABORT;
    }
    uint32_t n_press = suite_arg_u32(argc, argv, "n", 10u);
    if (n_press > 32u) n_press = 32u;
    uint32_t to_ms = suite_arg_u32(argc, argv, "step", 30u) * 1000u;

    const uint16_t div0 = lcd_cfg.spi_div;
    const uint32_t speed0 = lcd_gpio_speed();
    const bool f16_0 = lcd_frame16();
    bool nat0 = lcd_set_native(true);
    if (speed0 < 2u) lcd_set_gpio_speed(2);
    lcd_apply_spi_div(clock_lcd_div_for_hz(48000000u));
    lcd_set_frame16(false);

    static const char *const ans[] = {"press buttons", NULL};
    char txt[64];
    snprintf(txt, sizeof(txt), "Press any button %u times (screen turns white on each press)", (unsigned)n_press);
    ui_prompt_emit("input_latency", txt, ans, to_ms * n_press);

    uint32_t st_pre[32], dn_pre[32], st_cold[32], dn_cold[32];
    uint32_t n_pre = 0, n_cold = 0, done_n = 0;
    const char *end = "done";
    suite_verdict_t v = SUITE_PASS;
    input_exti_enable(true);
    lcd_fill_rect(0, 0, FW, FH, COLOR_BLACK);

    for (uint32_t i = 0; i < n_press; i++) {
        bool cold = (i & 1u) != 0u;
        lcd_fill_rect(0, 0, FW, FH, COLOR_BLACK);
        lcd_fill_rect(0, 0, 4, 4, COLOR_DARKGRAY);   /* armed marker */
        input_edge_t e;
        while (input_edge_pop(&e)) {
        }
        if (!cold) {
            lcd_stream_window(0, 0, FW - 1u, FH - 1u);   /* window staged while idle */
            spi1_dff(true);
        }
        uint32_t t_wait0 = HAL_GetTick(), last_svc = time_us();
        bool got = false;
        for (;;) {
            if (input_edge_pop(&e)) {
                if (e.pressed && e.btn != BTN_POWER) {
                    got = true;
                    break;
                }
                continue;
            }
            if ((uint32_t)(time_us() - last_svc) > 1000u) {
                sys_service();
                last_svc = time_us();
                if (abort_requested()) { end = "abort"; v = SUITE_ABORT; break; }
                if ((uint32_t)(HAL_GetTick() - t_wait0) > to_ms) { end = "timeout"; break; }
            }
        }
        if (!got) {
            if (!cold) lcd_end();
            break;
        }
        uint32_t t_edge = e.t_us;
        if (cold) {
            lcd_stream_window(0, 0, FW - 1u, FH - 1u);
            spi1_dff(true);
        }
        dma3_start(&k_white, NPX, true, false, false);
        uint32_t t_dma = time_us();
        dma3_finish();
        uint32_t t_done = time_us();
        lcd_end();
        uint32_t l_dma = t_dma - t_edge, l_done = t_done - t_edge;
        if (cold) { st_cold[n_cold] = l_dma; dn_cold[n_cold++] = l_done; }
        else { st_pre[n_pre] = l_dma; dn_pre[n_pre++] = l_done; }
        done_n++;
        res_printf("{\"t\":\"latency\",\"suite\":\"input.latency\",\"i\":%" PRIu32 ",\"btn\":\"%s\",\"mode\":\"%s\","
                   "\"edge_to_dma_us\":%" PRIu32 ",\"edge_to_done_us\":%" PRIu32 ",\"dma_us\":%" PRIu32 "}",
                   i + 1u, input_name((button_t)e.btn), cold ? "cold" : "staged", l_dma, l_done, t_done - t_dma);
        /* marker: press counter (after the measurement) */
        gfx_textf(4, 4, COLOR_BLACK, COLOR_WHITE, 2, "%u/%u", (unsigned)(i + 1u), (unsigned)n_press);
        uint32_t t0 = HAL_GetTick();
        while ((uint32_t)(HAL_GetTick() - t0) < 500u) {
            sys_service();
        }
        t0 = HAL_GetTick();
        while (input_held() != 0u && (uint32_t)(HAL_GetTick() - t0) < 5000u) {
            sys_service();
        }
    }
    input_exti_enable(false);
    ui_answer_emit("input_latency", end);

    uint32_t all_s[32], all_d[32], n = 0;
    for (uint32_t i = 0; i < n_pre; i++) { all_s[n] = st_pre[i]; all_d[n++] = dn_pre[i]; }
    for (uint32_t i = 0; i < n_cold; i++) { all_s[n] = st_cold[i]; all_d[n++] = dn_cold[i]; }
    lat_emit("staged", st_pre, dn_pre, n_pre);
    lat_emit("cold", st_cold, dn_cold, n_cold);
    lat_emit("all", all_s, all_d, n);
    res_printf("{\"t\":\"latency_note\",\"suite\":\"input.latency\",\"presses\":%" PRIu32 ",\"note\":\"firmware side only: "
               "total = this + panel refresh phase (up to one panel period, ~16 ms at 60 Hz) + LC response; film the "
               "screen and the button with a 240 fps phone; edge = EXTI ISR entry (TIM2 1 us), dma = DMA2 S3 enabled, "
               "done = last pixel clocked out\"}",
               done_n);

    lcd_set_frame16(f16_0);
    lcd_set_gpio_speed(speed0);
    lcd_apply_spi_div(div0);
    lcd_set_native(nat0);
    lcd_fill_rect(0, 0, FW, FH, COLOR_BLACK);
    ui_con_begin("input.latency");
    if (n) {
        ui_con_line(COLOR_GREEN, "n=%u med edge->dma %u us", (unsigned)n, (unsigned)med_u32(all_s, n));
        ui_con_line(COLOR_GREEN, "med edge->done %u us", (unsigned)med_u32(all_d, n));
    } else {
        ui_con_line(COLOR_YELLOW, "no presses");
    }
    if (v == SUITE_ABORT) return v;
    return done_n >= n_press ? SUITE_PASS : SUITE_ABORT;
}
