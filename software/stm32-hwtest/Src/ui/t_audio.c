/* P4 audio suites: audio.tone/sweep/chirp/noise/hiss/matrix/clip/pop ([U]) and audio.cpu (auto).

   Every step shows its id in big letters, emits `@R {"t":"step",...}` (marker for lining up the
   phone recording with the host log) and a progress bar. B aborts. Amp control is always
   pop-free (ramped) except where the suite is explicitly testing the naive sequences.
   Safe defaults: master level <= max_db, clip guard -6 dBFS (audio.clip unlocks 0 dBFS). */

#include "suites.h"
#include "audio.h"
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
#include <stdlib.h>
#include <string.h>

extern uint8_t g_arena[];

/* ------------------------------------------------------------------ helpers */

static const char *s_suite;
static char s_id[16], s_l1[48], s_l2[48];
static uint16_t s_col;
static uint32_t s_step_t0, s_step_dur, s_suite_t0, s_last_draw;
static unsigned s_step_k, s_step_n;

static void f1(char *b, size_t n, float v)
{
    int32_t x = (int32_t)(v * 10.0f + (v < 0 ? -0.5f : 0.5f));
    snprintf(b, n, "%s%ld.%ld", (x < 0) ? "-" : "", (long)(labs(x) / 10), (long)(labs(x) % 10));
}

static float argf(int argc, char **argv, const char *key, float def)
{
    const char *v = suite_arg_str(argc, argv, key, NULL);
    if (!v || !*v) return def;
    char *e;
    float r = strtof(v, &e);
    return (e == v) ? def : r;
}

static void draw_progress(uint32_t el_ms)
{
    int16_t w = (int16_t)lcd_width(), h = (int16_t)lcd_height();
    uint32_t dur = s_step_dur ? s_step_dur : 1u;
    uint32_t pct = (el_ms >= dur) ? 100u : (el_ms * 100u / dur);
    int16_t bw = (int16_t)(w - 8);
    gfx_rect(4, (int16_t)(h - 30), bw, 12, COLOR_GRAY);
    int16_t fw = (int16_t)((bw - 2) * (int32_t)pct / 100);
    gfx_rect_fill(5, (int16_t)(h - 29), fw, 10, s_col);
    gfx_rect_fill((int16_t)(5 + fw), (int16_t)(h - 29), (int16_t)(bw - 2 - fw), 10, COLOR_BLACK);
    char b[40];
    snprintf(b, sizeof(b), "%u/%u  %lu/%lu s  B=abort", s_step_k, s_step_n, (unsigned long)(el_ms / 1000u),
             (unsigned long)(s_step_dur / 1000u));
    gfx_text_pad(4, (int16_t)(h - 15), b, COLOR_GRAY, COLOR_BLACK, 1, (int16_t)(w - 8));
}

static void draw_step(void)
{
    int16_t w = (int16_t)lcd_width();
    gfx_fill(COLOR_BLACK);
    ui_status_bar();
    gfx_text_pad(2, 12, s_suite, COLOR_CYAN, COLOR_BLACK, 1, (int16_t)(w - 4));
    gfx_text_pad(4, 24, s_id, s_col, COLOR_BLACK, 5, (int16_t)(w - 8));
    gfx_text_pad(4, 68, s_l1, COLOR_WHITE, COLOR_BLACK, 2, (int16_t)(w - 8));
    gfx_text_pad(4, 88, s_l2, COLOR_WHITE, COLOR_BLACK, 2, (int16_t)(w - 8));
    draw_progress(0);
}

/* Begins a step: screen + @R marker. `f_hz`/`db` < -900 are omitted. */
static void step_begin(const char *id, const char *l1, const char *l2, uint16_t col, uint32_t dur_ms,
                       float f_hz, float db)
{
    snprintf(s_id, sizeof(s_id), "%s", id);
    snprintf(s_l1, sizeof(s_l1), "%s", l1);
    snprintf(s_l2, sizeof(s_l2), "%s", l2);
    s_col = col;
    s_step_k++;
    s_step_dur = dur_ms;
    s_step_t0 = HAL_GetTick();
    s_last_draw = 0;
    draw_step();
    char fb[16] = "null", db_[16] = "null";
    if (f_hz > -900.0f) f1(fb, sizeof(fb), f_hz);
    if (db > -900.0f) f1(db_, sizeof(db_), db);
    res_printf("{\"t\":\"step\",\"suite\":\"%s\",\"id\":\"%s\",\"l1\":\"%s\",\"l2\":\"%s\",\"f_hz\":%s,"
               "\"db\":%s,\"dur_ms\":%lu,\"t_ms\":%lu,\"n\":%u,\"of\":%u}",
               s_suite, s_id, s_l1, s_l2, fb, db_, (unsigned long)dur_ms,
               (unsigned long)(s_step_t0 - s_suite_t0), s_step_k, s_step_n);
}

/* Services USB, B=abort, progress redraw. false = abort. */
static bool prog_tick(void)
{
    sys_service();
    input_event_t ev;
    while (input_pop(&ev)) {
        if (ev.pressed && ev.button == BTN_B) {
            abort_request();
        }
    }
    if (abort_requested()) {
        return false;
    }
    uint32_t el = HAL_GetTick() - s_step_t0;
    if (el - s_last_draw >= 200u || s_last_draw == 0u) {
        s_last_draw = el ? el : 1u;
        draw_progress(el);
    }
    return true;
}

static bool hold_until(uint32_t ms)
{
    while ((HAL_GetTick() - s_step_t0) < ms) {
        if (!prog_tick()) return false;
    }
    draw_progress(ms);
    return true;
}

static void suite_begin(const char *name, unsigned nsteps)
{
    s_suite = name;
    s_step_k = 0;
    s_step_n = nsteps;
    s_suite_t0 = HAL_GetTick();
    input_flush();
}

/* ------------------------------------------------------------------ session */

typedef struct {
    unsigned carrier;
    aud_engine_t eng;
    uint32_t fs;
    uint32_t ramp;
} sess_t;

static sess_t sess_parse(int argc, char **argv)
{
    sess_t s;
    unsigned c = (g_hwcal.audio_cfg_id >= 1u && g_hwcal.audio_cfg_id <= AUD_NCARR) ? g_hwcal.audio_cfg_id - 1u : 1u;
    s.carrier = suite_arg_u32(argc, argv, "cfg", c);
    if (s.carrier >= AUD_NCARR) s.carrier = 1;
    const char *e = suite_arg_str(argc, argv, "eng", "b");
    s.eng = (e[0] == 'a' || e[0] == 'A') ? AUD_ENG_A : AUD_ENG_B;
    s.fs = suite_arg_u32(argc, argv, "fs", 46875u);
    s.ramp = suite_arg_u32(argc, argv, "ramp", g_hwcal.amp_ramp_ms ? g_hwcal.amp_ramp_ms : 20u);
    return s;
}

static bool sess_open(const sess_t *s, aud_dither_t d)
{
    audio_init();
    audio_silence();
    audio_set_master_db(-90.0f);
    audio_pwm_start(s->carrier, false);
    if (!audio_amp_on(s->ramp)) return false;
    return audio_engine_start(s->eng, s->fs, d);
}

static void sess_close(const sess_t *s)
{
    audio_set_master_db(-90.0f);
    audio_wait_ms(0);
    uint32_t t0 = time_us();
    while (time_us() - t0 < 15000u) sys_service();
    audio_silence();
    audio_engine_stop();
    audio_amp_off(s->ramp);
    audio_pwm_stop();
    audio_set_max_db(-6.0f);
}

static float clamp_tone(float db, bool *clamped)
{
    float m = audio_max_db();
    if (db > m) {
        if (clamped) *clamped = true;
        return m;
    }
    return db;
}

static void tone_on(float f, float db, aud_gen_t type)
{
    audio_voice_tone(0, type, f, 0.0f);
    audio_set_master_db(db);
}

static void tone_off(void)
{
    audio_set_master_db(-90.0f);
    uint32_t t0 = time_us();
    while (time_us() - t0 < 12000u) sys_service();
    audio_voice_off(0);
}

/* Measures the real sample/block rate over >= ms, using the ISR-entry timestamps (TIM2, 1 us) of
   two block/sample events so polling latency does not matter. Returns mHz (0 = failed/abort). */
static bool snap(bool a, uint32_t *cnt, uint32_t *us)
{
    for (int i = 0; i < 4; i++) {
        uint32_t c = a ? g_aud_stats.isr_count : (g_aud_stats.dma_ht + g_aud_stats.dma_tc);
        uint32_t t = g_aud_stats.last_us;
        uint32_t c2 = a ? g_aud_stats.isr_count : (g_aud_stats.dma_ht + g_aud_stats.dma_tc);
        if (c == c2) {
            *cnt = c;
            *us = t;
            return true;
        }
    }
    return false;
}

static uint32_t measure_rate(uint32_t ms)
{
    bool a = (audio_engine() == AUD_ENG_A);
    uint32_t unit = a ? 1u : AUD_HALF;
    uint32_t ca, ta, cb, tb;
    uint32_t t_start = time_us();
    if (!prog_tick() || !snap(a, &ca, &ta)) return 0;
    while ((int32_t)(time_us() - t_start - ms * 1000u) < 0) {
        if (!prog_tick()) return 0;
    }
    if (!snap(a, &cb, &tb) || tb == ta || cb == ca) return 0;
    return (uint32_t)(((uint64_t)(cb - ca) * unit * 1000000000ull) / (uint64_t)(tb - ta));
}

static void stats_emit(const char *name, uint32_t fs_meas_mhz)
{
    const aud_stats_t *s = &g_aud_stats;
    bool a = (audio_engine() == AUD_ENG_A);
    uint32_t calls = s->calls ? s->calls : 1u;
    res_printf("{\"t\":\"audio_stat\",\"suite\":\"%s\",\"name\":\"%s\",\"engine\":\"%s\",\"carrier\":\"%s\","
               "\"rcr_div\":%lu,\"fs_exp_mhz\":%lu,\"fs_meas_mhz\":%lu,\"isr\":%lu,\"dma_ht\":%lu,\"dma_tc\":%lu,"
               "\"late\":%lu,\"dma_err\":%lu,\"clipped\":%lu,\"cyc_avg\":%lu,\"cyc_max\":%lu,\"pass\":%s}",
               s_suite, name, a ? "A" : "B", g_aud_carriers[audio_carrier()].name,
               (unsigned long)audio_rcr_div(), (unsigned long)audio_fs_mhz(), (unsigned long)fs_meas_mhz,
               (unsigned long)s->isr_count, (unsigned long)s->dma_ht, (unsigned long)s->dma_tc,
               (unsigned long)s->late, (unsigned long)s->dma_err, (unsigned long)s->clipped,
               (unsigned long)(s->cyc_sum / calls), (unsigned long)s->cyc_max,
               (fs_meas_mhz && s->dma_err == 0 &&
                (uint64_t)fs_meas_mhz * 1000u > (uint64_t)audio_fs_mhz() * 995u &&
                (uint64_t)fs_meas_mhz * 1000u < (uint64_t)audio_fs_mhz() * 1005u)
                   ? "true" : "false");
}

/* ------------------------------------------------------------------ verdict picker */

/* Returns the chosen index, -1 = skipped/timeout, -2 = aborted. */
static int pick(const char *id, const char *title, const char *const *opts, int n, int def)
{
    static const char *const ans[] = {"UP/DN=choose", "A=pick", "B=skip", NULL};
    ui_prompt_emit(id, title, ans, 300000u);
    int16_t w = (int16_t)lcd_width(), h = (int16_t)lcd_height();
    int rows = (h - 40) / 10;
    int sel = def < 0 ? 0 : def, top = 0, res = -1;
    bool redraw = true;
    const char *why = "timeout";
    uint32_t t0 = HAL_GetTick();
    input_flush();
    for (;;) {
        sys_service();
        if (redraw) {
            redraw = false;
            if (sel < top) top = sel;
            if (sel >= top + rows) top = sel - rows + 1;
            gfx_fill(COLOR_BLACK);
            ui_status_bar();
            gfx_text_pad(2, 12, title, COLOR_YELLOW, COLOR_BLACK, 1, (int16_t)(w - 4));
            for (int i = 0; i < rows && top + i < n; i++) {
                bool s = (top + i == sel);
                gfx_text_pad(2, (int16_t)(24 + 10 * i), opts[top + i], s ? COLOR_BLACK : COLOR_WHITE,
                             s ? COLOR_CYAN : COLOR_BLACK, 1, (int16_t)(w - 4));
            }
            gfx_text(2, (int16_t)(h - 9), "UP/DN/L/R move  A=pick  B=skip", COLOR_GRAY, COLOR_BLACK, 1);
        }
        input_event_t ev;
        if (input_pop(&ev) && ev.pressed) {
            if (ev.button == BTN_UP && sel > 0) { sel--; redraw = true; }
            else if (ev.button == BTN_DOWN && sel < n - 1) { sel++; redraw = true; }
            else if (ev.button == BTN_LEFT) { sel = sel > 5 ? sel - 6 : 0; redraw = true; }
            else if (ev.button == BTN_RIGHT) { sel = sel < n - 7 ? sel + 6 : n - 1; redraw = true; }
            else if (ev.button == BTN_A) { res = sel; why = opts[sel]; break; }
            else if (ev.button == BTN_B) { why = "skip"; break; }
        }
        ui_answer_t a;
        if (ui_host_answer_take(id, &a)) {
            if (a == UI_ANS_OK) { res = sel; why = opts[sel]; } else why = "skip";
            break;
        }
        if (abort_requested()) { res = -2; why = "abort"; break; }
        if (HAL_GetTick() - t0 > 300000u) break;
    }
    ui_answer_emit(id, why);
    ui_con_begin(id);
    return res;
}

static void cal_emit(const char *suite, const char *name, const char *value)
{
    res_printf("{\"t\":\"cal\",\"suite\":\"%s\",\"name\":\"%s\",\"value\":\"%s\",\"saved_to\":\"ram\"}", suite, name,
               value);
}

/* ------------------------------------------------------------------ tone / sweep / chirp / noise */

suite_verdict_t suite_audio_tone(int argc, char **argv)
{
    float f = argf(argc, argv, "f", 1000.0f), db = argf(argc, argv, "db", -12.0f);
    uint32_t s = suite_arg_u32(argc, argv, "s", 5u);
    if (s < 1u) s = 1u;
    if (s > 600u) s = 600u;
    sess_t se = sess_parse(argc, argv);
    if (!suite_user_start("audio.tone", "audio.tone: sine, amp ON, quiet. A=start", argc, argv)) return SUITE_ABORT;
    suite_begin("audio.tone", 1);
    bool clamped = false;
    db = clamp_tone(db, &clamped);
    if (!sess_open(&se, AUD_DITHER_OFF)) {
        sess_close(&se);
        return SUITE_ABORT;
    }
    char l1[48], l2[48], a[16], b[16];
    f1(a, sizeof(a), f);
    f1(b, sizeof(b), db);
    snprintf(l1, sizeof(l1), "%s Hz  %s dBFS", a, b);
    snprintf(l2, sizeof(l2), "%s %s %luHz", g_aud_carriers[se.carrier].name, se.eng == AUD_ENG_A ? "ISR" : "DMA",
              (unsigned long)(audio_fs_mhz() / 1000u));
    step_begin("T1", l1, l2, COLOR_YELLOW, s * 1000u, f, db);
    tone_on(f, db, AUD_GEN_SINE);
    bool ok = true;
    uint32_t rate = 0;
    if (s >= 2u) {
        rate = measure_rate(1000u);
        ok = (rate != 0u) || abort_requested();
    }
    if (hold_until(s * 1000u)) {
        stats_emit("tone", rate);
    }
    res_printf("{\"t\":\"test\",\"suite\":\"audio.tone\",\"name\":\"tone\",\"pass\":%s,\"clamped\":%s,\"f_hz\":%s,\"db\":%s}",
               ok ? "true" : "false", clamped ? "true" : "false", a, b);
    tone_off();
    sess_close(&se);
    return abort_requested() ? SUITE_ABORT : (ok ? SUITE_PASS : SUITE_FAIL);
}

static const uint16_t s_iso[] = {50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250,
                                 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500};
#define N_ISO ((unsigned)(sizeof(s_iso) / sizeof(s_iso[0])))

suite_verdict_t suite_audio_sweep(int argc, char **argv)
{
    float db = argf(argc, argv, "db", -12.0f);
    uint32_t step_s = suite_arg_u32(argc, argv, "s", 3u);
    /* fmin: tones below it are replaced by silence of the same length (timing grid unchanged for
       phyphox_sweep.py); default 200 Hz protects the speaker (large excursion below ~200 Hz, F14) */
    uint32_t fmin = suite_arg_u32(argc, argv, "fmin", 200u);
    sess_t se = sess_parse(argc, argv);
    if (!suite_user_start("audio.sweep", "audio.sweep: 25 tones 50 Hz-12.5 kHz, 3 s each, ~100 s. Phone near speaker. A=start", argc, argv))
        return SUITE_ABORT;
    suite_begin("audio.sweep", N_ISO);
    bool clamped = false;
    db = clamp_tone(db, &clamped);
    if (!sess_open(&se, AUD_DITHER_OFF)) {
        sess_close(&se);
        return SUITE_ABORT;
    }
    bool ok = true;
    for (unsigned i = 0; i < N_ISO && ok; i++) {
        char id[12], l1[48], l2[48], b[16];
        f1(b, sizeof(b), db);
        snprintf(id, sizeof(id), "S%02u", i + 1u);
        snprintf(l1, sizeof(l1), "%u Hz", s_iso[i]);
        snprintf(l2, sizeof(l2), "%s dBFS  %s", b, g_aud_carriers[se.carrier].name);
        bool play = s_iso[i] >= fmin;
        if (!play) snprintf(l2, sizeof(l2), "skipped (fmin %lu)", (unsigned long)fmin);
        step_begin(id, l1, l2, COLOR_YELLOW, step_s * 1000u, (float)s_iso[i], play ? db : -120.0f);
        if (play) tone_on((float)s_iso[i], db, AUD_GEN_SINE);
        ok = hold_until(step_s * 1000u);
        if (play) tone_off();
        if (ok && i + 1u < N_ISO) {
            /* 0.5 s silence + 20 ms 1 kHz tick (boundary marker) + 0.1 s silence */
            res_printf("{\"t\":\"mark\",\"suite\":\"audio.sweep\",\"id\":\"%s\",\"ev\":\"gap\"}", id);
            ok = audio_wait_ms(500);
            if (ok) {
                tone_on(1000.0f, db, AUD_GEN_SINE);
                ok = audio_wait_ms(20);
                tone_off();
            }
            if (ok) ok = audio_wait_ms(100);
        }
    }
    stats_emit("sweep", 0);
    sess_close(&se);
    return abort_requested() ? SUITE_ABORT : SUITE_PASS;
}

suite_verdict_t suite_audio_chirp(int argc, char **argv)
{
    float db = argf(argc, argv, "db", -12.0f);
    uint32_t dur = suite_arg_u32(argc, argv, "s", 10u);
    sess_t se = sess_parse(argc, argv);
    if (!suite_user_start("audio.chirp", "audio.chirp: 20 Hz-15 kHz exp. 10 s x3, ~35 s. A=start", argc, argv))
        return SUITE_ABORT;
    suite_begin("audio.chirp", 3);
    bool clamped = false;
    db = clamp_tone(db, &clamped);
    if (!sess_open(&se, AUD_DITHER_OFF)) {
        sess_close(&se);
        return SUITE_ABORT;
    }
    bool ok = true;
    for (unsigned i = 0; i < 3 && ok; i++) {
        char id[12], l2[48], b[16];
        f1(b, sizeof(b), db);
        snprintf(id, sizeof(id), "CH%u", i + 1u);
        snprintf(l2, sizeof(l2), "%s dBFS  %s", b, g_aud_carriers[se.carrier].name);
        step_begin(id, "20 Hz -> 15 kHz", l2, COLOR_YELLOW, dur * 1000u, -999.0f, db);
        audio_voice_chirp(0, 20.0f, 15000.0f, (float)dur, 0.0f);
        audio_set_master_db(db);
        ok = hold_until(dur * 1000u);
        tone_off();
        if (ok && i < 2u) ok = audio_wait_ms(1000);
    }
    stats_emit("chirp", 0);
    sess_close(&se);
    return abort_requested() ? SUITE_ABORT : SUITE_PASS;
}

suite_verdict_t suite_audio_noise(int argc, char **argv)
{
    const char *type = suite_arg_str(argc, argv, "type", "pink");
    bool pink = strcmp(type, "white") != 0;
    float db = argf(argc, argv, "db", -15.0f);
    uint32_t s = suite_arg_u32(argc, argv, "s", 30u);
    sess_t se = sess_parse(argc, argv);
    if (!suite_user_start("audio.noise", "audio.noise: pink/white noise, amp ON, quiet. A=start", argc, argv))
        return SUITE_ABORT;
    suite_begin("audio.noise", 1);
    float lim = audio_max_db() - 9.0f;      /* RMS cap: keep the clip guard from flattening the peaks */
    bool clamped = db > lim;
    if (clamped) db = lim;
    if (!sess_open(&se, AUD_DITHER_OFF)) {
        sess_close(&se);
        return SUITE_ABORT;
    }
    char l1[48], l2[48], b[16];
    f1(b, sizeof(b), db);
    snprintf(l1, sizeof(l1), "%s noise %s dB", pink ? "PINK" : "WHITE", b);
    snprintf(l2, sizeof(l2), "%s RMS dBFS", g_aud_carriers[se.carrier].name);
    step_begin(pink ? "N-PINK" : "N-WHITE", l1, l2, COLOR_YELLOW, s * 1000u, -999.0f, db);
    audio_voice_noise(0, pink, db);
    audio_set_master_db(0.0f);
    uint32_t rate = 0;
    bool ok = true;
    if (s >= 2u) {
        rate = measure_rate(1000u);
        ok = (rate != 0u) || abort_requested();
    }
    if (hold_until(s * 1000u)) {
        stats_emit("noise", rate);
    }
    res_printf("{\"t\":\"test\",\"suite\":\"audio.noise\",\"name\":\"noise\",\"pass\":%s,\"clamped\":%s,\"type\":\"%s\",\"db\":%s}",
               ok ? "true" : "false", clamped ? "true" : "false", pink ? "pink" : "white", b);
    tone_off();
    sess_close(&se);
    return abort_requested() ? SUITE_ABORT : (ok ? SUITE_PASS : SUITE_FAIL);
}

/* ------------------------------------------------------------------ hiss */

suite_verdict_t suite_audio_hiss(int argc, char **argv)
{
    uint32_t s = suite_arg_u32(argc, argv, "s", 15u);
    sess_t se = sess_parse(argc, argv);
    static const char *const labels[] = {"H-a  amp off", "H-b1 amp on PA8 low", "H-b2 amp on PA8 high",
                                         "H-b3 amp on PA8 hi-Z", "H-c1 amp on PWM 750k/7b", "H-c2 amp on PWM 375k/8b",
                                         "H-c3 amp on PWM 187k/9b", "H-c4 amp on PWM 94k/10b"};
    static const char *const ids[] = {"H-a", "H-b1", "H-b2", "H-b3", "H-c1", "H-c2", "H-c3", "H-c4"};
    if (!suite_user_start("audio.hiss", "audio.hiss: 8 idle cases x15 s (no signal), ~2.5 min, expect amp on/off pops. A=start",
                          argc, argv))
        return SUITE_ABORT;
    suite_begin("audio.hiss", 8);
    audio_init();
    bool ok = true;
    for (unsigned i = 0; i < 8 && ok; i++) {
        /* quiet transition into the case */
        if (audio_amp_is_on()) audio_amp_off(se.ramp);
        audio_pwm_stop();
        if (i == 0) {
            audio_pa8_mode(AUD_PA8_LOW);
        } else if (i <= 3) {
            audio_pa8_mode(i == 1 ? AUD_PA8_LOW : i == 2 ? AUD_PA8_HIGH : AUD_PA8_HIZ);
            audio_amp_raw(true);
        } else {
            audio_pwm_start(i - 4u, false);
            audio_amp_on(se.ramp);
        }
        char l2[48];
        snprintf(l2, sizeof(l2), "idle, no signal");
        step_begin(ids[i], labels[i] + 5, l2, COLOR_ORANGE, s * 1000u, -999.0f, -999.0f);
        ok = hold_until(s * 1000u);
    }
    if (audio_amp_is_on()) audio_amp_off(se.ramp);
    audio_shutdown();
    if (!ok) return SUITE_ABORT;
    int p = pick("audio_hiss_best", "Quietest idle (lowest hiss)?", labels, 8, 0);
    if (p >= 0) {
        cal_emit("audio.hiss", "hiss_best", ids[p]);
        if (p >= 4) g_hwcal.audio_cfg_id = (uint8_t)(p - 3);
    }
    return p == -2 ? SUITE_ABORT : SUITE_PASS;
}

/* ------------------------------------------------------------------ matrix */

#define MX_N (AUD_NCARR * 4u * 3u)
static const uint32_t s_mx_fs[4] = {11025u, 16000u, 22050u, 32000u};
static const char *const s_mx_fsn[4] = {"11k", "16k", "22k", "32k"};
static const char *const s_mx_dn[3] = {"OFF", "TPDF", "NS"};

static void mx_label(unsigned idx, char *id, size_t ni, char *lab, size_t nl)
{
    unsigned c = idx / 12u, fsi = (idx / 3u) % 4u, d = idx % 3u;
    snprintf(id, ni, "M%02u", idx + 1u);
    snprintf(lab, nl, "%s %s %s", g_aud_carriers[c].name, s_mx_fsn[fsi], s_mx_dn[d]);
}

suite_verdict_t suite_audio_matrix(int argc, char **argv)
{
    uint32_t idle = suite_arg_u32(argc, argv, "idle", 8u), tone = suite_arg_u32(argc, argv, "tone", 8u);
    uint32_t from = suite_arg_u32(argc, argv, "from", 1u), to = suite_arg_u32(argc, argv, "to", MX_N);
    uint32_t ramp = suite_arg_u32(argc, argv, "ramp", g_hwcal.amp_ramp_ms ? g_hwcal.amp_ramp_ms : 20u);
    if (from < 1u) from = 1u;
    if (to > MX_N) to = MX_N;
    if (!suite_user_start("audio.matrix", "audio.matrix: 48 configs x (8 s idle + 8 s 1 kHz -6 dB), ~13 min. A=start",
                          argc, argv))
        return SUITE_ABORT;
    suite_begin("audio.matrix", to >= from ? to - from + 1u : 0u);
    audio_init();
    sess_t se = {.carrier = 0, .eng = AUD_ENG_B, .fs = 22050u, .ramp = ramp};
    bool ok = true, first = true;
    for (uint32_t idx = from - 1u; idx < to && ok; idx++) {
        unsigned c = idx / 12u, fsi = (idx / 3u) % 4u, d = idx % 3u;
        char id[12], lab[40], l1[48], l2[48];
        mx_label(idx, id, sizeof(id), lab, sizeof(lab));
        audio_set_master_db(-90.0f);
        if (first) {
            audio_silence();
            audio_pwm_start(c, false);
            audio_amp_on(ramp);
            first = false;
        } else {
            audio_engine_stop();
            audio_pwm_start(c, true);
        }
        audio_engine_start(AUD_ENG_B, s_mx_fs[fsi], (aud_dither_t)d);
        snprintf(l1, sizeof(l1), "%s", lab);
        snprintf(l2, sizeof(l2), "IDLE  Fs=%luHz", (unsigned long)(audio_fs_mhz() / 1000u));
        step_begin(id, l1, l2, COLOR_ORANGE, (idle + tone) * 1000u, 0.0f, -999.0f);
        ok = hold_until(idle * 1000u);
        if (!ok) break;
        snprintf(s_l2, sizeof(s_l2), "1 kHz -6 dBFS");
        gfx_text_pad(4, 88, s_l2, COLOR_GREEN, COLOR_BLACK, 2, (int16_t)(lcd_width() - 8));
        res_printf("{\"t\":\"mark\",\"suite\":\"audio.matrix\",\"id\":\"%s\",\"ev\":\"tone\",\"f_hz\":1000.0,\"db\":-6.0,"
                   "\"fs_mhz\":%lu,\"rcr_div\":%lu,\"carrier\":\"%s\",\"dither\":\"%s\",\"t_ms\":%lu}",
                   id, (unsigned long)audio_fs_mhz(), (unsigned long)audio_rcr_div(), g_aud_carriers[c].name,
                   s_mx_dn[d], (unsigned long)(HAL_GetTick() - s_suite_t0));
        tone_on(1000.0f, -6.0f, AUD_GEN_SINE);
        ok = hold_until((idle + tone) * 1000u);
        tone_off();
    }
    stats_emit("matrix", 0);
    sess_close(&se);
    if (!ok) return SUITE_ABORT;

    char (*lbl)[24] = (char (*)[24])(void *)(g_arena + 4096);
    static const char *ptr[MX_N];
    for (unsigned i = 0; i < MX_N; i++) {
        char id[12], lab[40];
        mx_label(i, id, sizeof(id), lab, sizeof(lab));
        snprintf(lbl[i], 24, "%.3s %.19s", id, lab);
        ptr[i] = lbl[i];
    }
    int p = pick("audio_matrix_best", "Best overall config (clean, low hiss)?", ptr, (int)MX_N, (int)from - 1);
    if (p >= 0) {
        char id[12], lab[40];
        mx_label((unsigned)p, id, sizeof(id), lab, sizeof(lab));
        cal_emit("audio.matrix", "matrix_best", id);
        g_hwcal.audio_cfg_id = (uint8_t)(p / 12 + 1);
    }
    return p == -2 ? SUITE_ABORT : SUITE_PASS;
}

/* ------------------------------------------------------------------ clip */

suite_verdict_t suite_audio_clip(int argc, char **argv)
{
    uint32_t step_s = suite_arg_u32(argc, argv, "s", 2u);
    sess_t se = sess_parse(argc, argv);
    if (!suite_user_start("audio.clip", "audio.clip: 1 kHz then 200 Hz, -20..0 dBFS in 1 dB steps, 2 s each, ~90 s. LOUD at the end: lower the phone gain. A=start",
                          argc, argv))
        return SUITE_ABORT;
    suite_begin("audio.clip", 42);
    audio_init();
    if (!sess_open(&se, AUD_DITHER_OFF)) {
        sess_close(&se);
        return SUITE_ABORT;
    }
    audio_set_max_db(0.0f);   /* this suite is what unlocks the full range (reset by sess_close) */
    bool ok = true;
    static const float fq[2] = {1000.0f, 200.0f};
    for (unsigned r = 0; r < 2 && ok; r++) {
        for (int db = -20; db <= 0 && ok; db++) {
            char id[12], l1[48], l2[48];
            snprintf(id, sizeof(id), "C%u%+d", r + 1u, db);
            snprintf(l1, sizeof(l1), "%s Hz  %d dBFS", r ? "200" : "1000", db);
            snprintf(l2, sizeof(l2), "%s", g_aud_carriers[se.carrier].name);
            step_begin(id, l1, l2, db >= -3 ? COLOR_RED : COLOR_YELLOW, step_s * 1000u, fq[r], (float)db);
            tone_on(fq[r], (float)db, AUD_GEN_SINE);
            ok = hold_until(step_s * 1000u);
            tone_off();
            if (ok) ok = audio_wait_ms(150);
        }
    }
    stats_emit("clip", 0);
    sess_close(&se);
    if (!ok) return SUITE_ABORT;

    static char lbl[21][12];
    static const char *ptr[21];
    for (int i = 0; i < 21; i++) {
        snprintf(lbl[i], sizeof(lbl[i]), "%d dBFS", -20 + i);
        ptr[i] = lbl[i];
    }
    int p1 = pick("audio_clip_1k", "Highest CLEAN level at 1 kHz?", ptr, 21, 14);
    if (p1 >= 0) {
        char v[12];
        snprintf(v, sizeof(v), "%d", -20 + p1);
        cal_emit("audio.clip", "clean_max_db_1k", v);
    }
    int p2 = (p1 == -2) ? -2 : pick("audio_clip_200", "Highest CLEAN level at 200 Hz?", ptr, 21, 14);
    if (p2 >= 0) {
        char v[12];
        snprintf(v, sizeof(v), "%d", -20 + p2);
        cal_emit("audio.clip", "clean_max_db_200", v);
    }
    return (p1 == -2 || p2 == -2) ? SUITE_ABORT : SUITE_PASS;
}

/* ------------------------------------------------------------------ pop */

suite_verdict_t suite_audio_pop(int argc, char **argv)
{
    sess_t se = sess_parse(argc, argv);
    static const uint16_t seq[6] = {0, 20, 0, 50, 0, 100};
    if (!suite_user_start("audio.pop", "audio.pop: 18 amp on/off cycles (naive vs 20/50/100 ms ramp), ~70 s. Listen for pops. A=start",
                          argc, argv))
        return SUITE_ABORT;
    suite_begin("audio.pop", 18);
    audio_init();
    bool ok = true;
    for (unsigned k = 0; k < 18u && ok; k++) {
        uint32_t ramp = seq[k % 6u];
        char id[12], l1[48], l2[48];
        snprintf(id, sizeof(id), "P%02u", k + 1u);
        if (ramp) snprintf(l1, sizeof(l1), "RAMP %lu ms", (unsigned long)ramp);
        else snprintf(l1, sizeof(l1), "NAIVE");
        snprintf(l2, sizeof(l2), "on 1.5 s, off 1.5 s");
        audio_pwm_start(se.carrier, ramp == 0u);
        ok = audio_wait_ms(400);
        if (!ok) break;
        step_begin(id, l1, l2, ramp ? COLOR_GREEN : COLOR_RED, 3400u, -999.0f, -999.0f);
        res_printf("{\"t\":\"mark\",\"suite\":\"audio.pop\",\"id\":\"%s\",\"ev\":\"on\",\"ramp_ms\":%lu}", id, (unsigned long)ramp);
        audio_amp_on(ramp);
        ok = hold_until(1900u);
        if (!ok) break;
        res_printf("{\"t\":\"mark\",\"suite\":\"audio.pop\",\"id\":\"%s\",\"ev\":\"off\",\"ramp_ms\":%lu}", id, (unsigned long)ramp);
        audio_amp_off(ramp);
        ok = hold_until(3400u);
        audio_pwm_stop();
    }
    audio_shutdown();
    if (!ok) return SUITE_ABORT;
    static const char *const opt[] = {"NAIVE (0 ms)", "RAMP 20 ms", "RAMP 50 ms", "RAMP 100 ms"};
    static const uint16_t val[4] = {0, 20, 50, 100};
    int p = pick("audio_pop_best", "Quietest on/off sequence (fewest pops)?", opt, 4, 1);
    if (p >= 0) {
        char v[12];
        snprintf(v, sizeof(v), "%u", val[p]);
        cal_emit("audio.pop", "amp_ramp_ms", v);
        g_hwcal.amp_ramp_ms = val[p];
    }
    return p == -2 ? SUITE_ABORT : SUITE_PASS;
}

/* ------------------------------------------------------------------ cpu (auto, amp OFF) */

static uint32_t spin_count(uint32_t ms)
{
    uint32_t t0 = HAL_GetTick(), n = 0;
    volatile uint32_t x = 1;
    while (HAL_GetTick() - t0 < ms) {
        for (int i = 0; i < 32; i++) x = x * 1664525u + 1013904223u;
        n++;
    }
    return n;
}

suite_verdict_t suite_audio_cpu(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    suite_begin("audio.cpu", 5);
    audio_init();
    audio_amp_raw(false);
    audio_pwm_start(1, true);   /* carrier running, amp stays OFF */
    audio_set_master_db(-30.0f);
    audio_set_max_db(-6.0f);
    uint32_t base = spin_count(1000);
    uint32_t b2 = spin_count(1000);
    if (b2 > base) base = b2;
    static const struct { aud_engine_t e; unsigned ch; aud_dither_t d; const char *name; } cfg[5] = {
        {AUD_ENG_A, 1, AUD_DITHER_OFF, "A_1ch_off"},    {AUD_ENG_A, 4, AUD_DITHER_TPDF, "A_4ch_tpdf"},
        {AUD_ENG_B, 1, AUD_DITHER_OFF, "B_1ch_off"},    {AUD_ENG_B, 4, AUD_DITHER_TPDF, "B_4ch_tpdf"},
        {AUD_ENG_B, 4, AUD_DITHER_SHAPED, "B_4ch_shaped"},
    };
    bool ok = true;
    for (unsigned i = 0; i < 5 && ok; i++) {
        audio_silence();
        audio_engine_stop();
        if (!audio_engine_start(cfg[i].e, 22050u, cfg[i].d)) { ok = false; break; }
        audio_voice_tone(0, AUD_GEN_SINE, 1000.0f, -6.0f);
        if (cfg[i].ch >= 2) audio_voice_tone(1, AUD_GEN_SQUARE, 440.0f, -12.0f);
        if (cfg[i].ch >= 3) audio_voice_noise(2, false, -18.0f);
        if (cfg[i].ch >= 4) audio_voice_noise(3, true, -18.0f);
        char id[8];
        snprintf(id, sizeof(id), "CPU%u", i + 1u);
        step_begin(id, cfg[i].name, "22.05 kHz, amp OFF", COLOR_CYAN, 1500u, -999.0f, -999.0f);
        audio_stats_reset();
        uint32_t n = spin_count(1000);
        if (!prog_tick()) { ok = false; break; }
        aud_stats_t st = g_aud_stats;
        uint32_t calls = st.calls ? st.calls : 1u;
        uint32_t avg = st.cyc_sum / calls;
        uint32_t per_sample = (cfg[i].e == AUD_ENG_A) ? avg : avg / AUD_HALF;
        uint32_t load_x100 = (n >= base) ? 0u : (uint32_t)(((uint64_t)(base - n) * 10000u) / base);
        uint32_t body_x100 = (uint32_t)(((uint64_t)per_sample * audio_fs_mhz() * 10u) / SystemCoreClock);
        res_printf("{\"t\":\"audio_cpu\",\"suite\":\"audio.cpu\",\"name\":\"%s\",\"engine\":\"%s\",\"voices\":%u,"
                   "\"fs_mhz\":%lu,\"calls\":%lu,\"cyc_avg\":%lu,\"cyc_min\":%lu,\"cyc_max\":%lu,"
                   "\"cyc_per_sample\":%lu,\"body_pct_x100\":%lu,\"load_pct_x100\":%lu,\"late\":%lu,\"dma_err\":%lu,"
                   "\"spin\":%lu,\"spin_base\":%lu,\"pass\":%s}",
                   cfg[i].name, cfg[i].e == AUD_ENG_A ? "A" : "B", cfg[i].ch, (unsigned long)audio_fs_mhz(),
                   (unsigned long)st.calls, (unsigned long)avg, (unsigned long)st.cyc_min, (unsigned long)st.cyc_max,
                   (unsigned long)per_sample, (unsigned long)body_x100, (unsigned long)load_x100,
                   (unsigned long)st.late, (unsigned long)st.dma_err, (unsigned long)n, (unsigned long)base,
                   (st.late == 0 && st.dma_err == 0) ? "true" : "false");
    }
    audio_silence();
    audio_shutdown();
    return abort_requested() ? SUITE_ABORT : (ok ? SUITE_PASS : SUITE_FAIL);
}
