#!/usr/bin/env python3
"""Build a media container (PROTOCOL.md "Media container") for the hwtest `media.play` suite.

  py -3.14 tools/make_media.py [--start 00:01:30] [--dur 40] [--fps 15] [--out tools/build/bbb.bin] [--src file.mp4]

Source: Big Buck Bunny 320x180 (downloaded to tools/build/, git-ignored, never committed):
  (c) copyright 2008, Blender Foundation / www.bigbuckbunny.org, CC-BY 3.0.

Video: 160x128 letterboxed (aspect preserved, black bars), per frame 254-colour median-cut palette + Floyd-Steinberg
dither (indices 254/255 are reserved for the player overlay: black / white), palette as RGB565, pixels transposed to
the panel's native column-major order (px[x * 128 + y]).
Audio (container v2): mono 16-bit signed LE at EXACTLY the rate engine B produces with the 10-bit carrier (93.75 kHz,
RCR+1 = 5 -> 18750 Hz = 15 fps x 1250 samples), so the firmware plays it 1:1 (no interpolation, no clock drift):
4th-order high-pass 380 Hz (the speaker is useless below ~500 Hz, bass only eats headroom), optional presence peak,
RMS compressor (dialogue audible on a small speaker), look-ahead smooth limiter (never hard-clips) to a digital peak of
-9.3 dBFS (the firmware output guard is -9 dBFS), 16 bit with TPDF dither. audio_gain_cdb = 0. The firmware dithers
16 -> 10 bit (carrier resolution) at play time. The result is trimmed to fit the 13 MiB media region
(0x200000-0xEFFFFF) and <= 2.5 MB/s.
"""
from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
import sys
import urllib.request
import zipfile
import zlib
from pathlib import Path

import numpy as np
from PIL import Image

URL = "https://download.blender.org/peach/bigbuckbunny_movies/BigBuckBunny_320x180.mp4.zip"   # the .mp4 itself is 404
FFMPEG_FALLBACK = (r"C:\Users\Bool\AppData\Local\Microsoft\WinGet\Packages\Gyan.FFmpeg_Microsoft.Winget.Source_"
                   r"8wekyb3d8bbwe\ffmpeg-8.0.1-full_build\bin\ffmpeg.exe")
W, H = 160, 128
PAL_BYTES, PIX_BYTES = 512, W * H
REGION = 0xF00000 - 0x200000          # 13 MiB
MAGIC = 0x564D4253                    # 'SBMV'
VERSION = 2
CARRIER_HZ = 93750                    # TIM1 10-bit carrier (96 MHz / 1024); engine B rate = CARRIER_HZ / (RCR + 1)
PEAK_DBFS = -9.3                      # digital ceiling of the track (firmware guard is -9 dBFS)
ATTRIB = ("Big Buck Bunny (c) copyright 2008, Blender Foundation / www.bigbuckbunny.org, "
          "licensed CC-BY 3.0 (https://creativecommons.org/licenses/by/3.0/)")


def find_ffmpeg() -> str:
    return shutil.which("ffmpeg") or (FFMPEG_FALLBACK if Path(FFMPEG_FALLBACK).exists() else "ffmpeg")


def download(dst: Path) -> None:
    if dst.exists() and dst.stat().st_size > 1_000_000:
        return
    dst.parent.mkdir(parents=True, exist_ok=True)
    print(f"downloading {URL}")
    tmp = dst.with_suffix(".zip.part")

    def hook(n, bs, total):
        if total > 0 and n % 200 == 0:
            print(f"\r  {n * bs / 1e6:6.1f} / {total / 1e6:.1f} MB", end="", flush=True)

    urllib.request.urlretrieve(URL, tmp, hook)
    print()
    with zipfile.ZipFile(tmp) as z:                  # extract exactly one member to a fixed path (no member paths used)
        member = next(i for i in z.infolist() if i.filename.lower().endswith(".mp4"))
        with z.open(member) as fi, open(dst, "wb") as fo:
            shutil.copyfileobj(fi, fo)
    tmp.unlink()


def rgb565(rgb: np.ndarray) -> np.ndarray:
    r, g, b = (rgb[:, 0].astype(np.uint16), rgb[:, 1].astype(np.uint16), rgb[:, 2].astype(np.uint16))
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def quant_frame(img: Image.Image) -> tuple[bytes, bytes]:
    """-> (palette 512 B RGB565 LE, pixels 20480 B column-major)"""
    q0 = img.quantize(colors=254, method=Image.Quantize.MEDIANCUT)
    flat = list(q0.getpalette() or [])[:254 * 3]
    flat += [0] * (254 * 3 - len(flat))
    pal = flat + flat[:3] * 2                       # entries 254/255 duplicate entry 0 so they are never picked
    pimg = Image.new("P", (1, 1))
    pimg.putpalette(pal)
    q = img.quantize(palette=pimg, dither=Image.Dither.FLOYDSTEINBERG)
    idx = np.asarray(q, dtype=np.uint8).copy()      # (H, W)
    idx[idx >= 254] = 0
    rgb = np.array(flat, dtype=np.uint8).reshape(254, 3)
    p565 = np.zeros(256, dtype="<u2")
    p565[:254] = rgb565(rgb)
    p565[254] = 0x0000
    p565[255] = 0xFFFF
    return p565.tobytes(), np.ascontiguousarray(idx.T).tobytes()   # idx.T -> [x][y]


def process_audio(x: np.ndarray, fs: int, ratio: float, crest_db: float) -> tuple[np.ndarray, str]:
    """RMS compressor (soft knee) + look-ahead smooth limiter -> peak PEAK_DBFS."""
    ceil = 10 ** (PEAK_DBFS / 20)
    w = int(0.03 * fs)                               # ~30 ms RMS detector
    lev = 10 * np.log10(np.convolve(x * x, np.ones(w) / w, mode="same") + 1e-12)
    thr = float(np.percentile(lev[lev > -70], 95)) - 12.0   # 12 dB below the loud-segment level
    over = np.maximum(lev - thr, 0.0)
    knee = 6.0
    k = 1.0 - 1.0 / ratio
    red = np.where(over < knee, k * over * over / (2 * knee), k * (over - knee / 2))
    tgt = -red
    ka, kr = np.exp(-1 / (0.008 * fs)), np.exp(-1 / (0.25 * fs))   # attack 8 ms / release 250 ms
    g = np.empty_like(tgt)
    cur = 0.0
    for i, t in enumerate(tgt):
        c = ka if t < cur else kr
        cur = c * cur + (1 - c) * t
        g[i] = cur
    y = x * 10 ** (g / 20)
    y *= ceil / (np.sqrt(np.mean(y * y)) * 10 ** (crest_db / 20))     # drive: limiter trims peaks above the crest
    lw = int(0.005 * fs) | 1                                          # 5 ms look-ahead
    graw = np.minimum(1.0, ceil / np.maximum(np.abs(y), 1e-9))
    pad = np.pad(graw, (lw // 2, lw // 2), mode="edge")
    gmin = np.lib.stride_tricks.sliding_window_view(pad, lw).min(axis=1)
    gs = np.convolve(np.pad(gmin, (lw // 2, lw // 2), mode="edge"), np.ones(lw) / lw, mode="valid")
    y *= gs                                                           # gs <= graw everywhere: never clips
    assert np.max(np.abs(y)) <= ceil * 1.0001
    rms = float(np.sqrt(np.mean(y * y)))
    pk = float(np.max(np.abs(y)))
    info = (f"compressor thr {thr:.1f} dB ratio {ratio:g}; limiter gain p1 {20 * np.log10(np.percentile(gs, 1)):.1f} dB; "
            f"peak {20 * np.log10(pk):.1f} dBFS, rms {20 * np.log10(rms):.1f} dBFS (crest {20 * np.log10(pk / rms):.1f} dB)")
    return y, info


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", help="source video (default: download Big Buck Bunny 320x180 into tools/build/)")
    ap.add_argument("--start", default="00:01:30", help="segment start (default 00:01:30)")
    ap.add_argument("--dur", type=float, default=40.0, help="segment length in seconds (default 40; trimmed to fit)")
    ap.add_argument("--fps", type=int, default=15, help="frame rate (default 15; lowered if > 2.5 MB/s)")
    ap.add_argument("--audio-rate", type=int, default=18750,
                    help="audio rate, must be CARRIER_HZ/n and fps * integer (default 18750: 10-bit carrier, n=5)")
    ap.add_argument("--hp", type=float, default=380.0, help="high-pass corner Hz, 4th order (default 380)")
    ap.add_argument("--presence", type=float, default=0.0,
                    help="gentle peaking boost in dB at 2 kHz (default 0: the speaker already peaks at 1.2-3.2 kHz)")
    ap.add_argument("--comp-ratio", type=float, default=3.0, help="compressor ratio (default 3)")
    ap.add_argument("--target-crest", type=float, default=10.0, help="crest factor after limiting, dB (default 10)")
    ap.add_argument("--out", default=str(Path(__file__).parent / "build" / "bbb.bin"))
    args = ap.parse_args()

    ffmpeg = find_ffmpeg()
    build = Path(__file__).parent / "build"
    src = Path(args.src) if args.src else build / "BigBuckBunny_320x180.mp4"
    if not args.src:
        download(src)

    fps = args.fps
    apf = round(args.audio_rate / fps)
    stride = PAL_BYTES + PIX_BYTES + 2 * apf
    while fps * stride > 2_500_000 and fps > 1:
        fps -= 1
        apf = round(args.audio_rate / fps)
        stride = PAL_BYTES + PIX_BYTES + 2 * apf
    arate = apf * fps
    n_div = CARRIER_HZ / arate
    if abs(n_div - round(n_div)) > 1e-9 or arate != args.audio_rate:
        sys.exit(f"audio rate {arate} Hz (= {fps} fps x {apf}) is not {CARRIER_HZ}/n: choose --fps/--audio-rate so that "
                 f"fps * round(rate/fps) == {CARRIER_HZ}/n exactly (e.g. 15 fps / 18750 Hz)")
    frames = int(args.dur * fps)
    max_frames = (REGION - 4096) // stride
    if frames > max_frames:
        print(f"trimming {frames} -> {max_frames} frames to fit the {REGION >> 20} MiB region")
        frames = max_frames
    dur = frames / fps
    print(f"{frames} frames @ {fps} fps = {dur:.2f} s, stride {stride} B, audio {arate} Hz 16 bit ({apf} samples/frame, "
          f"engine RCR+1 = {round(n_div)}), {fps * stride / 1e3:.0f} kB/s, file {(4096 + frames * stride) / 1e6:.2f} MB")

    # ---- audio
    af = [f"highpass=f={args.hp}:poles=2", f"highpass=f={args.hp}:poles=2"]
    if args.presence:
        af.append(f"equalizer=f=2000:t=q:w=0.7:g={args.presence}")
    af += ["lowpass=f=8800:poles=2", f"aresample={arate}:resampler=soxr:precision=28"]
    cmd = [ffmpeg, "-v", "error", "-ss", args.start, "-t", f"{dur:.3f}", "-i", str(src), "-vn", "-ac", "1",
           "-af", ",".join(af), "-f", "f32le", "-"]
    raw = subprocess.run(cmd, capture_output=True, check=True).stdout
    x = np.frombuffer(raw, dtype="<f4").astype(np.float64)
    need = frames * apf
    x = np.pad(x, (0, max(0, need - len(x))))[:need]
    x, info = process_audio(x, arate, args.comp_ratio, args.target_crest)
    rng = np.random.default_rng(1)
    tpdf = rng.random(len(x)) - rng.random(len(x))          # +-1 LSB triangular dither at 16 bit
    a16 = np.clip(np.rint(x * 32767.0 + tpdf), -32767, 32767).astype("<i2")
    print(f"audio: {len(a16)} samples @ {arate} Hz; {info}")

    # ---- video
    vf = (f"fps={fps},scale={W}:{H}:force_original_aspect_ratio=decrease:flags=lanczos,"
          f"pad={W}:{H}:(ow-iw)/2:(oh-ih)/2:black,format=rgb24")
    cmd = [ffmpeg, "-v", "error", "-ss", args.start, "-t", f"{dur + 0.5:.3f}", "-i", str(src), "-an", "-vf", vf,
           "-frames:v", str(frames), "-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    fsz = W * H * 3
    body = bytearray()
    n = 0
    while n < frames:
        buf = p.stdout.read(fsz)
        if len(buf) < fsz:
            break
        pal, pix = quant_frame(Image.frombytes("RGB", (W, H), buf))
        body += pal + pix + a16[n * apf:(n + 1) * apf].tobytes()
        n += 1
        if n % 60 == 0:
            print(f"\r  frame {n}/{frames}", end="", flush=True)
    p.stdout.close()
    p.wait()
    print(f"\r  frame {n}/{frames}")
    if n < frames:
        print(f"warning: source ended after {n} frames; keeping {n}")
        frames = n

    # ---- container
    data_crc = zlib.crc32(bytes(body)) & 0xFFFFFFFF
    total = 4096 + len(body)
    assert total == 4096 + frames * stride
    hdr = struct.pack("<15I", MAGIC, VERSION, W, H, fps, frames, arate, 16, stride, apf, 4096, total, data_crc, 0, 0)
    hdr += struct.pack("<I", zlib.crc32(hdr) & 0xFFFFFFFF)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(hdr + b"\xff" * (4096 - len(hdr)) + bytes(body))
    out.with_suffix(".README.txt").write_text(
        f"{out.name}: media container v{VERSION} for the stm32-hwtest `media.play` suite (not committed).\n"
        f"Content: {ATTRIB}\n"
        f"Segment: start {args.start}, {frames / fps:.1f} s, {fps} fps, 160x128 letterboxed, mono {arate} Hz 16-bit.\n"
        f"Upload: py -3.14 tools/hwtest.py media upload {out}\nPlay:   run media.play\n", encoding="utf-8")
    print(f"wrote {out} ({total} B, data crc32 {data_crc:#010x})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
