#!/usr/bin/env python3
"""Build a media container (PROTOCOL.md "Media container") for the hwtest `media.play` suite.

  py -3.14 tools/make_media.py [--start 00:01:30] [--dur 40] [--fps 15] [--out tools/build/bbb.bin] [--src file.mp4]

Source: Big Buck Bunny 320x180 (downloaded to tools/build/, git-ignored, never committed):
  (c) copyright 2008, Blender Foundation / www.bigbuckbunny.org, CC-BY 3.0.

Video: 160x128 letterboxed (aspect preserved, black bars), per frame 254-colour median-cut palette + Floyd-Steinberg
dither (indices 254/255 are reserved for the player overlay: black / white), palette as RGB565, pixels transposed to
the panel's native column-major order (px[x * 128 + y]).
Audio: mono, high-pass 400 Hz, loudness-normalised, hard-limited, peak-normalised to full 8-bit scale with TPDF dither;
the container's audio_gain_cdb = -900 makes the firmware play it with the peak at -9 dBFS (speaker/amp safe).
The result is trimmed so that it fits the 13 MiB media region (0x200000-0xEFFFFF) and <= 2.5 MB/s.
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


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", help="source video (default: download Big Buck Bunny 320x180 into tools/build/)")
    ap.add_argument("--start", default="00:01:30", help="segment start (default 00:01:30)")
    ap.add_argument("--dur", type=float, default=40.0, help="segment length in seconds (default 40; trimmed to fit)")
    ap.add_argument("--fps", type=int, default=15, help="frame rate (default 15; lowered if > 2.5 MB/s)")
    ap.add_argument("--audio-rate", type=int, default=16000, help="nominal audio rate (the exact rate = fps * bytes/frame)")
    ap.add_argument("--out", default=str(Path(__file__).parent / "build" / "bbb.bin"))
    args = ap.parse_args()

    ffmpeg = find_ffmpeg()
    build = Path(__file__).parent / "build"
    src = Path(args.src) if args.src else build / "BigBuckBunny_320x180.mp4"
    if not args.src:
        download(src)

    fps = args.fps
    apf = round(args.audio_rate / fps)
    stride = PAL_BYTES + PIX_BYTES + apf
    while fps * stride > 2_500_000 and fps > 1:
        fps -= 1
        apf = round(args.audio_rate / fps)
        stride = PAL_BYTES + PIX_BYTES + apf
    arate = apf * fps
    frames = int(args.dur * fps)
    max_frames = (REGION - 4096) // stride
    if frames > max_frames:
        print(f"trimming {frames} -> {max_frames} frames to fit the {REGION >> 20} MiB region")
        frames = max_frames
    dur = frames / fps
    print(f"{frames} frames @ {fps} fps = {dur:.2f} s, stride {stride} B, audio {arate} Hz ({apf} B/frame), "
          f"{fps * stride / 1e3:.0f} kB/s, file {(4096 + frames * stride) / 1e6:.2f} MB")

    # ---- audio
    af = ("highpass=f=400,loudnorm=I=-16:TP=-1.5:LRA=11,alimiter=limit=0.89:level=disabled")
    cmd = [ffmpeg, "-v", "error", "-ss", args.start, "-t", f"{dur:.3f}", "-i", str(src), "-vn", "-ac", "1",
           "-ar", str(arate), "-af", af, "-f", "f32le", "-"]
    raw = subprocess.run(cmd, capture_output=True, check=True).stdout
    x = np.frombuffer(raw, dtype="<f4").astype(np.float64)
    need = frames * apf
    x = np.pad(x, (0, max(0, need - len(x))))[:need]
    peak = float(np.max(np.abs(x))) or 1.0
    x = x / peak                                            # peak-normalise: full 8-bit scale, played at -9 dB
    rng = np.random.default_rng(1)
    tpdf = rng.random(len(x)) - rng.random(len(x))          # +-1 LSB triangular dither
    a8 = np.clip(np.rint(x * 127.0 + tpdf), -127, 127).astype(np.int16) + 128
    a8 = a8.astype(np.uint8)
    print(f"audio: {len(a8)} samples, source peak {20 * np.log10(peak + 1e-12):.1f} dBFS -> normalised; "
          f"played at -9.0 dBFS peak")

    # ---- video
    vf = (f"fps={fps},scale={W}:{H}:force_original_aspect_ratio=decrease:flags=lanczos,"
          f"pad={W}:{H}:(ow-iw)/2:(oh-ih)/2:black,format=rgb24")
    cmd = [ffmpeg, "-v", "error", "-ss", args.start, "-t", f"{dur + 0.5:.3f}", "-i", str(src), "-an", "-vf", vf,
           "-frames:v", str(frames), "-f", "rawvideo", "-pix_fmt", "rgb24", "-"]
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    fsz = W * H * 3
    body = bytearray()
    n = 0
    last = None
    while n < frames:
        buf = p.stdout.read(fsz)
        if len(buf) < fsz:
            break
        last = Image.frombytes("RGB", (W, H), buf)
        pal, pix = quant_frame(last)
        body += pal + pix + a8[n * apf:(n + 1) * apf].tobytes()
        n += 1
        if n % 60 == 0:
            print(f"\r  frame {n}/{frames}", end="", flush=True)
    p.stdout.close()
    p.wait()
    print(f"\r  frame {n}/{frames}")
    if n < frames:                                          # short source: hold the last frame (silence)
        print(f"warning: source ended after {n} frames; keeping {n}")
        frames = n

    # ---- container
    data_crc = zlib.crc32(bytes(body)) & 0xFFFFFFFF
    total = 4096 + len(body)
    assert total == 4096 + frames * stride
    hdr = struct.pack("<15I", MAGIC, 1, W, H, fps, frames, arate, 8, stride, apf, 4096, total, data_crc,
                      (-900) & 0xFFFFFFFF, 0)
    hdr += struct.pack("<I", zlib.crc32(hdr) & 0xFFFFFFFF)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(hdr + b"\xff" * (4096 - len(hdr)) + bytes(body))
    out.with_suffix(".README.txt").write_text(
        f"{out.name}: media container for the stm32-hwtest `media.play` suite (not committed).\n"
        f"Content: {ATTRIB}\n"
        f"Segment: start {args.start}, {frames / fps:.1f} s, {fps} fps, 160x128 letterboxed, mono {arate} Hz 8-bit.\n"
        f"Upload: py -3.14 tools/hwtest.py media upload {out}\nPlay:   run media.play\n", encoding="utf-8")
    print(f"wrote {out} ({total} B, data crc32 {data_crc:#010x})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
