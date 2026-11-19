#!/usr/bin/env python3
"""Design the 4x polyphase interpolator of the noise-shaped PWM engine (Inc/ns_core.h `ns_fir`).

  py -3.14 tools/ns_fir.py           -> prints the C table + response summary

64-tap Kaiser (beta 5) windowed sinc, cutoff = 9.375 kHz (the input Nyquist) at the 75 kHz output rate, split into 4 phases of
16 taps (packed two int16 per word for SMLAD); every phase is normalised to unity DC gain (Q15 sum exactly 32768) so there is no 18.75 kHz DC ripple.
Coefficients are stored oldest-sample-first (the firmware window is ascending in time).
"""
import numpy as np

L, NTAP, BETA = 4, 64, 5.0


def design():
    m = np.arange(NTAP) - (NTAP - 1) / 2
    h = np.sinc(2 * 0.125 * m) * np.kaiser(NTAP, BETA)
    h *= L / h.sum()
    rows = []
    for p in range(L):
        c = np.array(h[p::L])
        c /= c.sum()
        q = np.round(c * 32768).astype(int)
        q[np.argmax(np.abs(q))] += 32768 - q.sum()
        rows.append(q[::-1])          # ascending time
    return np.array(rows)


def c_table(rows):
    lines = ["    {" + ", ".join(f"PK({int(r[2 * j])}, {int(r[2 * j + 1])})" for j in range(8)) + "}," for r in rows]
    return "static const uint32_t ns_fir[4][8] = {\n" + "\n".join(lines) + "\n};\n"


def response(rows, n=1 << 16):
    h = np.zeros(NTAP)
    for p in range(L):
        h[p::L] = rows[p][::-1] / 32768.0
    f = np.fft.rfftfreq(n, 1 / 75000.0)
    return f, np.abs(np.fft.rfft(h, n)) / L


if __name__ == "__main__":
    rows = design()
    print(c_table(rows))
    f, H = response(rows)
    for a in (1000, 4000, 6000, 8000, 9375, 10000, 11000, 13000, 18750):
        print(f"{a:6d} Hz {20 * np.log10(H[np.searchsorted(f, a)] + 1e-12):7.1f} dB")
