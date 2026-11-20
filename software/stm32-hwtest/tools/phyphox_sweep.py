"""Turn a phyphox "Audio Amplitude" recording of `audio.sweep` into a frequency-response table.

The sweep plays 25 ISO 1/3-octave tones (50 Hz .. 12.5 kHz), 3 s each, separated by 0.62 s (0.5 s silence + 20 ms 1 kHz tick + 0.1 s)
(silence + 20 ms tick). phyphox exports ~10 samples/s of relative SPL (dB). We locate the step
grid (period ~3.5 s) by maximising the tone/gap contrast, then take the median level of the
middle of each tone window and compare it with the background (first seconds before the sweep).

usage: py -3.14 tools/phyphox_sweep.py Amplitudes.csv OUTDIR [--period 3.5]
"""
import csv
import statistics as st
import sys
from pathlib import Path

FREQS = [50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000,
         2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500]


def load(p):
    t, v = [], []
    for row in list(csv.reader(open(p, encoding="utf-8")))[1:]:
        try:
            a, b = float(row[0]), float(row[1])
        except (ValueError, IndexError):
            continue
        if b == b:  # skip nan
            t.append(a)
            v.append(b)
    return t, v


def med(t, v, a, b):
    xs = [y for x, y in zip(t, v) if a <= x <= b]
    return st.median(xs) if xs else float("nan")


def fit_grid(t, v, period):
    """Find t0 such that tone windows are loud and gaps quiet (only the audible tail counts)."""
    best = None
    t_end = t[-1]
    for i in range(0, int((t_end - 25 * period) * 20)):
        t0 = i / 20
        score = 0.0
        for k in range(10, 25):  # upper steps are well above the background
            s = t0 + k * period
            score += med(t, v, s + 0.7, s + 2.6) - med(t, v, s + 3.05, s + 3.45)
        if best is None or score > best[0]:
            best = (score, t0)
    return best[1]


def main():
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    period = float(sys.argv[sys.argv.index("--period") + 1]) if "--period" in sys.argv else 3.5  # measured 2026-11-19/20: ticks every 3.50 s (3.62 misaligned the grid by one step)
    out.mkdir(parents=True, exist_ok=True)
    t, v = load(src)
    t0 = fit_grid(t, v, period)
    bg = med(t, v, 0.5, max(1.0, t0 - 1.0))
    rows = []
    for k, f in enumerate(FREQS):
        s = t0 + k * period
        lvl = med(t, v, s + 0.7, s + 2.6)
        rows.append((f, lvl, lvl - bg))
    ref = st.median([r[1] for r in rows if 1000 <= r[0] <= 8000])
    with open(out / "fr.csv", "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["freq_hz", "level_db", "above_bg_db", "rel_mid_db", "valid"])
        for f, lvl, snr in rows:
            w.writerow([f, f"{lvl:.1f}", f"{snr:.1f}", f"{lvl - ref:.1f}", int(snr >= 6)])
    md = ["| Freq (Hz) | Level (dB rel.) | Above background (dB) | Rel. to 1-8 kHz median (dB) |",
          "| --- | --- | --- | --- |"]
    for f, lvl, snr in rows:
        rel = f"{lvl - ref:+.1f}" if snr >= 6 else f"< {lvl - ref:+.1f} (in noise)"
        md.append(f"| {f} | {lvl:.1f} | {snr:.1f} | {rel} |")
    (out / "fr.md").write_text(f"Background {bg:.1f} dB, grid t0 {t0:.2f} s, period {period} s\n\n"
                               + "\n".join(md) + "\n", encoding="utf-8")
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        fig, ax = plt.subplots(2, 1, figsize=(11, 8))
        ax[0].plot(t, v, lw=0.7)
        for k in range(25):
            ax[0].axvspan(t0 + k * period + 0.7, t0 + k * period + 2.6, color="tab:orange", alpha=0.15)
        ax[0].axhline(bg, color="gray", ls="--", lw=0.8)
        ax[0].set_xlabel("time, s")
        ax[0].set_ylabel("level, dB (rel.)")
        ax[0].set_title("phyphox recording with detected sweep steps")
        fs = [r[0] for r in rows]
        ax[1].semilogx(fs, [r[1] - ref for r in rows], "o-")
        ax[1].axhline(bg - ref, color="gray", ls="--", lw=0.8, label="background")
        ax[1].set_xlabel("frequency, Hz")
        ax[1].set_ylabel("dB rel. to 1-8 kHz median")
        ax[1].set_title("STM-boy speaker frequency response (phone mic, 10 cm, -12 dBFS)")
        ax[1].grid(True, which="both", alpha=0.3)
        ax[1].legend()
        fig.tight_layout()
        fig.savefig(out / "fr.png", dpi=110)
    except ImportError:
        pass
    print((out / "fr.md").read_text(encoding="utf-8"))


if __name__ == "__main__":
    main()
