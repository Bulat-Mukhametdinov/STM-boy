#!/usr/bin/env python3
"""Unit tests for audio_fr.py with synthetic data.  Run:  py -3.14 tools/test_audio_fr.py"""
from __future__ import annotations

import json
import math
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import audio_fr as A  # noqa: E402


def synth_db(f: float, hp: float = 300.0, lp: float = 8000.0, gain: float = -37.0) -> float:
    """2nd-order Butterworth high-pass at `hp` x low-pass at `lp` (power law), arbitrary absolute gain."""
    h = 1.0 / (1.0 + (hp / f) ** 4) / (1.0 + (f / lp) ** 4)
    return gain + 10.0 * math.log10(h)


def synth_csv(sep=",", dec=".", header=True) -> str:
    rows = []
    if header:
        rows.append(sep.join(["Frequency (Hz)", "Level (dB)"]))
    for f in A.ISO_THIRD_OCT:
        rows.append(sep.join([str(f), f"{synth_db(f):.2f}".replace(".", dec)]))
    return "\n".join(rows) + "\n"


class TestFR(unittest.TestCase):
    def test_edges_of_synthetic_response(self):
        res = A.analyse_fr(synth_csv())
        lo_exp = 300.0 / 3 ** 0.25      # 2nd order: -6.02 dB at fc / 3^(1/4)
        hi_exp = 8000.0 * 3 ** 0.25
        self.assertAlmostEqual(res["edge6"]["lo"], lo_exp, delta=lo_exp * 0.06)
        self.assertAlmostEqual(res["edge6"]["hi"], hi_exp, delta=hi_exp * 0.06)
        # -3 dB points are at the corner frequencies
        self.assertAlmostEqual(res["edge3"]["lo"], 300.0, delta=18.0)
        self.assertAlmostEqual(res["edge3"]["hi"], 8000.0, delta=480.0)
        # normalised: 1 kHz point is 0 dB
        p1k = [p for p in res["points"] if p["f"] == 1000][0]
        self.assertAlmostEqual(p1k["db"], 0.0, places=6)
        self.assertAlmostEqual(res["ref_1k_db"], synth_db(1000), places=1)

    def test_hpf_suggestion(self):
        s = A.analyse_fr(synth_csv())["suggest"]
        self.assertAlmostEqual(s["hpf_hz"], 228.0, delta=15.0)
        self.assertEqual(s["hpf_iso_hz"], 250)
        self.assertEqual(s["hpf_order"], 1)
        for c in s["comp"]:
            self.assertLessEqual(abs(c["db"]), 6.0)

    def test_shelf_for_tilted_response(self):
        # +0 dB mid, -4 dB above 4 kHz and below 250 Hz (flat otherwise): shelves must be proposed, boost > 0
        lines = []
        for f in A.ISO_THIRD_OCT:
            lv = -4.0 if (f >= 4000 or f <= 250) else 0.0
            lines.append(f"{f},{lv}")
        res = A.analyse_fr("\n".join(lines))
        sug = res["suggest"]
        self.assertIsNone(res["edge6"]["lo"])
        self.assertIsNotNone(sug["high_shelf"])
        self.assertAlmostEqual(sug["high_shelf"]["gain_db"], 4.0, delta=0.6)
        self.assertGreater(sug["low_shelf"]["gain_db"], 3.0)

    def test_phone_formats(self):
        a = A.analyse_fr(synth_csv())
        b = A.analyse_fr(synth_csv(sep=";", dec=","))            # semicolon + decimal comma (EU phone export)
        c = A.analyse_fr(synth_csv(sep="\t", header=False))
        for r in (b, c):
            self.assertAlmostEqual(r["edge6"]["lo"], a["edge6"]["lo"], places=1)
            self.assertAlmostEqual(r["edge6"]["hi"], a["edge6"]["hi"], places=1)

    def test_freq_strings_comments_extra_columns_duplicates(self):
        txt = ("# exported by app\n"
               "Time;Freq;Amplitude dBFS;Comment\n"
               "0;100 Hz;-40,0;low\n0;1k;-30,0;\n0;1000;-32,0;dup\n0;5 kHz;-36,5;\n0;10000 Hz;-45;\n")
        raw = A.load_fr(txt)
        self.assertEqual([p[0] for p in raw], [100.0, 1000.0, 5000.0, 10000.0])
        self.assertAlmostEqual(raw[1][1], -31.0)                 # dB mean of the two 1 kHz rows
        self.assertEqual(raw[0][2], "low")

    def test_errors(self):
        with self.assertRaises(ValueError):
            A.analyse_fr("100,-3\n200,-2\n")                      # too few points
        with self.assertRaises(ValueError):
            A.analyse_fr("2000,0\n3000,-1\n4000,-2\n")            # no 1 kHz in range

    def test_markdown_and_cli(self):
        with tempfile.TemporaryDirectory() as d:
            fr = Path(d) / "fr.csv"
            fr.write_text(synth_csv(), encoding="utf-8")
            md, js = Path(d) / "o.md", Path(d) / "o.json"
            self.assertEqual(A.main([str(fr), "--md", str(md), "--json", str(js)]), 0)
            text = md.read_text(encoding="utf-8")
            self.assertIn("| Freq Hz | Level dB | Notes |", text)
            self.assertIn("-6 dB band", text)
            self.assertIn("high-pass: 1st order", text)
            obj = json.loads(js.read_text(encoding="utf-8"))
            self.assertIn("edge6", obj["fr"])
            self.assertEqual(A.main([str(Path(d) / "missing.csv")]), 1)


class TestMatrix(unittest.TestCase):
    def test_config_mapping_matches_firmware(self):
        c = A.config_info(19)                                    # M19 = 375k/8b 22k OFF
        self.assertEqual((c["carrier"], c["bits"], c["fs"], c["dither"]), ("375k", 8, 22050, "off"))
        c = A.config_info(48)
        self.assertEqual((c["carrier"], c["bits"], c["fs"], c["dither"]), ("93.75k", 10, 32000, "shaped"))
        c = A.config_info(1)
        self.assertEqual((c["carrier"], c["bits"], c["fs"], c["dither"]), ("750k", 7, 11025, "off"))
        with self.assertRaises(ValueError):
            A.config_info(49)

    def test_table(self):
        csv = ("cfg,idle dBA,1k level,H2,H3\n"
               "M19,31.0,78.0,-45,-52\n"          # snr 47, good
               "M20,36.5,78.0,-44,-50\n"          # snr 41.5 -> ok (SNR < 45)
               "M21,30.0,76.0,-28,-50\n"          # snr 46, H2 too high -> poor
               "7,40,70,-40,-41\n"                # bare number
               "M99,1,1,1,1\n")                   # ignored
        rows = A.load_matrix(csv)
        self.assertEqual([r["cfg"] for r in rows], ["M07", "M19", "M20", "M21"])
        by = {r["cfg"]: r for r in rows}
        self.assertEqual(by["M19"]["verdict"], "good, BEST")
        self.assertEqual(by["M20"]["verdict"], "ok")
        self.assertEqual(by["M21"]["verdict"], "poor")
        self.assertAlmostEqual(by["M19"]["snr"], 47.0)
        md = A.matrix_markdown(rows)
        self.assertIn("| Cfg | Carrier | Bits | Fs | Dither | Idle dBA | 1k level | H2 | H3 | SNR | Verdict |", md)
        self.assertIn("| M19 | 375k | 8 | 22050 | off | 31.0 | 78.0 | -45.0 | -52.0 | 47.0 | good, BEST |", md)

    def test_headerless(self):
        rows = A.load_matrix("M03,30,80,-45,-50\nM04,31,80,-44,-50\n")
        self.assertEqual([r["cfg"] for r in rows], ["M03", "M04"])
        self.assertEqual(rows[0]["verdict"], "good, BEST")

    def test_abs_harmonics_and_manual_verdict(self):
        rows = A.load_matrix("cfg;idle;level;h2;h3;verdict\nM05;30;80;38;30;mine\n", abs_harmonics=True)
        self.assertAlmostEqual(rows[0]["h2"], -42.0)
        self.assertEqual(rows[0]["verdict"], "mine")


if __name__ == "__main__":
    unittest.main(verbosity=1)
