"""Frequency response from a phyphox "Audio Amplitude" recording of `audio.chirp`.

audio.chirp plays 3 exponential sweeps 20 Hz -> 15 kHz, 10 s each, 1 s gaps (period 11 s):
f(tau) = 20 * 750 ** (tau / 10). We fit the start time, map every sample to its frequency,
take the median per 1/6 octave over the 3 passes and plot it together with the stepped-sine
result (fr.csv from phyphox_sweep.py) if present in OUTDIR.

usage: py -3.14 tools/phyphox_chirp.py Amplitudes.csv OUTDIR
"""
import csv
import math
import statistics as st
import sys
from pathlib import Path


def main():
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    t, v = [], []
    for row in list(csv.reader(open(src, encoding="utf-8")))[1:]:
        try:
            x, y = float(row[0]), float(row[1])
        except (ValueError, IndexError):
            continue
        if y == y:
            t.append(x)
            v.append(y)
    bg = st.median(v[:30])

    def lvl(a, b):
        xs = [y for x, y in zip(t, v) if a <= x <= b]
        return st.median(xs) if xs else bg

    best = None
    for i in range(0, int((t[-1] - 33) * 20)):
        t0 = i / 20
        s = sum(lvl(t0 + k * 11 + 5, t0 + k * 11 + 9.8) - lvl(t0 + k * 11 + 10.15, t0 + k * 11 + 10.85)
                for k in range(3))
        if best is None or s > best[0]:
            best = (s, t0)
    t0 = best[1]
    pts = []
    for x, y in zip(t, v):
        for k in range(3):
            tau = x - (t0 + k * 11)
            if 0.05 <= tau <= 9.95:
                pts.append((20 * 750 ** (tau / 10), y))
    edges = [20 * 2 ** (i / 6) for i in range(0, 60)]
    rows = []
    for lo, hi in zip(edges, edges[1:]):
        ys = [y for f, y in pts if lo <= f < hi]
        if ys:
            rows.append((math.sqrt(lo * hi), st.median(ys), len(ys)))
    ref = st.median([y for f, y, n in rows if 1000 <= f <= 8000])
    with open(out / "chirp_fr.csv", "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["freq_hz", "level_db", "rel_mid_db", "above_bg_db", "n"])
        for f, y, n in rows:
            w.writerow([round(f), round(y, 1), round(y - ref, 1), round(y - bg, 1), n])
    print(f"background {bg:.1f} dB, t0 {t0:.2f} s")
    for f, y, n in rows:
        print(f"{f:7.0f} Hz {y - ref:+6.1f}{'' if y - bg >= 6 else '  (in noise)'}")
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        fig, ax = plt.subplots(figsize=(10, 5))
        ax.semilogx([f for f, y, n in rows], [y - ref for f, y, n in rows], "-",
                    label="chirp x3 (1/6-octave median)")
        sw = out / "fr.csv"
        if sw.exists():
            d = list(csv.DictReader(open(sw, encoding="utf-8")))
            ax.semilogx([float(r["freq_hz"]) for r in d], [float(r["rel_mid_db"]) for r in d], "o",
                        label="stepped sine")
        ax.axhline(bg - ref, color="gray", ls="--", lw=0.8, label="background (fan)")
        ax.set_xlabel("frequency, Hz")
        ax.set_ylabel("dB rel. to 1-8 kHz median")
        ax.set_title("STM-boy speaker response (iPhone mic, 10 cm, -12 dBFS)")
        ax.grid(True, which="both", alpha=0.3)
        ax.legend()
        fig.tight_layout()
        fig.savefig(out / "fr_combined.png", dpi=110)
    except ImportError:
        pass


if __name__ == "__main__":
    main()
