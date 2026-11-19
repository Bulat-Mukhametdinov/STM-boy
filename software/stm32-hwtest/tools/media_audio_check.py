#!/usr/bin/env python3
"""Decode the audio track of a media container, print statistics, write the stored WAV and a board-output simulation.

  py -3.14 tools/media_audio_check.py [tools/build/bbb.bin]  -> tools/build/bbb_audio_as_stored.wav, bbb_audio_board_sim.wav
The simulation mirrors Src/drv/audio.c (PCM voice, linear interpolation to the engine rate, gain, clip guard, quantiser
with TPDF dither at the carrier resolution); the carrier/Fs are the real engine values for the container version.
"""
import struct, sys, wave
from pathlib import Path
import numpy as np

PAL, PIX = 512, 20480


def load(path):
    d = Path(path).read_bytes()
    h = struct.unpack("<16I", d[:64])
    ver, fps, frames, rate, bits, stride, apf, off = h[1], h[4], h[5], h[6], h[7], h[8], h[9], h[10]
    gain_cdb = struct.unpack("<i", struct.pack("<I", h[13]))[0]
    chunks = [d[off + i * stride + PAL + PIX: off + (i + 1) * stride] for i in range(frames)]
    raw = b"".join(chunks)
    if bits == 8:
        x = (np.frombuffer(raw, np.uint8).astype(np.int32) - 128) << 8
    else:
        x = np.frombuffer(raw, "<i2").astype(np.int32)
    return dict(ver=ver, fps=fps, frames=frames, rate=rate, bits=bits, apf=apf, gain_db=gain_cdb / 100.0, x=x)


def wav_write(p, x, rate):
    x = np.clip(np.rint(x), -32768, 32767).astype("<i2")
    with wave.open(str(p), "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(int(round(rate))); w.writeframes(x.tobytes())


def stats(x, rate, label):
    f = x / 32768.0
    pk = np.max(np.abs(f)); rms = np.sqrt(np.mean(f ** 2))
    S = np.abs(np.fft.rfft(f * np.hanning(len(f)))) ** 2; fr = np.fft.rfftfreq(len(f), 1 / rate)
    tot = S.sum()
    print(f"[{label}] n={len(x)} rate={rate:.2f} peak={20*np.log10(pk):.1f} dBFS rms={20*np.log10(rms):.1f} dBFS crest={20*np.log10(pk/rms):.1f} dB "
          f"DC={np.mean(f)*32768:.1f} LSB  distinct levels={len(np.unique(x))}")
    print(f"   energy <400 Hz {100*S[fr<400].sum()/tot:.2f}% <800 Hz {100*S[fr<800].sum()/tot:.2f}%  >10 kHz {100*S[fr>10000].sum()/tot:.2f}%")
    return pk


def board_sim(x, rate_in, bits_in, gain_db, fs_eng, car_bits, max_db=-9.0, master_db=0.0, seed=2, mode="tpdf"):
    """float sim of render_sample(): linear interp to fs_eng, gain, clip guard, quantiser (units: 16-bit full scale)."""
    n_out = int(len(x) * fs_eng / rate_in)
    pos = np.arange(n_out) * (rate_in / fs_eng)
    i0 = np.floor(pos).astype(int); fr = pos - i0
    i1 = np.minimum(i0 + 1, len(x) - 1)
    s = x[i0] + (x[i1] - x[i0]) * fr
    s = s * 10 ** (gain_db / 20) * 10 ** (master_db / 20)
    lim = 32767 * 10 ** (max_db / 20)
    clipped = np.mean(np.abs(s) > lim)
    s = np.clip(s, -lim, lim)
    lsb = 2 ** (16 - car_bits)
    rng = np.random.default_rng(seed)
    if mode == "off":
        d = np.zeros(len(s))
    else:
        d = rng.uniform(-lsb / 2, lsb / 2, len(s)) + rng.uniform(-lsb / 2, lsb / 2, len(s))
    if mode == "shaped":
        out = np.empty(len(s)); err = 0.0
        for i in range(len(s)):
            t = s[i] - err + d[i]
            k = np.floor((t + lsb / 2) / lsb) * lsb
            err = k - t; out[i] = k
        y = out
    else:
        y = np.floor((s + d + lsb / 2) / lsb) * lsb
    return y, clipped


if __name__ == "__main__":
    p = sys.argv[1] if len(sys.argv) > 1 else str(Path(__file__).parent / "build" / "bbb.bin")
    c = load(p)
    print(f"container v{c['ver']}: {c['rate']} Hz, {c['bits']} bit, {c['frames']} frames @ {c['fps']} fps, gain {c['gain_db']} dB")
    x = c["x"]
    stats(x, c["rate"], "stored")
    ceil = np.max(np.abs(x))
    print(f"   at peak ceiling (|x|>=0.99*max): {100*np.mean(np.abs(x) >= 0.99*ceil):.3f}% ; >=0.9*max {100*np.mean(np.abs(x) >= 0.9*ceil):.2f}%")
    out = Path(p).parent
    wav_write(out / "bbb_audio_as_stored.wav", x, c["rate"])
    fs_eng = 31250.0 if c["ver"] == 1 else float(c["rate"])   # v1 played at 31.25 kHz / 9 bit, v2 1:1 at 18750 Hz / 10 bit
    car_bits = 9 if c["ver"] == 1 else 10
    y, clipped = board_sim(x.astype(float), c["rate"], c["bits"], c["gain_db"] if c["ver"] == 1 else min(c["gain_db"], 0.0), fs_eng, car_bits,
                           mode="tpdf")
    print(f"board sim: Fs {fs_eng}, {car_bits}-bit carrier, clipped {100*clipped:.3f}%")
    stats(y, fs_eng, "board")
    # SNR of the simulated output against the ideal (unquantised, un-interpolated) signal
    ideal, _ = board_sim(x.astype(float), c["rate"], c["bits"], c["gain_db"] if c["ver"] == 1 else min(c["gain_db"], 0.0), fs_eng, 16, mode="off")
    e = y - ideal
    print(f"   quantisation+dither error rms {20*np.log10(np.std(e)/32768):.1f} dBFS; signal rms {20*np.log10(np.std(ideal)/32768):.1f} dBFS; SNR {20*np.log10(np.std(ideal)/np.std(e)):.1f} dB")
    wav_write(out / "bbb_audio_board_sim.wav", y, fs_eng)
    print("wrote", out / "bbb_audio_as_stored.wav", "and", out / "bbb_audio_board_sim.wav")
