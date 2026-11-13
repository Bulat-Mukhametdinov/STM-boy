#!/usr/bin/env python3
"""Host-side analysis of the stm32-hwtest audio measurements (P4).

Frequency response (input of ``audio.sweep`` read with a phone, or a phone-app CSV export):

    py -3.14 tools/audio_fr.py fr.csv [--json out.json] [--md out.md]

* input: CSV/TSV/semicolon text with a ``freq_hz, level_db[, notes]`` layout.  A header row is
  optional; phone-app exports are accepted (``Frequency (Hz);Level (dB)``, decimal commas,
  ``1.25k`` / ``1250 Hz`` frequency strings, ``#`` comment lines, extra columns).  Duplicate
  frequencies are averaged (dB mean).
* the response is normalised to its 1 kHz value, the -6 dB (and -3 dB) band edges are found by
  interpolation on a log-frequency axis, and a high-pass corner plus an optional low/high shelf
  compensation (clamped to +-6 dB) is suggested.

Matrix table (``audio.matrix``, one row per config with the values read from the phone):

    py -3.14 tools/audio_fr.py --matrix matrix.csv [--md out.md]

Columns (header names matched loosely): ``cfg`` (``M07`` or ``7``), ``idle`` (dBA), ``level`` or
``1k`` (dB), ``h2``, ``h3`` (dBc, negative; use ``--abs-harmonics`` if they are absolute dB),
optional ``snr`` and ``verdict``.  Output columns: Cfg | Carrier | Bits | Fs | Dither | Idle dBA |
1k level | H2 | H3 | SNR | Verdict.  The config numbering is the firmware's (carrier-major, then
Fs, then dither).
"""
from __future__ import annotations

import argparse
import json
import math
import re
import sys
from pathlib import Path
from typing import Optional

# ---------------------------------------------------------------- firmware config table

CARRIERS = [("750k", 7), ("375k", 8), ("187.5k", 9), ("93.75k", 10)]
FS_LIST = [11025, 16000, 22050, 32000]
DITHERS = ["off", "TPDF", "shaped"]
N_CFG = len(CARRIERS) * len(FS_LIST) * len(DITHERS)
ISO_THIRD_OCT = [50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600,
                 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500]


def config_info(n: int) -> dict:
    """n = 1..48 (the M01..M48 ids of audio.matrix)."""
    if not 1 <= n <= N_CFG:
        raise ValueError(f"config {n} out of range 1..{N_CFG}")
    i = n - 1
    c, fsi, d = i // 12, (i // 3) % 4, i % 3
    return {"cfg": f"M{n:02d}", "n": n, "carrier": CARRIERS[c][0], "bits": CARRIERS[c][1],
            "fs": FS_LIST[fsi], "dither": DITHERS[d]}


# ---------------------------------------------------------------- parsing

_NUM = re.compile(r"^[-+]?(\d+([.,]\d*)?|[.,]\d+)([eE][-+]?\d+)?$")


def parse_number(tok: str) -> Optional[float]:
    """'1,5' / '1.25k' / '1250 Hz' / '-12.5 dB' -> float, or None."""
    t = tok.strip().strip('"').strip()
    if not t:
        return None
    t = re.sub(r"\s*(hz|db[a-z]*|khz)$", lambda m: "k" if m.group(1).lower() == "khz" else "", t, flags=re.I)
    mult = 1.0
    if t[-1:] in "kK" and len(t) > 1:
        mult, t = 1000.0, t[:-1]
    if not _NUM.match(t):
        return None
    return float(t.replace(",", ".")) * mult


def _split(line: str, delim: str) -> list[str]:
    return [c.strip().strip('"').strip() for c in line.split(delim)]


def _detect_delim(lines: list[str]) -> str:
    sample = lines[0]
    if "\t" in sample:
        return "\t"
    if ";" in sample:
        return ";"
    return ","


def read_rows(text: str) -> tuple[Optional[list[str]], list[list[str]]]:
    """-> (header or None, data rows as string cells)."""
    lines = [ln.strip() for ln in text.lstrip("﻿").splitlines()]
    lines = [ln for ln in lines if ln and not ln.startswith("#")]
    if not lines:
        raise ValueError("no data")
    delim = _detect_delim(lines)
    rows = [_split(ln, delim) for ln in lines]
    first = rows[0]
    header = None
    if parse_number(first[0]) is None or all(parse_number(c) is None for c in first):
        header = [c.lower() for c in first]
        rows = rows[1:]
    return header, rows


def _find_col(header: Optional[list[str]], keys: tuple[str, ...], exclude: tuple[int, ...] = ()) -> Optional[int]:
    if not header:
        return None
    for k in keys:
        for i, h in enumerate(header):
            if i not in exclude and k in h:
                return i
    return None


def load_fr(text: str) -> list[tuple[float, float, str]]:
    header, rows = read_rows(text)
    fi = _find_col(header, ("freq", "hz"))
    if fi is None:
        fi = 0
    li = _find_col(header, ("level", "spl", "magnitude", "ampl", "db", "gain"), exclude=(fi,))
    if li is None:
        li = 1 if fi == 0 else 0
    ni = _find_col(header, ("note", "comment", "label"), exclude=(fi, li))
    if ni is None and header is None and rows and len(rows[0]) > 2:
        ni = 2
    acc: dict[float, list[float]] = {}
    notes: dict[float, str] = {}
    for r in rows:
        if max(fi, li) >= len(r):
            continue
        f, lv = parse_number(r[fi]), parse_number(r[li])
        if f is None or lv is None or f <= 0:
            continue
        acc.setdefault(f, []).append(lv)
        if ni is not None and ni < len(r) and r[ni]:
            notes[f] = r[ni]
    pts = [(f, sum(v) / len(v), notes.get(f, "")) for f, v in sorted(acc.items())]
    if len(pts) < 3:
        raise ValueError("need at least 3 valid (freq, level) rows")
    return pts


# ---------------------------------------------------------------- analysis

def interp(pts: list[tuple[float, float]], f: float) -> float:
    """Linear in (log f, dB), clamped to the ends."""
    if f <= pts[0][0]:
        return pts[0][1]
    if f >= pts[-1][0]:
        return pts[-1][1]
    for (f0, l0), (f1, l1) in zip(pts, pts[1:]):
        if f0 <= f <= f1:
            if f1 == f0:
                return l0
            t = math.log(f / f0) / math.log(f1 / f0)
            return l0 + t * (l1 - l0)
    return pts[-1][1]


def normalise(raw: list[tuple[float, float, str]], ref_hz: float = 1000.0) -> tuple[list[tuple[float, float]], float]:
    pts = [(f, lv) for f, lv, _ in raw]
    if not (pts[0][0] <= ref_hz <= pts[-1][0]):
        raise ValueError(f"measurement does not cover {ref_hz:g} Hz ({pts[0][0]:g}..{pts[-1][0]:g})")
    ref = interp(pts, ref_hz)
    return [(f, lv - ref) for f, lv in pts], ref


def _crossing(a: tuple[float, float], b: tuple[float, float], level: float) -> float:
    (f0, l0), (f1, l1) = a, b
    if l1 == l0:
        return f0
    t = (level - l0) / (l1 - l0)
    return math.exp(math.log(f0) + t * (math.log(f1) - math.log(f0)))


def band_edges(pts: list[tuple[float, float]], drop: float = 6.0, ref_hz: float = 1000.0) -> dict:
    """Walk away from the reference frequency until the response is `drop` dB down.
    -> {"lo": Hz or None (never dropped: below the first point), "hi": Hz or None}."""
    level = -drop
    lo = hi = None
    below = [p for p in pts if p[0] <= ref_hz][::-1]     # descending in f, starting at/below 1 kHz
    above = [p for p in pts if p[0] >= ref_hz]
    ref_pt = (ref_hz, interp(pts, ref_hz))
    prev = ref_pt
    for p in below:
        if p[0] == prev[0]:
            continue
        if p[1] <= level:
            lo = _crossing(p, prev, level)
            break
        prev = p
    prev = ref_pt
    for p in above:
        if p[0] == prev[0]:
            continue
        if p[1] <= level:
            hi = _crossing(prev, p, level)
            break
        prev = p
    return {"lo": lo, "hi": hi}


def nearest_iso(f: float) -> int:
    return min(ISO_THIRD_OCT, key=lambda x: abs(math.log(x / f)))


def suggest(pts: list[tuple[float, float]], e6: dict, max_boost: float = 6.0) -> dict:
    """High-pass corner and optional shelves.

    HPF: 1st-order at the -6 dB low edge (below it the speaker only costs excursion, current and
    distortion).  Shelves: the compensation needed to flatten the response inside [lo, hi] is
    clamped to +-max_boost; a shelf is proposed when its band mean is off by >= 1.5 dB."""
    lo = e6["lo"] if e6["lo"] is not None else pts[0][0]
    hi = e6["hi"] if e6["hi"] is not None else pts[-1][0]
    mid = [lv for f, lv in pts if 500 <= f <= 2000] or [0.0]
    r_mid = sum(mid) / len(mid)

    def comp(f: float) -> float:
        return max(-max_boost, min(max_boost, r_mid - interp(pts, f)))

    out: dict = {"hpf_hz": round(lo, 1), "hpf_iso_hz": nearest_iso(lo), "hpf_order": 1,
                 "ref_mid_db": round(r_mid, 2), "low_shelf": None, "high_shelf": None, "comp": []}
    low_band = [f for f in ISO_THIRD_OCT if lo <= f <= 250]
    high_band = [f for f in ISO_THIRD_OCT if 4000 <= f <= hi]
    if low_band:
        g = sum(comp(f) for f in low_band) / len(low_band)
        if abs(g) >= 1.5:
            out["low_shelf"] = {"gain_db": round(g, 1), "corner_hz": 250}
    if high_band:
        g = sum(comp(f) for f in high_band) / len(high_band)
        if abs(g) >= 1.5:
            out["high_shelf"] = {"gain_db": round(g, 1), "corner_hz": 4000}
    out["comp"] = [{"f": f, "db": round(comp(f), 1)} for f in ISO_THIRD_OCT if lo * 0.9 <= f <= hi * 1.1]
    return out


def analyse_fr(text: str) -> dict:
    raw = load_fr(text)
    pts, ref = normalise(raw)
    e6 = band_edges(pts, 6.0)
    e3 = band_edges(pts, 3.0)
    sug = suggest(pts, e6)
    inband = [lv for f, lv in pts if (e6["lo"] or 0) <= f <= (e6["hi"] or 1e9)]
    return {
        "ref_1k_db": round(ref, 2),
        "points": [{"f": f, "db": round(lv, 2), "note": n} for (f, _, n), (_, lv) in zip(raw, pts)],
        "edge6": {k: (round(v, 1) if v else None) for k, v in e6.items()},
        "edge3": {k: (round(v, 1) if v else None) for k, v in e3.items()},
        "peak_db": round(max(lv for _, lv in pts), 2),
        "ripple_db": round(max(inband) - min(inband), 2) if inband else None,
        "suggest": sug,
    }


# ---------------------------------------------------------------- matrix

def _cfg_number(tok: str) -> Optional[int]:
    m = re.match(r"^\s*[Mm]?0*(\d+)\s*$", tok)
    return int(m.group(1)) if m else None


def load_matrix(text: str, abs_harmonics: bool = False) -> list[dict]:
    header, rows = read_rows(text) if text.strip() else (None, [])
    if header is not None and _cfg_number(header[0]) is not None:   # first row was data ("M07,..."), not a header
        rows = [[c for c in header]] + rows
        header = None
    if header is None:
        header = ["cfg", "idle", "level", "h2", "h3", "snr", "verdict"]   # headerless: fixed column order
    ci = _find_col(header, ("cfg", "id", "config"))
    ci = 0 if ci is None else ci
    ii = _find_col(header, ("idle", "noise", "hiss"))
    li = _find_col(header, ("1k", "level", "tone"), exclude=(ci,) + ((ii,) if ii is not None else ()))
    h2 = _find_col(header, ("h2",))
    h3 = _find_col(header, ("h3",))
    si = _find_col(header, ("snr",))
    vi = _find_col(header, ("verdict", "note", "comment"))
    out = []
    for r in rows:
        n = _cfg_number(r[ci]) if ci < len(r) else None
        if n is None or not 1 <= n <= N_CFG:
            continue

        def get(i: Optional[int]) -> Optional[float]:
            return parse_number(r[i]) if i is not None and i < len(r) else None

        row = config_info(n)
        row.update({"idle_dba": get(ii), "level_1k": get(li), "h2": get(h2), "h3": get(h3), "snr": get(si),
                    "verdict": (r[vi] if vi is not None and vi < len(r) else "")})
        if abs_harmonics and row["level_1k"] is not None:
            for k in ("h2", "h3"):
                if row[k] is not None:
                    row[k] = row[k] - row["level_1k"]
        if row["snr"] is None and row["idle_dba"] is not None and row["level_1k"] is not None:
            row["snr"] = round(row["level_1k"] - row["idle_dba"], 1)
        out.append(row)
    out.sort(key=lambda x: x["n"])
    _verdicts(out)
    return out


def _verdicts(rows: list[dict]) -> None:
    """Fill empty verdicts: good = SNR >= 45 dB and H2,H3 <= -40 dBc; ok = SNR >= 35 and <= -30; else poor.
    The 'good' row with the best SNR is marked BEST."""
    best = None
    for r in rows:
        if r["verdict"]:
            continue
        if r["snr"] is None:
            r["verdict"] = "n/a"
            continue
        worst_h = max([h for h in (r["h2"], r["h3"]) if h is not None], default=-99.0)
        if r["snr"] >= 45 and worst_h <= -40:
            r["verdict"] = "good"
            if best is None or r["snr"] > best["snr"]:
                best = r
        elif r["snr"] >= 35 and worst_h <= -30:
            r["verdict"] = "ok"
        else:
            r["verdict"] = "poor"
    if best is not None:
        best["verdict"] = "good, BEST"


# ---------------------------------------------------------------- markdown

def _fmt(v, nd=1) -> str:
    return "" if v is None else f"{v:.{nd}f}"


def fr_markdown(res: dict) -> str:
    e6, e3, s = res["edge6"], res["edge3"], res["suggest"]
    out = ["## Frequency response (re 1 kHz)", "", "| Freq Hz | Level dB | Notes |", "| ---: | ---: | --- |"]
    for p in res["points"]:
        out.append(f"| {p['f']:g} | {p['db']:+.1f} | {p['note']} |")
    lo6 = f"{e6['lo']:g} Hz" if e6["lo"] else "below the first point"
    hi6 = f"{e6['hi']:g} Hz" if e6["hi"] else "above the last point"
    lo3 = f"{e3['lo']:g} Hz" if e3["lo"] else "-"
    hi3 = f"{e3['hi']:g} Hz" if e3["hi"] else "-"
    out += ["", f"* 1 kHz reference level: {res['ref_1k_db']:+.1f} dB (as measured)",
            f"* -6 dB band: **{lo6} .. {hi6}**   (-3 dB: {lo3} .. {hi3})",
            f"* peak {res['peak_db']:+.1f} dB, in-band ripple {_fmt(res['ripple_db'])} dB", "",
            "### Suggested correction", "",
            f"* high-pass: 1st order at **{s['hpf_hz']:g} Hz** (nearest ISO {s['hpf_iso_hz']} Hz)"]
    out.append(f"* low shelf: {s['low_shelf']['gain_db']:+.1f} dB below {s['low_shelf']['corner_hz']} Hz"
               if s["low_shelf"] else "* low shelf: not needed")
    out.append(f"* high shelf: {s['high_shelf']['gain_db']:+.1f} dB above {s['high_shelf']['corner_hz']} Hz"
               if s["high_shelf"] else "* high shelf: not needed")
    out += ["* boost is clamped to +-6 dB; do not boost below the high-pass corner.", "",
            "| Freq Hz | Comp dB |", "| ---: | ---: |"]
    out += [f"| {c['f']} | {c['db']:+.1f} |" for c in s["comp"]]
    return "\n".join(out) + "\n"


def matrix_markdown(rows: list[dict]) -> str:
    out = ["## Audio matrix", "",
           "| Cfg | Carrier | Bits | Fs | Dither | Idle dBA | 1k level | H2 | H3 | SNR | Verdict |",
           "| --- | --- | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | --- |"]
    for r in rows:
        out.append(f"| {r['cfg']} | {r['carrier']} | {r['bits']} | {r['fs']} | {r['dither']} | {_fmt(r['idle_dba'])} | "
                   f"{_fmt(r['level_1k'])} | {_fmt(r['h2'])} | {_fmt(r['h3'])} | {_fmt(r['snr'])} | {r['verdict']} |")
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------- CLI

def main(argv: Optional[list[str]] = None) -> int:
    ap = argparse.ArgumentParser(description="stm32-hwtest audio FR / matrix analysis")
    ap.add_argument("fr", nargs="?", help="frequency-response CSV (freq_hz, level_db[, notes]) or phone-app export")
    ap.add_argument("--matrix", help="audio.matrix readings CSV (cfg, idle, level, h2, h3[, snr, verdict])")
    ap.add_argument("--abs-harmonics", action="store_true", help="H2/H3 columns are absolute dB, not dBc")
    ap.add_argument("--json", help="write the result object as JSON")
    ap.add_argument("--md", help="write the markdown tables to this file (default: stdout)")
    a = ap.parse_args(argv)
    if not a.fr and not a.matrix:
        ap.error("give a frequency-response CSV and/or --matrix")
    result: dict = {}
    md = ""
    try:
        if a.fr:
            result["fr"] = analyse_fr(Path(a.fr).read_text(encoding="utf-8-sig", errors="replace"))
            md += fr_markdown(result["fr"]) + "\n"
        if a.matrix:
            result["matrix"] = load_matrix(Path(a.matrix).read_text(encoding="utf-8-sig", errors="replace"),
                                           a.abs_harmonics)
            md += matrix_markdown(result["matrix"])
    except (ValueError, OSError) as e:
        print(f"audio_fr: {e}", file=sys.stderr)
        return 1
    if a.md:
        Path(a.md).write_text(md, encoding="utf-8")
    else:
        sys.stdout.write(md)
    if a.json:
        Path(a.json).write_text(json.dumps(result, indent=2), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
