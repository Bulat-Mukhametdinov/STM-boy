#!/usr/bin/env python3
"""Host-side analysis of a STM-boy battery log (`hwtest.py battlog dump` -> .bin / .csv).

    py -3.14 tools/battlog_analyze.py results/<uid>/battlog-YYYYMMDD-HHMMSS.csv   (or .bin)
        [--p12 MW] [--p STATE=MW ...] [--cap-mah MAH] [--eta 1.0] [--i-chg 174.5] [--outdir DIR]

Writes <input stem>-analysis/ next to the input: summary.md, summary.json, ocv_table.csv, rint_steps.csv,
and (if matplotlib is installed) cycle.png, discharge.png, charge.png, ocv.png, rint.png.

Model / assumptions (also written into summary.md):
  * every load state is a constant-POWER sink (buck-boost): I = P_state / Vbat.
  * the state of the interval [t_i, t_i+1] is the state of the LATER record (a phase switch happens right
    after the "before" record of a step cluster).
  * discharge = records from the first VBUS=0 record up to the first cutoff-flag record (loaded Vbat < cut).
    SoC 100 % = start of the discharge (assumed fully charged), SoC 0 % = the cutoff point (NOT an empty
    cell: the cell is still ~3.3 V loaded).  SoC(t) = 1 - Q(t)/Q_total with Q = integral of I dt.
  * the power of the "free" state (S12 by default) is solved so that the delivered charge equals the charge
    that the charger put back: eta*(I_cc*t_cc + I_cv_avg*t_cv) - Stop drain (1.3 mA * gap).  The CV tail is
    an exponential decay from I_cc to 10 % of I_cc (average current (I0-I1)/ln(I0/I1)).
    Without a charge phase it falls back to --p12 / 207 mW (estimate) and Q_total.
  * OCV points = end of rests (median of last 3 records), weighted by rest length (long x-rests weigh 10x),
    plus anchors: loaded cutoff voltage + I*R_int at SoC 0 %, end-of-charge voltage at SoC 100 %.
    Isotonic regression (PAV) + PCHIP, evaluated at 0,5,..,100 %.
  * R_int(step) = (V_rest-state - V_load-state) / (I_load - I_rest) from step records (+1 s, +10 s).
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
import struct
import sys
from pathlib import Path

REC = struct.Struct("<IHHHhBBH")
FLAGS = ("chg", "vbus", "rest", "cal_due", "marker", "cutoff", "step", "chg_step")

# measured system input power per load state, mW (HW_LOG), constant-power model
P_TABLE = {2: 41.3, 3: 52.0, 4: 68.0, 5: 99.0, 6: 126.0, 7: 155.0, 8: 76.5, 9: 116.0, 13: 89.0, 15: 124.0}
P12_DEFAULT = 207.0
FREE_STATE = 12
I_CHG_MA = 174.5
I_STOP_MA = 1.3
CAP_NOMINAL_MAH = 310.0
RUNTIME_LOADS = (("game mix (S15)", 124.0), ("idle menu BL100+busy (est)", 200.0),
                 ("BL50 static (S5)", 99.0), ("Stop", 5.0))


# ------------------------------------------------------------------ input
def load_log(path: Path) -> list[dict]:
    recs: list[dict] = []
    if path.suffix.lower() == ".bin":
        data = path.read_bytes()
        for off in range(0, len(data) - REC.size + 1, REC.size):
            t, vb, vc, vd, tc, st, fl, seq = REC.unpack_from(data, off)
            if t == 0xFFFFFFFF:        # erased flash
                break
            recs.append({"t": t, "v": vb, "vc": vc, "vdda": vd, "temp": tc / 10.0, "st": st, "fl": fl, "seq": seq})
    else:
        with path.open(newline="", encoding="utf-8") as f:
            for r in csv.DictReader(f):
                recs.append({"t": int(r["t_s"]), "v": int(r["vbat_mv"]), "vc": int(r["vcomp_mv"]),
                             "vdda": int(r["vdda_mv"]), "temp": float(r["temp_c"]), "st": int(r["load_state"]),
                             "fl": int(r["flags"]), "seq": int(r["seq"])})
    for r in recs:
        for i, n in enumerate(FLAGS):
            r[n] = (r["fl"] >> i) & 1
    return recs


# ------------------------------------------------------------------ numerics
def pav(xs, ys, ws):
    """Weighted isotonic (non-decreasing) regression; points sorted by x. Returns blocks (x_mean, y_mean, w)."""
    blocks = []
    for x, y, w in zip(xs, ys, ws):
        blocks.append([x * w, y * w, w, 1])
        while len(blocks) > 1 and blocks[-2][1] / blocks[-2][2] > blocks[-1][1] / blocks[-1][2]:
            b = blocks.pop()
            blocks[-1][0] += b[0]
            blocks[-1][1] += b[1]
            blocks[-1][2] += b[2]
            blocks[-1][3] += b[3]
    return [(b[0] / b[2], b[1] / b[2], b[2]) for b in blocks]


def pchip(xs, ys, x):
    n = len(xs)
    if n == 1:
        return ys[0]
    if x <= xs[0]:
        return ys[0]
    if x >= xs[-1]:
        return ys[-1]
    h = [xs[i + 1] - xs[i] for i in range(n - 1)]
    d = [(ys[i + 1] - ys[i]) / h[i] for i in range(n - 1)]
    m = [0.0] * n
    if n == 2:
        m = [d[0], d[0]]
    else:
        for i in range(1, n - 1):
            if d[i - 1] * d[i] > 0:
                w1, w2 = 2 * h[i] + h[i - 1], h[i] + 2 * h[i - 1]
                m[i] = (w1 + w2) / (w1 / d[i - 1] + w2 / d[i])
        m[0] = d[0] if d[0] * (d[1] if n > 2 else d[0]) >= 0 else 0.0
        m[-1] = d[-1]
    i = max(j for j in range(n - 1) if xs[j] <= x)
    t = (x - xs[i]) / h[i]
    h00, h10, h01, h11 = 2 * t**3 - 3 * t**2 + 1, t**3 - 2 * t**2 + t, -2 * t**3 + 3 * t**2, t**3 - t**2
    return h00 * ys[i] + h10 * h[i] * m[i] + h01 * ys[i + 1] + h11 * h[i] * m[i + 1]


def med(v):
    return statistics.median(v) if v else float("nan")


def isnum(x):
    return x is not None and not (isinstance(x, float) and math.isnan(x))


# ------------------------------------------------------------------ analysis
def power_of(st, ptab):
    return ptab.get(st)


def analyze(recs, p_over=None, cap_mah=None, eta=1.0, i_chg=I_CHG_MA, p12=None):
    A: dict = {"notes": [], "n_records": len(recs)}
    notes = A["notes"]
    n = len(recs)
    if n < 3:
        A["error"] = "fewer than 3 records"
        return A
    ptab = dict(P_TABLE)
    if p_over:
        ptab.update(p_over)
    A["t_total_s"] = recs[-1]["t"] - recs[0]["t"]
    A["v_range_mv"] = [min(r["v"] for r in recs), max(r["v"] for r in recs)]
    A["temp_range_c"] = [min(r["temp"] for r in recs), max(r["temp"] for r in recs)]

    # ---- phase split (indices)
    i_bat = next((i for i, r in enumerate(recs) if not r["vbus"]), None)
    i_cut = next((i for i, r in enumerate(recs) if r["cutoff"]), None)
    A["usb_only"] = i_bat is None
    if i_bat is None:
        notes.append("all records have VBUS=1 (dry run or USB-powered): the cell is not discharging, "
                     "discharge / OCV / R_int analysis skipped")
        A.update(i0=0, i_cut=None, i_chg0=None)
        A["discharge_end_t"] = None
        return A
    i0 = max(0, i_bat - 1) if recs[max(0, i_bat - 1)]["vbus"] else i_bat
    after = i_cut if i_cut is not None else i_bat
    i_chg0 = next((i for i in range(after, n) if recs[i]["vbus"] and recs[i]["chg_step"]), None)
    if i_chg0 is None:
        i_chg0 = next((i for i in range(after + 1, n) if recs[i]["vbus"]), None)
    i_end = i_cut if i_cut is not None else ((i_chg0 - 1) if i_chg0 is not None else n - 1)
    A.update(i0=i0, i_cut=i_cut, i_chg0=i_chg0, i_end=i_end)
    A["cutoff"] = i_cut is not None
    if i_cut is None:
        notes.append("no cutoff record: log ended before the cell reached the cutoff; capacity is a lower bound, "
                     f"SoC axis normalised to {cap_mah or CAP_NOMINAL_MAH:.0f} mAh")
    A["discharge_s"] = recs[i_end]["t"] - recs[i0]["t"]
    t_bat0 = recs[i_bat]["t"]
    A["discharge_s_battery"] = recs[i_end]["t"] - t_bat0

    # ---- states used in discharge
    used = {}
    for i in range(i_bat, i_end + 1):
        used[recs[i]["st"]] = used.get(recs[i]["st"], 0) + 1
    unknown = [s for s in used if s not in ptab and s != FREE_STATE]
    for s in unknown:
        ptab[s] = ptab.get(2, 41.3)
        notes.append(f"state S{s} has no power in the table, using S2 power (override with --p {s}=MW)")
    free_used = FREE_STATE in used and FREE_STATE not in (p_over or {})

    def integrate(pfree):
        """Return (Q_mAh cumulative list over i_bat..i_end, E_mWh) with S12 power = pfree."""
        q, e = [0.0], 0.0
        pt = dict(ptab)
        pt[FREE_STATE] = pfree
        for i in range(i_bat + 1, i_end + 1):
            a, b = recs[i - 1], recs[i]
            dt = (b["t"] - a["t"]) / 3600.0
            if b["vbus"]:
                q.append(q[-1])
                continue
            p = pt.get(b["st"], pt[2])
            v = max(b["v"], 2500) / 1000.0
            q.append(q[-1] + p / v * dt)
            e += p * dt
        return q, e

    # ---- charge phase: charge-side coulomb count
    ch = {}
    if i_chg0 is not None and i_chg0 < n - 1:
        crecs = recs[i_chg0:]
        t_plug = recs[i_chg0]["t"]
        chg_flags = [r["chg"] for r in crecs]
        t_high = None
        seen_low = False
        for r in crecs:
            if r["chg"]:
                seen_low = True
            elif seen_low and r["vbus"]:
                t_high = r["t"]
                break
        vs = [r["v"] for r in crecs if r["vbus"]]
        vmax = max(vs) if vs else 0
        t_cv = None
        for k in range(1, len(crecs) - 1):
            m3 = med([crecs[k - 1]["v"], crecs[k]["v"], crecs[k + 1]["v"]])
            if m3 >= vmax - 8 and crecs[k]["vbus"] and (t_high is None or crecs[k]["t"] <= t_high):
                t_cv = crecs[k]["t"]
                break
        ch.update(t_plug=t_plug, t_chg_high=t_high, t_cv=t_cv, v_max_mv=vmax,
                  t_end_log=crecs[-1]["t"], usb_removed_at_end=not crecs[-1]["vbus"])
        if t_high is not None:
            ch["charge_s"] = t_high - t_plug
        if t_high is not None and t_cv is not None:
            cc_s = max(0, t_cv - t_plug)
            cv_s = max(0, t_high - t_cv)
            i_avg_cv = (i_chg - 0.1 * i_chg) / math.log(10.0)
            ch["q_cc_mah"] = i_chg * cc_s / 3600.0
            ch["q_cv_mah"] = i_avg_cv * cv_s / 3600.0
            ch["q_chg_mah"] = ch["q_cc_mah"] + ch["q_cv_mah"]
            ch["cc_s"], ch["cv_s"] = cc_s, cv_s
        else:
            notes.append("charge phase did not reach CHG-high: no charge-side capacity cross-check")
    A["charge"] = ch
    gap_s = (recs[i_chg0]["t"] - recs[i_end]["t"]) if (i_cut is not None and i_chg0 is not None) else 0
    A["stop_gap_s"] = gap_s
    q_stop = I_STOP_MA * gap_s / 3600.0
    A["q_stop_mah"] = q_stop

    # ---- power of the free state
    p12_src = "table"
    if p12 is not None:
        pfree = p12
        p12_src = "--p12"
    elif free_used:
        pfree = P12_DEFAULT
        p12_src = "default estimate"
        if i_cut is not None and "q_chg_mah" in ch:
            q0, _ = integrate(0.0)
            q1, _ = integrate(1.0)
            a = q1[-1] - q0[-1]
            target = eta * ch["q_chg_mah"] - q_stop
            if a > 0 and target > q0[-1]:
                pfree = (target - q0[-1]) / a
                p12_src = "solved from charge-side coulomb count"
            else:
                notes.append("could not solve S12 power from charge data, using default estimate")
        else:
            notes.append("no cutoff + complete charge: S12 power not derivable, default estimate "
                         f"{P12_DEFAULT:.0f} mW used")
    else:
        pfree = P12_DEFAULT
    A["p_free_mw"] = pfree
    A["p_free_src"] = p12_src
    ptab[FREE_STATE] = pfree
    A["p_table"] = {str(k): v for k, v in sorted(ptab.items())}
    q, e_mwh = integrate(pfree)
    q_total = q[-1]
    if i_cut is None:
        q_total = cap_mah or CAP_NOMINAL_MAH
    elif cap_mah:
        q_total = cap_mah
    A["q_delivered_mah"] = q[-1]
    A["q_total_mah"] = q_total
    A["e_delivered_mwh"] = e_mwh
    A["avg_power_mw"] = e_mwh / (A["discharge_s_battery"] / 3600.0) if A["discharge_s_battery"] else float("nan")
    if "q_chg_mah" in ch:
        A["q_chg_vs_dis_pct"] = 100.0 * (eta * ch["q_chg_mah"] - q_stop) / q[-1] if q[-1] else None

    # SoC per record
    soc = [None] * n
    for k, i in enumerate(range(i_bat, i_end + 1)):
        soc[i] = max(0.0, min(1.0, 1.0 - q[k] / q_total))
    A["_soc"] = soc
    A["_q"] = {i: q[k] for k, i in enumerate(range(i_bat, i_end + 1))}

    def i_of(r):
        return ptab.get(r["st"], ptab[2]) / (max(r["v"], 2500) / 1000.0)

    # ---- step clusters -> R_int. Step records are interleaved with regular samples, so pair by time:
    # "before" = step record followed (<= 3 s) by a step record in a different state (+1 s), then a
    # step record of that state 7..14 s after the "before" one (+10 s).
    steps = []
    sidx = [k for k in range(i_bat, i_end + 1) if recs[k]["step"] and not recs[k]["chg_step"]]
    used_after = set()
    for pos, i in enumerate(sidx):
        if i in used_after:
            continue
        b = recs[i]
        later = [k for k in sidx[pos + 1:] if recs[k]["t"] - b["t"] <= 15]
        k1 = next((k for k in later if recs[k]["st"] != b["st"] and recs[k]["t"] - b["t"] <= 3), None)
        if k1 is None:
            continue
        k10 = next((k for k in later if k > k1 and recs[k]["st"] == recs[k1]["st"] and 7 <= recs[k]["t"] - b["t"] <= 14), None)
        used_after.update(k for k in (k1, k10) if k is not None)
        row = {"t": b["t"], "soc": soc[i], "st_before": b["st"], "st_after": recs[k1]["st"], "v_before": b["v"]}
        for tag, k in (("p1", k1), ("p10", k10)):
            if k is None:
                continue
            a = recs[k]
            di = i_of(b) - i_of(a)
            if abs(di) < 20.0:
                continue
            row["dv_" + tag] = a["v"] - b["v"]
            row["r_" + tag] = 1000.0 * (a["v"] - b["v"]) / di
            row["di_" + tag] = di
        if "r_p1" in row or "r_p10" in row:
            steps.append(row)
    A["steps"] = steps
    # R in mOhm: dV[mV]/dI[mA] = ohm -> *1000
    r1 = [s["r_p1"] for s in steps if "r_p1" in s]
    r10 = [s["r_p10"] for s in steps if "r_p10" in s]
    A["rint_step_p1_mohm"] = {"median": med(r1), "n": len(r1)}
    A["rint_step_p10_mohm"] = {"median": med(r10), "n": len(r10)}
    bins = {}
    for s in steps:
        if "r_p1" in s and s["soc"] is not None:
            bins.setdefault(min(9, int(s["soc"] * 10)), []).append(s["r_p1"])
    A["rint_vs_soc_p1"] = {f"{b * 10}-{b * 10 + 10}%": {"median": med(v), "n": len(v)} for b, v in sorted(bins.items())}
    rint_ref = A["rint_step_p10_mohm"]["median"] if isnum(A["rint_step_p10_mohm"]["median"]) else (
        A["rint_step_p1_mohm"]["median"] if isnum(A["rint_step_p1_mohm"]["median"]) else 150.0)
    A["rint_used_mohm"] = rint_ref

    # ---- charger plug / unplug steps (R_int from charge current)
    plugs = []
    k = 0
    while k < n:
        r = recs[k]
        if r["chg_step"] and k > i_bat:
            j = k
            while j + 1 < n and recs[j + 1]["chg_step"] and recs[j + 1]["t"] - r["t"] <= 70:
                j += 1
            grp = recs[k:j + 1]
            if len(grp) >= 2:
                before = grp[0]
                a1 = next((g for g in grp[1:] if 0 <= g["t"] - before["t"] <= 3), None) or grp[1]
                plugs.append((k, before, a1, grp))
            k = j + 1
        else:
            k += 1
    prow = []
    for k, before, a1, grp in plugs:
        plugin = bool(a1["vbus"])
        row = {"t": before["t"], "kind": "plug" if plugin else "unplug", "v_before": before["v"],
               "v_p1": a1["v"], "dv_p1": a1["v"] - before["v"]}
        if plugin:
            ref = before
            if i_cut is not None and k >= i_end and before["t"] - recs[i_end]["t"] > 5:
                ref = recs[i_end]            # `before` sample is stale; use last real loaded sample
                row["ref"] = "cutoff record"
            else:
                row["ref"] = "before record"
            i_ld = i_of(ref)
            row["r_simple_mohm"] = (a1["v"] - ref["v"]) / i_chg * 1000.0
            row["r_refined_mohm"] = (a1["v"] - ref["v"]) / (i_chg + i_ld) * 1000.0
            row["i_load_ref_ma"] = i_ld
        else:
            if before["chg"] and before["v"] < 4150:
                row["r_simple_mohm"] = (before["v"] - a1["v"]) / i_chg * 1000.0
            else:
                row["note"] = "charger in CV/terminated: current unknown, no R_int"
        prow.append(row)
    A["plug_steps"] = prow
    first_plug = next((p for p in prow if p["kind"] == "plug" and "r_simple_mohm" in p), None)
    A["rint_plug_mohm"] = first_plug

    # ---- rest-end OCV points
    pts = []
    k = i_bat
    while k <= i_end:
        if recs[k]["rest"] and not recs[k]["vbus"] and recs[k]["st"] == 2:
            j = k
            while j + 1 <= i_end and recs[j + 1]["rest"] and not recs[j + 1]["vbus"]:
                j += 1
            run = recs[k:j + 1]
            dur = run[-1]["t"] - run[0]["t"]
            if len(run) >= 3 and dur >= 30 and soc[j] is not None:
                tail = run[-3:]
                v = med([x["v"] for x in tail])
                pts.append({"soc": soc[j], "v": v, "w": 10.0 if dur >= 600 else 1.0, "src": "xrest" if dur >= 600 else "rest",
                            "dur": dur, "t": run[-1]["t"], "i": j})
            k = j + 1
        else:
            k += 1
    anchors = []
    if i_cut is not None:
        tail = recs[max(i_bat, i_cut - 2):i_cut + 1]
        vl = med([x["v"] for x in tail])
        il = med([i_of(x) for x in tail])
        anchors.append({"soc": 0.0, "v": vl + il * rint_ref / 1000.0, "w": 2.0, "src": "cutoff+IR", "dur": 0, "t": recs[i_cut]["t"]})
    if ch.get("t_chg_high") is not None:
        last = [r for r in recs[i_chg0:] if r["vbus"] and r["t"] <= ch["t_chg_high"]][-3:]
        if last:
            anchors.append({"soc": 1.0, "v": med([x["v"] for x in last]), "w": 2.0, "src": "end of charge", "dur": 0,
                            "t": ch["t_chg_high"]})
    A["ocv_points"] = pts + anchors
    A["n_rest_points"] = len(pts)

    # ---- OCV table
    allp = sorted(pts + anchors, key=lambda p: p["soc"])
    tab = None
    if len(allp) >= 4:
        blocks = pav([p["soc"] for p in allp], [p["v"] for p in allp], [p["w"] for p in allp])
        bx = [b[0] for b in blocks]
        by = [b[1] for b in blocks]
        # force strictly increasing x for PCHIP
        for ii in range(1, len(bx)):
            if bx[ii] <= bx[ii - 1]:
                bx[ii] = bx[ii - 1] + 1e-6
        smin, smax = allp[0]["soc"], allp[-1]["soc"]
        tab = []
        extrap = []
        for s in range(0, 101, 5):
            x = s / 100.0
            tab.append(int(round(pchip(bx, by, x))))
            if x < smin - 0.03 or x > smax + 0.03:
                extrap.append(s)
        # enforce monotone after rounding
        for ii in range(1, 21):
            tab[ii] = max(tab[ii], tab[ii - 1])
        A["ocv_table_mv"] = tab
        A["ocv_extrapolated_pct"] = extrap
        A["_ocv_blocks"] = (bx, by)
        if extrap:
            notes.append(f"OCV table points at SoC {extrap} % are outside the measured range (flat extrapolation)")
    else:
        notes.append("fewer than 4 OCV points: no OCV table")

    # ---- runtime estimates
    rt = {}
    if tab and q_total > 0 and isnum(rint_ref):
        for name, p in RUNTIME_LOADS:
            qs = 400
            tt = 0.0
            for k2 in range(qs):
                s = 1.0 - (k2 + 0.5) / qs
                ocv = pchip([x / 20.0 for x in range(21)], [float(v) for v in tab], s) / 1000.0
                v = ocv
                for _ in range(4):
                    v = ocv - (p / v) * rint_ref / 1e6
                    v = max(v, 2.5)
                tt += (q_total / qs) / (p / v)
            rt[name] = {"power_mw": p, "hours": tt}
    A["runtime"] = rt
    return A


# ------------------------------------------------------------------ output
def fmt(x, nd=1):
    return "n/a" if not isnum(x) else f"{x:.{nd}f}"


def hms(s):
    if s is None:
        return "n/a"
    s = int(s)
    return f"{s // 3600}:{s % 3600 // 60:02d}:{s % 60:02d}"


def write_outputs(recs, A, outdir: Path, src: Path):
    outdir.mkdir(parents=True, exist_ok=True)
    ch = A.get("charge", {}) or {}
    tab = A.get("ocv_table_mv")
    L = [f"# Battery log analysis: {src.name}", ""]
    L.append(f"- records: {A['n_records']}, log span {hms(A.get('t_total_s'))}, Vbat {A['v_range_mv'][0]}..{A['v_range_mv'][1]} mV, "
             f"T {A['temp_range_c'][0]:.1f}..{A['temp_range_c'][1]:.1f} C")
    if A.get("usb_only"):
        L.append("- all records on USB power: dry run or no battery discharge, nothing derived")
    else:
        L += [
            f"- discharge duration (USB unplug .. cutoff{'' if A['cutoff'] else ', NO cutoff in log'}): {hms(A['discharge_s_battery'])}",
            f"- Stop gap (cutoff .. USB plug): {hms(A['stop_gap_s'])}, drain {fmt(A['q_stop_mah'], 2)} mAh at {I_STOP_MA} mA",
            f"- charge duration (plug .. CHG high): {hms(ch.get('charge_s'))} (CC {hms(ch.get('cc_s'))}, CV {hms(ch.get('cv_s'))})",
            f"- delivered charge (to cutoff): {fmt(A['q_delivered_mah'])} mAh; energy {fmt(A['e_delivered_mwh'], 0)} mWh; "
            f"average power {fmt(A['avg_power_mw'])} mW",
            f"- charge-side: CC {fmt(ch.get('q_cc_mah'))} + CV tail {fmt(ch.get('q_cv_mah'))} = {fmt(ch.get('q_chg_mah'))} mAh "
            f"(eta applied -> {fmt(A.get('q_chg_vs_dis_pct'))} % of delivered after Stop-drain correction)",
            f"- S{FREE_STATE} power: **{fmt(A['p_free_mw'])} mW** ({A['p_free_src']})",
            f"- Q_total used for the SoC axis: {fmt(A['q_total_mah'])} mAh (nominal 310)",
        ]
        r1, r10 = A["rint_step_p1_mohm"], A["rint_step_p10_mohm"]
        L.append(f"- R_int from load<->rest steps: +1 s median {fmt(r1['median'])} mOhm (n={r1['n']}), "
                 f"+10 s median {fmt(r10['median'])} mOhm (n={r10['n']})")
        for p in A["plug_steps"]:
            L.append(f"- charger {p['kind']} step at t={p['t']} s: V {p['v_before']} -> {p['v_p1']} mV (+1 s), "
                     f"R simple {fmt(p.get('r_simple_mohm'))} mOhm, refined {fmt(p.get('r_refined_mohm'))} mOhm"
                     f"{' (' + p['note'] + ')' if p.get('note') else ''}")
        L += ["", "## R_int vs SoC (+1 s step, median per 10 % bin)", "", "| SoC | R_int mOhm | n |", "|---|---|---|"]
        for k, v in A["rint_vs_soc_p1"].items():
            L.append(f"| {k} | {fmt(v['median'])} | {v['n']} |")
        L += ["", "## OCV table (0..100 % SoC, step 5 %)", ""]
        if tab:
            L += ["| SoC % | OCV mV |", "|---|---|"] + [f"| {5 * i} | {v} |" for i, v in enumerate(tab)]
            L += ["", "```c", c_array(tab), "```", "",
                  "HWCAL: `hwcal set` in Src/cmd.c only accepts display/backlight/debounce scalar fields "
                  "(madctl_*, xoff_*, yoff_*, bgr, invert, gamma_id, spi1_*, debounce_ms, bl_pwm_hz, bl_min_level). "
                  "`ocv_mv[21]`, `rint_mohm` and `i_chg_ma` can NOT be set from the host yet; "
                  "put the array above into `ocv_default` in Src/hwcal.c (or add the keys to hwcal_set_field), "
                  f"then `hwcal set rint_mohm={int(round(A['rint_used_mohm']))}` once supported.",
                  f"Suggested values: rint_mohm={int(round(A['rint_used_mohm']))}, i_chg_ma=175 (ISET 5.1k, 174.5 mA nominal)."]
        else:
            L.append("not enough data (need rests spread over the SoC range).")
        L += ["", "## Runtime estimates (OCV table, constant power, R_int; no extrapolation below the S12 cutoff)", "",
              "| load | mW | hours |", "|---|---|---|"]
        for k, v in A["runtime"].items():
            L.append(f"| {k} | {v['power_mw']:.0f} | {v['hours']:.1f} |")
    L += ["", "## Assumptions / caveats", ""]
    L += [
        "- constant-power load states (buck-boost), I = P/Vbat; powers from the HW_LOG table, S12 solved from the charge side.",
        "- interval state = state of the later record; time resolution is 1 s.",
        "- SoC 100 % = start of discharge (assumed full), SoC 0 % = loaded cutoff 3.30 V (not empty).",
        "- charge efficiency eta = 1.0 (coulombic); CC current 174.5 mA; CV tail modelled as exponential to 10 % ISET.",
        "- short rests (1 min) are not fully relaxed: they bias OCV low by a few mV; 15-min rests weigh 10x.",
        "- R_int(step) includes some fast polarisation (+1 s) / more (+10 s); plug-step 'refined' value divides by (I_chg + I_load) "
        "of the last loaded sample (upper bound, polarisation of the loaded sample leaks in).",
    ]
    L += [f"- NOTE: {m}" for m in A["notes"]]
    (outdir / "summary.md").write_text("\n".join(L) + "\n", encoding="utf-8")

    J = {k: v for k, v in A.items() if not k.startswith("_")}
    J["c_array"] = c_array(tab) if tab else None
    (outdir / "summary.json").write_text(json.dumps(J, indent=2, default=lambda o: None), encoding="utf-8")

    with (outdir / "rint_steps.csv").open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["t_s", "soc", "st_before", "st_after", "v_before_mv", "dv_p1_mv", "r_p1_mohm", "dv_p10_mv", "r_p10_mohm"])
        for s in A.get("steps", []):
            w.writerow([s["t"], f"{s['soc']:.4f}" if s["soc"] is not None else "", s["st_before"], s["st_after"], s["v_before"],
                        s.get("dv_p1", ""), f"{s['r_p1']:.1f}" if "r_p1" in s else "", s.get("dv_p10", ""),
                        f"{s['r_p10']:.1f}" if "r_p10" in s else ""])
    if tab:
        with (outdir / "ocv_table.csv").open("w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["soc_pct", "ocv_mv"])
            for i, v in enumerate(tab):
                w.writerow([5 * i, v])
    plots(recs, A, outdir)


def c_array(tab):
    rows = []
    for k in range(0, 21, 7):
        rows.append("    " + ", ".join(str(v) for v in tab[k:k + 7]) + ",")
    return "static const uint16_t ocv_mv[21] = {\n" + "\n".join(rows) + "\n};"


def plots(recs, A, outdir):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except Exception as e:  # noqa: BLE001
        A["notes"].append(f"matplotlib unavailable ({e}), no PNG plots")
        return
    t = [r["t"] / 3600.0 for r in recs]
    v = [r["v"] / 1000.0 for r in recs]
    # 1 cycle
    fig, ax = plt.subplots(figsize=(12, 5))
    ax.plot(t, v, lw=0.7, color="tab:blue")
    ax.set_xlabel("time, h")
    ax.set_ylabel("Vbat, V")
    ax.grid(alpha=0.3)
    ax.set_title("Battery cycle")
    i0, ic, i1 = A.get("i0"), A.get("i_cut"), A.get("i_chg0")
    if A.get("usb_only"):
        ax.text(0.5, 0.9, "USB powered (dry run)", transform=ax.transAxes, ha="center")
    else:
        if i0 is not None:
            ax.axvspan(recs[i0]["t"] / 3600.0, recs[A["i_end"]]["t"] / 3600.0, color="tab:orange", alpha=0.08, label="discharge")
        if ic is not None and i1 is not None:
            ax.axvspan(recs[ic]["t"] / 3600.0, recs[i1]["t"] / 3600.0, color="gray", alpha=0.15, label="Stop gap")
        if ic is not None:
            ax.axvline(recs[ic]["t"] / 3600.0, color="red", ls="--", label="cutoff")
        if i1 is not None:
            ax.axvspan(recs[i1]["t"] / 3600.0, recs[-1]["t"] / 3600.0, color="tab:green", alpha=0.08, label="charge")
        th = (A.get("charge") or {}).get("t_chg_high")
        if th is not None:
            ax.axvline(th / 3600.0, color="green", ls="--", label="CHG high")
        ax.legend(loc="best")
    fig.tight_layout()
    fig.savefig(outdir / "cycle.png", dpi=110)
    plt.close(fig)
    if A.get("usb_only") or A.get("_soc") is None:
        return
    soc, q = A["_soc"], A["_q"]
    # discharge V vs mAh
    fig, ax = plt.subplots(figsize=(9, 5))
    idx = sorted(q)
    ax.plot([q[i] for i in idx], [recs[i]["v"] / 1000.0 for i in idx], lw=0.7, label="Vbat (loaded + rest)")
    rp = [p for p in A["ocv_points"] if p["src"] in ("rest", "xrest")]
    ax.plot([(1 - p["soc"]) * A["q_total_mah"] for p in rp], [p["v"] / 1000.0 for p in rp], "r.", label="rest-end")
    ax.set_xlabel("delivered, mAh")
    ax.set_ylabel("Vbat, V")
    ax.grid(alpha=0.3)
    ax.legend()
    ax.set_title("Discharge")
    fig.tight_layout()
    fig.savefig(outdir / "discharge.png", dpi=110)
    plt.close(fig)
    # charge
    if A.get("i_chg0") is not None:
        cr = recs[A["i_chg0"]:]
        if len(cr) > 2:
            fig, ax = plt.subplots(figsize=(9, 5))
            tc = [(r["t"] - cr[0]["t"]) / 60.0 for r in cr]
            ax.plot(tc, [r["v"] / 1000.0 for r in cr], lw=0.8)
            ax.set_xlabel("time since plug, min")
            ax.set_ylabel("Vbat, V")
            ax2 = ax.twinx()
            ax2.step(tc, [r["chg"] for r in cr], where="post", color="tab:red", alpha=0.6)
            ax2.set_ylabel("CHG active (flag)")
            ax2.set_ylim(-0.1, 1.5)
            ax.grid(alpha=0.3)
            ax.set_title("Charge")
            fig.tight_layout()
            fig.savefig(outdir / "charge.png", dpi=110)
            plt.close(fig)
    # OCV
    if A.get("_ocv_blocks"):
        fig, ax = plt.subplots(figsize=(8, 5))
        for src, col in (("rest", "tab:blue"), ("xrest", "tab:red"), ("cutoff+IR", "k"), ("end of charge", "g")):
            pp = [p for p in A["ocv_points"] if p["src"] == src]
            ax.plot([p["soc"] * 100 for p in pp], [p["v"] for p in pp], ".", color=col, label=src)
        ax.plot(range(0, 101, 5), A["ocv_table_mv"], "-o", color="tab:orange", ms=3, label="table")
        ax.set_xlabel("SoC, %")
        ax.set_ylabel("OCV, mV")
        ax.grid(alpha=0.3)
        ax.legend()
        fig.tight_layout()
        fig.savefig(outdir / "ocv.png", dpi=110)
        plt.close(fig)
    # R_int
    st = [s for s in A.get("steps", []) if s["soc"] is not None]
    if st:
        fig, ax = plt.subplots(figsize=(8, 5))
        ax.plot([s["soc"] * 100 for s in st if "r_p1" in s], [s["r_p1"] for s in st if "r_p1" in s], ".", label="+1 s")
        ax.plot([s["soc"] * 100 for s in st if "r_p10" in s], [s["r_p10"] for s in st if "r_p10" in s], ".", label="+10 s")
        pl = A.get("rint_plug_mohm")
        if pl:
            ax.axhline(pl["r_refined_mohm"], color="g", ls=":", label="charger plug (refined)")
        ax.set_xlabel("SoC, %")
        ax.set_ylabel("R_int, mOhm")
        ax.grid(alpha=0.3)
        ax.legend()
        fig.tight_layout()
        fig.savefig(outdir / "rint.png", dpi=110)
        plt.close(fig)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("log", type=Path, help="battlog .bin or .csv from `hwtest.py battlog dump`")
    ap.add_argument("--p12", type=float, help="power of S12 in mW (default: solved from the charge phase)")
    ap.add_argument("--p", action="append", default=[], metavar="STATE=MW", help="override/add a state power")
    ap.add_argument("--cap-mah", type=float, help="force Q_total for the SoC axis")
    ap.add_argument("--eta", type=float, default=1.0, help="coulombic charge efficiency")
    ap.add_argument("--i-chg", type=float, default=I_CHG_MA, help="CC charge current, mA")
    ap.add_argument("--outdir", type=Path)
    a = ap.parse_args(argv)
    recs = load_log(a.log)
    over = {}
    for s in a.p:
        k, v = s.split("=")
        over[int(k)] = float(v)
    A = analyze(recs, over, a.cap_mah, a.eta, a.i_chg, a.p12)
    if "error" in A:
        print("error:", A["error"], file=sys.stderr)
        return 2
    out = a.outdir or a.log.with_name(a.log.stem + "-analysis")
    write_outputs(recs, A, out, a.log)
    print(f"wrote {out}")
    if A.get("usb_only"):
        print("USB-only log: no discharge analysis")
    else:
        print(f"discharge {hms(A['discharge_s_battery'])}, Q {fmt(A['q_delivered_mah'])} mAh, P12 {fmt(A['p_free_mw'])} mW "
              f"({A['p_free_src']}), R_int(step +10s) {fmt(A['rint_step_p10_mohm']['median'])} mOhm")
        if A.get("ocv_table_mv"):
            print(c_array(A["ocv_table_mv"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
