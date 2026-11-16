/* power.current [U]: guided current measurement per load state (meter in
   series with the battery lead). Results -> g_hwcal.i_state_ma[] (RAM) + @R. */

#include "pwrtest.h"
#include "suites.h"
#include "adc.h"
#include "backlight.h"
#include "cmd.h"
#include "gfx.h"
#include "hwcal.h"
#include "input.h"
#include "lcd.h"
#include "loads.h"
#include "log.h"
#include "lowpower.h"
#include "power.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static void emit_reading(unsigned id, const char *name, uint32_t x10, const char *stage, uint32_t n_s,
                         uint32_t vloaded, int temp)
{
    res_printf("{\"t\":\"current\",\"suite\":\"power.current\",\"name\":\"S%u\",\"label\":\"%s\",\"stage\":\"%s\","
               "\"ma_x10\":%" PRIu32 ",\"i_state_ma\":%u,\"n_s\":%" PRIu32 ",\"vbat_loaded_mv\":%" PRIu32
               ",\"temp_dC\":%d,\"saved_to\":\"ram\"}",
               id, name, stage, x10, g_hwcal.i_state_ma[id], n_s, vloaded, temp);
}

static void draw_state(unsigned id, const char *name, const char *range, bool dark, const char *extra)
{
    gfx_fill(COLOR_BLACK);
    ui_status_bar();
    char b[8];
    snprintf(b, sizeof(b), "S%u", id);
    gfx_text(4, 14, b, COLOR_CYAN, COLOR_BLACK, 3);
    gfx_text(4, 42, name, COLOR_WHITE, COLOR_BLACK, 1);
    gfx_textf(4, 54, COLOR_YELLOW, COLOR_BLACK, 1, "meter range: %s%s", range, dark ? "  (screen DARK)" : "");
    if (extra) {
        gfx_text_wrap(4, 70, (int16_t)(lcd_width() - 8), extra, COLOR_GRAY, COLOR_BLACK, 1);
    }
}

static ui_answer_t read_entry(unsigned id, const char *title, uint32_t step_s, int32_t *x10)
{
    char idb[24];
    snprintf(idb, sizeof(idb), "cur_S%u_read", id);
    int32_t v[2] = {(int32_t)pwr_state_x10(id), 0};
    g_cur_wait_state = (int)id;
    g_cur_host.pending = false;
    ui_answer_t a = pwr_num_entry(idb, title, "meter reading (mA)", NULL, 1, v, 1, 10, 0, 9999, 10, 1, NULL,
                                  &g_cur_host, step_s * 1000u);
    g_cur_wait_state = 0;
    *x10 = v[0];
    return a;
}

static bool s_entry = true;                        /* suite arg entry=0 skips the meter entry */

/* 0 = reading stored, 1 = skipped, 2 = abort */
static int do_state(unsigned id, uint32_t n_s, uint32_t step_s)
{
    static const char *const start_ans[] = {"A=start", "B=skip", NULL};
    char idb[24], txt[150];
    uint32_t vloaded = 0;
    int temp = 0;

    if (id == 16u) {
        static const char *const stage[] = {"CC", "early CV", "near termination"};
        snprintf(idb, sizeof(idb), "cur_S16_start");
        draw_state(16, "charge current", "mA (USB in)", false,
                   "Connect USB. Meter in series with the battery. Read CC, early CV, near termination.");
        ui_prompt_emit(idb, "S16 charge current: USB in, read the meter in 3 phases", start_ans, step_s * 1000u);
        ui_answer_t a;
        {
            uint32_t t0 = HAL_GetTick();
            for (;;) {
                sys_service();
                input_event_t ev;
                if (input_pop(&ev) && ev.pressed && (ev.button == BTN_A || ev.button == BTN_B)) {
                    a = ev.button == BTN_A ? UI_ANS_OK : UI_ANS_FAIL;
                    break;
                }
                if (ui_host_answer_take(idb, &a)) break;
                if (abort_requested()) { a = UI_ANS_ABORT; break; }
                if ((uint32_t)(HAL_GetTick() - t0) > step_s * 1000u) { a = UI_ANS_TIMEOUT; break; }
            }
        }
        ui_answer_emit(idb, ui_answer_str(a));
        if (a == UI_ANS_ABORT) return 2;
        if (a != UI_ANS_OK) return 1;
        for (unsigned k = 0; k < 3; k++) {
            int32_t x10;
            snprintf(txt, sizeof(txt), "S16 %s current (mA)", stage[k]);
            if (k == 0) {
                snprintf(idb, sizeof(idb), "cur_S16_read");
            }
            ui_answer_t r = read_entry(16, txt, step_s, &x10);
            if (r == UI_ANS_ABORT) return 2;
            if (r != UI_ANS_OK) continue;
            if (k == 0) {
                pwr_set_state_x10(16, (uint32_t)x10);
                g_hwcal.i_chg_ma = (uint16_t)((x10 + 5) / 10);
            }
            adc_measure(100);
            emit_reading(16, "charge", (uint32_t)x10, stage[k], 0, adc_result()->vbat_mv, adc_result()->temp_dC);
        }
        return 0;
    }

    const load_def_t *d = load_def((int)id);
    static const load_def_t stop_def = {1, "S1 Stop (LCD sleep)", "uA..mA", true, false};
    if (id == 1u) d = &stop_def;
    if (!d) {
        res_printf("{\"t\":\"skip\",\"suite\":\"power.current\",\"name\":\"S%u\",\"reason\":\"%s\"}", id,
                   id == 10u ? "amp: AMP_SD stays low in P2 (P4)" : "undefined state");
        ui_con_line(COLOR_YELLOW, "S%u skipped", id);
        return 1;
    }
    snprintf(idb, sizeof(idb), "cur_S%u_start", id);
    snprintf(txt, sizeof(txt), "%s: meter range %s, %u s. %s", d->name, d->range, (unsigned)n_s,
             d->dark ? "Screen goes DARK; read the meter at the end, the screen lights when done." : "");
    draw_state(id, d->name, d->range, d->dark,
               id == 1u ? "USB drops (Stop mode), wakes after N s or POWER. Reconnect the host afterwards." : NULL);
    ui_prompt_emit(idb, txt, start_ans, step_s * 1000u);
    ui_answer_t a;
    {
        uint32_t t0 = HAL_GetTick();
        gfx_text(4, (int16_t)(lcd_height() - 10), "A=start  B=skip", COLOR_CYAN, COLOR_BLACK, 1);
        input_flush();
        for (;;) {
            sys_service();
            input_event_t ev;
            if (input_pop(&ev) && ev.pressed && (ev.button == BTN_A || ev.button == BTN_B)) {
                a = ev.button == BTN_A ? UI_ANS_OK : UI_ANS_FAIL;
                break;
            }
            if (ui_host_answer_take(idb, &a)) break;
            if (abort_requested()) { a = UI_ANS_ABORT; break; }
            if ((uint32_t)(HAL_GetTick() - t0) > step_s * 1000u) { a = UI_ANS_TIMEOUT; break; }
        }
    }
    ui_answer_emit(idb, ui_answer_str(a));
    if (a == UI_ANS_ABORT) return 2;
    if (a != UI_ANS_OK) return 1;

    if (id == 1u) {
        for (int c = 3; c > 0; c--) {
            gfx_textf(4, 100, COLOR_RED, COLOR_BLACK, 2, "STOP in %d", c);
            uint32_t t0 = HAL_GetTick();
            while ((uint32_t)(HAL_GetTick() - t0) < 1000u) sys_service();
        }
        log_printf("power.current S1: entering Stop for %u s (USB detaches)\r\n", (unsigned)n_s);
        log_flush(50);
        lp_rtc_start();
        lp_peripherals_sleep();
        uint32_t cause = lp_stop(n_s, LP_WAKE_TIMER | LP_WAKE_OFFREQ);
        lp_peripherals_wake();
        backlight_full();
        HAL_Delay(300);
        ui_con_begin("power.current");
        ui_con_line(COLOR_CYAN, "S1 woke (%s)", (cause & LP_WAKE_OFFREQ) ? "OFF_REQ" : "timer");
        adc_measure(100);
        vloaded = adc_result()->vbat_mv;
        temp = adc_result()->temp_dC;
    } else {
        load_begin((int)id);
        uint32_t t0 = HAL_GetTick(), last_s = 0xFFFFFFFFu;
        uint32_t el_ms;
        if (!d->owns_lcd) {
            draw_state(id, d->name, d->range, d->dark, NULL);
        }
        while ((el_ms = HAL_GetTick() - t0) < n_s * 1000u) {
            sys_service();
            if (abort_requested()) { load_end(); return 2; }
            load_step();
            uint32_t left = n_s - el_ms / 1000u;
            if (left != last_s) {
                last_s = left;
                if (d->owns_lcd) {
                    load_progress(el_ms, n_s * 1000u);
                } else if (!d->dark) {
                    gfx_textf(4, 90, left <= 5 ? COLOR_GREEN : COLOR_WHITE, COLOR_BLACK, 3, "%2u s ", (unsigned)left);
                }
                if (left % 5u == 0u) {
                    res_printf("{\"t\":\"progress\",\"suite\":\"power.current\",\"name\":\"S%u\",\"left_s\":%u}", id,
                               (unsigned)left);
                }
            }
        }
        adc_measure(100);
        vloaded = adc_result()->vbat_mv;
        temp = adc_result()->temp_dC;
        load_end();
        draw_state(id, d->name, d->range, false, "load stopped");
    }
    pwr_state_vbat_set(id, vloaded, temp);         /* always kept, even if the entry is skipped */
    res_printf("{\"t\":\"current_v\",\"suite\":\"power.current\",\"name\":\"S%u\",\"vbat_loaded_mv\":%" PRIu32
               ",\"temp_dC\":%d}", id, vloaded, temp);
    gfx_textf(4, 112, COLOR_CYAN, COLOR_BLACK, 1, "Vbat under load %u mV", (unsigned)vloaded);
    if (!s_entry) {                                /* entry=0: values are typed on the host later */
        uint32_t t0 = HAL_GetTick();
        while ((uint32_t)(HAL_GetTick() - t0) < 1500u) sys_service();
        return 0;
    }
    int32_t x10;
    snprintf(txt, sizeof(txt), "%s current (mA)", d->name);
    ui_answer_t r = read_entry(id, txt, step_s, &x10);
    if (r == UI_ANS_ABORT) return 2;
    if (r != UI_ANS_OK) return 1;
    pwr_set_state_x10(id, (uint32_t)x10);
    emit_reading(id, d->name, (uint32_t)x10, "run", n_s, vloaded, temp);
    ui_con_line(COLOR_GREEN, "S%u = %d.%d mA (Vb %u)", id, (int)(x10 / 10), (int)(x10 % 10), (unsigned)vloaded);
    return 0;
}

suite_verdict_t suite_power_current(int argc, char **argv)
{
    uint32_t n_s = suite_arg_u32(argc, argv, "n", 20u);
    uint32_t step_s = suite_arg_u32(argc, argv, "step", 300u);
    uint32_t sel = suite_arg_u32(argc, argv, "s", 0u);
    s_entry = suite_arg_u32(argc, argv, "entry", 1u) != 0u;
    if (n_s < 3u) n_s = 3u;
    if (n_s > 600u) n_s = 600u;
    if (!suite_user_start("power.current",
                          "Current measurement: meter in series with the battery lead. States run in order; USB out except S16.",
                          argc, argv)) {
        return SUITE_ABORT;
    }
    uint8_t list[20];
    unsigned nl = 0;
    if (sel) {
        list[nl++] = (uint8_t)sel;
    } else {
        list[nl++] = 1;
        for (const uint8_t *p = g_load_order; *p; p++) list[nl++] = *p;
        list[nl++] = 16;
    }
    unsigned stored = 0;
    suite_verdict_t v = SUITE_ABORT;
    for (unsigned i = 0; i < nl; i++) {
        int r = do_state(list[i], n_s, step_s);
        if (r == 2) { v = SUITE_ABORT; goto out; }
        if (r == 0) stored++;
    }
    v = stored ? SUITE_PASS : SUITE_ABORT;
out:
    backlight_full();
    res_printf("{\"t\":\"current_summary\",\"suite\":\"power.current\",\"stored\":%u,\"i_state_ma_note\":\"RAM only: run hwcal save\"}",
               stored);
    ui_con_line(COLOR_YELLOW, "run `hwcal save` to persist");
    return v;
}
