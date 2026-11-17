/* Voltage-only battery gauge, see Inc/gauge.h and docs/battery-gauge-design.md.
 *
 * Pure C11, no HAL / malloc / libm / double. Float math uses single precision
 * only (f-suffixed literals) so the Cortex-M4F FPU handles it in hardware. */

#include "gauge.h"

#include <stddef.h>

/* Generic 1S LiPo rested OCV curve (same numbers as hwcal_defaults() in
   Src/hwcal.c), SoC 0,5,...,100 %. Typical LiCoO2/graphite pouch-cell shape;
   0 % is set at 3.30 V = the loaded cut-off, i.e. "usable" SoC. Replaced by
   the curve measured on this cell (P2b log -> tools/battlog_to_ocv.py). */
static const uint16_t k_ocv_default[GAUGE_OCV_POINTS] = {
    3320, 3475, 3505, 3541, 3568, 3589, 3614, 3647, 3667, 3688, 3741,
    3781, 3826, 3865, 3899, 3954, 3994, 4009, 4060, 4110, 4161,
};

#define RINT_DEFAULT_MOHM 150u
#define ICHG_DEFAULT_MA   175u
#define CAP_DEFAULT_MAH   310u

void gauge_cfg_defaults(gauge_cfg_t *c)
{
    static const uint8_t thr[GAUGE_N_SEGMENTS] = {5, 25, 45, 65, 85};
    /* plan §C order: S0 off, S1 Stop, S2 WFI, S3..S7 backlight 10..100 %, S8 CPU max,
       S9 LCD DMA, S10 amp idle, S11 1 kHz tone, S12 pink noise, S13 flash read,
       S14 flash erase/program, S15 game mix, S16 charge, S17 overclock.
       Placeholder estimates until the S-state current table is measured (P2). */
    static const uint16_t ist[GAUGE_N_STATES] = {
        0, 1, 12, 15, 20, 28, 36, 45, 30, 20, 8, 20, 40, 5, 15, 75, 175, 40,
    };
    size_t i;
    for (i = 0; i < GAUGE_OCV_POINTS; i++) c->ocv_mv[i] = k_ocv_default[i];
    for (i = 0; i < GAUGE_N_STATES; i++) c->i_state_ma[i] = ist[i];
    for (i = 0; i < GAUGE_N_SEGMENTS; i++) c->seg_thr_pct[i] = thr[i];
    c->rint_mohm = RINT_DEFAULT_MOHM;
    c->i_chg_ma = ICHG_DEFAULT_MA;
    c->capacity_mah = CAP_DEFAULT_MAH;
    c->hyst_pct = 3;
    c->drop_dwell_ms = 20000;
    c->drop_min_interval_ms = 60000;
    c->escape_pct = 15;
    c->escape_ms = 300000;
    c->rise_dwell_ms = 20000;
    c->tau_fast_ms = 2000;
    c->tau_slow_ms = 60000;
    c->fast_window_ms = 10000;
    c->boot_estimate_ms = 3000;
    c->relax_ms = 180000;
    c->chg_debounce_ms = 2000;
    c->full_min_mv = 4100;
    c->full_idle_ms = 10000;
    c->cv_mv = 4170;
    c->chg_cc_eff_pct = 90;
    c->chg_cv_eff_pct = 40;
    c->chg_cap_level = 4;
    c->warn_mv = 3500;
    c->warn_ms = 10000;
    c->crit_mv = 3400;
    c->crit_ms = 10000;
    c->crit_loaded_mv = 3300;
    c->crit_loaded_count = 3;
    c->nobatt_spread_mv = 200;
    c->nobatt_min_mv = 2700;
    c->nobatt_max_mv = 4350;
    c->nobatt_ms = 2000;
    c->nobatt_clear_ms = 10000;
}

/* ---------------- measurement helpers ---------------- */

uint32_t gauge_trimmed_mean(uint16_t *buf, uint32_t n, uint32_t drop, uint16_t *spread_raw)
{
    uint32_t i, j, sum = 0, cnt;
    if (n == 0) {
        if (spread_raw) *spread_raw = 0;
        return 0;
    }
    for (i = 1; i < n; i++) {           /* insertion sort, n is tiny */
        uint16_t v = buf[i];
        j = i;
        while (j > 0 && buf[j - 1] > v) {
            buf[j] = buf[j - 1];
            j--;
        }
        buf[j] = v;
    }
    if (spread_raw) *spread_raw = (uint16_t)(buf[n - 1] - buf[0]);
    if (2u * drop >= n) drop = (n - 1u) / 2u;
    cnt = n - 2u * drop;
    for (i = drop; i < n - drop; i++) sum += buf[i];
    return (sum * 16u + cnt / 2u) / cnt;  /* Q4 */
}

uint16_t gauge_adc_to_mv(uint32_t raw_bat, uint32_t raw_vref, uint16_t vrefint_cal,
                         uint32_t gain_q16, int16_t offset_mv)
{
    /* Vbat = raw_bat*3300*VREFINT_CAL/(raw_vref*4095) * 115/82 * gain + offset
       First the uncalibrated voltage in mV/1024 (keeps sub-mV resolution), then
       gain (Q16), then round to mV and add the offset. */
    uint64_t num, den, v1024, vg;
    int32_t mv;
    if (raw_vref == 0u) return 0u;
    num = (uint64_t)raw_bat * 3300u * vrefint_cal * 115u * 1024u;
    den = (uint64_t)raw_vref * 4095u * 82u;
    v1024 = (num + den / 2u) / den;
    vg = (v1024 * gain_q16 + (1u << 25)) >> 26;  /* /1024 and /65536, rounded */
    mv = (int32_t)vg + offset_mv;
    if (mv < 0) mv = 0;
    if (mv > 65535) mv = 65535;
    return (uint16_t)mv;
}

uint16_t gauge_sum_states(const gauge_cfg_t *cfg, uint32_t state_mask)
{
    uint32_t sum = 0, i;
    for (i = 0; i < GAUGE_N_STATES; i++)
        if (state_mask & (1u << i)) sum += cfg->i_state_ma[i];
    return (uint16_t)(sum > 65535u ? 65535u : sum);
}

float gauge_soc_from_ocv(const gauge_cfg_t *cfg, float v)
{
    const uint16_t *t = cfg->ocv_mv;
    uint32_t i;
    if (v <= (float)t[0]) return 0.0f;
    if (v >= (float)t[GAUGE_OCV_POINTS - 1]) return 100.0f;
    for (i = 0; i < GAUGE_OCV_POINTS - 1u; i++) {
        float lo = (float)t[i], hi = (float)t[i + 1];
        if (v < hi) {
            float f = (hi > lo) ? (v - lo) / (hi - lo) : 0.0f;
            if (f < 0.0f) f = 0.0f;      /* non-monotone table guard */
            return 5.0f * ((float)i + f);
        }
    }
    return 100.0f;
}

uint16_t gauge_ocv_from_soc(const gauge_cfg_t *cfg, float soc)
{
    uint32_t i;
    float f, v;
    if (soc <= 0.0f) return cfg->ocv_mv[0];
    if (soc >= 100.0f) return cfg->ocv_mv[GAUGE_OCV_POINTS - 1];
    i = (uint32_t)(soc / 5.0f);
    if (i >= GAUGE_OCV_POINTS - 1u) i = GAUGE_OCV_POINTS - 2u;
    f = soc / 5.0f - (float)i;
    v = (float)cfg->ocv_mv[i] + f * ((float)cfg->ocv_mv[i + 1] - (float)cfg->ocv_mv[i]);
    return (uint16_t)(v + 0.5f);
}

uint8_t gauge_level_for_soc(const gauge_cfg_t *cfg, float soc)
{
    uint8_t l = 0, i;
    for (i = 0; i < GAUGE_N_SEGMENTS; i++)
        if (soc >= (float)cfg->seg_thr_pct[i]) l = (uint8_t)(i + 1u);
    return l;
}

/* ---------------- internals ---------------- */

static uint16_t cfg_rint(const gauge_cfg_t *c) { return c->rint_mohm ? c->rint_mohm : RINT_DEFAULT_MOHM; }
static uint16_t cfg_ichg(const gauge_cfg_t *c) { return c->i_chg_ma ? c->i_chg_ma : ICHG_DEFAULT_MA; }
static uint16_t cfg_cap(const gauge_cfg_t *c) { return c->capacity_mah ? c->capacity_mah : CAP_DEFAULT_MAH; }

static bool time_reached(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }

/* cond must hold continuously for `need` ms. */
static bool dwell(bool cond, bool *armed, uint32_t *t0, uint32_t now, uint32_t need)
{
    if (!cond) {
        *armed = false;
        return false;
    }
    if (!*armed) {
        *armed = true;
        *t0 = now;
    }
    return (uint32_t)(now - *t0) >= need;
}

static void start_fast(gauge_t *g, uint32_t now)
{
    g->t_fast_until = now + g->cfg->fast_window_ms;
}

static void set_mode(gauge_t *g, gauge_mode_t m, uint32_t now)
{
    if (m == g->mode) return;
    g->mode = m;
    g->t_mode = now;
    g->below_armed = g->above_armed = g->escape_armed = false;
    g->flags &= (uint8_t)~(GAUGE_F_CV | GAUGE_F_TOPUP);
    g->dirty = true;
    start_fast(g, now);
}

static void set_level(gauge_t *g, uint8_t l, bool escape)
{
    if (l == g->level) return;
    if (l > g->level) {
        g->stats.rises++;
        if (escape) g->stats.escapes++;
        else if (g->mode == GAUGE_DISCHARGING || g->mode == GAUGE_RELAX)
            g->stats.reversals[g->mode]++;
    } else {
        g->stats.drops++;
        if (g->mode == GAUGE_CHARGING || g->mode == GAUGE_FULL)
            g->stats.reversals[g->mode]++;
    }
    g->level = l;
    g->dirty = true;
}

/* Battery current seen by the cell, mA, + = discharge. */
static float batt_current(const gauge_t *g, const gauge_input_t *in)
{
    if (!in->vbus) return (float)in->load_ma;     /* on battery */
    if (in->chg_low) return -(float)cfg_ichg(g->cfg); /* power path: load from USB */
    return 0.0f;
}

static float ema(float y, float x, float dt, float tau)
{
    return y + (dt / (tau + dt)) * (x - y);
}

static void reset_alarms(gauge_t *g)
{
    g->flags &= (uint8_t)~(GAUGE_F_LOW_WARN | GAUGE_F_CRITICAL);
    g->warn_armed = g->crit_armed = false;
    g->crit_count = 0;
}

/* Mode for the current (debounced) inputs at boot / after NO_BATT. */
static gauge_mode_t fresh_mode(const gauge_t *g, float vbat)
{
    if (!g->vbus) return GAUGE_DISCHARGING;
    if (g->chg) return GAUGE_CHARGING;
    if (vbat >= (float)g->cfg->full_min_mv) return GAUGE_FULL;
    return GAUGE_DISCHARGING;
}

/* ---------------- public ---------------- */

void gauge_init(gauge_t *g, const gauge_cfg_t *cfg, const gauge_persist_t *stored,
                uint32_t now_ms)
{
    uint8_t *p = (uint8_t *)g;
    size_t i;
    for (i = 0; i < sizeof(*g); i++) p[i] = 0;
    g->cfg = cfg;
    g->mode = GAUGE_UNKNOWN;
    g->t_init = g->t_last = g->t_mode = now_ms;
    if (stored && stored->level <= GAUGE_N_SEGMENTS) {
        g->have_stored = true;
        g->stored = *stored;
        g->seq = stored->seq;
        g->level = stored->level;
    }
    start_fast(g, now_ms);
}

uint32_t gauge_update(gauge_t *g, const gauge_input_t *in)
{
    const gauge_cfg_t *c = g->cfg;
    const uint32_t now = in->now_ms;
    const uint8_t prev_level = g->level, prev_flags = g->flags;
    const gauge_mode_t prev_mode = g->mode;
    const bool prev_dirty = g->dirty;
    uint32_t ev = 0, dt_ms;
    float dt, i_batt, vcomp, tau;

    /* ---- inputs, debounce ---- */
    if (!g->started) {
        g->started = true;
        g->vbus = g->vbus_raw = in->vbus;
        g->chg = g->chg_raw = in->chg_low;
        g->t_vbus_edge = g->t_chg_edge = now;
        g->t_crit_sample = now;
        g->t_last = now;
        i_batt = batt_current(g, in);
        g->v_slow = g->v_fast = (float)in->vbat_mv + i_batt * (float)cfg_rint(c) / 1000.0f;
        g->charged_since_plug = in->vbus && in->chg_low;
    }
    dt_ms = now - g->t_last;
    g->t_last = now;
    g->stats.time_ms[g->mode] += dt_ms;
    dt = (float)dt_ms;

    if (in->vbus != g->vbus_raw) { g->vbus_raw = in->vbus; g->t_vbus_edge = now; start_fast(g, now); }
    if (in->chg_low != g->chg_raw) { g->chg_raw = in->chg_low; g->t_chg_edge = now; start_fast(g, now); }
    if ((uint32_t)(now - g->t_vbus_edge) >= c->chg_debounce_ms) g->vbus = g->vbus_raw;
    if ((uint32_t)(now - g->t_chg_edge) >= c->chg_debounce_ms) g->chg = g->chg_raw;
    if (in->mode_changed) start_fast(g, now);

    /* ---- filter: EMA of the load-compensated voltage ---- */
    i_batt = batt_current(g, in);
    vcomp = (float)in->vbat_mv + i_batt * (float)cfg_rint(c) / 1000.0f;
    tau = time_reached(now, g->t_fast_until) ? (float)c->tau_slow_ms : (float)c->tau_fast_ms;
    if (dt_ms > 0) {
        g->v_slow = ema(g->v_slow, vcomp, dt, tau);
        g->v_fast = ema(g->v_fast, vcomp, dt, (float)c->tau_fast_ms);
    }

    /* ---- alarms (on battery only) ---- */
    if (!in->vbus) {
        if (!(g->flags & GAUGE_F_LOW_WARN) &&
            dwell(g->v_fast < (float)c->warn_mv, &g->warn_armed, &g->t_warn, now, c->warn_ms))
            g->flags |= GAUGE_F_LOW_WARN;
        if (!(g->flags & GAUGE_F_CRITICAL)) {
            bool crit = dwell(g->v_fast < (float)c->crit_mv, &g->crit_armed, &g->t_crit, now,
                              c->crit_ms);
            if ((uint32_t)(now - g->t_crit_sample) >= 1000u) {
                g->t_crit_sample = now;
                g->crit_count = (in->vbat_mv < c->crit_loaded_mv) ? (uint8_t)(g->crit_count + 1u) : 0u;
            }
            if (crit || g->crit_count >= c->crit_loaded_count)
                g->flags |= GAUGE_F_CRITICAL | GAUGE_F_LOW_WARN;
        }
    } else if (g->vbus) {
        reset_alarms(g);
    }

    /* ---- battery presence (VBUS only) ---- */
    {
        bool nb = in->vbus && (in->spread_mv > c->nobatt_spread_mv || in->vbat_mv < c->nobatt_min_mv ||
                               in->vbat_mv > c->nobatt_max_mv);
        if (g->mode != GAUGE_NO_BATT) {
            g->nb_clear_armed = false;
            if (dwell(nb, &g->nb_armed, &g->t_nb, now, c->nobatt_ms)) {
                set_mode(g, GAUGE_NO_BATT, now);
                g->nb_armed = false;
            }
        } else if (!in->vbus ||
                   dwell(!nb, &g->nb_clear_armed, &g->t_nb_clear, now, c->nobatt_clear_ms)) {
            /* battery (re)appeared: start a fresh estimate as after boot */
            set_mode(g, GAUGE_UNKNOWN, now);
            g->t_init = now;
            g->have_stored = false;
            g->v_slow = g->v_fast = vcomp;
            g->nb_clear_armed = false;
        }
    }

    /* ---- boot estimate ---- */
    if (g->mode == GAUGE_UNKNOWN) {
        if ((uint32_t)(now - g->t_init) >= c->boot_estimate_ms) {
            gauge_mode_t m = fresh_mode(g, (float)in->vbat_mv);
            uint8_t fresh;
            g->soc = gauge_soc_from_ocv(c, g->v_slow);
            fresh = gauge_level_for_soc(c, g->soc);
            if (m == GAUGE_FULL) fresh = GAUGE_N_SEGMENTS;
            else if (m == GAUGE_CHARGING && fresh > c->chg_cap_level) fresh = c->chg_cap_level;
            if (g->have_stored && fresh == (uint8_t)(g->stored.level + 1u)) {
                fresh = g->stored.level;
                g->stats.boot_kept_stored++;
            }
            g->level = fresh;   /* initial assignment: not a rise/drop */
            g->soc_cc = g->soc;
            g->charged_since_plug = (m == GAUGE_CHARGING);
            set_mode(g, m, now);
            g->dirty = true;
            ev |= GAUGE_EV_ESTIMATE;
        }
        goto done;
    }
    if (g->mode == GAUGE_NO_BATT) goto done;

    /* ---- mode transitions (debounced inputs) ---- */
    if (!g->vbus) {
        if (g->mode == GAUGE_CHARGING || g->mode == GAUGE_FULL) set_mode(g, GAUGE_RELAX, now);
        else if (g->mode == GAUGE_RELAX) {
            if ((uint32_t)(now - g->t_mode) >= c->relax_ms) set_mode(g, GAUGE_DISCHARGING, now);
        } else set_mode(g, GAUGE_DISCHARGING, now);
        g->charged_since_plug = false;
    } else if (g->chg) {
        if (g->mode == GAUGE_FULL) g->flags |= GAUGE_F_TOPUP;   /* BQ24075 top-up: stay FULL */
        else if (g->mode != GAUGE_CHARGING) {
            /* coulomb counter starts from the on-battery estimate just before plug-in */
            g->soc_cc = gauge_soc_from_ocv(c, g->v_slow);
            set_mode(g, GAUGE_CHARGING, now);
        }
        g->charged_since_plug = true;
    } else {
        if (g->mode == GAUGE_FULL) {
            g->flags &= (uint8_t)~GAUGE_F_TOPUP;
        } else if (g->chg_raw) {
            /* CHG edge still debouncing: wait */
        } else if (g->mode == GAUGE_CHARGING ||
                   (!g->charged_since_plug &&
                    (uint32_t)(now - g->t_vbus_edge) >= c->full_idle_ms)) {
            /* CHG released (or never asserted since plug-in): full if the
               voltage (no battery current now) says so, else charger idle/fault */
            set_mode(g, (in->vbat_mv >= c->full_min_mv) ? GAUGE_FULL : GAUGE_DISCHARGING, now);
        }
    }

    /* ---- SoC estimate and level ---- */
    switch (g->mode) {
    case GAUGE_CHARGING: {
        float soc_v = gauge_soc_from_ocv(c, g->v_slow);
        float rate = (float)cfg_ichg(c) * 100.0f / ((float)cfg_cap(c) * 3600000.0f); /* %/ms */
        if (!(g->flags & GAUGE_F_CV) && in->vbat_mv >= c->cv_mv && in->chg_low) {
            g->flags |= GAUGE_F_CV;
            if (soc_v > g->soc_cc) g->soc_cc = soc_v;
        }
        if (in->chg_low) {
            float eff = (float)((g->flags & GAUGE_F_CV) ? c->chg_cv_eff_pct : c->chg_cc_eff_pct);
            g->soc_cc += rate * eff / 100.0f * dt;
            if (g->soc_cc > 100.0f) g->soc_cc = 100.0f;
        }
        g->soc = (g->flags & GAUGE_F_CV) ? g->soc_cc : (soc_v > g->soc_cc ? soc_v : g->soc_cc);
        if (g->level < c->chg_cap_level && g->level < GAUGE_N_SEGMENTS) {
            float thr = (float)c->seg_thr_pct[g->level] + (float)c->hyst_pct;
            if (dwell(g->soc >= thr, &g->above_armed, &g->t_above, now, c->rise_dwell_ms)) {
                set_level(g, (uint8_t)(g->level + 1u), false);
                g->above_armed = false;
            }
        }
        break;
    }
    case GAUGE_FULL:
        g->soc = 100.0f;
        set_level(g, GAUGE_N_SEGMENTS, false);
        break;
    case GAUGE_RELAX:
        g->soc = gauge_soc_from_ocv(c, g->v_slow);   /* level frozen */
        break;
    case GAUGE_DISCHARGING:
        g->soc = gauge_soc_from_ocv(c, g->v_slow);
        if (g->level > 0) {
            float thr = (float)c->seg_thr_pct[g->level - 1u] - (float)c->hyst_pct;
            bool spaced = !g->any_drop || (uint32_t)(now - g->t_last_drop) >= c->drop_min_interval_ms;
            if (dwell(g->soc < thr, &g->below_armed, &g->t_below, now, c->drop_dwell_ms) && spaced) {
                set_level(g, (uint8_t)(g->level - 1u), false);
                g->t_last_drop = now;
                g->any_drop = true;
                g->below_armed = false;
            }
        }
        if (g->level < GAUGE_N_SEGMENTS) {
            float thr = (float)c->seg_thr_pct[g->level] + (float)c->escape_pct;
            if (dwell(g->soc >= thr, &g->escape_armed, &g->t_escape, now, c->escape_ms)) {
                uint8_t l = gauge_level_for_soc(c, g->soc);
                if (l > g->level) set_level(g, l, true);
                g->escape_armed = false;
            }
        }
        break;
    default:
        break;
    }

    /* critical overrides the drop limits */
    if ((g->flags & GAUGE_F_CRITICAL) && !g->vbus && g->level > 0) {
        set_level(g, 0, false);
        g->t_last_drop = now;
        g->any_drop = true;
    }

done:
    if (g->level != prev_level) ev |= GAUGE_EV_LEVEL;
    if (g->mode != prev_mode) ev |= GAUGE_EV_MODE;
    if ((g->flags & GAUGE_F_LOW_WARN) && !(prev_flags & GAUGE_F_LOW_WARN)) ev |= GAUGE_EV_WARN;
    if ((g->flags & GAUGE_F_CRITICAL) && !(prev_flags & GAUGE_F_CRITICAL)) {
        ev |= GAUGE_EV_CRITICAL;
        g->dirty = true;
    }
    if (g->dirty && (!prev_dirty || (ev & (GAUGE_EV_LEVEL | GAUGE_EV_MODE | GAUGE_EV_CRITICAL))))
        ev |= GAUGE_EV_PERSIST;
    return ev;
}

void gauge_display(const gauge_t *g, gauge_display_t *o)
{
    o->level = g->level;
    o->valid = g->mode != GAUGE_UNKNOWN;
    o->blink = (g->flags & GAUGE_F_CRITICAL) != 0 ||
               (g->level == 0 && g->mode != GAUGE_CHARGING && g->mode != GAUGE_NO_BATT);
    o->charge_anim = g->mode == GAUGE_CHARGING && g->level < GAUGE_N_SEGMENTS;
    o->plug = g->vbus;
    o->crossed = g->mode == GAUGE_NO_BATT;
}

gauge_mode_t gauge_mode(const gauge_t *g) { return g->mode; }
uint8_t gauge_flags(const gauge_t *g) { return (uint8_t)(g->flags | (g->vbus ? GAUGE_F_VBUS : 0u)); }
uint8_t gauge_level(const gauge_t *g) { return g->level; }
float gauge_soc(const gauge_t *g) { return g->soc; }
float gauge_vcomp(const gauge_t *g) { return g->v_slow; }
bool gauge_persist_dirty(const gauge_t *g) { return g->dirty; }
const gauge_stats_t *gauge_stats(const gauge_t *g) { return &g->stats; }

void gauge_export(gauge_t *g, gauge_persist_t *out)
{
    out->level = g->level;
    out->mode = (uint8_t)g->mode;
    out->seq = ++g->seq;
    g->dirty = false;
}

const char *gauge_mode_name(gauge_mode_t m)
{
    static const char *const names[GAUGE_MODE_COUNT] = {
        "UNKNOWN", "NO_BATT", "CHARGING", "FULL", "RELAX", "DISCHARGING",
    };
    return (unsigned)m < GAUGE_MODE_COUNT ? names[m] : "?";
}
