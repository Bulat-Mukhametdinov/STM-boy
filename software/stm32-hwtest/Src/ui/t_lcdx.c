/* display.colmod and input.latency (block C).

   display.colmod: COLMOD 0x03 (12 bit, 3 bytes per 2 px), 0x05 (16 bit), 0x06 (18 bit, 3 bytes per px). A full
   160x128 gradient frame is pushed in native (column-major) order by SPI1 TX DMA (DMA2 S3) at 48 MHz, 8-bit frames;
   the frame time is measured, the mode is labelled on screen. [U]: A next mode, B exit. auto=1: t=<s> per mode.

   input.latency [U] [marker=1]: black screen; every EXTI press edge (TIM2 timestamp at ISR entry) immediately toggles
   the whole screen black->white / white->black through DMA (window staged while idle, re-armed right after each
   present, edges queue so nothing is lost). Scope marker: PA8 HIGH for 2 ms at the edge (PA8 = MCU side of R24 3.3k).
   Arena (colmod): 0..61439 frame, 61440..63999 label mask. */

#include "suites.h"
#include "audio.h"
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

/* Scope marker: PA8 (audio pin; amp SD# is LOW and the PWM is off, so it is a plain push-pull GPIO) goes HIGH for
   2 ms at the moment the edge is processed. Probe point: PA8 = MCU side of R24 (3.3k). */
suite_verdict_t suite_input_latency(int argc, char **argv)
{
    if (!suite_user_start("input.latency",
                          "Button->pixel latency: screen toggles black/white on every press (no dead time). Film the "
                          "screen + the button at 240 fps. A=start B=skip",
                          argc, argv)) {
        return SUITE_ABORT;
    }
    uint32_t n_press = suite_arg_u32(argc, argv, "n", 10u);
    if (n_press > 32u) n_press = 32u;
    uint32_t to_ms = suite_arg_u32(argc, argv, "step", 30u) * 1000u;
    bool marker = suite_arg_u32(argc, argv, "marker", 1u) != 0u;

    const uint16_t div0 = lcd_cfg.spi_div;
    const uint32_t speed0 = lcd_gpio_speed();
    const bool f16_0 = lcd_frame16();
    bool nat0 = lcd_set_native(true);
    if (speed0 < 2u) lcd_set_gpio_speed(2);
    lcd_apply_spi_div(clock_lcd_div_for_hz(48000000u));
    lcd_set_frame16(false);

    static const char *const ans[] = {"press buttons", NULL};
    char txt[80];
    snprintf(txt, sizeof(txt), "Press any button %u times (screen toggles black/white on each press)", (unsigned)n_press);
    ui_prompt_emit("input_latency", txt, ans, to_ms * n_press);

    static const uint16_t k_col[2] = {0xFFFFu, 0x0000u};   /* target colour of press parity 0 / 1 */
    static const char *const k_tr[2] = {"black_to_white", "white_to_black"};
    uint32_t st[2][32], dn[2][32], nt[2] = {0, 0}, done_n = 0;
    const char *end = "done";
    suite_verdict_t v = SUITE_PASS;
    input_exti_enable(true);
    lcd_fill_rect(0, 0, FW, FH, COLOR_BLACK);
    input_edge_t e;
    while (input_edge_pop(&e)) {
    }
    lcd_stream_window(0, 0, FW - 1u, FH - 1u);   /* window staged while idle */
    spi1_dff(true);
    uint32_t last_t = 0;
    bool have_last = false;

    for (uint32_t i = 0; i < n_press; i++) {
        uint32_t par = i & 1u;
        uint32_t t_wait0 = HAL_GetTick(), last_svc = time_us();
        bool got = false;
        for (;;) {
            if (input_edge_pop(&e)) {
                /* releases are ignored; presses closer than 5 ms to the last accepted one are contact bounce */
                if (e.pressed && e.btn != BTN_POWER && (!have_last || (uint32_t)(e.t_us - last_t) > 5000u)) {
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
            lcd_end();
            break;
        }
        uint32_t t_edge = e.t_us;
        last_t = t_edge;
        have_last = true;
        uint32_t t_mark = time_us();
        if (marker) audio_pa8_mode(AUD_PA8_HIGH);
        dma3_start(&k_col[par], NPX, true, false, false);
        uint32_t t_dma = time_us();
        if (marker) {
            while ((uint32_t)(time_us() - t_mark) < 2000u) {
            }
            audio_pa8_mode(AUD_PA8_HIZ);   /* restore: PA8 back to its reset (input) state */
        }
        dma3_finish();
        uint32_t t_done = time_us();
        lcd_end();
        lcd_stream_window(0, 0, FW - 1u, FH - 1u);   /* re-arm at once for the next edge (edges are queued) */
        spi1_dff(true);
        uint32_t l_dma = t_dma - t_edge, l_done = t_done - t_edge;
        st[par][nt[par]] = l_dma;
        dn[par][nt[par]++] = l_done;
        done_n++;
        res_printf("{\"t\":\"latency\",\"suite\":\"input.latency\",\"i\":%" PRIu32 ",\"btn\":\"%s\",\"mode\":\"%s\","
                   "\"edge_to_dma_us\":%" PRIu32 ",\"edge_to_done_us\":%" PRIu32 ",\"dma_us\":%" PRIu32 "}",
                   i + 1u, input_name((button_t)e.btn), k_tr[par], l_dma, l_done, t_done - t_dma);
    }
    lcd_end();
    input_exti_enable(false);
    ui_answer_emit("input_latency", end);

    uint32_t all_s[32], all_d[32], n = 0;
    for (uint32_t k = 0; k < 2u; k++) {
        for (uint32_t i = 0; i < nt[k]; i++) { all_s[n] = st[k][i]; all_d[n++] = dn[k][i]; }
    }
    lat_emit(k_tr[0], st[0], dn[0], nt[0]);
    lat_emit(k_tr[1], st[1], dn[1], nt[1]);
    lat_emit("all", all_s, all_d, n);
    res_printf("{\"t\":\"latency_note\",\"suite\":\"input.latency\",\"presses\":%" PRIu32 ",\"marker\":%s,\"note\":\"firmware "
               "side only: total = this + panel refresh phase (up to one panel period, ~16 ms at 60 Hz) + LC response; "
               "film the screen and the button with a 240 fps phone; edge = EXTI ISR entry (TIM2 1 us), dma = DMA2 S3 "
               "enabled, done = last pixel clocked out. Scope marker: PA8 = MCU side of R24 (3.3k) goes HIGH for 2 ms "
               "when the edge is processed (marker=0 disables)\"}",
               done_n, marker ? "true" : "false");

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
