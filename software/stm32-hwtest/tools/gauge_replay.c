/* gauge_replay: run Src/drv/gauge.c on a recorded or simulated log (host).
 *
 * Input CSV (header row, columns by name, extra columns ignored):
 *   t_s, vbat_mv, i_est_ma, vbus, chg, spread_mv [, true_soc] [, boot]
 *   chg = 1 when the BQ24075 #CHG pin is low (charging). boot = 1 marks the
 *   first sample after a power cycle: the gauge state is exported, the gauge
 *   re-initialised and the stored state imported (exercises the boot rule).
 *
 * Output CSV (-o): one row per input row with level/mode/flags/display.
 * Summary on stdout (key=value), optional JSON (-j).
 *
 * Criteria (exit 1 if any fails, plan §B.9):
 *   - 0 reversals (oracle from the inputs: level up while on battery, level
 *     down while VBUS+CHG; boot rows excluded; escapes count as reversals)
 *   - last segment off (first level 0 of the final discharge) within
 *     +-5 % of true empty (needs true_soc; true_soc 0 = loaded cut-off)
 *   - no FULL while the charger has not yet terminated since plug-in
 *
 * Usage: gauge_replay [-c cfg.json] [--rint N] [--ichg N] [--cap N]
 *                     [-o out.csv] [-j summary.json] [--empty-tol PCT] log.csv
 * cfg.json: any JSON with "ocv_mv":[21 numbers], optional "rint_mohm",
 *   "i_chg_ma", "capacity_mah" (battlog_to_ocv.py output, `hwcal get`).
 */
#include "gauge.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXCOL 32
#define DNAN ((double)NAN)
#define LINE 4096

enum { C_T, C_V, C_I, C_VBUS, C_CHG, C_SPREAD, C_SOC, C_BOOT, C_N };
static const char *const k_cols[C_N] = {"t_s", "vbat_mv", "i_est_ma", "vbus", "chg",
                                        "spread_mv", "true_soc", "boot"};

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *b;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    if (b) b[n] = 0;
    fclose(f);
    return b;
}

/* "key": number  or  "key": [n, n, ...]; returns count parsed */
static int json_nums(const char *js, const char *key, double *out, int max)
{
    char pat[64];
    const char *p;
    int n = 0;
    snprintf(pat, sizeof pat, "\"%s\"", key);
    p = strstr(js, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p && (isspace((unsigned char)*p) || *p == ':')) p++;
    if (*p == '[') {
        p++;
        while (*p && *p != ']' && n < max) {
            char *e;
            double v = strtod(p, &e);
            if (e == p) { p++; continue; }
            out[n++] = v;
            p = e;
        }
        return n;
    }
    {
        char *e;
        double v = strtod(p, &e);
        if (e == p) return 0;
        out[0] = v;
        return 1;
    }
}

static int load_cfg(gauge_cfg_t *c, const char *path)
{
    double v[32];
    int n, i;
    char *js = read_file(path);
    if (!js) { fprintf(stderr, "cannot read %s\n", path); return -1; }
    n = json_nums(js, "ocv_mv", v, 32);
    if (n == GAUGE_OCV_POINTS) {
        for (i = 0; i < n; i++) c->ocv_mv[i] = (uint16_t)lround(v[i]);
    } else if (n) {
        fprintf(stderr, "%s: ocv_mv has %d points, need 21\n", path, n);
        free(js);
        return -1;
    }
    if (json_nums(js, "rint_mohm", v, 1)) c->rint_mohm = (uint16_t)lround(v[0]);
    if (json_nums(js, "i_chg_ma", v, 1)) c->i_chg_ma = (uint16_t)lround(v[0]);
    if (json_nums(js, "capacity_mah", v, 1)) c->capacity_mah = (uint16_t)lround(v[0]);
    n = json_nums(js, "i_state_ma", v, 32);
    if (n == GAUGE_N_STATES)
        for (i = 0; i < n; i++) c->i_state_ma[i] = (uint16_t)lround(v[i]);
    free(js);
    return 0;
}

static int split(char *s, char **f)
{
    int n = 0;
    char *p = s;
    while (n < MAXCOL) {
        f[n++] = p;
        p = strchr(p, ',');
        if (!p) break;
        *p++ = 0;
    }
    return n;
}

static void trim(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) s[--n] = 0;
}

typedef struct {
    double t;
    int level, mode;
} drop_t;

int main(int argc, char **argv)
{
    gauge_cfg_t cfg;
    gauge_t g;
    const char *in_path = NULL, *out_path = NULL, *json_path = NULL;
    double empty_tol = 5.0;
    FILE *fin, *fout = NULL;
    char line[LINE];
    char *f[MAXCOL];
    int col[C_N];
    int i, nf, rows = 0;
    int fails = 0;

    gauge_cfg_defaults(&cfg);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c") && i + 1 < argc) {
            if (load_cfg(&cfg, argv[++i])) return 2;
        } else if (!strcmp(argv[i], "--rint") && i + 1 < argc) cfg.rint_mohm = (uint16_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ichg") && i + 1 < argc) cfg.i_chg_ma = (uint16_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--cap") && i + 1 < argc) cfg.capacity_mah = (uint16_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--empty-tol") && i + 1 < argc) empty_tol = atof(argv[++i]);
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) out_path = argv[++i];
        else if (!strcmp(argv[i], "-j") && i + 1 < argc) json_path = argv[++i];
        else if (argv[i][0] == '-' && argv[i][1]) {
            fprintf(stderr, "usage: gauge_replay [-c cfg.json] [--rint N] [--ichg N] [--cap N] "
                            "[--empty-tol PCT] [-o out.csv] [-j summary.json] log.csv\n");
            return 2;
        } else in_path = argv[i];
    }
    if (!in_path) { fprintf(stderr, "no input\n"); return 2; }
    fin = fopen(in_path, "r");
    if (!fin) { fprintf(stderr, "cannot open %s\n", in_path); return 2; }
    if (!fgets(line, sizeof line, fin)) { fprintf(stderr, "empty input\n"); return 2; }
    trim(line);
    nf = split(line, f);
    for (i = 0; i < C_N; i++) {
        int k;
        col[i] = -1;
        for (k = 0; k < nf; k++) {
            char *s = f[k];
            while (*s == ' ') s++;
            if (!strcmp(s, k_cols[i])) col[i] = k;
        }
        if (i < C_SOC && col[i] < 0) { fprintf(stderr, "missing column %s\n", k_cols[i]); return 2; }
    }
    if (out_path) {
        fout = fopen(out_path, "w");
        if (!fout) { fprintf(stderr, "cannot write %s\n", out_path); return 2; }
        fprintf(fout, "t_s,level,mode,flags,valid,blink,anim,plug,crossed,soc,vcomp_mv,true_soc,events\n");
    }

    {
        /* replay state + oracle */
        bool started = false;
        uint32_t t0_ms = 0;
        double prev_i = 0, prev_t = 0;
        int prev_vbus = 0, prev_valid = 0, prev_level = -1;
        int plug_terminated = 0, plug_chg_seen = 0;
        unsigned long rev_up_batt = 0, rev_down_chg = 0, full_early = 0;
        unsigned long boots = 0, boot_kept = 0;
        unsigned long tot_rev[GAUGE_MODE_COUNT] = {0};
        double tot_time[GAUGE_MODE_COUNT] = {0};
        unsigned long tot_escapes = 0, tot_drops = 0, tot_rises = 0;
        double first_off_t = -1, first_off_soc = DNAN, crit_t = -1, crit_soc = DNAN;
        double warn_t = -1, warn_soc = DNAN;
        double last_true_soc = DNAN;
        /* level drops on battery vs truth: true_soc - (T[new]) at each drop */
        double drop_err_sum = 0, drop_err_max = 0;
        int drop_err_n = 0;
        double stint_start_t = 0;
        int have_soc = col[C_SOC] >= 0;
        int boot_lv_before[64], boot_lv_after[64], boot_pending = -1;
        double boot_soc[64];
        int nboot_rec = 0;
        const gauge_stats_t *st;

        while (fgets(line, sizeof line, fin)) {
            gauge_input_t in;
            gauge_display_t d;
            uint32_t ev;
            double t, tsoc = DNAN;
            int boot = 0, vbus, chg;
            trim(line);
            if (!line[0]) continue;
            nf = split(line, f);
            if (nf < 6) continue;
            t = atof(f[col[C_T]]);
            vbus = atoi(f[col[C_VBUS]]) != 0;
            chg = atoi(f[col[C_CHG]]) != 0;
            if (have_soc && col[C_SOC] < nf && f[col[C_SOC]][0]) tsoc = atof(f[col[C_SOC]]);
            if (col[C_BOOT] >= 0 && col[C_BOOT] < nf) boot = atoi(f[col[C_BOOT]]) != 0;

            in.now_ms = (uint32_t)llround(t * 1000.0);
            in.vbat_mv = (uint16_t)lround(atof(f[col[C_V]]));
            in.load_ma = (uint16_t)lround(atof(f[col[C_I]]));
            in.vbus = vbus;
            in.chg_low = chg;
            in.spread_mv = (uint16_t)lround(atof(f[col[C_SPREAD]]));
            in.mode_changed = started && (fabs(atof(f[col[C_I]]) - prev_i) > 0.5 || vbus != prev_vbus);

            if (!started || boot) {
                gauge_persist_t p;
                if (started) {
                    st = gauge_stats(&g);
                    for (i = 0; i < GAUGE_MODE_COUNT; i++) {
                        tot_rev[i] += st->reversals[i];
                        tot_time[i] += st->time_ms[i] / 1000.0;
                    }
                    tot_escapes += st->escapes; tot_drops += st->drops; tot_rises += st->rises;
                    boot_kept += st->boot_kept_stored;
                    gauge_export(&g, &p);
                    if (nboot_rec < 64) {
                        boot_lv_before[nboot_rec] = gauge_level(&g);
                        boot_soc[nboot_rec] = tsoc;
                        boot_pending = nboot_rec++;
                    }
                    gauge_init(&g, &cfg, &p, in.now_ms);
                    boots++;
                    in.mode_changed = false;
                } else {
                    gauge_init(&g, &cfg, NULL, in.now_ms);
                    t0_ms = in.now_ms;
                }
                started = true;
                prev_valid = 0;
                prev_level = -1;
            }
            (void)t0_ms;

            /* plug-in tracking for the FULL oracle */
            if (vbus && !prev_vbus) { plug_terminated = 0; plug_chg_seen = chg; }
            if (vbus) {
                if (chg) plug_chg_seen = 1;
                else if (plug_chg_seen) plug_terminated = 1;
            }

            ev = gauge_update(&g, &in);
            gauge_display(&g, &d);

            if (boot_pending >= 0 && (ev & GAUGE_EV_ESTIMATE)) {
                boot_lv_after[boot_pending] = gauge_level(&g);
                boot_pending = -1;
            }
            if (d.valid && prev_valid && prev_level >= 0 && (int)d.level != prev_level) {
                if ((int)d.level > prev_level && !vbus) rev_up_batt++;
                if ((int)d.level < prev_level && vbus && chg) rev_down_chg++;
                if ((int)d.level < prev_level && !vbus && have_soc && !isnan(tsoc) &&
                    !(gauge_flags(&g) & GAUGE_F_CRITICAL)) {
                    double e = tsoc - (double)cfg.seg_thr_pct[d.level];
                    drop_err_sum += fabs(e);
                    if (fabs(e) > fabs(drop_err_max)) drop_err_max = e;
                    drop_err_n++;
                }
            }
            if (gauge_mode(&g) == GAUGE_FULL && vbus && !plug_terminated && plug_chg_seen) full_early++;

            /* final-discharge empty point: first level 0 after the last time on VBUS */
            if (vbus) { first_off_t = -1; crit_t = -1; warn_t = -1; stint_start_t = t; }
            if (!vbus && d.valid && d.level == 0 && first_off_t < 0) { first_off_t = t; first_off_soc = tsoc; }
            if (!vbus && (gauge_flags(&g) & GAUGE_F_CRITICAL) && crit_t < 0) { crit_t = t; crit_soc = tsoc; }
            if (!vbus && (gauge_flags(&g) & GAUGE_F_LOW_WARN) && warn_t < 0) { warn_t = t; warn_soc = tsoc; }
            last_true_soc = tsoc;

            if (fout)
                fprintf(fout, "%.3f,%u,%s,%u,%d,%d,%d,%d,%d,%.2f,%.1f,%.2f,%lu\n", t, d.level,
                        gauge_mode_name(gauge_mode(&g)), gauge_flags(&g), d.valid, d.blink, d.charge_anim,
                        d.plug, d.crossed, (double)gauge_soc(&g), (double)gauge_vcomp(&g),
                        have_soc ? tsoc : DNAN, (unsigned long)ev);

            prev_i = atof(f[col[C_I]]);
            prev_vbus = vbus;
            prev_valid = d.valid;
            prev_level = d.valid ? (int)d.level : -1;
            prev_t = t;
            rows++;
        }
        fclose(fin);
        if (fout) fclose(fout);
        if (!started) { fprintf(stderr, "no rows\n"); return 2; }
        st = gauge_stats(&g);
        for (i = 0; i < GAUGE_MODE_COUNT; i++) {
            tot_rev[i] += st->reversals[i];
            tot_time[i] += st->time_ms[i] / 1000.0;
        }
        tot_escapes += st->escapes; tot_drops += st->drops; tot_rises += st->rises;
        boot_kept += st->boot_kept_stored;
        (void)stint_start_t;
        (void)last_true_soc;

        /* ---- criteria ---- */
        {
            int c_rev = (rev_up_batt + rev_down_chg) == 0;
            int c_full = full_early == 0;
            int empty_checked = have_soc && first_off_t >= 0 && !isnan(first_off_soc);
            int c_empty = !empty_checked || fabs(first_off_soc) <= empty_tol;
            double off_to_crit_min = (first_off_t >= 0 && crit_t >= 0) ? (crit_t - first_off_t) / 60.0 : DNAN;
            fails = !c_rev + !c_full + !c_empty;

            printf("rows=%d duration_s=%.0f\n", rows, prev_t);
            printf("time_s:");
            for (i = 0; i < GAUGE_MODE_COUNT; i++) printf(" %s=%.0f", gauge_mode_name((gauge_mode_t)i), tot_time[i]);
            printf("\nreversals_internal:");
            for (i = 0; i < GAUGE_MODE_COUNT; i++) printf(" %s=%lu", gauge_mode_name((gauge_mode_t)i), tot_rev[i]);
            printf("\nreversals_oracle: up_on_battery=%lu down_while_charging=%lu\n", rev_up_batt, rev_down_chg);
            printf("drops=%lu rises=%lu escapes=%lu boots=%lu boot_kept_stored=%lu\n", tot_drops, tot_rises,
                   tot_escapes, boots, boot_kept);
            for (i = 0; i < nboot_rec; i++)
                printf("boot[%d]: level_before=%d level_after=%d true_soc=%.1f\n", i, boot_lv_before[i],
                       boot_lv_after[i], boot_soc[i]);
            if (drop_err_n)
                printf("drop_vs_threshold: n=%d mean_abs=%.2f worst=%.2f (true_soc - T at each drop)\n",
                       drop_err_n, drop_err_sum / drop_err_n, drop_err_max);
            if (warn_t >= 0) printf("low_warn: t=%.0f true_soc=%.2f\n", warn_t, warn_soc);
            if (crit_t >= 0) printf("critical: t=%.0f true_soc=%.2f\n", crit_t, crit_soc);
            if (empty_checked)
                printf("empty: level0_t=%.0f true_soc_at_level0=%.2f level0_to_critical_min=%.1f\n",
                       first_off_t, first_off_soc, off_to_crit_min);
            printf("full_before_termination=%lu\n", full_early);
            printf("CRITERIA reversals=%s full=%s empty=%s -> %s\n", c_rev ? "PASS" : "FAIL",
                   c_full ? "PASS" : "FAIL", empty_checked ? (c_empty ? "PASS" : "FAIL") : "n/a",
                   fails ? "FAIL" : "PASS");

            if (json_path) {
                FILE *j = fopen(json_path, "w");
                if (j) {
                    fprintf(j, "{\"rows\":%d,\"duration_s\":%.1f,\"time_s\":{", rows, prev_t);
                    for (i = 0; i < GAUGE_MODE_COUNT; i++)
                        fprintf(j, "%s\"%s\":%.1f", i ? "," : "", gauge_mode_name((gauge_mode_t)i), tot_time[i]);
                    fprintf(j, "},\"reversals_internal\":{");
                    for (i = 0; i < GAUGE_MODE_COUNT; i++)
                        fprintf(j, "%s\"%s\":%lu", i ? "," : "", gauge_mode_name((gauge_mode_t)i), tot_rev[i]);
                    fprintf(j, "},\"reversals_up_on_battery\":%lu,\"reversals_down_while_charging\":%lu,",
                            rev_up_batt, rev_down_chg);
                    fprintf(j, "\"drops\":%lu,\"rises\":%lu,\"escapes\":%lu,\"boots\":%lu,\"boot_kept_stored\":%lu,",
                            tot_drops, tot_rises, tot_escapes, boots, boot_kept);
                    fprintf(j, "\"boot\":[");
                    for (i = 0; i < nboot_rec; i++)
                        fprintf(j, "%s{\"before\":%d,\"after\":%d,\"true_soc\":%.2f}", i ? "," : "",
                                boot_lv_before[i], boot_lv_after[i], isnan(boot_soc[i]) ? -1.0 : boot_soc[i]);
                    fprintf(j, "],\"drop_err_n\":%d,\"drop_err_mean_abs\":%.3f,\"drop_err_worst\":%.3f,",
                            drop_err_n, drop_err_n ? drop_err_sum / drop_err_n : 0.0, drop_err_max);
                    fprintf(j, "\"warn_true_soc\":%.3f,\"critical_true_soc\":%.3f,",
                            warn_t >= 0 && !isnan(warn_soc) ? warn_soc : -999.0,
                            crit_t >= 0 && !isnan(crit_soc) ? crit_soc : -999.0);
                    fprintf(j, "\"empty_checked\":%s,\"true_soc_at_level0\":%.3f,\"level0_to_critical_min\":%.2f,",
                            empty_checked ? "true" : "false", empty_checked ? first_off_soc : -999.0,
                            isnan(off_to_crit_min) ? -1.0 : off_to_crit_min);
                    fprintf(j, "\"full_before_termination\":%lu,\"pass_reversals\":%s,\"pass_full\":%s,"
                               "\"pass_empty\":%s,\"pass\":%s}\n",
                            full_early, c_rev ? "true" : "false", c_full ? "true" : "false",
                            c_empty ? "true" : "false", fails ? "false" : "true");
                    fclose(j);
                }
            }
        }
    }
    return fails ? 1 : 0;
}
