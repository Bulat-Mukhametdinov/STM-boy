#!/usr/bin/env python3
"""Synthetic STM-boy battery simulator for the gauge (no hardware needed).

Model: 1S LiPo (OCV(SoC) curve + R0(SoC) + two RC pairs), BQ24075 charger
(CC 175 mA -> CV 4.20 V -> terminate at 10 % ISET, recharge below 4.10 V,
power path: with USB the system runs from VBUS and the cell sees only the
charge current, unless the load exceeds the input limit), the gauge's ADC
front end (16 samples per window, +-5 mV noise, 12-bit quantisation through
the 82/115 divider, 4+4 trimmed mean) and STM-boy load profiles.

Writes a gauge replay CSV (250 ms rows):
    t_s,vbat_mv,i_est_ma,vbus,chg,spread_mv,true_soc,boot,cell_soc,i_true_ma,v_term_mv
true_soc is "usable" SoC in % (0 = the cell state at which a 70 mA game load
reaches the 3.30 V cut-off, 100 = full cell). i_est_ma is what the firmware
would compute from its per-state current table (true currents with a fixed
per-component error), so the gauge also sees load-estimate error.

The `pulsed` profile is the P2b battery-cycle run and can also emit the
16-byte battlog records (--battlog) consumed by battlog_to_ocv.py.

    py -3.14 tools/gauge_sim.py steady  --seed 1 -o steady.csv
    py -3.14 tools/gauge_sim.py pulsed  --seed 1 -o pulsed.csv --battlog pulsed.bin --meta pulsed.json
    py -3.14 tools/gauge_sim.py plug | cycle | nobatt ...
"""
from __future__ import annotations

import argparse
import bisect
import json
import math
import random
import struct
import sys
from pathlib import Path

PROFILES = ("steady", "pulsed", "plug", "cycle", "nobatt")
BATTLOG_FMT = "<IHHHhBBH"   # t_s, vbat, vcomp, vdda, temp_dC, load_state, flags, seq
assert struct.calcsize(BATTLOG_FMT) == 16
F_CHG, F_VBUS, F_REST, F_CAL, F_MARK = 1, 2, 4, 8, 16

# ---------------------------------------------------------------- cell model

# Rested OCV of the simulated cell vs cell SoC (fraction). Deliberately NOT the
# gauge's default table: a plausible LiCoO2 pouch-cell shape (flat 3.7-3.8 V
# plateau, knee below 10 %).
CELL_OCV_PTS = [
    (0.00, 3270.0), (0.03, 3450.0), (0.06, 3560.0), (0.10, 3630.0), (0.15, 3672.0),
    (0.20, 3700.0), (0.30, 3738.0), (0.40, 3768.0), (0.50, 3803.0), (0.60, 3851.0),
    (0.70, 3910.0), (0.80, 3975.0), (0.90, 4060.0), (0.95, 4115.0), (1.00, 4185.0),
]


def pchip_slopes(xs, ys):
    """Fritsch-Carlson monotone cubic slopes."""
    n = len(xs)
    h = [xs[i + 1] - xs[i] for i in range(n - 1)]
    d = [(ys[i + 1] - ys[i]) / h[i] for i in range(n - 1)]
    m = [0.0] * n
    m[0], m[-1] = d[0], d[-1]
    for i in range(1, n - 1):
        if d[i - 1] * d[i] <= 0:
            m[i] = 0.0
        else:
            w1, w2 = 2 * h[i] + h[i - 1], h[i] + 2 * h[i - 1]
            m[i] = (w1 + w2) / (w1 / d[i - 1] + w2 / d[i])
    return m


def pchip_eval(xs, ys, ms, x):
    if x <= xs[0]:
        return ys[0] + ms[0] * (x - xs[0])
    if x >= xs[-1]:
        return ys[-1] + ms[-1] * (x - xs[-1])
    i = bisect.bisect_right(xs, x) - 1
    h = xs[i + 1] - xs[i]
    t = (x - xs[i]) / h
    h00 = (1 + 2 * t) * (1 - t) ** 2
    h10 = t * (1 - t) ** 2
    h01 = t * t * (3 - 2 * t)
    h11 = t * t * (t - 1)
    return h00 * ys[i] + h10 * h * ms[i] + h01 * ys[i + 1] + h11 * h * ms[i + 1]


class Cell:
    def __init__(self, cap_mah=310.0, r0=160.0, r1=40.0, tau1=15.0, r2=60.0, tau2=400.0,
                 soc=1.0, ocv_shift_mv=0.0):
        self.cap = cap_mah
        self.r0, self.r1, self.tau1, self.r2, self.tau2 = r0, r1, tau1, r2, tau2
        self.soc = soc
        self.v1 = 0.0   # RC voltages, mV (+ while discharging)
        self.v2 = 0.0
        self.shift = ocv_shift_mv
        self._xs = [p[0] for p in CELL_OCV_PTS]
        self._ys = [p[1] for p in CELL_OCV_PTS]
        self._ms = pchip_slopes(self._xs, self._ys)

    def ocv(self, soc=None):
        s = self.soc if soc is None else soc
        if s < 0:   # steep collapse below the table
            return self._ys[0] + self.shift + 8000.0 * s
        return pchip_eval(self._xs, self._ys, self._ms, s) + self.shift

    def r0_at(self, soc=None):
        s = self.soc if soc is None else soc
        k = max(0.0, (0.2 - s) / 0.2)
        return self.r0 * (1.0 + 0.8 * k * k)

    def terminal(self, i_ma):
        """Terminal voltage, mV, for battery current i_ma (+ = discharge)."""
        return self.ocv() - i_ma * self.r0_at() / 1000.0 - self.v1 - self.v2

    def step(self, i_ma, dt):
        self.soc -= i_ma * dt / 3600.0 / self.cap
        if self.soc > 1.0:
            self.soc = 1.0
        e1 = math.exp(-dt / self.tau1)
        e2 = math.exp(-dt / self.tau2)
        self.v1 = self.v1 * e1 + i_ma * self.r1 / 1000.0 * (1 - e1)
        self.v2 = self.v2 * e2 + i_ma * self.r2 / 1000.0 * (1 - e2)

    def soc_at_cutoff(self, i_ref=70.0, v_cut=3300.0):
        """Cell SoC where a steady i_ref load hits v_cut (RC pairs saturated)."""
        lo, hi = -0.05, 0.5
        for _ in range(60):
            mid = (lo + hi) / 2
            v = self.ocv(mid) - i_ref * (self.r0_at(mid) + self.r1 + self.r2) / 1000.0
            if v < v_cut:
                lo = mid
            else:
                hi = mid
        return (lo + hi) / 2


class Charger:
    """BQ24075: CC/CV, termination at ITERM, recharge below VRCH, power path."""

    def __init__(self, icc=172.0, vreg=4200.0, iterm_frac=0.10, vrch=100.0, chg_delay=0.5):
        self.icc, self.vreg, self.iterm, self.vrch = icc, vreg, icc * iterm_frac, vrch
        self.state = "off"
        self.chg_delay = chg_delay
        self.t_on = 0.0

    def plug(self, t):
        self.state = "cc"
        self.t_on = t

    def unplug(self):
        self.state = "off"

    def charge_current(self, cell, t):
        """Charge current into the cell (mA, >= 0) and CHG pin state."""
        if self.state == "off":
            return 0.0, False
        if t - self.t_on < self.chg_delay:
            return 0.0, False
        if self.state == "done":
            if cell.terminal(0.0) < self.vreg - self.vrch:
                self.state = "cc"
            else:
                return 0.0, False
        r0 = cell.r0_at()
        if self.state == "cc":
            v = cell.ocv() + self.icc * r0 / 1000.0 - cell.v1 - cell.v2
            if v >= self.vreg:
                self.state = "cv"
        if self.state == "cc":
            return self.icc, True
        i = (self.vreg - cell.ocv() + cell.v1 + cell.v2) * 1000.0 / r0
        i = min(i, self.icc)
        if i < self.iterm:
            self.state = "done"
            return 0.0, False
        return i, True


# ---------------------------------------------------------------- loads

# true current per component, mA (game mix ~74 mA = base + backlight 50 % + audio)
LOAD_COMPONENTS = {"base": 38.0, "bl25": 8.0, "bl50": 16.0, "bl75": 24.0, "bl100": 32.0,
                   "audio": 20.0, "cpu_max": 30.0, "wfi": 12.0, "stop": 0.05}


class Loads:
    def __init__(self, rng, est_err=0.15):
        self.err = {k: 1.0 + rng.uniform(-est_err, est_err) for k in LOAD_COMPONENTS}

    def current(self, comps):
        true = sum(LOAD_COMPONENTS[c] for c in comps)
        est = sum(LOAD_COMPONENTS[c] * self.err[c] for c in comps)
        return true, est


# ---------------------------------------------------------------- ADC front end

LSB_MV = 3300.0 / 4095.0 * 115.0 / 82.0


def measure(v_true, rng, noise=5.0, gain_err=0.0, offset_mv=0.0, nobatt=False):
    """16-sample window -> (trimmed-mean mV, spread mV)."""
    v = v_true * (1.0 + gain_err) + offset_mv
    if nobatt:
        s = [rng.uniform(3000.0, 4300.0) for _ in range(16)]
    else:
        s = [v + rng.uniform(-noise, noise) for _ in range(16)]
    s = sorted(round(x / LSB_MV) * LSB_MV for x in s)
    mean = sum(s[4:12]) / 8.0
    return int(round(mean)), int(round(s[-1] - s[0]))


# ---------------------------------------------------------------- simulation core

class Sim:
    def __init__(self, seed, dt=0.25, cell_kw=None, gain_err=0.0, offset_mv=0.0, noise=5.0,
                 est_err=0.15, icc=172.0):
        self.rng = random.Random(seed)
        self.dt = dt
        self.cell = Cell(**(cell_kw or {}))
        self.chg = Charger(icc=icc)
        self.loads = Loads(self.rng, est_err)
        self.gain_err, self.offset_mv, self.noise = gain_err, offset_mv, noise
        self.t = 0.0
        self.rows = []
        self.blog = []          # battlog records (1 s)
        self.blog_next = 0.0
        self.blog_seq = 0
        self.vbus = False
        self.s_cut = self.cell.soc_at_cutoff()
        self.boot_next = False
        self.ilim = 500.0
        self.crit_cnt = 0
        self.crit_next = 0.0

    def true_soc(self):
        return (self.cell.soc - self.s_cut) / (1.0 - self.s_cut) * 100.0

    def plug(self):
        self.vbus = True
        self.chg.plug(self.t)

    def unplug(self):
        self.vbus = False
        self.chg.unplug()

    def run(self, comps, seconds, log=True, load_state=15, rest=False, until_cutoff=False,
            battlog=False, stop_at=None, marker=False):
        """Advance `seconds` with load components `comps`. Returns 'cutoff' if the
        loaded 3 x 1 s < 3.30 V rule fired (device would go to Stop)."""
        n = int(round(seconds / self.dt))
        for k in range(n):
            i_load, i_est = self.loads.current(comps)
            if self.vbus:
                i_c, chg = self.chg.charge_current(self.cell, self.t)
                i_batt = -i_c + max(0.0, i_load - self.ilim)
            else:
                chg = False
                i_batt = i_load
            v_term = self.cell.terminal(i_batt)
            if log:
                vb, spread = measure(v_term, self.rng, self.noise, self.gain_err, self.offset_mv)
                self.rows.append((round(self.t, 3), vb, round(i_est, 1), int(self.vbus), int(chg),
                                  spread, round(self.true_soc(), 3), int(self.boot_next),
                                  round(self.cell.soc * 100, 3), round(i_batt, 1), round(v_term, 1)))
                self.boot_next = False
                if battlog and self.t >= self.blog_next:
                    self.blog_next = self.t + 1.0
                    vcomp = vb + int(round(i_est * 150 / 1000.0)) if not self.vbus else vb
                    fl = (F_CHG if chg else 0) | (F_VBUS if self.vbus else 0) | (F_REST if rest else 0)
                    if marker and k == 0:
                        fl |= F_MARK
                    self.blog.append((int(round(self.t)), vb, vcomp,
                                      3300 + int(self.rng.uniform(-3, 3)), 250,
                                      load_state if not (self.vbus and chg) else 16, fl,
                                      self.blog_seq & 0xFFFF))
                    self.blog_seq += 1
                if until_cutoff and not self.vbus and self.t >= self.crit_next:
                    self.crit_next = self.t + 1.0
                    self.crit_cnt = self.crit_cnt + 1 if vb < 3300 else 0
                    if self.crit_cnt >= 3:
                        self.cell.step(i_batt, self.dt)
                        self.t += self.dt
                        return "cutoff"
            self.cell.step(i_batt, self.dt)
            self.t += self.dt
            if stop_at is not None and stop_at(self):
                return "stop"
        return None

    def idle(self, seconds, i_ma=0.05):
        """Device off / in Stop: no rows, cell relaxes (big steps)."""
        step = 5.0
        t_end = self.t + seconds
        while self.t < t_end:
            d = min(step, t_end - self.t)
            if self.vbus:
                i_c, _ = self.chg.charge_current(self.cell, self.t)
                self.cell.step(-i_c, d)
            else:
                self.cell.step(i_ma, d)
            self.t += d

    def write_csv(self, path):
        with open(path, "w", newline="") as f:
            f.write("t_s,vbat_mv,i_est_ma,vbus,chg,spread_mv,true_soc,boot,cell_soc,i_true_ma,v_term_mv\n")
            for r in self.rows:
                f.write(",".join(str(x) for x in r) + "\n")

    def write_battlog(self, path):
        with open(path, "wb") as f:
            for r in self.blog:
                f.write(struct.pack(BATTLOG_FMT, *r))


GAME = ["base", "bl50", "audio"]


def game_mix(rng):
    """Random backlight level and audio on/off, like a real play session."""
    bl = rng.choices(["bl25", "bl50", "bl75", "bl100"], weights=[1, 4, 2, 1])[0]
    comps = ["base", bl]
    if rng.random() < 0.8:
        comps.append("audio")
    return comps


def play(sim, seconds=None, until_cutoff=False, stop_at=None):
    """Game-like load, changing backlight/audio every 1..6 min."""
    t_end = None if seconds is None else sim.t + seconds
    while True:
        seg = sim.rng.uniform(60, 360)
        if t_end is not None:
            seg = min(seg, t_end - sim.t)
            if seg <= 0:
                return None
        r = sim.run(game_mix(sim.rng), seg, until_cutoff=until_cutoff, stop_at=stop_at)
        if r:
            return r


# ---------------------------------------------------------------- profiles

def prof_steady(sim):
    sim.cell.soc = 0.99
    return play(sim, until_cutoff=True)


def prof_pulsed(sim, charge=True):
    """P2b: 9 min game mix / 1 min WFI rest, 15 min rests at ~75/50/25/10 %
    (by the firmware's own coulomb estimate on 310 mAh), cut-off 3.30 V -> Stop,
    then USB: charge to termination + 30 min."""
    sim.cell.soc = 0.99
    sim.run(["wfi"], 120, load_state=2, rest=True, battlog=True)   # rested start
    q_used = 0.0
    long_rests = [75.0, 50.0, 25.0, 10.0]
    while True:
        r = sim.run(GAME, 540, load_state=15, until_cutoff=True, battlog=True)
        q_used += 540 * 74.0 / 3600.0
        if r == "cutoff":
            break
        est = 100.0 - q_used / 310.0 * 100.0
        rest_s = 60
        if long_rests and est <= long_rests[0]:
            long_rests.pop(0)
            rest_s = 900
        sim.run(["wfi"], rest_s, load_state=2, rest=True, battlog=True, marker=rest_s > 60)
        q_used += rest_s * 12.0 / 3600.0
    if charge:
        sim.idle(20 * 60)                     # Stop until the user plugs USB
        sim.plug()
        sim.run(["wfi"], 4 * 3600, load_state=2, battlog=True,
                stop_at=lambda s: s.chg.state == "done")
        sim.run(["wfi"], 30 * 60, load_state=2, battlog=True)
    return "ok"


def prof_plug(sim):
    sim.cell.soc = 0.60
    play(sim, 40 * 60)
    sim.plug()
    play(sim, 25 * 60)                         # partial charge
    sim.unplug()
    play(sim, 60 * 60)
    sim.plug()
    play(sim, 4 * 3600, stop_at=lambda s: s.chg.state == "done")
    play(sim, 20 * 60)                         # full, on USB
    sim.ilim = 100.0                           # USB100 input limit: battery supplements
    sim.run(["base", "bl100", "audio", "cpu_max"], 40 * 60)
    sim.ilim = 500.0
    play(sim, 3600, stop_at=lambda s: s.chg.state == "done")  # top-up to termination
    play(sim, 10 * 60)
    sim.unplug()
    play(sim, 40 * 60)
    return "ok"


def prof_cycle(sim, n_boots=10):
    """Power cycles at random SoC: off 1..20 min (cell relaxes), one off period on
    USB (charges while off); boot=1 marks the first row after each power-on."""
    sim.cell.soc = sim.rng.uniform(0.75, 0.95)
    charge_boot = sim.rng.randrange(2, n_boots - 2)
    for b in range(n_boots):
        r = play(sim, sim.rng.uniform(10, 35) * 60, until_cutoff=True)
        if r == "cutoff":
            return r
        if b == charge_boot:
            sim.plug()
            sim.idle(sim.rng.uniform(15, 25) * 60)
            sim.unplug()
            sim.idle(60)
        else:
            sim.idle(sim.rng.uniform(1, 20) * 60)
        sim.boot_next = True
    return play(sim, until_cutoff=True)


def prof_nobatt(sim):
    """USB without a battery (window spread huge), then a battery is inserted."""
    sim.vbus = True
    for _ in range(int(120 / sim.dt)):
        vb, spread = measure(3800.0, sim.rng, nobatt=True)
        sim.rows.append((round(sim.t, 3), vb, 50.0, 1, int(sim.rng.random() < 0.5), spread,
                         "", 0, "", 0, vb))
        sim.t += sim.dt
    sim.cell.soc = 0.5
    sim.plug()
    sim.chg.t_on = sim.t - 1
    play(sim, 10 * 60)
    return "ok"


def simulate(profile, seed=1, **kw):
    sim = Sim(seed, **kw)
    res = {"steady": prof_steady, "pulsed": prof_pulsed, "plug": prof_plug,
           "cycle": prof_cycle, "nobatt": prof_nobatt}[profile](sim)
    return sim, res


def truth_meta(sim, log_start_soc=None, log_end_soc=None):
    c = sim.cell
    meta = {"cap_mah": c.cap, "r0_mohm": c.r0, "r1_mohm": c.r1, "tau1_s": c.tau1,
            "r2_mohm": c.r2, "tau2_s": c.tau2, "ocv_shift_mv": c.shift, "s_cut": sim.s_cut,
            "icc_ma": sim.chg.icc,
            "true_ocv_usable_mv": [round(c.ocv(sim.s_cut + (1 - sim.s_cut) * k / 20), 1)
                                   for k in range(21)]}
    if log_start_soc is not None:
        meta["log_start_cell_soc"] = log_start_soc
        meta["log_end_cell_soc"] = log_end_soc
        meta["true_ocv_log_axis_mv"] = [
            round(c.ocv(log_end_soc + (log_start_soc - log_end_soc) * k / 20), 1) for k in range(21)]
    return meta


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("profile", choices=PROFILES)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("-o", "--out", required=True, help="replay CSV")
    ap.add_argument("--battlog", help="(pulsed) write 16-byte battlog records here")
    ap.add_argument("--battlog-csv", help="(pulsed) write the battlog as CSV too")
    ap.add_argument("--meta", help="write cell/truth JSON here")
    ap.add_argument("--cap", type=float, default=310.0, help="cell capacity, mAh")
    ap.add_argument("--r0", type=float, default=160.0, help="ohmic resistance, mOhm")
    ap.add_argument("--ocv-shift", type=float, default=0.0, help="shift the cell OCV curve, mV")
    ap.add_argument("--gain-err", type=float, default=0.0, help="residual ADC gain error (e.g. 0.002)")
    ap.add_argument("--offset", type=float, default=0.0, help="residual ADC offset, mV")
    ap.add_argument("--noise", type=float, default=5.0, help="per-sample noise, +- mV")
    ap.add_argument("--est-err", type=float, default=0.15, help="max relative load-estimate error")
    a = ap.parse_args(argv)
    cell_kw = {"cap_mah": a.cap, "r0": a.r0, "ocv_shift_mv": a.ocv_shift}
    sim, _ = simulate(a.profile, a.seed, cell_kw=cell_kw, gain_err=a.gain_err, offset_mv=a.offset,
                      noise=a.noise, est_err=a.est_err)
    sim.write_csv(a.out)
    if a.battlog and sim.blog:
        sim.write_battlog(a.battlog)
    if a.battlog_csv and sim.blog:
        with open(a.battlog_csv, "w", newline="") as f:
            f.write("t_s,vbat_mv,vcomp_mv,vdda_mv,temp_dC,load_state,flags,seq\n")
            for r in sim.blog:
                f.write(",".join(str(x) for x in r) + "\n")
    if a.meta:
        meta = truth_meta(sim)
        # log axis for the pulsed run: SoC at the first row and at the cut-off
        if a.profile == "pulsed":
            dis = [r for r in sim.rows if not r[3]]
            meta["log_start_cell_soc"] = dis[0][8] / 100.0
            meta["log_end_cell_soc"] = dis[-1][8] / 100.0
            c = sim.cell
            s0, s1 = meta["log_start_cell_soc"], meta["log_end_cell_soc"]
            meta["true_ocv_log_axis_mv"] = [round(c.ocv(s1 + (s0 - s1) * k / 20), 1) for k in range(21)]
        Path(a.meta).write_text(json.dumps(meta, indent=1))
    print(f"{a.profile}: {len(sim.rows)} rows, {sim.t / 3600:.2f} h, battlog {len(sim.blog)} records",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
