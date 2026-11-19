#!/usr/bin/env python3
"""Host model of the media.play audio path: PCM -> PWM codes -> board RC -> speaker band.

  py -3.14 tools/ns_sim.py [--wav tools/build/bbb_audio_as_stored.wav] [--start 4] [--secs 24] [--hp 1600] [--wavs] [--selftest]

Needs tools/build/ns_host (gcc -O2 -std=c11 -DNS_HOST -IInc tools/ns_host.c -o tools/build/ns_host -lm): the firmware's
Inc/ns_core.h compiled unchanged = bit-exact interpolator + shaper + quantiser. --selftest cross-checks it against an
independent integer Python model on 0.3 s. The old engine-B path (hold at the PCM rate, TPDF 16 -> N bit) is modelled
in numpy (same distribution, numpy RNG).

Analog model: every carrier period is replaced by its average (ideal sampled PWM; the PWM edge-position distortion and the
carrier ripple itself are NOT modelled), 1st-order RC fc = 22 kHz (3.3k / 2.2 nF), speaker band 800 Hz..10 kHz (brick wall).
Noise = (codes - pre-quantiser signal): pure quantisation / dither noise, identical interpolation for the reference.
"total" = (codes - ideal band-limited upsampled PCM): adds hold/FIR droop and images.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
HOST = ROOT / "tools" / "build" / ("ns_host.exe" if sys.platform == "win32" else "ns_host")
FS_PCM = 18750
FC_RC = 22000.0
BAND = (800.0, 10000.0)
FS_FULL = 32768.0


def load_wav(path):
    with wave.open(str(path)) as w:
        assert w.getframerate() == FS_PCM and w.getsampwidth() == 2 and w.getnchannels() == 1
        return np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64)


def write_wav(path, x, rate=48000):
    x = np.clip(np.round(x), -32768, 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(x.tobytes())


def hp4(x, fc, fs=FS_PCM):
    """media.play runtime 4th-order high-pass (two RBJ Butterworth sections), output truncated to int16 like the firmware."""
    y = x.copy()
    for q in (0.5412, 1.3066):
        w = 2 * np.pi * fc / fs
        cw, al = np.cos(w), np.sin(w) / (2 * q)
        a0 = 1 + al
        b0 = (1 + cw) / 2 / a0
        b1 = -(1 + cw) / a0
        a1 = -2 * cw / a0
        a2 = (1 - al) / a0
        z1 = z2 = 0.0
        out = np.empty_like(y)
        for i, v in enumerate(y):
            o = b0 * v + z1
            z1 = b1 * v - a1 * o + z2
            z2 = b0 * v - a2 * o
            out[i] = o
        y = out
    return np.trunc(np.clip(y, -32768, 32767))


# ------------------------------------------------------------------ the three signal paths

def run_zoh(x, bits, R, seed=1):
    """Engine B as shipped: PCM at 18.75 kHz, TPDF (+-1 LSB) 16 -> bits, held for R carrier periods. Returns codes, ideal(Q0)."""
    rng = np.random.default_rng(seed)
    sh = 16 - bits
    lsb = 1 << sh
    # voice interpolator at 1:1: s = prev + ((cur - prev) * 65535 >> 16)
    xi = x.astype(np.int64)
    prev = np.concatenate(([0], xi[:-1]))
    s = prev + (((xi - prev) * 65535) >> 16)
    lim = int(32767.0 * 10 ** (-9 / 20))
    s = np.clip(s, -lim, lim)
    d = rng.integers(0, lsb, len(s)) + rng.integers(0, lsb, len(s)) - (lsb - 1)
    k = np.clip((s + d + (lsb >> 1)) >> sh, -(1 << (bits - 1)), (1 << (bits - 1)) - 1)
    codes = np.repeat(k + (1 << (bits - 1)), R)
    ideal = np.repeat(s.astype(np.float64), R)
    return codes.astype(np.int64), ideal


def run_ns(x, bits, order, damp8, tmp=ROOT / "tools" / "build"):
    """Firmware ns_render() on the host: returns codes, pre-quantiser signal (16-bit scale)."""
    xi = np.asarray(x, dtype="<i2")
    fin, fout, ftr = tmp / "_ns_in.s16", tmp / "_ns_out.u16", tmp / "_ns_tr.i32"
    xi.tofile(fin)
    subprocess.run([str(HOST), str(bits), str(order), str(damp8), str(fin), str(fout), str(ftr)], check=True,
                   stderr=subprocess.DEVNULL)
    codes = np.fromfile(fout, dtype="<u2").astype(np.int64)
    tr = np.fromfile(ftr, dtype="<i4").astype(np.float64) / 256.0
    for f in (fin, fout, ftr):
        f.unlink()
    return codes, tr


# ------------------------------------------------------------------ independent integer model (selftest)

def py_ns(x, bits, order, damp8, seed=0xC0FFEE11):
    import re
    txt = (ROOT / "Inc" / "ns_core.h").read_text().split("ns_fir[4][8] = {")[1].split("};")[0]
    flat = [int(v) for v in re.findall(r"-?\d+", txt)]
    fir = [flat[16 * p: 16 * p + 16] for p in range(4)]
    lsh = 8 + 16 - bits
    half = 1 << (bits - 1)
    m = 10 if bits == 7 else 5
    n1, n2 = (-2, 1) if order >= 2 else ((-1, 0) if order == 1 else (0, 0))
    dmul = damp8 << (lsh - 16)
    lim = int(np.float32(32767.0) * np.float32(10.0) ** np.float32(-9 * 0.05))
    hist = [0] * 16
    prev = 0
    e1 = e2 = 0
    rng = seed
    out, tr = [], []
    for xv in x:
        hist = hist[1:] + [int(xv)]
        # window for the sample being rendered: 16 samples ending with the newest (ascending time)
        for p in range(4):
            acc = sum(c * v for c, v in zip(fir[p], hist))
            y = acc >> 15
            y = max(-lim, min(lim, y))
            step = -((-(y - prev) * 256) // m) if (y - prev) < 0 else ((y - prev) * 256) // m
            cur = prev << 8
            prev = y
            for _ in range(m):
                cur += step
                tr.append(cur)
                rng ^= (rng << 13) & 0xFFFFFFFF
                rng ^= rng >> 17
                rng ^= (rng << 5) & 0xFFFFFFFF
                d = ((((rng & 0xFFFF) + (rng >> 16) - 65535) * dmul) >> 3)
                v = cur + n1 * e1 + n2 * e2
                k = (v + d + (1 << (lsh - 1))) >> lsh
                if k >= half:
                    k, e1, e2 = half - 1, 0, 0
                elif k < -half:
                    k, e1, e2 = -half, 0, 0
                else:
                    e2, e1 = e1, (k << lsh) - v
                out.append(k + half)
    return np.array(out), np.array(tr, dtype=np.float64) / 256.0


# ------------------------------------------------------------------ analysis

def _one_sided(S, mask, N, w):
    return 2.0 * float(np.sum((np.abs(S[mask]) ** 2) * w[mask])) / (N * N)


def analyse(codes, ideal, bits, R, x_ref):
    """Band powers after the RC: signal, quantisation noise, total error vs the ideal upsampled PCM, HF residue."""
    N = len(codes)
    fs = FS_PCM * R
    half = 1 << (bits - 1)
    y = (codes - half).astype(np.float64) * (1 << (16 - bits))
    f = np.fft.rfftfreq(N, 1.0 / fs)
    w = 1.0 / (1.0 + (f / FC_RC) ** 2)       # |H_RC|^2
    band = (f >= BAND[0]) & (f <= BAND[1])
    hf = f >= 30000.0
    Y = np.fft.rfft(y)
    Q = np.fft.rfft(y - ideal)
    n18 = len(x_ref)
    X = np.zeros(N // 2 + 1, dtype=complex)
    X[: n18 // 2 + 1] = np.fft.rfft(x_ref) * R
    p_sig = _one_sided(np.fft.rfft(ideal), band, N, w)
    p_q = _one_sided(Q, band, N, w)
    # total error vs the ideal reference: align the pure delay of the path (FIR / hold) first, fit on the in-band bins
    fb, Yb, Xb, wb = f[band], Y[band], X[band], w[band]

    def terr(tau):
        return float(np.sum(np.abs(Yb - Xb * np.exp(-2j * np.pi * fb * tau)) ** 2 * wb))

    grid = np.arange(-2e-3, 4e-3, 4e-6)
    t0 = grid[int(np.argmin([terr(t) for t in grid]))]
    fine = np.arange(t0 - 4e-6, t0 + 4e-6, 0.1e-6)
    t1 = fine[int(np.argmin([terr(t) for t in fine]))]
    p_tot = 2.0 * terr(t1) / (N * N)
    p_hf = _one_sided(Q, hf, N, w)
    db = lambda p: 10 * np.log10(p / FS_FULL ** 2 + 1e-30)
    return dict(sig=db(p_sig), noise=db(p_q), snr=db(p_sig) - db(p_q), total=db(p_tot), delay_us=t1 * 1e6, snr_tot=db(p_sig) - db(p_tot),
                hf=db(p_hf))


def render48(codes, bits, R, band_limit):
    """Codes -> RC (exact analog response in the frequency domain) -> optional speaker band -> 48 kHz (brick-wall decimation)."""
    N = len(codes)
    fs = FS_PCM * R
    n48 = N * 48000 // fs
    assert n48 * fs == N * 48000
    y = (codes - (1 << (bits - 1))).astype(np.float64) * (1 << (16 - bits))
    Y = np.fft.rfft(y)[: n48 // 2 + 1]
    f = np.fft.rfftfreq(n48, 1.0 / 48000)
    Y = Y / (1 + 1j * f / FC_RC)
    Y[0] = 0
    if band_limit:
        Y[(f < BAND[0]) | (f > BAND[1])] = 0
    return np.fft.irfft(Y, n48) * (n48 / N)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wav", default=str(ROOT / "tools/build/bbb_audio_as_stored.wav"))
    ap.add_argument("--start", type=float, default=4.0)
    ap.add_argument("--secs", type=float, default=24.0)
    ap.add_argument("--hp", type=float, default=1600.0)
    ap.add_argument("--wavs", action="store_true", help="write the A/B WAVs to tools/build/")
    ap.add_argument("--ns", default="d", help="chosen NS case for the WAVs (c,d,e)")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--damp", type=int, default=4)
    ap.add_argument("--extra", action="store_true", help="dither amplitude / idle-tone variants")
    a = ap.parse_args()

    x_all = load_wav(a.wav)
    if a.hp > 0:
        x_all = hp4(x_all, a.hp)
    i0 = int(a.start * FS_PCM)
    x = x_all[i0: i0 + int(a.secs * FS_PCM)]
    print(f"segment {a.start:.1f}..{a.start + len(x) / FS_PCM:.1f} s, rms {20 * np.log10(np.sqrt(np.mean(x ** 2)) / FS_FULL):.1f} dBFS, "
          f"peak {20 * np.log10(np.abs(x).max() / FS_FULL):.1f} dBFS")

    if a.selftest:
        for bits, order, damp in ((8, 2, 4), (8, 1, 8), (7, 2, 4), (8, 0, 0)):
            xs = x[: int(0.3 * FS_PCM)]
            c1, t1 = run_ns(xs, bits, order, damp)
            c2, t2 = py_ns(xs.astype(np.int64), bits, order, damp)
            print(f"selftest bits {bits} order {order} damp {damp}: codes equal {np.array_equal(c1, c2)}, "
                  f"trace equal {np.array_equal(t1, t2)} ({len(c1)} periods)")

    cases = {
        "a": ("a  187.5k/9b TPDF (hold)", "zoh", 9, 10, 0, 0),
        "b": ("b  375k/8b TPDF (hold)", "zoh", 8, 20, 0, 0),
        "c": ("c  NS 375k/8b 1st", "ns", 8, 20, 1, a.damp),
        "d": ("d  NS 375k/8b 2nd", "ns", 8, 20, 2, a.damp),
        "e": ("e  NS 750k/7b 2nd", "ns", 7, 40, 2, a.damp),
    }
    if a.extra:
        cases.update({
            "d0": ("d' NS 375k/8b 2nd damp 0", "ns", 8, 20, 2, 0),
            "d8": ("d' NS 375k/8b 2nd damp 8 (+-1 LSB)", "ns", 8, 20, 2, 8),
            "e0": ("e' NS 750k/7b 2nd damp 0", "ns", 7, 40, 2, 0),
            "e8": ("e' NS 750k/7b 2nd damp 8", "ns", 7, 40, 2, 8),
        })
    print(f"{'case':38s} {'sig':>7s} {'noise':>7s} {'SNR':>6s} {'total':>7s} {'SNRtot':>6s} {'HF>30k':>7s}   (dBFS rms, 800-10k after RC 22k)")
    res = {}
    for key, (name, kind, bits, R, order, damp) in cases.items():
        codes, ideal = run_zoh(x, bits, R) if kind == "zoh" else run_ns(x, bits, order, damp)
        r = analyse(codes, ideal, bits, R, x)
        res[key] = (codes, bits, R)
        print(f"{name:38s} {r['sig']:7.1f} {r['noise']:7.1f} {r['snr']:6.1f} {r['total']:7.1f} {r['snr_tot']:6.1f} {r['hf']:7.1f}")

    if a.extra:   # idle tones: DC offsets and a quiet tone
        print("idle / quiet-signal noise in 800-10k (dBFS) for NS 375k/8b 2nd, damp 0 / 4 / 8:")
        n = FS_PCM * 8
        t = np.arange(n) / FS_PCM
        for label, sig in (("DC +23 (0.09 LSB8)", np.full(n, 23.0)), ("DC +100", np.full(n, 100.0)),
                           ("1 kHz -50 dBFS", 32768 * 10 ** (-50 / 20) * np.sin(2 * np.pi * 1000 * t)),
                           ("silence", np.zeros(n))):
            row = []
            for dm in (0, 4, 8):
                codes, ideal = run_ns(np.round(sig), 8, 2, dm)
                row.append(analyse(codes, ideal, 8, 20, np.round(sig))["noise"])
            print(f"  {label:20s} " + "  ".join(f"{v:7.1f}" for v in row))

    if a.wavs:
        gain = 2.5      # same for every file: peak -9.3 dBFS -> -1.3 dBFS
        out = ROOT / "tools" / "build"
        key = a.ns
        for tag, k in (("a_187k9b_tpdf", "a"), (f"ns_{key}", key)):
            codes, bits, R = res[k]
            write_wav(out / f"ns_cmp_{tag}.wav", render48(codes, bits, R, False) * gain)
            write_wav(out / f"ns_cmp_{tag}_spk.wav", render48(codes, bits, R, True) * gain)
        # ideal reference (no quantisation), same RC-less brick wall
        n48 = len(x) * 48000 // FS_PCM
        X = np.fft.rfft(x)[: n48 // 2 + 1]
        X[0] = 0
        ref = np.fft.irfft(X, n48) * (n48 / len(x))
        write_wav(out / "ns_cmp_ref_ideal.wav", ref * gain)
        print("wrote", *sorted(p.name for p in out.glob("ns_cmp_*.wav")))


if __name__ == "__main__":
    main()
