/* battlog (P2b): pulsed discharge/charge logger. Records go append-only to the
   W25Q (0xF00000: header sector, 0xF01000..0xF7FFFF: 16-byte records) or, with
   dry=1, to RAM only. See PROTOCOL.md. */

#include "pwrtest.h"
#include "suites.h"
#include "adc.h"
#include "backlight.h"
#include "board.h"
#include "cmd.h"
#include "crc.h"
#include "fw_id.h"
#include "gfx.h"
#include "hwcal.h"
#include "input.h"
#include "lcd.h"
#include "loads.h"
#include "log.h"
#include "lowpower.h"
#include "power.h"
#include "w25q.h"
#include "stm32f4xx_hal.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint8_t g_arena[];

#define BL_HDR      0xF00000u
#define BL_END      0xF00100u
#define BL_DATA     0xF01000u
#define BL_LIMIT    0xF80000u
#define BL_MAXREC   ((BL_LIMIT - BL_DATA) / 16u)
#define BL_MAGIC    0x474F4C42u    /* "BLOG" */
#define BL_END_MAGIC 0x444E4542u   /* "BEND" */
#define DRY_CAP     512u
#define CAPACITY_MAH 310u

#define F_CHG    0x01u
#define F_VBUS   0x02u
#define F_REST   0x04u
#define F_CALDUE 0x08u
#define F_MARKER 0x10u
#define F_CUTOFF 0x20u
#define F_STEP   0x40u
#define F_CHGSTEP 0x80u

typedef struct __attribute__((packed)) {
    uint32_t t_s;
    uint16_t vbat_mv, vcomp_mv, vdda_mv;
    int16_t temp_dC;
    uint8_t load_state, flags;
    uint16_t seq;
} blrec_t;
_Static_assert(sizeof(blrec_t) == 16, "record size");

typedef struct {
    uint32_t magic;
    uint16_t version, rec_size;
    uint32_t data_base, data_limit;
    char uid[24];
    char fw[16];
    uint32_t gain_q16;
    int16_t offset_mv;
    uint16_t rint_mohm;
    uint16_t i_x10[18];
    uint32_t crc;
} blhdr_t;

static blrec_t *const s_dry = (blrec_t *)(void *)(g_arena + 65536);   /* 512 records, after the 64 KB dump/load area */

static bool s_run, s_dry_mode;
static uint32_t s_n, s_dry_n, s_werr;
static uint16_t s_seq;
static const char *s_phase_name = "-";
static uint32_t s_t_ms_now;
static bool s_host_start;

/* ---------------------------------------------------------------- flash log */

static bool read_hdr(blhdr_t *h)
{
    if (!w25q_present() || w25q_read(BL_HDR, h, sizeof(*h)) != W25Q_OK) return false;
    return h->magic == BL_MAGIC && h->crc == crc32_zlib(0, h, sizeof(*h) - 4u);
}

static bool log_ended(void)
{
    uint32_t m = 0;
    w25q_read(BL_END, &m, 4);
    return m == BL_END_MAGIC;
}

static uint32_t flash_count(void)
{
    uint32_t lo = 0, hi = BL_MAXREC;
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2u, t = 0;
        w25q_read(BL_DATA + mid * 16u, &t, 4);
        if (t == 0xFFFFFFFFu) hi = mid; else lo = mid + 1u;
    }
    return lo;
}

static void write_end(uint8_t reason, uint32_t records, uint32_t t_s)
{
    struct { uint32_t magic; uint8_t reason, r1; uint16_t r2; uint32_t records, t_s, crc; } e;
    memset(&e, 0, sizeof(e));
    e.magic = BL_END_MAGIC;
    e.reason = reason;
    e.records = records;
    e.t_s = t_s;
    e.crc = crc32_zlib(0, &e, sizeof(e) - 4u);
    w25q_program(BL_END, &e, sizeof(e));
}

static void write_hdr(void)
{
    blhdr_t h;
    memset(&h, 0, sizeof(h));
    h.magic = BL_MAGIC;
    h.version = 1;
    h.rec_size = 16;
    h.data_base = BL_DATA;
    h.data_limit = BL_LIMIT;
    char u[25];
    board_uid_hex(u);
    memcpy(h.uid, u, 24);
    strncpy(h.fw, g_fw_hash, sizeof(h.fw) - 1u);
    h.gain_q16 = g_hwcal.vbat_gain_q16;
    h.offset_mv = g_hwcal.vbat_offset_mv;
    h.rint_mohm = g_hwcal.rint_mohm;
    for (unsigned i = 0; i < 18u; i++) h.i_x10[i] = (uint16_t)pwr_state_x10(i);
    h.crc = crc32_zlib(0, &h, sizeof(h) - 4u);
    w25q_erase_sector(BL_HDR);
    w25q_program(BL_HDR, &h, sizeof(h));
}

static bool store(const blrec_t *r)
{
    if (s_dry_mode) {
        if (s_dry_n >= DRY_CAP) return false;
        s_dry[s_dry_n++] = *r;
        s_n = s_dry_n;
        return true;
    }
    if (s_n >= BL_MAXREC) return false;
    uint32_t a = BL_DATA + s_n * 16u;
    if ((a & 0xFFFu) == 0u) w25q_erase_sector(a);
    if (w25q_program(a, r, 16) != W25Q_OK) {
        s_werr++;
        return false;
    }
    s_n++;
    return true;
}

/* ---------------------------------------------------------------- time, estimates */

static uint32_t s_t0, s_base_ms;
static uint32_t s_rtc_stop_total, s_rtc_start;

static uint32_t t_ms(void) { return HAL_GetTick() - s_t0 + s_base_ms; }

static uint32_t rtc_delta(uint32_t a, uint32_t b) { return b >= a ? b - a : b + 86400u - a; }

static void stop_adjust(uint32_t rtc_before, uint32_t hal_before)
{
    uint32_t after = lp_rtc_seconds();
    uint32_t d = rtc_delta(rtc_before, after);
    uint32_t run_rtc = rtc_delta(s_rtc_start, rtc_before) - s_rtc_stop_total;
    uint32_t run_hal = (hal_before - s_t0) / 1000u;
    float sc = 1.0f;
    if (run_rtc > 120u) {
        sc = (float)run_hal / (float)run_rtc;
        if (sc < 0.5f) sc = 0.5f;
        if (sc > 2.0f) sc = 2.0f;
    }
    s_rtc_stop_total += d;
    s_base_ms += (uint32_t)((float)d * sc * 1000.0f);
}

static uint32_t pct_from_v(uint32_t v)
{
    const uint16_t *o = g_hwcal.ocv_mv;
    if (v <= o[0]) return 0;
    for (unsigned i = 0; i < 20u; i++) {
        if (v <= o[i + 1] && o[i + 1] > o[i]) return 5u * i + 5u * (v - o[i]) / (o[i + 1] - o[i]);
    }
    return 100;
}

/* ---------------------------------------------------------------- state */

typedef enum { PH_LOAD, PH_REST, PH_XREST, PH_EMPTY, PH_CHARGE } phase_t;
static const char *const k_ph[] = {"LOAD", "REST", "XREST", "EMPTY", "CHARGE"};

typedef struct {
    bool dry, resume;
    uint32_t ls, load_s, rest_s, xrest_s, dur_s, load_samp_s, cut_mv, chg_end_s;
} cfg_t;

static const uint16_t k_cal_thr[5] = {4100, 3900, 3750, 3600, 3450};
static const uint8_t k_xr_pct[4] = {75, 50, 25, 10};
static const uint16_t k_offs_step[] = {1, 10};
static const uint16_t k_offs_chg[] = {1, 10, 60};

static cfg_t C;

/* Run parameters, stored next to the header (same sector, written once after write_hdr) so that a resume
   after a power loss continues with the ORIGINAL load profile. Before 2026-11-21 a resume silently used the
   defaults (ls=15 game mix instead of ls=12, cycle #2). Separate block: the host header parser is unchanged. */
#define BL_CFG       0xF00200u
#define BL_CFG_MAGIC 0x47464342u   /* "BCFG" */
typedef struct {
    uint32_t magic, ls, load_s, rest_s, xrest_s, load_samp_s, cut_mv, chg_end_s, crc;
} blcfg_t;

static void write_cfg(void)
{
    blcfg_t c = {BL_CFG_MAGIC, C.ls, C.load_s, C.rest_s, C.xrest_s, C.load_samp_s, C.cut_mv, C.chg_end_s, 0};
    c.crc = crc32_zlib(0, &c, sizeof(c) - 4u);
    w25q_program(BL_CFG, &c, sizeof(c));
}

static bool restore_cfg(void)
{
    blcfg_t c;
    if (w25q_read(BL_CFG, &c, sizeof(c)) != W25Q_OK || c.magic != BL_CFG_MAGIC ||
        c.crc != crc32_zlib(0, &c, sizeof(c) - 4u)) {
        return false;
    }
    C.ls = c.ls;
    C.load_s = c.load_s;
    C.rest_s = c.rest_s;
    C.xrest_s = c.xrest_s;
    C.load_samp_s = c.load_samp_s;
    C.cut_mv = c.cut_mv;
    C.chg_end_s = c.chg_end_s;
    return true;
}

static phase_t s_ph;
static uint32_t s_ph_t0, s_cal_done, s_xr_done, s_cal_due_until, s_cut_cnt;
static uint32_t s_used_x10s;
static bool s_xr_pending, s_marker, s_first_marker;
static uint8_t s_state;           /* current load state id */
static adc_result_t s_hist;       /* last 1 Hz measurement */
static uint8_t s_hist_state;
static uint32_t s_pct;

/* stepped-measurement event (load<->rest, plug/unplug) */
static struct { bool act, finish; uint8_t flag, n, i; uint32_t t0; const uint16_t *offs; } EV;

static void emit_core(uint8_t state, uint8_t flags, uint16_t vb, uint16_t vdda, int16_t temp)
{
    blrec_t r;
    r.t_s = t_ms() / 1000u;
    r.vbat_mv = vb;
    uint32_t x10 = pwr_state_x10(state);
    r.vcomp_mv = (uint16_t)(vb + (x10 * g_hwcal.rint_mohm) / 10000u);
    r.vdda_mv = vdda;
    r.temp_dC = temp;
    r.load_state = state;
    power_sense_init();   /* fresh pin config + sample at every record */
    if (power_charging()) flags |= F_CHG;
    if (power_vbus()) flags |= F_VBUS;
    if (s_ph == PH_REST || s_ph == PH_XREST || s_ph == PH_CHARGE) flags |= F_REST;
    if ((int32_t)(s_cal_due_until - HAL_GetTick()) > 0) flags |= F_CALDUE;
    if (s_marker || s_first_marker) {
        flags |= F_MARKER;
        s_marker = s_first_marker = false;
    }
    r.flags = flags;
    r.seq = s_seq++;
    bool ok = store(&r);
    res_printf("{\"t\":\"bl\",\"n\":%" PRIu32 ",\"t_s\":%" PRIu32 ",\"vbat\":%u,\"vcomp\":%u,\"st\":%u,\"fl\":%u,\"ok\":%s}",
               s_n, r.t_s, vb, r.vcomp_mv, state, flags, ok ? "true" : "false");
    s_pct = pct_from_v(r.vcomp_mv);
}

static void take(uint8_t state, uint8_t flags)
{
    adc_measure(100);
    const adc_result_t *a = adc_result();
    emit_core(state, flags, a->vbat_mv, a->vdda_mv, a->temp_dC);
}

static void ev_start(uint8_t flag, uint8_t state, const adc_result_t *pre, bool finish)
{
    emit_core(state, flag, pre->vbat_mv, pre->vdda_mv, pre->temp_dC);   /* "before" sample */
    EV.act = true;
    EV.finish = finish;
    EV.flag = flag;
    EV.i = 0;
    EV.offs = (flag == F_CHGSTEP) ? k_offs_chg : k_offs_step;
    EV.n = (flag == F_CHGSTEP) ? 3u : 2u;
    EV.t0 = HAL_GetTick();
}

static void enter_phase(phase_t p)
{
    load_end();
    s_ph = p;
    s_phase_name = k_ph[p];
    s_ph_t0 = HAL_GetTick();
    if (p == PH_LOAD) {
        s_state = (uint8_t)C.ls;
        load_begin((int)C.ls);
    } else {
        gfx_fill(COLOR_BLACK);
        s_state = 2;
        load_begin(2);
    }
}

static void status_lines(uint32_t el_ph)
{
    char b[32];
    const adc_result_t *r = &s_hist;
    uint32_t ts = t_ms() / 1000u;
    uint16_t vc = (uint16_t)(r->vbat_mv + (pwr_state_x10(s_state) * g_hwcal.rint_mohm) / 10000u);
    int16_t y = 12;
    snprintf(b, sizeof(b), "BATLOG%s %02u:%02u:%02u", C.dry ? " DRY" : "", (unsigned)(ts / 3600u),
             (unsigned)(ts / 60u % 60u), (unsigned)(ts % 60u));
    load_fb_text(y, b, COLOR_CYAN); y = (int16_t)(y + 10);
    snprintf(b, sizeof(b), "Vbat %u  Vcomp %u", r->vbat_mv, vc);
    load_fb_text(y, b, COLOR_WHITE); y = (int16_t)(y + 10);
    snprintf(b, sizeof(b), "S%u %s  est %u%%", s_state, s_phase_name, (unsigned)s_pct);
    load_fb_text(y, b, COLOR_WHITE); y = (int16_t)(y + 10);
    snprintf(b, sizeof(b), "recs %u  err %u", (unsigned)s_n, (unsigned)s_werr);
    load_fb_text(y, b, COLOR_WHITE); y = (int16_t)(y + 10);
    uint32_t span = (s_ph == PH_LOAD) ? C.load_s : C.rest_s;
    snprintf(b, sizeof(b), "phase %us/%us", (unsigned)(el_ph / 1000u), (unsigned)span);
    load_fb_text(y, b, COLOR_GRAY); y = (int16_t)(y + 10);
    if ((int32_t)(s_cal_due_until - HAL_GetTick()) > 0) {
        load_fb_text(y, "CAL POINT DUE (A=done)", COLOR_YELLOW);
    } else {
        snprintf(b, sizeof(b), "T %d.%dC chg%d usb%d", r->temp_dC / 10, abs(r->temp_dC % 10), power_charging(),
                 power_vbus());
        load_fb_text(y, b, COLOR_GRAY);
    }
    y = (int16_t)(y + 10);
    load_fb_text(y, "A=mark  hold B=stop", COLOR_GRAY);
}

static void banner(const char *l1, const char *l2, uint16_t col)
{
    backlight_full();
    gfx_fill(COLOR_BLACK);
    gfx_text(4, 30, l1, col, COLOR_BLACK, 1);
    gfx_text(4, 44, l2, COLOR_WHITE, COLOR_BLACK, 1);
}

/* ---------------------------------------------------------------- the suite */

static uint32_t arg_or(int argc, char **argv, const char *k, uint32_t d) { return suite_arg_u32(argc, argv, k, d); }

suite_verdict_t suite_battlog(int argc, char **argv)
{
    memset(&C, 0, sizeof(C));
    C.dry = arg_or(argc, argv, "dry", 0) != 0;
    C.resume = arg_or(argc, argv, "resume", 0) != 0;
    C.ls = arg_or(argc, argv, "ls", 15u);      /* load-phase state: 15 game mix, 12 max (fast discharge) */
    C.load_s = arg_or(argc, argv, "load", C.dry ? 20u : 540u);
    C.rest_s = arg_or(argc, argv, "rest", C.dry ? 10u : 60u);
    C.xrest_s = arg_or(argc, argv, "xrest", C.dry ? 10u : 900u);
    C.dur_s = arg_or(argc, argv, "dur", C.dry ? 120u : 0u);
    C.load_samp_s = arg_or(argc, argv, "samp", C.dry ? 2u : 5u);
    C.cut_mv = arg_or(argc, argv, "cut", 3300u);
    C.chg_end_s = arg_or(argc, argv, "chgend", C.dry ? 20u : 1800u);
    bool force_new = arg_or(argc, argv, "new", 0) != 0;
    bool host = s_host_start;
    s_host_start = false;
    if (argc <= 1 && !host && !suite_user_start("battlog", "Battery log. Pulsed S15/S2. Unplug USB after start; hold B 2 s = stop.", argc, argv)) {
        return SUITE_ABORT;
    }
    s_dry_mode = C.dry;
    s_n = 0;
    s_seq = 0;
    s_base_ms = 0;
    s_first_marker = false;
    if (C.dry) {
        s_dry_n = 0;
    } else {
        blhdr_t h;
        if (!w25q_present()) {
            res_printf("{\"t\":\"skip\",\"suite\":\"battlog\",\"reason\":\"no W25Q\"}");
            return SUITE_FAIL;
        }
        bool hv = read_hdr(&h);
        uint32_t cnt = hv ? flash_count() : 0u;
        if (C.resume) {
            if (!hv || log_ended() || cnt == 0u) {
                res_printf("{\"t\":\"skip\",\"suite\":\"battlog\",\"reason\":\"no unfinished log to resume\"}");
                return SUITE_ABORT;
            }
            bool cfg_ok = restore_cfg();
            res_printf("{\"t\":\"battlog\",\"event\":\"resume_cfg\",\"restored\":%s,\"ls\":%u,\"load_s\":%u,\"rest_s\":%u,\"cut_mv\":%u}",
                       cfg_ok ? "true" : "false", (unsigned)C.ls, (unsigned)C.load_s, (unsigned)C.rest_s, (unsigned)C.cut_mv);
            blrec_t last;
            w25q_read(BL_DATA + (cnt - 1u) * 16u, &last, 16);
            s_n = cnt;
            s_seq = (uint16_t)(last.seq + 1u);
            s_base_ms = (last.t_s + 1u) * 1000u;
            s_first_marker = true;
        } else {
            if (hv && cnt > 0u && !force_new) {
                bool ok = false;
                if (!host) {
                    char t[80];
                    static const char *const a[] = {"A=erase+new", "B=cancel", NULL};
                    snprintf(t, sizeof(t), "Existing log (%u records). Erase it and start new?", (unsigned)cnt);
                    ok = ui_prompt("battlog_new", t, a, 60000u) == UI_ANS_OK;
                }
                if (!ok) {
                    res_printf("{\"t\":\"skip\",\"suite\":\"battlog\",\"reason\":\"existing log (%u records): dump it, then new=1\"}",
                               (unsigned)cnt);
                    return SUITE_ABORT;
                }
            }
            write_hdr();
            write_cfg();
            /* Wipe stale records of the previous log: store() erases a sector only when it starts writing it,
               so old records beyond the write position used to survive and fool flash_count()'s binary search
               on resume (cycle #2, 2026-11-21: resume landed on a sector boundary, 44 empty slots). Records
               are written sequentially from each sector start, so a sector whose first word is 0xFFFFFFFF is
               already blank. */
            unsigned wiped = 0;
            for (uint32_t a = BL_DATA; a < BL_LIMIT; a += 0x1000u) {
                uint32_t w = 0;
                w25q_read(a, &w, 4);
                if (w != 0xFFFFFFFFu) {
                    w25q_erase_sector(a);
                    wiped++;
                }
                sys_service();
            }
            res_printf("{\"t\":\"battlog\",\"event\":\"wipe\",\"sectors\":%u}", wiped);
        }
    }
    ui_con_begin("battlog");
    lp_rtc_start();
    s_rtc_start = lp_rtc_seconds();
    s_rtc_stop_total = 0;
    s_t0 = HAL_GetTick();
    s_cal_done = s_xr_done = s_cut_cnt = s_used_x10s = 0;
    s_xr_pending = s_marker = false;
    s_cal_due_until = HAL_GetTick() - 1u;
    EV.act = false;
    s_run = true;
    adc_measure(100);
    s_hist = *adc_result();
    s_pct = pct_from_v(s_hist.vbat_mv);
    uint32_t last_samp = HAL_GetTick(), last_1hz = last_samp, bhold = 0;
    bool prev_vbus = power_vbus(), chg_was_low = power_charging(), chg_seen_low = false;
    uint32_t chg_high_t = 0;
    const char *reason = "stop";
    uint8_t reason_code = 1;
    suite_verdict_t verdict = SUITE_PASS;
    enter_phase(PH_LOAD);
    res_printf("{\"t\":\"battlog\",\"event\":\"start\",\"dry\":%s,\"resume\":%s,\"load_s\":%u,\"rest_s\":%u,\"records\":%u}",
               C.dry ? "true" : "false", C.resume ? "true" : "false", (unsigned)C.load_s, (unsigned)C.rest_s, (unsigned)s_n);

    for (;;) {
        sys_service();
        adc_task();
        uint32_t now = HAL_GetTick();
        s_t_ms_now = t_ms();
        if (abort_requested()) { reason = "host stop"; break; }
        input_event_t iev;
        while (input_pop(&iev)) {
            if (iev.pressed && iev.button == BTN_A) {
                s_marker = true;
                s_cal_due_until = now - 1u;
            }
        }
        if (input_held() & BTN_MASK(BTN_B)) {
            if (!bhold) bhold = now;
            else if (now - bhold >= 2000u) { reason = "user (B hold)"; break; }
        } else {
            bhold = 0;
        }
        if (C.dur_s && s_t_ms_now / 1000u >= C.dur_s) { reason = "dur reached"; reason_code = 2; break; }
        if (s_n >= BL_MAXREC && !C.dry) { reason = "log full"; reason_code = 3; break; }

        if (s_ph == PH_EMPTY) {
            /* minimal-load Stop until USB (or POWER) */
            banner("BATTERY EMPTY", "CONNECT USB", COLOR_RED);
            uint32_t b0 = HAL_GetTick();
            while ((uint32_t)(HAL_GetTick() - b0) < 8000u) sys_service();
            uint32_t rb = lp_rtc_seconds(), hb = HAL_GetTick();
            lp_peripherals_sleep();
            uint32_t cause = lp_stop(0, LP_WAKE_VBUS | LP_WAKE_OFFREQ);
            lp_peripherals_wake();
            stop_adjust(rb, hb);
            if (cause & LP_WAKE_VBUS) {
                s_xr_pending = false;
                prev_vbus = true;
                chg_seen_low = false;
                chg_high_t = 0;
                enter_phase(PH_CHARGE);
                ev_start(F_CHGSTEP, 2, &s_hist, false);
                last_samp = HAL_GetTick();
            } else {
                uint32_t w0 = HAL_GetTick();
                while ((uint32_t)(HAL_GetTick() - w0) < 400u) sys_service();   /* power_poll() shuts down */
            }
            continue;
        }

        load_step();

        /* USB plug / unplug reference steps */
        bool vb = power_vbus();
        if (vb != prev_vbus) {
            prev_vbus = vb;
            if (vb && !EV.act) {
                ev_start(F_CHGSTEP, s_state, &s_hist, false);
            } else if (!vb && chg_was_low && !EV.act) {
                ev_start(F_CHGSTEP, s_state, &s_hist, s_ph == PH_CHARGE);
            }
        }
        if (EV.act && (now - EV.t0) >= (uint32_t)EV.offs[EV.i] * 1000u) {
            take(s_state, EV.flag);
            if (++EV.i >= EV.n) {
                EV.act = false;
                if (EV.finish) { reason = "usb removed"; reason_code = 4; break; }
            }
        }

        /* regular sampling */
        uint32_t el_ph = now - s_ph_t0;
        uint32_t iv;
        if (s_ph == PH_LOAD) iv = C.load_samp_s * 1000u;
        else if (s_ph == PH_CHARGE) iv = C.dry ? 1000u : 10000u;
        else iv = (C.dry || el_ph < 60000u) ? 1000u : 10000u;
        if (s_hist.vbat_mv < 3400u && !vb) iv = 1000u;
        if ((uint32_t)(now - last_samp) >= iv) {
            last_samp = now;
            uint8_t fl = 0;
            adc_measure(100);
            const adc_result_t *a = adc_result();
            uint16_t vc = (uint16_t)(a->vbat_mv + (pwr_state_x10(s_state) * g_hwcal.rint_mohm) / 10000u);
            for (unsigned i = 0; i < 5u; i++) {
                if (!(s_cal_done & (1u << i)) && vc <= k_cal_thr[i] && !vb) {
                    s_cal_done |= 1u << i;
                    s_cal_due_until = now + 60000u;
                }
            }
            if (!vb) {
                for (unsigned i = 0; i < 4u; i++) {
                    if (!(s_xr_done & (1u << i)) && s_pct <= k_xr_pct[i]) {
                        s_xr_done |= 1u << i;
                        s_xr_pending = true;
                    }
                }
            }
            if (a->vbat_mv < C.cut_mv && a->vbat_mv >= 2700u && !vb) s_cut_cnt++; else s_cut_cnt = 0;
            if (s_cut_cnt >= 3u) fl |= F_CUTOFF;
            emit_core(s_state, fl, a->vbat_mv, a->vdda_mv, a->temp_dC);
            if (fl & F_CUTOFF) {
                s_cut_cnt = 0;
                if (C.dry) { reason = "cutoff (dry: no Stop)"; reason_code = 5; break; }
                s_ph = PH_EMPTY;
                s_phase_name = "EMPTY";
                load_end();
                continue;
            }
        }

        /* 1 Hz housekeeping */
        if ((uint32_t)(now - last_1hz) >= 1000u) {
            last_1hz = now;
            adc_measure(100);
            s_hist = *adc_result();
            s_hist_state = s_state;
            s_used_x10s += pwr_state_x10(s_state);
            bool chg = power_charging();
            chg_was_low = chg;
            if (s_ph == PH_LOAD) status_lines(el_ph);
            if (s_ph == PH_CHARGE) {
                if (chg) { chg_seen_low = true; chg_high_t = 0; }
                else if (chg_seen_low && chg_high_t == 0) chg_high_t = now;
                if (chg_high_t && (now - chg_high_t) >= C.chg_end_s * 1000u) { reason = "charge done"; reason_code = 6; break; }
            }
        }

        /* phase changes */
        if (s_ph == PH_LOAD && el_ph >= C.load_s * 1000u) {
            take(s_state, F_STEP);                         /* loaded V just before the step */
            enter_phase(s_xr_pending ? PH_XREST : PH_REST);
            if (s_ph == PH_XREST) s_xr_pending = false;
            EV.act = true; EV.finish = false; EV.flag = F_STEP; EV.i = 0; EV.n = 2; EV.offs = k_offs_step;
            EV.t0 = HAL_GetTick();
            last_samp = HAL_GetTick();
        } else if ((s_ph == PH_REST && el_ph >= C.rest_s * 1000u) || (s_ph == PH_XREST && el_ph >= C.xrest_s * 1000u)) {
            take(s_state, F_STEP);
            enter_phase(PH_LOAD);
            EV.act = true; EV.finish = false; EV.flag = F_STEP; EV.i = 0; EV.n = 2; EV.offs = k_offs_step;
            EV.t0 = HAL_GetTick();
            last_samp = HAL_GetTick();
        }
    }

    /* finish */
    load_end();
    if (!C.dry) {
        take(s_state, 0);
        write_end(reason_code, s_n, t_ms() / 1000u);
    }
    s_run = false;
    backlight_full();
    ui_con_begin("battlog");
    ui_con_line(COLOR_WHITE, "stopped: %s", reason);
    ui_con_line(COLOR_WHITE, "records %u (%s)", (unsigned)s_n, C.dry ? "RAM" : "W25Q");
    res_printf("{\"t\":\"battlog\",\"event\":\"end\",\"reason\":\"%s\",\"records\":%u,\"dry\":%s,\"write_errors\":%u}",
               reason, (unsigned)s_n, C.dry ? "true" : "false", (unsigned)s_werr);
    return verdict;
}

/* ---------------------------------------------------------------- commands */

static void dump(bool ram)
{
    uint32_t n = ram ? s_dry_n : 0;
    blhdr_t h;
    bool hv = false;
    if (!ram) {
        hv = read_hdr(&h);
        n = hv ? flash_count() : 0u;
    }
    if (n == 0) {
        proto_err("battlog", "nothing to dump");
        return;
    }
    uint32_t frames = (n + 4095u) / 4096u;
    res_printf("{\"t\":\"battlog_dump\",\"src\":\"%s\",\"records\":%" PRIu32 ",\"rec_size\":16,\"frames\":%" PRIu32 ",\"hdr\":%s,"
               "\"uid\":\"%.24s\",\"fw\":\"%.15s\",\"gain_q16\":%" PRIu32 ",\"offset_mv\":%d,\"rint_mohm\":%u}",
               ram ? "ram" : "flash", n, frames, hv ? "true" : "false", hv ? h.uid : "", hv ? h.fw : g_fw_hash,
               hv ? h.gain_q16 : g_hwcal.vbat_gain_q16, hv ? h.offset_mv : g_hwcal.vbat_offset_mv,
               hv ? h.rint_mohm : g_hwcal.rint_mohm);
    for (uint32_t f = 0; f < frames; f++) {
        uint32_t cnt = n - f * 4096u;
        if (cnt > 4096u) cnt = 4096u;
        const void *p;
        if (ram) {
            p = &s_dry[f * 4096u];
        } else {
            if (w25q_read(BL_DATA + f * 65536u, g_arena, cnt * 16u) != W25Q_OK) {
                proto_err("battlog", "read failed");
                return;
            }
            p = g_arena;
        }
        uint32_t crc = crc32_zlib(0, p, cnt * 16u);
        char hd[32];
        int k = snprintf(hd, sizeof(hd), "@B %" PRIu32 " %08" PRIX32 "\r\n", cnt * 16u, crc);
        if (!log_write_blocking(hd, (size_t)k, 2000u) || !log_write_blocking(p, cnt * 16u, 2000u)) {
            log_flush(100);
            proto_err("battlog", "usb stalled");
            return;
        }
    }
    proto_ok("battlog dump");
}

void battlog_cmd(int argc, char **argv, bool restricted)
{
    const char *sub = argc >= 2 ? argv[1] : "";
    if (strcmp(sub, "status") == 0) {
        blhdr_t h;
        bool hv = !s_run && read_hdr(&h);
        res_printf("{\"t\":\"battlog_status\",\"running\":%s,\"dry\":%s,\"phase\":\"%s\",\"t_s\":%u,\"records\":%u,"
                   "\"hdr\":%s,\"ended\":%s,\"flash_records\":%u,\"dry_records\":%u,\"write_errors\":%u,\"vbat_mv\":%u,\"est_pct\":%u}",
                   s_run ? "true" : "false", s_dry_mode ? "true" : "false", s_phase_name, (unsigned)(s_t_ms_now / 1000u),
                   (unsigned)s_n, hv ? "true" : "false", (hv && log_ended()) ? "true" : "false",
                   (unsigned)(hv ? flash_count() : 0u), (unsigned)s_dry_n, (unsigned)s_werr, s_hist.vbat_mv,
                   (unsigned)s_pct);
        proto_ok("battlog status");
    } else if (strcmp(sub, "stop") == 0) {
        if (s_run) {
            abort_request();
        } else {
            blhdr_t h;
            if (read_hdr(&h) && !log_ended()) write_end(7, flash_count(), 0);
        }
        proto_ok("battlog stop");
    } else if (restricted) {
        proto_err("battlog", "busy");
    } else if (strcmp(sub, "dump") == 0) {
        const char *src = suite_arg_str(argc - 1, &argv[1], "src", s_dry_n ? "ram" : "flash");
        dump(strcmp(src, "ram") == 0);
    } else if (strcmp(sub, "start") == 0) {
        const suite_t *s = suite_find("battlog");
        s_host_start = true;
        suite_run(s, argc - 1, &argv[1]);
        proto_ok("battlog start");
    } else {
        proto_err("battlog", "usage: battlog start [dry=1] [resume=1] [new=1] | stop | status | dump [src=ram|flash]");
    }
}

/* Unfinished log after a reset/power loss: offer to continue. */
void battlog_boot_check(void)
{
    blhdr_t h;
    if (!w25q_present() || !read_hdr(&h) || log_ended()) return;
    uint32_t cnt = flash_count();
    if (cnt == 0u) return;
    char t[96];
    static const char *const a[] = {"A=continue", "B=discard", NULL};
    snprintf(t, sizeof(t), "Unfinished battery log (%u records). Continue logging?", (unsigned)cnt);
    ui_answer_t r = ui_prompt("battlog_resume", t, a, 20000u);
    if (r == UI_ANS_OK) {
        char *av[2] = {"battlog", "resume=1"};
        s_host_start = true;
        suite_run(suite_find("battlog"), 2, av);
    } else if (r == UI_ANS_FAIL) {
        write_end(8, cnt, 0);
    }
    ui_invalidate();
}
