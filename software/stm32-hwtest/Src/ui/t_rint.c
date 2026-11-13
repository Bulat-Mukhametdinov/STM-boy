/* power.rint [U]: estimates R_int and per-state currents without a current
   meter. Charger reference: BQ24075 CC current ~174.5 mA (ISET 5.1k), so
   R_int = dV(1 s after plug) / 0.1745 A; per-state currents = dV(state vs S2) / R_int
   (relative to S2). Runs autonomously on battery; results go to RAM + W25Q
   0xFDE000 for `power.rint report`. */

#include "pwrtest.h"
#include "suites.h"
#include "adc.h"
#include "backlight.h"
#include "cmd.h"
#include "crc.h"
#include "gfx.h"
#include "hwcal.h"
#include "input.h"
#include "lcd.h"
#include "loads.h"
#include "log.h"
#include "power.h"
#include "w25q.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define RINT_ADDR  0xFDE000u
#define RINT_MAGIC 0x544E4952u   /* "RINT" */
#define ICHG_X10   1745u         /* 174.5 mA in 0.1 mA */

static const uint8_t k_ids[] = {3, 4, 5, 6, 7, 8, 9, 13, 15};
#define N_IDS (sizeof(k_ids) / sizeof(k_ids[0]))

typedef struct {
    uint32_t magic;
    uint16_t valid;            /* 1 = R_int computed from a real charger step */
    uint16_t rint_mohm;
    uint16_t v_rest, v_before, v1, v10, v60;
    uint16_t charging;         /* CHG low seen after plug */
    uint8_t ids[N_IDS];
    uint16_t dv_mv[N_IDS];
    uint16_t i_x10[N_IDS];     /* relative to S2 */
    uint32_t crc;
} rint_rec_t;

static uint16_t avg_last(const uint16_t *a, unsigned n, unsigned k)
{
    uint32_t s = 0;
    for (unsigned i = n - k; i < n; i++) s += a[i];
    return (uint16_t)(s / k);
}

/* runs the load `st` (0 = keep current) for `secs`, sampling Vbat each second; false = abort */
static bool run_secs(unsigned secs, uint16_t *out)
{
    uint32_t t0 = HAL_GetTick();
    for (unsigned k = 0; k < secs; k++) {
        while ((uint32_t)(HAL_GetTick() - t0) < (k + 1u) * 1000u) {
            sys_service();
            if (abort_requested()) return false;
            load_step();
        }
        adc_measure(100);
        out[k] = adc_result()->vbat_mv;
    }
    return true;
}

static void emit_rec(const rint_rec_t *r, const char *src)
{
    res_printf("{\"t\":\"rint\",\"suite\":\"power.rint\",\"name\":\"rint\",\"src\":\"%s\",\"valid\":%s,\"rint_mohm\":%u,"
               "\"v_rest_mv\":%u,\"v_before_mv\":%u,\"v1_mv\":%u,\"v10_mv\":%u,\"v60_mv\":%u,\"charging\":%s,"
               "\"ichg_ma_x10\":%u,\"note\":\"currents relative to S2 (estimated)\"}",
               src, r->valid ? "true" : "false", r->rint_mohm, r->v_rest, r->v_before, r->v1, r->v10, r->v60,
               r->charging ? "true" : "false", (unsigned)ICHG_X10);
    for (unsigned i = 0; i < N_IDS; i++) {
        res_printf("{\"t\":\"rint_state\",\"suite\":\"power.rint\",\"name\":\"S%u\",\"dv_mv\":%u,\"ma_x10\":%u,\"estimated\":true}",
                   r->ids[i], r->dv_mv[i], r->i_x10[i]);
    }
}

static bool wait_vbus(bool want, const char *id, const char *text, uint32_t timeout_ms)
{
    static const char *const ans[] = {"B=skip", NULL};
    ui_prompt_emit(id, text, ans, timeout_ms);
    backlight_full();
    gfx_fill(COLOR_BLACK);
    ui_status_bar();
    gfx_text_wrap(2, 24, (int16_t)(lcd_width() - 4), text, COLOR_YELLOW, COLOR_BLACK, 1);
    uint32_t t0 = HAL_GetTick();
    bool ok = false;
    const char *res = "timeout";
    input_flush();
    for (;;) {
        sys_service();
        ui_answer_t a;
        input_event_t ev;
        if (power_vbus() == want) { ok = true; res = "done"; break; }
        if (input_pop(&ev) && ev.pressed && ev.button == BTN_B) { res = "skip"; break; }
        if (ui_host_answer_take(id, &a)) { res = ui_answer_str(a); break; }
        if (abort_requested()) { res = "abort"; break; }
        if ((uint32_t)(HAL_GetTick() - t0) > timeout_ms) break;
    }
    ui_answer_emit(id, res);
    return ok;
}

static suite_verdict_t report(void)
{
    rint_rec_t r;
    if (w25q_read(RINT_ADDR, &r, sizeof(r)) != W25Q_OK || r.magic != RINT_MAGIC ||
        r.crc != crc32_zlib(0, &r, sizeof(r) - 4u)) {
        res_printf("{\"t\":\"rint\",\"suite\":\"power.rint\",\"name\":\"rint\",\"src\":\"flash\",\"valid\":false,\"why\":\"no stored result\"}");
        ui_con_line(COLOR_YELLOW, "no stored result");
        return SUITE_FAIL;
    }
    emit_rec(&r, "flash");
    ui_con_line(COLOR_WHITE, "R_int %u mOhm valid=%u", r.rint_mohm, r.valid);
    return r.valid ? SUITE_PASS : SUITE_FAIL;
}

suite_verdict_t suite_power_rint(int argc, char **argv)
{
    if (suite_has_word(argc, argv, "report")) return report();
    if (!suite_user_start("power.rint",
                          "R_int + currents without a meter. Battery partly discharged. ~7 min, you will unplug and replug USB.",
                          argc, argv)) {
        return SUITE_ABORT;
    }
    static uint16_t rest[60], base[10], st[10], after[61];
    rint_rec_t r;
    memset(&r, 0, sizeof(r));
    r.magic = RINT_MAGIC;
    suite_verdict_t v = SUITE_ABORT;

    if (!wait_vbus(false, "rint_unplug", "UNPLUG USB now. Test then runs on battery; screen goes dark.", 180000u)) {
        goto out;
    }
    ui_con_begin("power.rint");
    ui_con_line(COLOR_YELLOW, "rest 60 s...");
    load_begin(2);
    if (!run_secs(60, rest)) goto out;
    r.v_rest = avg_last(rest, 60, 5);
    for (unsigned i = 0; i < N_IDS; i++) {
        r.ids[i] = k_ids[i];
        load_begin(2);
        if (!run_secs(10, base)) goto out;
        load_begin(k_ids[i]);
        if (!run_secs(10, st)) goto out;
        uint16_t b0 = avg_last(base, 10, 3), s0 = avg_last(st, 10, 3);
        r.dv_mv[i] = (uint16_t)(b0 > s0 ? b0 - s0 : 0);
    }
    load_begin(2);
    backlight_full();
    gfx_fill(COLOR_BLACK);
    gfx_text_wrap(2, 24, (int16_t)(lcd_width() - 4), "When the screen goes dark, PLUG USB IN (within 3 min).", COLOR_YELLOW,
                  COLOR_BLACK, 1);
    {
        uint32_t t0 = HAL_GetTick();
        while ((uint32_t)(HAL_GetTick() - t0) < 6000u) sys_service();
    }
    load_begin(2);                          /* dark */
    {
        uint16_t prev = r.v_rest, prev2 = r.v_rest;
        uint32_t t0 = HAL_GetTick(), k = 0;
        bool plugged = false;
        while (!plugged && (uint32_t)(HAL_GetTick() - t0) < 180000u) {
            sys_service();
            load_step();
            if (abort_requested()) goto out;
            if (power_vbus()) {
                plugged = true;
                break;
            }
            if ((uint32_t)(HAL_GetTick() - t0) >= (k + 1u) * 1000u) {
                k++;
                adc_measure(100);
                prev2 = prev;
                prev = adc_result()->vbat_mv;
            }
        }
        if (!plugged) goto out;
        r.v_before = (uint16_t)((prev + prev2) / 2u);
        uint32_t p0 = HAL_GetTick();
        for (unsigned s = 1; s <= 60; s++) {
            while ((uint32_t)(HAL_GetTick() - p0) < s * 1000u) {
                sys_service();
                load_step();
            }
            adc_measure(100);
            after[s] = adc_result()->vbat_mv;
            if (s == 2 && power_charging()) r.charging = 1;
        }
        if (power_charging()) r.charging = 1;
    }
    r.v1 = after[1];
    r.v10 = after[10];
    r.v60 = after[60];
    load_end();
    if (r.charging && r.v1 > r.v_before + 5u) {
        r.rint_mohm = (uint16_t)(((uint32_t)(r.v1 - r.v_before) * 10000u + ICHG_X10 / 2u) / ICHG_X10);
        r.valid = 1;
        g_hwcal.rint_mohm = r.rint_mohm;
        for (unsigned i = 0; i < N_IDS; i++) {
            r.i_x10[i] = (uint16_t)(((uint32_t)r.dv_mv[i] * 10000u) / r.rint_mohm);
            pwr_set_state_x10(r.ids[i], r.i_x10[i]);
        }
        pwr_set_state_x10(2, 0);
        g_hwcal.reserved0 |= 1u;            /* i_state_ma[] are estimates (relative to S2) */
    }
    r.crc = crc32_zlib(0, &r, sizeof(r) - 4u);
    w25q_erase_sector(RINT_ADDR);
    w25q_program(RINT_ADDR, &r, sizeof(r));
    ui_con_begin("power.rint");
    ui_con_line(r.valid ? COLOR_GREEN : COLOR_RED, "R_int %u mOhm%s", r.rint_mohm, r.valid ? "" : " (invalid)");
    ui_con_line(COLOR_WHITE, "V %u -> %u -> %u mV", r.v_before, r.v1, r.v10);
    ui_con_line(COLOR_YELLOW, "hwcal save to persist");
    emit_rec(&r, "run");
    v = r.valid ? SUITE_PASS : SUITE_FAIL;
out:
    load_end();
    backlight_full();
    return v;
}
