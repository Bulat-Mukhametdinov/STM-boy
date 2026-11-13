#!/usr/bin/env python3
"""Turn bench results (hwtest.py run ... --out JSON, or a JSON list of @R objects) into markdown tables.

usage: gen_budget.py results.json [more.json ...] > BENCH.md
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

SKIP = {"t", "suite", "name", "cyc_min", "cyc_med", "us", "unit", "per_unit", "mbps", "clk", "ws", "art", "opt", "loc"}


def load(paths: list[str]) -> list[dict]:
    out: list[dict] = []
    for p in paths:
        d = json.loads(Path(p).read_text(encoding="utf-8"))
        rows = d.get("results", []) if isinstance(d, dict) else d
        out.extend(r for r in rows if isinstance(r, dict))
    return out


def table(rows: list[list[str]], head: list[str]) -> str:
    w = [max(len(str(x)) for x in col) for col in zip(head, *rows)]
    line = lambda r: "| " + " | ".join(str(x).ljust(n) for x, n in zip(r, w)) + " |"
    return "\n".join([line(head), "| " + " | ".join("-" * n for n in w) + " |"] + [line(r) for r in rows])


def bench_tables(results: list[dict]) -> str:
    suites: dict[str, list[dict]] = {}
    for r in results:
        if r.get("t") == "bench" and "name" in r:
            suites.setdefault(r.get("suite", "?"), []).append(r)
    md = []
    for s, rows in suites.items():
        head = ["name", "loc", "cyc_min", "cyc_med", "us", "per unit", "MB/s", "extra"]
        body = []
        for r in rows:
            extra = ", ".join(f"{k}={v}" for k, v in r.items() if k not in SKIP)
            pu = f"{r['per_unit']} /{r['unit']}" if "per_unit" in r else ""
            body.append([r["name"], r.get("loc", ""), r.get("cyc_min", ""), r.get("cyc_med", ""), r.get("us", ""), pu,
                         r.get("mbps", ""), extra])
        md.append(f"### {s}\n\n{table(body, head)}\n")
    return "\n".join(md)


def budget_tables(results: list[dict]) -> str:
    b = {}
    for r in results:
        if r.get("t") == "budget":
            b.update({k: v for k, v in r.items() if k not in ("t", "suite", "part")})
    if not b:
        return ""
    g = b.get
    rows30 = [["cycles per frame", g("frame_cyc_30"), g("frame_cyc_60")],
              ["converter (palette -> RGB565, pipelined) cycles", g("conv_cyc"), g("conv_cyc")],
              ["converter %", g("conv_pct_30"), g("conv_pct_60")],
              ["audio 4ch @ 22.05 kHz cycles", g("audio_cyc_30"), g("audio_cyc_60")],
              ["housekeeping (SysTick + input scan) cycles", g("hk_cyc_30"), g("hk_cyc_60")],
              ["remainder cycles for the game", g("rem_cyc_30"), g("rem_cyc_60")],
              ["remainder %", g("rem_pct_30"), g("rem_pct_60")],
              ["W25Q KB per frame (best DMA read)", g("w25q_kb_frame_30"), g("w25q_kb_frame_60")]]
    md = ["## Frame budget @ %s MHz\n" % (int(g("clk", 0)) // 1_000_000), table(rows30, ["item", "30 fps", "60 fps"]), ""]
    other = [["best SPI full-frame DMA (us)", g("best_spi_frame_dma_us")],
             ["pipelined present @48 MHz (us)", g("present_pipelined_48mhz_us")],
             ["audio 4ch@22k CPU %", g("audio_4ch_22k_pct")],
             ["W25Q best / concurrent-with-LCD kB/s", f"{g('w25q_best_kBps')} / {g('w25q_conc_kBps')}"],
             ["free RAM incl. 72 KB arena / excl. (bytes)", f"{g('free_ram')} / {g('free_ram_excl_arena')}"],
             ["CDC device->host kB/s", g("usb_tx_kBps")],
             ["boot to first pixel (ms)", g("boot_ms")],
             ["16x16 colorkey sprites per 60 fps frame", g("sprites16_key_per_frame_60")]]
    md += [table(other, ["metric", "value"]), ""]
    return "\n".join(md)


def main(argv: list[str]) -> int:
    if not argv:
        print(__doc__, file=sys.stderr)
        return 2
    res = load(argv)
    sys.stdout.write("# stm32-hwtest benchmark report\n\n" + budget_tables(res) + "\n" + bench_tables(res))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
