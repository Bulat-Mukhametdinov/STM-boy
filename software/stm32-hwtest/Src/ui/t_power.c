/* P2 power suites: power.info (auto), power.adccal [U], power.handshake [U],
   shared joystick number entry and the host commands `adccal` / `cur`. */

#include "pwrtest.h"
#include "suites.h"
#include "adc.h"
#include "board.h"
#include "cmd.h"
#include "gfx.h"
#include "hwcal.h"
#include "input.h"
#include "lcd.h"
#include "log.h"
#include "lowpower.h"
#include "noinit.h"
#include "power.h"
#include "timebase.h"
#include "w25q.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- number entry */

static void fmt_val(char *b, size_t n, int32_t v, int32_t div, int dec)
{
    int32_t a = v < 0 ? -v : v;
    int ip = (int)((a / div) % 100000);
    int fp = (int)((a % div) % 1000);
    if (dec >= 3) {
        snprintf(b, n, "%s%d.%03d", v < 0 ? "-" : "", ip, fp);
    } else if (dec >= 1) {
        snprintf(b, n, "%s%d.%d", v < 0 ? "-" : "", ip, fp % 10);
    } else {
        snprintf(b, n, "%d", (int)v);
    }
}

ui_answer_t pwr_num_entry(const char *id, const char *title, const char *l0, const char *l1, int nf,
                          int32_t v[2], int32_t step_s, int32_t step_b, int32_t lo, int32_t hi,
                          int32_t div, int dec, void (*live)(char *buf, size_t n), pwr_host_t *host,
                          uint32_t timeout_ms)
{
    static const char *const ans[] = {"A=ok", "B=skip", NULL};
    const char *lab[2] = {l0, l1};
    ui_prompt_emit(id, title, ans, timeout_ms);
    gfx_fill(COLOR_BLACK);
    ui_status_bar();
    gfx_text_pad(2, 12, title, COLOR_YELLOW, COLOR_BLACK, 1, (int16_t)(lcd_width() - 4));
    gfx_text(2, (int16_t)(lcd_height() - 19), "UD +-small LR +-big", COLOR_GRAY, COLOR_BLACK, 1);
    gfx_text(2, (int16_t)(lcd_height() - 10), "A=ok CENTER=field B=skip", COLOR_CYAN, COLOR_BLACK, 1);
    input_flush();
    int f = 0;
    bool dirty = true;
    uint32_t t0 = HAL_GetTick(), t_live = t0 - 1000u;
    ui_answer_t a = UI_ANS_OK;
    for (;;) {
        sys_service();
        if (abort_requested()) { a = UI_ANS_ABORT; break; }
        input_event_t ev;
        while (input_pop(&ev)) {
            if (!ev.pressed) continue;
            int32_t d = 0;
            switch (ev.button) {
            case BTN_UP: d = step_s; break;
            case BTN_DOWN: d = -step_s; break;
            case BTN_RIGHT: d = step_b; break;
            case BTN_LEFT: d = -step_b; break;
            case BTN_CENTER: if (nf > 1) { f = (f + 1) % nf; dirty = true; } break;
            case BTN_A:
                if (f + 1 < nf) { f++; dirty = true; } else { goto done; }
                break;
            case BTN_B: a = UI_ANS_FAIL; goto done;
            default: break;
            }
            if (d) {
                int32_t x = v[f] + d;
                v[f] = x < lo ? lo : (x > hi ? hi : x);
                dirty = true;
            }
        }
        if (host && host->pending) {
            host->pending = false;
            for (int i = 0; i < nf; i++) v[i] = host->v[i];
            break;
        }
        if (ui_host_answer_take(id, &a)) break;
        uint32_t el = HAL_GetTick() - t0;
        if (timeout_ms && el >= timeout_ms) { a = UI_ANS_TIMEOUT; break; }
        if (live && (uint32_t)(HAL_GetTick() - t_live) >= 700u) {
            char b[40];
            t_live = HAL_GetTick();
            live(b, sizeof(b));
            gfx_text_pad(2, 24, b, COLOR_GRAY, COLOR_BLACK, 1, (int16_t)(lcd_width() - 4));
        }
        if (dirty) {
            dirty = false;
            for (int i = 0; i < nf; i++) {
                char b[24];
                int16_t y = (int16_t)(38 + i * 32);
                gfx_text_pad(2, y, lab[i], i == f ? COLOR_CYAN : COLOR_WHITE, COLOR_BLACK, 1,
                             (int16_t)(lcd_width() - 4));
                fmt_val(b, sizeof(b), v[i], div, dec);
                gfx_text_pad(2, (int16_t)(y + 10), b, i == f ? COLOR_YELLOW : COLOR_GRAY, COLOR_BLACK, 2,
                             (int16_t)(lcd_width() - 4));
            }
        }
    }
done:
    ui_answer_emit(id, ui_answer_str(a));
    ui_con_begin(title);
    return a;
}

/* ---------------------------------------------------------------- per-state currents */

static uint16_t s_x10[18];
pwr_host_t g_cur_host;
volatile int g_cur_wait_state;

uint32_t pwr_state_x10(unsigned state)
{
    if (state >= 18u) return 0;
    return s_x10[state] ? s_x10[state] : (uint32_t)g_hwcal.i_state_ma[state] * 10u;
}

void pwr_set_state_x10(unsigned state, uint32_t x10)
{
    if (state >= 18u) return;
    s_x10[state] = (uint16_t)(x10 > 65535u ? 65535u : x10);
    g_hwcal.i_state_ma[state] = (uint16_t)((x10 + 9u) / 10u);
}

/* "12.3" / "12" / "0.45" -> x10 (rounded half up) */
static bool parse_x10(const char *s, uint32_t *out)
{
    uint32_t ip = 0, frac = 0;
    int nd = 0;
    bool any = false;
    for (; *s >= '0' && *s <= '9'; s++) {
        ip = ip * 10u + (uint32_t)(*s - '0');
        any = true;
        if (ip > 6000u) return false;
    }
    if (*s == '.') {
        s++;
        for (; *s >= '0' && *s <= '9'; s++) {
            any = true;
            if (nd < 2) {
                frac = frac * 10u + (uint32_t)(*s - '0');
                nd++;
            }
        }
    }
    if (!any || *s) return false;
    while (nd < 2) {
        frac *= 10u;
        nd++;
    }
    *out = ip * 10u + (frac + 5u) / 10u;
    return true;
}

/* ---------------------------------------------------------------- power.info */

static const char *pres_str(uint8_t p)
{
    return p == ADC_PRES_BATT ? "batt" : (p == ADC_PRES_NONE ? "no_batt" : "unknown");
}

suite_verdict_t suite_power_info(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    uint32_t t0 = HAL_GetTick();
    for (int i = 0; i < 10; i++) {              /* fills the 8-batch presence window (~2.5 s) */
        adc_measure(100);
        while ((uint32_t)(HAL_GetTick() - t0) < (uint32_t)(i + 1) * 250u) {
            sys_service();
            if (abort_requested()) return SUITE_ABORT;
        }
    }
    const adc_result_t *r = adc_result();
    bool chg = power_charging(), vbus = power_vbus();
    bool ok = r->vdda_mv >= 3000u && r->vdda_mv <= 3600u && r->temp_dC > -100 && r->temp_dC < 850 &&
              r->presence == ADC_PRES_BATT;
    res_printf("{\"t\":\"power\",\"suite\":\"power.info\",\"name\":\"info\",\"pass\":%s,\"vdda_mv\":%u,"
               "\"vpin_mv\":%u,\"vbat_unc_mv\":%u,\"vbat_mv\":%u,\"temp_dC\":%d,\"chg\":%s,\"vbus\":%s,"
               "\"presence\":\"%s\",\"raw_bat\":%u,\"raw_ref\":%u,\"raw_temp\":%u,\"noise_mv\":%u,"
               "\"spread_mv\":%u,\"gain_q16\":%" PRIu32 ",\"offset_mv\":%d,\"vrefint_cal\":%u,"
               "\"off_req\":%s}",
               ok ? "true" : "false", r->vdda_mv, r->vpin_mv, r->vbat_unc_mv, r->vbat_mv, r->temp_dC,
               chg ? "true" : "false", vbus ? "true" : "false", pres_str(r->presence), r->raw_bat,
               r->raw_ref, r->raw_temp, r->noise_mv, r->spread_mv, g_hwcal.vbat_gain_q16,
               g_hwcal.vbat_offset_mv, *(volatile const uint16_t *)0x1FFF7A2Au,
               power_off_req() ? "true" : "false");
    ui_con_line(COLOR_WHITE, "VDDA %u mV  Vpin %u mV", r->vdda_mv, r->vpin_mv);
    ui_con_line(r->presence == ADC_PRES_BATT ? COLOR_GREEN : COLOR_RED, "Vbat %u mV (%s)", r->vbat_mv,
                pres_str(r->presence));
    ui_con_line(COLOR_WHITE, "temp %d.%d C  spread %u mV", r->temp_dC / 10, abs(r->temp_dC % 10),
                r->spread_mv);
    ui_con_line(COLOR_WHITE, "CHG %s  VBUS %s", chg ? "low(chg)" : "high", vbus ? "yes" : "no");
    ui_con_line(COLOR_GRAY, "raw bat %u ref %u tmp %u", r->raw_bat, r->raw_ref, r->raw_temp);
    return ok ? SUITE_PASS : SUITE_FAIL;
}

/* ---------------------------------------------------------------- power.adccal */

static pwr_host_t s_cal_host;
static volatile bool s_cal_active;

static void cal_live(char *b, size_t n)
{
    adc_measure(100);
    const adc_result_t *r = adc_result();
    snprintf(b, n, "ADC Vbat %u  3V3 %u mV", r->vbat_unc_mv, r->vdda_mv);
}

static void emit_point(int i)
{
    const adc_cal_point_t *p = adc_cal_get(i);
    if (!p) return;
    res_printf("{\"t\":\"adccal_point\",\"n\":%d,\"meter_vbat_mv\":%u,\"meter_v3v3_mv\":%u,"
               "\"unc_vbat_mv\":%u,\"vdda_mv\":%u}",
               i + 1, p->meter_vbat_mv, p->meter_v3v3_mv, p->unc_vbat_mv, p->vdda_mv);
}

static bool cal_fit_apply(void)
{
    uint32_t g;
    int16_t o;
    int32_t me;
    int n = adc_cal_count();
    if (!adc_cal_fit(&g, &o, &me)) {
        res_printf("{\"t\":\"adccal_fit\",\"ok\":false,\"points\":%d,\"why\":\"no points or implausible fit\"}", n);
        return false;
    }
    g_hwcal.vbat_gain_q16 = g;
    g_hwcal.vbat_offset_mv = o;
    /* VDDA check from the 3V3 readings: meter / VREFINT estimate */
    int32_t dv = 0;
    for (int i = 0; i < n; i++) {
        const adc_cal_point_t *p = adc_cal_get(i);
        dv += (int32_t)p->meter_v3v3_mv - (int32_t)p->vdda_mv;
    }
    dv /= n;
    res_printf("{\"t\":\"adccal_fit\",\"ok\":true,\"points\":%d,\"gain_q16\":%" PRIu32 ",\"offset_mv\":%d,"
               "\"max_err_mv\":%" PRId32 ",\"vdda_err_mv\":%" PRId32 ",\"saved_to\":\"ram\"}",
               n, g, o, me, dv);
    return true;
}

suite_verdict_t suite_power_adccal(int argc, char **argv)
{
    if (!suite_user_start("power.adccal",
                          "ADC calibration: multimeter on VBAT (J2) and on 3V3 (SWD pad). Charger/USB may stay.",
                          argc, argv)) {
        return SUITE_ABORT;
    }
    s_cal_active = true;
    suite_verdict_t v = SUITE_FAIL;
    for (;;) {
        adc_measure(100);
        const adc_result_t *r = adc_result();
        int32_t val[2] = {(int32_t)(r->vbat_unc_mv / 10u) * 10, (int32_t)(r->vdda_mv / 10u) * 10};
        char idb[24];
        snprintf(idb, sizeof(idb), "adccal_pt%d", adc_cal_count() + 1);
        ui_answer_t a = pwr_num_entry(idb, "Enter multimeter readings (V)", "VBAT at J2", "3V3 at SWD pad", 2, val, 10,
                                      100, 2000, 5500, 1000, 3, cal_live, &s_cal_host,
                                      (suite_arg_u32(argc, argv, "step", 300u)) * 1000u);
        if (a == UI_ANS_ABORT) { v = SUITE_ABORT; break; }
        if (a == UI_ANS_OK) {
            if (adc_cal_add((uint16_t)val[0], (uint16_t)val[1])) {
                emit_point(adc_cal_count() - 1);
                ui_con_line(COLOR_GREEN, "pt%d: %d/%d mV", adc_cal_count(), (int)val[0], (int)val[1]);
            }
        }
        if (adc_cal_count() == 0) { v = SUITE_ABORT; break; }
        static const char *const more[] = {"A=add point", "B=fit+finish", NULL};
        ui_answer_t m = ui_prompt("adccal_more",
                                  "Add another point (change the battery voltage, e.g. charge/discharge) or fit?",
                                  more, 60000u);
        if (m == UI_ANS_OK && adc_cal_count() < ADC_CAL_MAX) continue;
        if (m == UI_ANS_ABORT) { v = SUITE_ABORT; break; }
        if (cal_fit_apply()) {
            ui_con_line(COLOR_GREEN, "gain %" PRIu32 " off %d", g_hwcal.vbat_gain_q16, g_hwcal.vbat_offset_mv);
            ui_con_line(COLOR_YELLOW, "run `hwcal save` to persist");
            v = SUITE_PASS;
        }
        break;
    }
    s_cal_active = false;
    return v;
}

/* ---------------------------------------------------------------- commands */

static bool arg_u32(const char *s, uint32_t *o)
{
    char *e;
    unsigned long v = strtoul(s, &e, 0);
    if (e == s || *e) return false;
    *o = (uint32_t)v;
    return true;
}

bool pwr_cmd(int argc, char **argv)
{
    if (strcmp(argv[0], "cur") == 0) {
        uint32_t st, x10;
        if (argc < 3 || !arg_u32(argv[1], &st) || st >= 18u || !parse_x10(argv[2], &x10)) {
            proto_err("cur", "usage: cur <state 0..17> <mA>");
            return true;
        }
        if (g_cur_wait_state == (int)st) {
            g_cur_host.v[0] = (int32_t)x10;
            g_cur_host.pending = true;
        } else {
            pwr_set_state_x10(st, x10);
            res_printf("{\"t\":\"cur\",\"state\":%u,\"ma_x10\":%u,\"i_state_ma\":%u,\"saved_to\":\"ram\"}",
                       (unsigned)st, (unsigned)x10, g_hwcal.i_state_ma[st]);
        }
        proto_ok("cur");
        return true;
    }
    /* adccal point|fit|list|clear */
    if (argc >= 2 && strcmp(argv[1], "point") == 0) {
        uint32_t vb, v3;
        if (argc < 4 || !arg_u32(argv[2], &vb) || !arg_u32(argv[3], &v3) || vb > 6000u || v3 > 6000u) {
            proto_err("adccal", "usage: adccal point <vbat_mv> <v3v3_mv>");
            return true;
        }
        if (s_cal_active) {
            s_cal_host.v[0] = (int32_t)vb;
            s_cal_host.v[1] = (int32_t)v3;
            s_cal_host.pending = true;
        } else {
            if (!adc_cal_add((uint16_t)vb, (uint16_t)v3)) {
                proto_err("adccal", "point not added (table full / no ADC result)");
                return true;
            }
            emit_point(adc_cal_count() - 1);
        }
        proto_ok("adccal point");
    } else if (argc >= 2 && strcmp(argv[1], "fit") == 0) {
        if (!cal_fit_apply()) {
            proto_err("adccal", "fit failed");
        } else {
            proto_ok("adccal fit");
        }
    } else if (argc >= 2 && strcmp(argv[1], "clear") == 0) {
        adc_cal_clear();
        proto_ok("adccal clear");
    } else if (argc >= 2 && strcmp(argv[1], "list") == 0) {
        for (int i = 0; i < adc_cal_count(); i++) emit_point(i);
        proto_ok("adccal list");
    } else {
        proto_err("adccal", "usage: adccal point <vbat_mv> <v3v3_mv> | fit | list | clear");
    }
    return true;
}

/* ---------------------------------------------------------------- power.handshake */

#define HSK_ADDR   0xFDF000u           /* last scratch sector */
#define HSK_MAGIC  0x424B5348u         /* "HSKB" */
#define HSK_MAX    128u
#define NOINIT_HSK 0x48534B31u

typedef struct {
    uint32_t magic;
    uint16_t seq;
    uint8_t stage;                     /* 1 REQ seen (before ACK), 2 ACK asserted, 3 alive */
    uint8_t mode;                      /* 0 now, 1 delay, 2 never */
    uint32_t t_ms;                     /* ms since the OFF_REQ falling edge */
    uint32_t lat_us;                   /* edge -> main loop detection */
    uint32_t ack_ms;                   /* ms of ACK since the edge (0 = none yet) */
    uint32_t rsv[3];
} hsk_t;
_Static_assert(sizeof(hsk_t) == 32, "hsk entry");

static const char *const k_mode[] = {"now", "delay", "never"};

static void hsk_crumb(uint32_t seq, uint8_t stage, uint8_t mode, uint32_t t_ms, uint32_t lat_us, uint32_t ack_ms)
{
    g_noinit.reserved[44] = NOINIT_HSK;
    g_noinit.reserved[45] = ((uint32_t)stage << 8) | mode;
    g_noinit.reserved[46] = t_ms;
    g_noinit.reserved[47] = ack_ms;
    if (seq >= HSK_MAX) return;
    hsk_t e;
    memset(&e, 0xFF, sizeof(e));
    e.magic = HSK_MAGIC;
    e.seq = (uint16_t)seq;
    e.stage = stage;
    e.mode = mode;
    e.t_ms = t_ms;
    e.lat_us = lat_us;
    e.ack_ms = ack_ms;
    w25q_program(HSK_ADDR + seq * sizeof(e), &e, sizeof(e));
}

static suite_verdict_t hsk_report(void)
{
    hsk_t e[HSK_MAX];
    if (w25q_read(HSK_ADDR, e, sizeof(e)) != W25Q_OK) {
        res_printf("{\"t\":\"handshake_report\",\"ok\":false,\"why\":\"w25q read\"}");
        return SUITE_FAIL;
    }
    unsigned n = 0, last_alive = 0, st1 = 0, ack = 0, mode = 0, lat = 0, hb = 0;
    for (; n < HSK_MAX && e[n].magic == HSK_MAGIC; n++) {
        mode = e[n].mode;
        if (e[n].stage == 1) { st1 = e[n].t_ms; lat = e[n].lat_us; }
        if (e[n].stage == 2) ack = e[n].ack_ms;
        if (e[n].stage == 3) { last_alive = e[n].t_ms; hb++; }
        if (e[n].ack_ms) ack = e[n].ack_ms;
    }
    bool crumb = g_noinit.reserved[44] == NOINIT_HSK;
    if (n == 0) {
        res_printf("{\"t\":\"handshake_report\",\"ok\":false,\"entries\":0,\"noinit\":%s,\"why\":\"no breadcrumbs\"}",
                   crumb ? "true" : "false");
        ui_con_line(COLOR_YELLOW, "no breadcrumbs found");
        return SUITE_FAIL;
    }
    unsigned last = last_alive > ack ? last_alive : (ack > st1 ? ack : st1);
    const char *verdict = (mode == 2) ? "never_acked_power_cut_at_last_alive"
                          : (ack == 0) ? "power_cut_before_ack"
                          : (last_alive > ack) ? "power_survived_ack_then_cut"
                                               : "power_cut_right_after_ack";
    res_printf("{\"t\":\"handshake_report\",\"ok\":true,\"mode\":\"%s\",\"entries\":%u,\"detect_lat_us\":%u,"
               "\"req_seen_ms\":%u,\"ack_ms\":%u,\"last_alive_ms\":%u,\"heartbeats\":%u,\"power_cut_est_ms\":%u,"
               "\"verdict\":\"%s\",\"noinit_crumb\":%s}",
               k_mode[mode > 2 ? 2 : mode], n, lat, st1, ack, last_alive, hb, last, verdict,
               crumb ? "true" : "false");
    ui_con_line(COLOR_WHITE, "mode %s  entries %u", k_mode[mode > 2 ? 2 : mode], n);
    ui_con_line(COLOR_WHITE, "REQ seen %u ms ACK %u ms", st1, ack);
    ui_con_line(COLOR_CYAN, "alive until ~%u ms", last);
    ui_con_line(COLOR_GREEN, "%s", verdict);
    return SUITE_PASS;
}

suite_verdict_t suite_power_handshake(int argc, char **argv)
{
    if (suite_has_word(argc, argv, "report")) {
        return hsk_report();
    }
    const char *m = suite_arg_str(argc, argv, "ack", "now");
    uint8_t mode = strcmp(m, "delay") == 0 ? 1 : (strcmp(m, "never") == 0 ? 2 : 0);
    if (mode == 0 && strcmp(m, "now") != 0) {
        ui_con_line(COLOR_RED, "ack=now|delay|never");
        return SUITE_FAIL;
    }
    char txt[100];
    snprintf(txt, sizeof(txt), "ack=%s. Disconnect ST-Link SWD for this test! Then A, hold POWER 1 s and release.", m);
    if (!suite_user_start("power.handshake", txt, argc, argv)) {
        return SUITE_ABORT;
    }
    if (power_off_req()) {
        ui_con_line(COLOR_RED, "OFF_REQ already low");
        return SUITE_FAIL;
    }
    /* clear the breadcrumb sector and the noinit copy */
    w25q_erase_sector(HSK_ADDR);
    g_noinit.reserved[44] = 0;
    power_inhibit(true);
    lp_off_req_arm();
    ui_con_line(COLOR_YELLOW, "armed (mode %s)", m);
    ui_con_line(COLOR_WHITE, "hold POWER 1 s, release");
    uint32_t t_wait = suite_arg_u32(argc, argv, "wait", 120u) * 1000u;
    uint32_t w0 = HAL_GetTick();
    suite_verdict_t v = SUITE_ABORT;
    for (;;) {
        sys_service();
        if (abort_requested()) goto out;
        if (power_off_req() || lp_off_req_seen) break;
        if ((uint32_t)(HAL_GetTick() - w0) > t_wait) {
            res_printf("{\"t\":\"skip\",\"suite\":\"power.handshake\",\"reason\":\"no OFF_REQ within %u s\"}",
                       (unsigned)(t_wait / 1000u));
            goto out;
        }
    }
    uint32_t det_us = time_us();
    uint32_t lat_us = lp_off_req_seen ? (uint32_t)(det_us - lp_off_req_edge_us) : 0u;
    uint32_t edge_ms = lp_off_req_seen ? lp_off_req_edge_ms : HAL_GetTick();
    /* same debounce as power_poll() */
    while ((uint32_t)(HAL_GetTick() - edge_ms) < 30u) {
    }
    if (!power_off_req()) {
        ui_con_line(COLOR_RED, "glitch (<30 ms), aborted");
        res_printf("{\"t\":\"handshake\",\"suite\":\"power.handshake\",\"name\":\"glitch\",\"pass\":false}");
        v = SUITE_FAIL;
        goto out;
    }
    uint32_t seq = 0;
    ui_con_line(COLOR_CYAN, "OFF_REQ! lat %u us", (unsigned)lat_us);
    hsk_crumb(seq++, 1, mode, HAL_GetTick() - edge_ms, lat_us, 0);
    res_printf("{\"t\":\"handshake\",\"suite\":\"power.handshake\",\"name\":\"req\",\"mode\":\"%s\","
               "\"detect_lat_us\":%u,\"edge_to_confirm_ms\":%u}",
               m, (unsigned)lat_us, (unsigned)(HAL_GetTick() - edge_ms));
    log_flush(20);
    if (mode == 1) {
        uint32_t d0 = HAL_GetTick();
        while ((uint32_t)(HAL_GetTick() - d0) < 1000u) {
            sys_service();
        }
    }
    uint32_t ack_ms = 0;
    if (mode != 2) {
        gpio_set(OFF_ACK_PORT, OFF_ACK_PIN);
        ack_ms = HAL_GetTick() - edge_ms;
        hsk_crumb(seq++, 2, mode, ack_ms, lat_us, ack_ms);
        ui_con_line(COLOR_GREEN, "ACK at %u ms", (unsigned)ack_ms);
    } else {
        ui_con_line(COLOR_YELLOW, "no ACK (never)");
    }
    /* heartbeat until the rail drops (ATtiny: 2.5 s after OFF_REQ) */
    uint32_t hb0 = HAL_GetTick(), last_hb = 0;
    while ((uint32_t)(HAL_GetTick() - edge_ms) < 8000u && seq < HSK_MAX) {
        sys_service();
        uint32_t el = HAL_GetTick() - edge_ms;
        if ((uint32_t)(HAL_GetTick() - hb0) - last_hb >= 100u) {
            last_hb = HAL_GetTick() - hb0;
            hsk_crumb(seq++, 3, mode, el, lat_us, ack_ms);
        }
    }
    gpio_clr(OFF_ACK_PORT, OFF_ACK_PIN);
    res_printf("{\"t\":\"handshake\",\"suite\":\"power.handshake\",\"name\":\"survived\",\"pass\":false,"
               "\"why\":\"power did not drop within 8 s (powered from SWD/USB-only rail?)\"}");
    ui_con_line(COLOR_RED, "power did NOT drop in 8 s");
    v = SUITE_FAIL;
out:
    gpio_clr(OFF_ACK_PORT, OFF_ACK_PIN);
    power_inhibit(false);
    return v;
}
