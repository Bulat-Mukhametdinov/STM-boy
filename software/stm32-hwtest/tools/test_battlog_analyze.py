#!/usr/bin/env python3
"""Tests for battlog_analyze.py with a synthetic log.  Run:  py -3.14 tools/test_battlog_analyze.py"""
from __future__ import annotations

import math
import random
import struct
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import battlog_analyze as B  # noqa: E402
import gauge_sim  # noqa: E402

# gauge_sim's cell curve below 85 %, with a top end that is consistent with a CC->CV charge at 0.56 C
OCV_PTS = [pt for pt in gauge_sim.CELL_OCV_PTS if pt[0] <= 0.80] + [
    (0.85, 4020.0), (0.90, 4090.0), (0.92, 4150.0), (0.95, 4175.0), (1.00, 4200.0)]
R0 = 0.150          # ohm
R1, TAU1 = 0.030, 30.0
Q_CELL = 310.0      # mAh between SoC 0 and 1 of the true cell
P12_TRUE = 190.0
P2_TRUE = 41.3
SOC_START = 0.994   # fully charged = where the CV termination (10 % ISET) leaves the cell
GAP_S = 3600


def ocv_true(s):
    s = min(1.0, max(0.0, s))
    for (x0, y0), (x1, y1) in zip(OCV_PTS, OCV_PTS[1:]):
        if s <= x1:
            return y0 + (y1 - y0) * (s - x0) / (x1 - x0)
    return OCV_PTS[-1][1]


class Cell:
    """OCV(SoC) - i*R0 - one RC polarisation (i > 0 = discharge)."""

    def __init__(self, soc):
        self.soc, self.vp = soc, 0.0

    def advance(self, i_ma, dt=1.0):
        self.vp += (i_ma * R1 - self.vp) * (1 - math.exp(-dt / TAU1))
        self.soc -= i_ma * dt / 3600.0 / Q_CELL

    def v(self, i_ma):
        return ocv_true(self.soc) - i_ma * R0 - self.vp


def i_for_power(cell, p_mw):
    i = p_mw / 3.8
    for _ in range(8):
        i = p_mw / (cell.v(i) / 1000.0)
    return i


def make_log(seed=1):
    rng = random.Random(seed)
    out = []
    t = 0
    seq = 0

    def emit(state, flags, v_true, vbus, chg):
        nonlocal seq
        v = int(round(v_true + rng.gauss(0, 2.5)))
        fl = flags | (1 if chg else 0) | (2 if vbus else 0)
        out.append((t, v, state, fl, seq))
        seq += 1
        return v

    cell = Cell(SOC_START)
    P = {12: P12_TRUE, 2: P2_TRUE}
    for _ in range(5):                      # on USB before the unplug
        emit(12, 0, ocv_true(SOC_START) - 8, 1, 0)
        t += 2
    phase, ph_t0, state = "L", t, 12
    xr_done, xr_pending = set(), False
    steps = []                              # due times of +1 / +10 s samples
    cut_cnt, cutoff = 0, False
    while not cutoff:
        i = i_for_power(cell, P[state])
        cell.advance(i)
        t += 1
        v = cell.v(i_for_power(cell, P[state]))
        el = t - ph_t0
        rest_bit = 4 if phase != "L" else 0
        if steps and t >= steps[0]:
            steps.pop(0)
            emit(state, 0x40 | rest_bit, v, 0, 0)
        elif (el % 5 == 0) if phase == "L" else (el < 60 or el % 10 == 0):
            vm = emit(state, rest_bit, v, 0, 0)
            cut_cnt = cut_cnt + 1 if vm < 3300 else 0
            if cut_cnt >= 3:
                out[-1] = out[-1][:3] + (out[-1][3] | 0x20, out[-1][4])
                cutoff = True
        if phase == "L":
            for thr in (0.75, 0.5, 0.25, 0.1):
                if thr not in xr_done and cell.soc <= thr:
                    xr_done.add(thr)
                    xr_pending = True
        if cutoff:
            break
        if phase == "L" and el >= 540:
            emit(state, 0x40, v, 0, 0)
            phase = "XR" if xr_pending else "R"
            xr_pending = False
            state, ph_t0, steps = 2, t, [t + 1, t + 10]
        elif (phase == "R" and el >= 60) or (phase == "XR" and el >= 900):
            emit(state, 0x40 | 4, v, 0, 0)
            phase, state, ph_t0, steps = "L", 12, t, [t + 1, t + 10]
    # Stop gap, then USB plug
    cell.soc -= 1.3 * GAP_S / 3600.0 / Q_CELL
    cell.vp = 0.0
    stale_v = out[-1][1]
    t += GAP_S
    out.append((t, stale_v, 2, 0x80 | 2, seq))     # "before" sample = stale loaded value
    seq += 1
    t_plug, cv, t_cv, t_done, chg = t, False, None, None, 1
    icc = 174.5
    while True:
        t += 1
        el = t - t_plug
        if not cv:
            cell.advance(-icc)
            vt = cell.v(-icc)
            if vt >= 4200:
                cv, t_cv = True, t
        else:
            cur = icc * math.exp(-(t - t_cv) / 400.0)
            cell.soc += cur / 3600.0 / Q_CELL
            vt = 4200.0
            if cur <= 0.1 * icc and t_done is None:
                chg, t_done = 0, t
        if el in (1, 10, 60):
            emit(2, 0x80, vt, 1, chg)
        elif el > 60 and el % 10 == 0:
            emit(2, 0x04, vt, 1, chg)
        if t_done is not None and t - t_done >= 1800:
            break
    blob = b"".join(struct.pack("<IHHHhBBH", ti, v, v, 3300, 280, st, fl, sq & 0xFFFF) for ti, v, st, fl, sq in out)
    return blob, cell


class T(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.p = Path(cls.tmp.name) / "synthetic.bin"
        blob, _ = make_log()
        cls.p.write_bytes(blob)
        cls.recs = B.load_log(cls.p)
        cls.A = B.analyze(cls.recs)

    def test_phases(self):
        A = self.A
        self.assertTrue(A["cutoff"])
        self.assertAlmostEqual(A["stop_gap_s"], GAP_S, delta=20)
        self.assertIsNotNone(A["charge"]["t_chg_high"])
        self.assertGreater(A["n_rest_points"], 20)

    def test_p12_solved(self):
        self.assertAlmostEqual(self.A["p_free_mw"], P12_TRUE, delta=P12_TRUE * 0.05)
        self.assertEqual(self.A["p_free_src"], "solved from charge-side coulomb count")

    def soc_cut_true(self):
        v_cut = [r for r in self.recs if r["cutoff"]][0]["v"]
        i = P12_TRUE / (v_cut / 1000.0)
        lo, hi = 0.0, 0.3
        for _ in range(60):
            mid = (lo + hi) / 2
            c = Cell(mid)
            c.vp = i * R1
            if c.v(i) < v_cut:
                lo = mid
            else:
                hi = mid
        return lo

    def test_ocv(self):
        tab = self.A["ocv_table_mv"]
        self.assertEqual(len(tab), 21)
        self.assertTrue(all(b >= a for a, b in zip(tab, tab[1:])))
        sc = self.soc_cut_true()
        worst = 0.0
        for k, v in enumerate(tab):
            s = k / 20.0
            truth = ocv_true(sc + s * (SOC_START - sc))
            worst = max(worst, abs(v - truth) if s >= 0.1 else 0)
            tol = 15 if s >= 0.10 else 40       # the knee below 10 % is ~35 mV per SoC-%
            self.assertLess(abs(v - truth), tol, f"SoC {5 * k}%: table {v} truth {truth:.0f}")
        print(f"\nOCV worst error (>=10 %): {worst:.1f} mV, table {tab}")

    def test_rint(self):
        r1 = self.A["rint_step_p1_mohm"]["median"]
        r10 = self.A["rint_step_p10_mohm"]["median"]
        # truth R0 = 150 mOhm; +10 s still contains ~28 % of the 30 mOhm RC
        self.assertLess(abs(r1 - 150.0), 30.0)         # within 20 %
        self.assertLess(abs(r10 - 150.0), 50.0)    # +10 s also sees OCV drift under load and the RC tail
        self.assertGreater(self.A["rint_step_p1_mohm"]["n"], 20)
        print(f"R_int +1s {r1:.1f}, +10s {r10:.1f} mOhm")

    def test_plug_rint(self):
        pl = self.A["rint_plug_mohm"]
        self.assertIsNotNone(pl)
        # biased by the OCV shift during the Stop gap (steep knee): only a sanity bound
        print(f"plug R simple {pl['r_simple_mohm']:.0f} refined {pl['r_refined_mohm']:.0f} mOhm")
        self.assertTrue(math.isfinite(pl["r_refined_mohm"]))

    def test_outputs_and_runtime(self):
        out = Path(self.tmp.name) / "out"
        B.write_outputs(self.recs, self.A, out, self.p)
        for f in ("summary.md", "summary.json", "ocv_table.csv", "rint_steps.csv"):
            self.assertTrue((out / f).exists(), f)
        rt = self.A["runtime"]
        self.assertGreater(rt["Stop"]["hours"], rt["BL50 static (S5)"]["hours"])
        self.assertGreater(rt["BL50 static (S5)"]["hours"], rt["game mix (S15)"]["hours"])

    def test_csv_roundtrip(self):
        c = Path(self.tmp.name) / "s.csv"
        lines = ["t_s,vbat_mv,vcomp_mv,vdda_mv,temp_c,load_state,flags,seq,chg,vbus,rest,cal_due,marker,cutoff,step,chg_step"]
        for r in self.recs:
            lines.append(f"{r['t']},{r['v']},{r['vc']},{r['vdda']},{r['temp']:.1f},{r['st']},{r['fl']},{r['seq']},0,0,0,0,0,0,0,0")
        c.write_text("\n".join(lines) + "\n")
        r2 = B.load_log(c)
        self.assertEqual(len(r2), len(self.recs))
        self.assertEqual(r2[5]["v"], self.recs[5]["v"])
        self.assertEqual(r2[5]["vbus"], self.recs[5]["vbus"])

    def test_dry_csv(self):
        dry = HERE / "results" / "002800343435511331323538" / "battlog-20261113-214159.csv"
        if not dry.exists():
            self.skipTest("no dry-run csv")
        recs = B.load_log(dry)
        A = B.analyze(recs)
        self.assertTrue(A["usb_only"])
        out = Path(self.tmp.name) / "dry"
        B.write_outputs(recs, A, out, dry)
        self.assertTrue((out / "summary.md").exists())


if __name__ == "__main__":
    unittest.main()
