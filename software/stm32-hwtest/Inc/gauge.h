#ifndef GAUGE_H
#define GAUGE_H

/* Voltage-only battery gauge for STM-boy (1S LiPo LP233350, BQ24075 charger).
 *
 * Pure C11, freestanding: no HAL, no malloc, no libm, no doubles. All time is
 * passed in by the caller as a millisecond timestamp (wraps after 49 days;
 * only differences are used). The same file builds for the MCU and for the
 * host replay tool (tools/gauge_replay.c), and gives bit-identical results on
 * both as long as FP contraction is off (GCC default with -std=c11).
 *
 * Units used throughout (no hidden scaling):
 *   voltage  mV        current  mA (positive = out of the battery)
 *   R        mOhm      time     ms          SoC  percent (float 0..100)
 *   level    0..5 display segments
 *
 * Usage (firmware):
 *   gauge_cfg_defaults(&cfg);  copy ocv_mv/rint/i_chg/... from HWCAL;
 *   gauge_init(&g, &cfg, have_stored ? &stored : NULL, now);
 *   every 250 ms:
 *      raw_b = gauge_trimmed_mean(bat16, 16, 4, &spread_raw);
 *      raw_v = gauge_trimmed_mean(vref16, 16, 4, NULL);
 *      in.vbat_mv = gauge_adc_to_mv(raw_b, raw_v, VREFINT_CAL, gain_q16, off);
 *      in.spread_mv = gauge_adc_to_mv(spread_raw*16u, raw_v, VREFINT_CAL, gain_q16, 0);
 *      in.load_ma = gauge_sum_states(&cfg, active_state_mask);
 *      ev = gauge_update(&g, &in);
 *      if (ev & GAUGE_EV_PERSIST) { gauge_export(&g, &p); save p to HWCAL; }
 *      if (ev & GAUGE_EV_CRITICAL) -> save, "connect USB" screen, Stop mode.
 *      gauge_display(&g, &disp) -> draw icon.
 *   On OFF_REQ: gauge_export() + save before acknowledging.
 */

#include <stdbool.h>
#include <stdint.h>

#define GAUGE_OCV_POINTS   21   /* SoC 0,5,...,100 % */
#define GAUGE_N_STATES     18   /* current states S0..S17 (plan §C) */
#define GAUGE_N_SEGMENTS    5

/* ---------------- configuration ---------------- */

typedef struct {
    /* cell model (field types mirror hwcal_t so integration is a copy) */
    uint16_t ocv_mv[GAUGE_OCV_POINTS]; /* rested OCV at 0,5..100 % usable SoC; monotone */
    uint16_t rint_mohm;          /* DC internal resistance (0 -> 150) */
    uint16_t i_chg_ma;           /* BQ24075 fast-charge current (0 -> 175) */
    uint16_t capacity_mah;       /* usable capacity (0 -> 310) */
    uint16_t i_state_ma[GAUGE_N_STATES]; /* measured current per state, for gauge_sum_states() */

    /* display segments */
    uint8_t  seg_thr_pct[GAUGE_N_SEGMENTS]; /* 5,25,45,65,85: level = #thresholds reached */
    uint8_t  hyst_pct;           /* 3: drop below T-h, rise above T+h */
    uint32_t drop_dwell_ms;      /* 20000: SoC must stay below T-h this long */
    uint32_t drop_min_interval_ms; /* 60000: at most one drop per interval (critical excepted) */
    uint8_t  escape_pct;         /* 15: discharging gross-error escape margin above T[level] */
    uint32_t escape_ms;          /* 300000: ... sustained this long */
    uint32_t rise_dwell_ms;      /* 20000: charging: SoC must stay above T+h this long */

    /* filter: EMA on the compensated voltage */
    uint32_t tau_fast_ms;        /* 2000 */
    uint32_t tau_slow_ms;        /* 60000 */
    uint32_t fast_window_ms;     /* 10000: fast tau after boot / load or mode change */
    uint32_t boot_estimate_ms;   /* 3000: UNKNOWN until the first estimate */

    /* modes */
    uint32_t relax_ms;           /* 180000: level frozen after unplug */
    uint32_t chg_debounce_ms;    /* 2000: CHG/VBUS edges must be stable this long */
    uint16_t full_min_mv;        /* 4100: FULL needs loaded vbat >= this (else charger fault/idle) */
    uint32_t full_idle_ms;       /* 10000: VBUS without CHG this long (never charged) -> FULL/idle */
    uint16_t cv_mv;              /* 4170: vbat >= this while charging = CV phase */
    uint8_t  chg_cc_eff_pct;     /* 90: soft coulomb counter rate in CC (fraction of i_chg) */
    uint8_t  chg_cv_eff_pct;     /* 40: ... in CV (current tapers, rate unknown) */
    uint8_t  chg_cap_level;      /* 4: max level while CHARGING (5 only in FULL) */

    /* alarms */
    uint16_t warn_mv;            /* 3500: comp voltage below this ... */
    uint32_t warn_ms;            /* 10000: ... this long -> LOW_WARN */
    uint16_t crit_mv;            /* 3400: comp voltage below this ... */
    uint32_t crit_ms;            /* 10000: ... this long -> CRITICAL */
    uint16_t crit_loaded_mv;     /* 3300: or loaded vbat below this ... */
    uint8_t  crit_loaded_count;  /* 3: ... in this many consecutive 1 s samples */

    /* battery presence (only meaningful with VBUS) */
    uint16_t nobatt_spread_mv;   /* 200: window max-min above this ... */
    uint16_t nobatt_min_mv;      /* 2700: ... or vbat below ... */
    uint16_t nobatt_max_mv;      /* 4350: ... or above */
    uint32_t nobatt_ms;          /* 2000: condition held this long -> NO_BATT */
    uint32_t nobatt_clear_ms;    /* 10000: condition absent this long -> leave NO_BATT */
} gauge_cfg_t;

/* ---------------- per-update input ---------------- */

typedef struct {
    uint32_t now_ms;      /* monotonic timestamp */
    uint16_t vbat_mv;     /* battery voltage under the current load, calibrated */
    uint16_t load_ma;     /* estimated system current (sum of active states). Ignored
                             while VBUS is present: BQ24075 power-path feeds the system
                             from USB, the battery sees only the charge current. */
    bool     vbus;        /* PA9: USB power present */
    bool     chg_low;     /* PA0 low: BQ24075 is charging */
    uint16_t spread_mv;   /* max-min of the 16-sample window, in mV (presence check) */
    bool     mode_changed;/* caller's load state changed since the last update */
} gauge_input_t;

/* ---------------- state / outputs ---------------- */

typedef enum {
    GAUGE_UNKNOWN = 0,   /* first boot_estimate_ms after init */
    GAUGE_NO_BATT,       /* VBUS present, battery absent/implausible */
    GAUGE_CHARGING,      /* VBUS + CHG low */
    GAUGE_FULL,          /* VBUS + CHG released after a charge (sticky while VBUS) */
    GAUGE_RELAX,         /* just unplugged: level frozen for relax_ms */
    GAUGE_DISCHARGING,   /* on battery (also VBUS without charging and not full) */
    GAUGE_MODE_COUNT
} gauge_mode_t;

#define GAUGE_F_LOW_WARN  0x01u
#define GAUGE_F_CRITICAL  0x02u
#define GAUGE_F_VBUS      0x04u  /* debounced VBUS */
#define GAUGE_F_CV        0x08u  /* charging in CV phase */
#define GAUGE_F_TOPUP     0x10u  /* FULL and the charger re-started (top-up) */

/* gauge_update() event bits */
#define GAUGE_EV_LEVEL     0x01u /* displayed level changed */
#define GAUGE_EV_MODE      0x02u /* mode changed */
#define GAUGE_EV_PERSIST   0x04u /* persistent state is dirty: export + save */
#define GAUGE_EV_WARN      0x08u /* LOW_WARN just raised */
#define GAUGE_EV_CRITICAL  0x10u /* CRITICAL just raised: save, sleep */
#define GAUGE_EV_ESTIMATE  0x20u /* boot estimate done (UNKNOWN left) */

typedef struct {
    uint8_t level;        /* 0..5 filled segments */
    bool    valid;        /* false while UNKNOWN (draw outline / last stored level) */
    bool    blink;        /* blink the outline (level 0 or CRITICAL) */
    bool    charge_anim;  /* animate segment `level` (the next one) */
    bool    plug;         /* show plug icon (VBUS) */
    bool    crossed;      /* crossed battery (NO_BATT) */
} gauge_display_t;

typedef struct {
    uint8_t  level;       /* 0..5 */
    uint8_t  mode;        /* gauge_mode_t */
    uint32_t seq;         /* incremented on every export */
} gauge_persist_t;

typedef struct {
    uint32_t reversals[GAUGE_MODE_COUNT]; /* level changes against the mode's direction
                                             (up while on battery, down while charging/full) */
    uint32_t drops;            /* level decrements */
    uint32_t rises;            /* level increments */
    uint32_t escapes;          /* gross-error escapes (allowed, not reversals) */
    uint32_t boot_kept_stored; /* boot rule kept stored level (fresh == stored+1) */
    uint32_t time_ms[GAUGE_MODE_COUNT];
} gauge_stats_t;

typedef struct {
    const gauge_cfg_t *cfg;
    gauge_mode_t mode;
    uint8_t  flags;
    uint8_t  level;
    bool     have_stored;
    gauge_persist_t stored;
    uint32_t seq;
    bool     dirty;

    bool     started;
    uint32_t t_init, t_last, t_mode, t_fast_until;
    uint32_t t_last_drop;  bool any_drop;
    uint32_t t_below, t_above, t_escape; /* dwell timers (0 = not armed; stored as now+1) */
    bool     below_armed, above_armed, escape_armed;

    float    v_slow;       /* switching-tau EMA of compensated voltage, mV */
    float    v_fast;       /* tau_fast EMA (alarms), mV */
    float    soc;          /* current SoC estimate, % */
    float    soc_cc;       /* charging soft coulomb counter, % */

    /* debounced inputs */
    bool     vbus, chg;
    bool     vbus_raw, chg_raw;
    uint32_t t_vbus_edge, t_chg_edge;
    bool     charged_since_plug; /* CHARGING seen since VBUS appeared */

    uint32_t t_warn, t_crit; bool warn_armed, crit_armed;
    uint32_t t_crit_sample; uint8_t crit_count;
    uint32_t t_nb, t_nb_clear; bool nb_armed, nb_clear_armed;

    gauge_stats_t stats;
} gauge_t;

/* ---------------- API ---------------- */

/* Fill cfg with defaults (generic LiPo OCV curve, plan §B thresholds). */
void gauge_cfg_defaults(gauge_cfg_t *cfg);

/* Measurement helpers.
 * gauge_trimmed_mean: sorts buf[0..n) in place, drops `drop` lowest and `drop`
 *   highest samples and returns the mean of the rest in Q4 (x16, rounded).
 *   *spread_raw (optional) = max-min of the whole window, in raw counts.
 * gauge_adc_to_mv: plan §B.1
 *   Vbat = raw_bat*3300*VREFINT_CAL/(raw_vref*4095) * 115/82 * gain + offset
 *   raw_bat and raw_vref may be in any common scale (e.g. both Q4 from
 *   gauge_trimmed_mean); gain_q16 = 65536 for 1.0. 64-bit integer math,
 *   rounded; returns 0 if raw_vref == 0, clamps to 0..65535. */
uint32_t gauge_trimmed_mean(uint16_t *buf, uint32_t n, uint32_t drop, uint16_t *spread_raw);
uint16_t gauge_adc_to_mv(uint32_t raw_bat, uint32_t raw_vref, uint16_t vrefint_cal,
                         uint32_t gain_q16, int16_t offset_mv);

/* Sum of cfg->i_state_ma[i] for every bit i set in state_mask (saturates). */
uint16_t gauge_sum_states(const gauge_cfg_t *cfg, uint32_t state_mask);

/* OCV table lookups (clamped to 0..100 % / table range). */
float    gauge_soc_from_ocv(const gauge_cfg_t *cfg, float ocv_mv);
uint16_t gauge_ocv_from_soc(const gauge_cfg_t *cfg, float soc_pct);

/* Level for an SoC without hysteresis: number of thresholds <= soc. */
uint8_t  gauge_level_for_soc(const gauge_cfg_t *cfg, float soc_pct);

/* cfg must stay valid for the gauge's lifetime. stored may be NULL. */
void     gauge_init(gauge_t *g, const gauge_cfg_t *cfg, const gauge_persist_t *stored,
                    uint32_t now_ms);
uint32_t gauge_update(gauge_t *g, const gauge_input_t *in); /* returns GAUGE_EV_* */

void     gauge_display(const gauge_t *g, gauge_display_t *out);
gauge_mode_t gauge_mode(const gauge_t *g);
uint8_t  gauge_flags(const gauge_t *g);
uint8_t  gauge_level(const gauge_t *g);
float    gauge_soc(const gauge_t *g);      /* internal estimate, % (diagnostics) */
float    gauge_vcomp(const gauge_t *g);    /* filtered compensated voltage, mV */
bool     gauge_persist_dirty(const gauge_t *g);
void     gauge_export(gauge_t *g, gauge_persist_t *out); /* seq++, clears dirty */
const gauge_stats_t *gauge_stats(const gauge_t *g);
const char *gauge_mode_name(gauge_mode_t m);

#endif
