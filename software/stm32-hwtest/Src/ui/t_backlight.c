/* Backlight suites (P1).
   backlight       (auto): TIM3_CH1 register check + PA6 AF2 check, and the
                   real PWM frequency / duty measured by sampling PA6 IDR
                   (the pin is driven push-pull into 1k + NPN base, so the
                   input register follows the output) for 200 Hz, 1 kHz,
                   5 kHz, 25 kHz at 50 % and 10 %.
   backlight.user  [U]: per frequency the lowest usable level (UP/DOWN, A)
                   and a flicker/whine verdict at that level. */

#include "suites.h"
#include "backlight.h"
#include "board.h"
#include "cmd.h"
#include "gfx.h"
#include "hwcal.h"
#include "input.h"
#include "lcd.h"
#include "log.h"
#include "timebase.h"
#include "ui.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const uint32_t s_freqs[4] = {200u, 1000u, 5000u, 25000u};

typedef struct {
    uint32_t hz_meas;     /* rising edges / window */
    uint32_t duty_ppm;    /* high samples / samples */
    uint32_t samples;
} pwm_meas_t;

/* Samples PA6 for `ms` milliseconds (IRQs stay enabled; a USB interrupt only
   delays sampling, it cannot hide a 20 us pulse). */
static void measure_pwm(uint32_t ms, pwm_meas_t *m)
{
    uint32_t t0 = time_us(), win = ms * 1000u;
    uint32_t n = 0, hi = 0, rises = 0;
    uint32_t prev = (GPIOA->IDR >> LCD_BLK_PIN) & 1u;
    uint32_t first_rise = 0, last_rise = 0;
    for (;;) {
        uint32_t now = time_us();
        if (now - t0 >= win) break;
        uint32_t lv = (GPIOA->IDR >> LCD_BLK_PIN) & 1u;
        n++;
        hi += lv;
        if (lv && !prev) {
            if (rises == 0) first_rise = now;
            last_rise = now;
            rises++;
        }
        prev = lv;
    }
    m->samples = n;
    m->duty_ppm = n ? (uint32_t)(((uint64_t)hi * 1000000u) / n) : 0u;
    /* frequency from the span between the first and the last rising edge */
    m->hz_meas = (rises >= 2u && last_rise != first_rise)
                     ? (uint32_t)(((uint64_t)(rises - 1u) * 1000000u) / (last_rise - first_rise))
                     : 0u;
}

static uint32_t absdiff(uint32_t a, uint32_t b) { return a > b ? a - b : b - a; }

suite_verdict_t suite_backlight(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    bool all = true;
    backlight_regs_t r;

    for (int f = 0; f < 4; f++) {
        backlight_set_freq(s_freqs[f]);
        static const uint32_t duties[2] = {500000u, 100000u};
        for (int d = 0; d < 2; d++) {
            backlight_set_ppm(duties[d]);
            delay_us(2000000u / s_freqs[f] + 2000u); /* CCR1 is preloaded: wait > 1 period */
            backlight_regs(&r);
            pwm_meas_t m;
            measure_pwm(50u, &m);
            uint32_t hz_set = backlight_freq_hz();
            bool regs_ok = (r.cr1 & TIM_CR1_CEN) && (((r.ccmr1 >> TIM_CCMR1_OC1M_Pos) & 7u) == 6u) &&
                           (r.ccer & TIM_CCER_CC1E) && r.moder6 == GPIO_MODE_AF_ && r.afr6 == 2u;
            bool hz_ok = absdiff(hz_set, s_freqs[f]) * 100u <= s_freqs[f];               /* 1 % */
            bool meas_ok = absdiff(m.hz_meas, hz_set) * 50u <= hz_set &&                   /* 2 % */
                           absdiff(m.duty_ppm, duties[d]) <= 20000u;                       /* 2 pp */
            bool pass = regs_ok && hz_ok && meas_ok;
            all = all && pass;
            res_printf("{\"t\":\"test\",\"suite\":\"backlight\",\"name\":\"pwm_%" PRIu32 "hz_%" PRIu32 "pct\","
                       "\"pass\":%s,\"regs_ok\":%s,\"hz_req\":%" PRIu32 ",\"hz_set\":%" PRIu32 ",\"hz_meas\":%" PRIu32
                       ",\"duty_req_ppm\":%" PRIu32 ",\"duty_meas_ppm\":%" PRIu32 ",\"samples\":%" PRIu32
                       ",\"psc\":%" PRIu32 ",\"arr\":%" PRIu32 ",\"ccr1\":%" PRIu32 ",\"timclk\":%" PRIu32
                       ",\"pa6_mode\":%" PRIu32 ",\"pa6_af\":%" PRIu32 "}",
                       s_freqs[f], duties[d] / 10000u, pass ? "true" : "false", regs_ok ? "true" : "false",
                       s_freqs[f], hz_set, m.hz_meas, duties[d], m.duty_ppm, m.samples, r.psc, r.arr, r.ccr1,
                       r.timclk, r.moder6, r.afr6);
            ui_con_line(pass ? COLOR_GREEN : COLOR_RED, "%5" PRIu32 "Hz %2" PRIu32 "%%: %" PRIu32 "Hz %" PRIu32 ".%" PRIu32 "%%",
                        s_freqs[f], duties[d] / 10000u, m.hz_meas, m.duty_ppm / 10000u, (m.duty_ppm / 1000u) % 10u);
            sys_service();
            if (abort_requested()) {
                backlight_set_freq(g_hwcal.bl_pwm_hz >= 50u ? g_hwcal.bl_pwm_hz : 1000u);
                backlight_full();
                return SUITE_ABORT;
            }
        }
    }
    /* full on / off levels */
    backlight_off();
    delay_us(2000);
    pwm_meas_t m0, m1;
    measure_pwm(10u, &m0);
    backlight_full();
    delay_us(2000);
    measure_pwm(10u, &m1);
    bool ends = (m0.duty_ppm == 0u) && (m1.duty_ppm == 1000000u);
    all = all && ends;
    res_printf("{\"t\":\"test\",\"suite\":\"backlight\",\"name\":\"off_full\",\"pass\":%s,\"off_duty_ppm\":%" PRIu32
               ",\"full_duty_ppm\":%" PRIu32 "}",
               ends ? "true" : "false", m0.duty_ppm, m1.duty_ppm);
    backlight_set_freq(g_hwcal.bl_pwm_hz >= 50u ? g_hwcal.bl_pwm_hz : 1000u);
    backlight_full();
    return all ? SUITE_PASS : SUITE_FAIL;
}

/* ================================================================ backlight.user */

static void bl_screen(uint32_t hz, uint8_t level, const char *line1, const char *line2)
{
    int16_t w = (int16_t)lcd_width(), h = (int16_t)lcd_height();
    gfx_fill(COLOR_BLACK);
    /* readable reference content: white text, grey ramp, colour bars */
    for (int i = 0; i < 8; i++) {
        uint8_t v = (uint8_t)(i * 255 / 7);
        lcd_fill_rect((int16_t)(i * w / 8), 70, (int16_t)(w / 8 + 1), 16, RGB565(v, v, v));
    }
    gfx_textf(2, 4, COLOR_YELLOW, COLOR_BLACK, 2, "%" PRIu32 " Hz", hz);
    gfx_textf(2, 24, COLOR_WHITE, COLOR_BLACK, 2, "level %u/15", (unsigned)level);
    uint32_t ppm = backlight_level_ppm(level);
    gfx_textf(2, 44, COLOR_GRAY, COLOR_BLACK, 1, "duty %" PRIu32 ".%02" PRIu32 " %%", ppm / 10000u,
              (ppm / 100u) % 100u);
    gfx_text(2, 56, "The quick brown fox", COLOR_WHITE, COLOR_BLACK, 1);
    gfx_text(2, (int16_t)(h - 30), line1, COLOR_CYAN, COLOR_BLACK, 1);
    gfx_text(2, (int16_t)(h - 20), line2, COLOR_CYAN, COLOR_BLACK, 1);
}

/* Returns button, -1 timeout, -2 abort (host answers map to A/B). */
static int bl_wait(const char *id, uint32_t to)
{
    uint32_t t0 = HAL_GetTick();
    for (;;) {
        sys_service();
        input_event_t ev;
        if (input_pop(&ev) && ev.pressed && ev.button != BTN_POWER) return ev.button;
        ui_answer_t ha;
        if (ui_host_answer_take(id, &ha)) return ha == UI_ANS_OK ? BTN_A : BTN_B;
        if (abort_requested()) return -2;
        if (to && HAL_GetTick() - t0 >= to) return -1;
    }
}

/* backlight.user [t=] [step=<idle s>]
   For 200 Hz, 1 kHz, 5 kHz, 25 kHz:
     1) UP/DOWN change the level (0..15, perceptual); A = this is the lowest
        level at which the screen is still usable; B = skip this frequency.
     2) At that level: A = no visible flicker and no audible whine,
        B = flicker or whine. */
suite_verdict_t suite_backlight_user(int argc, char **argv)
{
    if (!suite_user_start("backlight.user",
                          "Backlight: for 4 PWM frequencies find the lowest usable level (UP/DOWN, A) "
                          "and judge flicker/whine (A=none, B=yes). A=start B=skip",
                          argc, argv)) {
        return SUITE_ABORT;
    }
    uint32_t to = suite_arg_u32(argc, argv, "step", 120u) * 1000u;
    bool stop = false;
    uint8_t min_lvl[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    int8_t clean[4] = {-1, -1, -1, -1};

    for (int f = 0; f < 4 && !stop; f++) {
        uint32_t hz = s_freqs[f];
        backlight_set_freq(hz);
        uint8_t lvl = 3;
        char id[24];
        snprintf(id, sizeof(id), "bl_min_%" PRIu32, hz);
        char txt[80];
        snprintf(txt, sizeof(txt), "%" PRIu32 " Hz: UP/DOWN to the lowest still-usable level, then A", hz);
        static const char *const a1[] = {"UP/DOWN=level", "A=lowest usable", "B=skip", NULL};
        ui_prompt_emit(id, txt, a1, to);
        const char *end = "abort";
        for (;;) {
            backlight_set_level(lvl);
            bl_screen(hz, lvl, "UP/DN level, A = lowest", "usable level, B = skip");
            int b = bl_wait(id, to);
            if (b == BTN_UP && lvl < 15u) { lvl++; continue; }
            if (b == BTN_DOWN && lvl > 0u) { lvl--; continue; }
            if (b == BTN_A) { end = "ok"; break; }
            if (b == BTN_B) { end = "fail"; break; }
            if (b == -1) { end = "timeout"; break; }
            if (b == -2) { end = "abort"; break; }
        }
        ui_answer_emit(id, end);
        if (!strcmp(end, "timeout") || !strcmp(end, "abort")) { stop = true; break; }
        if (strcmp(end, "ok") != 0) continue;
        min_lvl[f] = lvl;

        snprintf(id, sizeof(id), "bl_flicker_%" PRIu32, hz);
        snprintf(txt, sizeof(txt), "%" PRIu32 " Hz level %u: A = no flicker and no whine, B = flicker or whine",
                 hz, (unsigned)lvl);
        static const char *const a2[] = {"A=clean", "B=flicker/whine", NULL};
        ui_prompt_emit(id, txt, a2, to);
        bl_screen(hz, lvl, "A = no flicker, no whine", "B = flicker or whine");
        int b;
        do {
            b = bl_wait(id, to);
        } while (b >= 0 && b != BTN_A && b != BTN_B);
        const char *e2 = (b == BTN_A) ? "ok" : (b == BTN_B) ? "fail" : (b == -1) ? "timeout" : "abort";
        ui_answer_emit(id, e2);
        if (b < 0) { stop = true; }
        else clean[f] = (b == BTN_A) ? 1 : 0;
        res_printf("{\"t\":\"backlight\",\"suite\":\"backlight.user\",\"name\":\"%" PRIu32 "hz\",\"min_level\":%u,"
                   "\"min_duty_ppm\":%" PRIu32 ",\"clean\":%s}",
                   hz, (unsigned)lvl, backlight_level_ppm(lvl), clean[f] < 0 ? "null" : (clean[f] ? "true" : "false"));
    }
    /* recommendation: lowest frequency >= 1 kHz judged clean (fewest switching
       edges, no whine), else 25 kHz if clean, else none */
    int rec = -1;
    for (int f = 1; f < 4 && rec < 0; f++) {
        if (clean[f] == 1) rec = f;
    }
    if (rec >= 0) {
        g_hwcal.bl_pwm_hz = (uint16_t)s_freqs[rec];
        g_hwcal.bl_min_level = min_lvl[rec];
        res_printf("{\"t\":\"cal\",\"suite\":\"backlight.user\",\"name\":\"backlight\",\"pass\":true,"
                   "\"bl_pwm_hz\":%" PRIu32 ",\"bl_min_level\":%u,\"saved_to\":\"ram\"}",
                   s_freqs[rec], (unsigned)min_lvl[rec]);
    }
    backlight_set_freq(g_hwcal.bl_pwm_hz >= 50u ? g_hwcal.bl_pwm_hz : 1000u);
    backlight_full();
    if (stop) return SUITE_ABORT;
    return rec >= 0 ? SUITE_PASS : SUITE_FAIL;
}
