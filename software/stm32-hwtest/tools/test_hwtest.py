#!/usr/bin/env python3
"""Unit tests for hwtest.py with a simulated device (no hardware needed).

Run:  py -3.14 tools/test_hwtest.py
"""
from __future__ import annotations

import json
import os
import queue
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import hwtest  # noqa: E402

_TMP = tempfile.TemporaryDirectory(prefix="hwtest-test-")
TMP = Path(_TMP.name)
# Never touch the real board lock / hardware log from tests.
hwtest.HWLOG.path = TMP / "HW_LOG.md"
hwtest.LOCK_PATH = TMP / "board.lock"

INFO = {"t": "info", "uid": "0123456789ABCDEF01234567", "serial": "315A356C3435", "fw": "abc1234",
        "build": 1763000000, "clk": 100000000, "opt": "-O2", "busy": "idle", "hwcal": True,
        "reset": "por", "flash_kb": 512}

BIN_PAYLOAD = b"\x00\x01\r\n@OK fake\r\n\xff\xfe@DONE x pass\n" + bytes(range(256))


class FakeBoard:
    """Simulates the firmware side of PROTOCOL.md on top of FakeSerial."""

    def __init__(self) -> None:
        self.pending_prompt: str | None = None
        self.running: str | None = None

    @staticmethod
    def line(s: str) -> bytes:
        return (s + "\r\n").encode()

    def handle(self, cmd: str) -> bytes:
        tok = cmd.split()
        if not tok:
            return b""
        c = tok[0]
        if c == "ping":
            return self.line("@OK ping")
        if c == "info":
            return self.line("@R " + json.dumps(INFO)) + self.line("@OK info")
        if c == "dump":
            return (self.line(f"@B {len(BIN_PAYLOAD)} {zlib.crc32(BIN_PAYLOAD):08x}") + BIN_PAYLOAD
                    + self.line("@OK dump"))
        if c == "dumpbad":
            return (self.line(f"@B {len(BIN_PAYLOAD)} deadbeef") + BIN_PAYLOAD + self.line("@OK dumpbad"))
        if c == "answer" and len(tok) == 3:
            if self.pending_prompt != tok[1]:
                return self.line(f"@ERR answer unknown prompt {tok[1]}")
            self.pending_prompt = None
            ans = tok[2]
            return (self.line("@A " + json.dumps({"id": tok[1], "answer": ans}))
                    + self.line("@R " + json.dumps({"t": "test", "suite": "selftest", "name": "lcd",
                                                    "pass": ans == "ok"}))
                    + self.line(f"@DONE selftest {'pass' if ans == 'ok' else 'fail'}"))
        if c == "abort":
            if self.running:
                s, self.running = self.running, None
                return self.line(f"@DONE {s} abort")
            return self.line("@ERR abort nothing running")
        if c == "run" and len(tok) >= 2:
            suite = tok[1]
            if suite == "selftest":
                self.pending_prompt = "p1"
                self.running = "selftest"
                return (self.line("selftest starting")
                        + self.line("@R " + json.dumps({"t": "test", "suite": "selftest", "name": "crc", "pass": True}))
                        + self.line("@P " + json.dumps({"id": "p1", "text": "Is the screen red?",
                                                        "answers": ["A=ok", "B=fail"]})))
            if suite == "failing":
                return (self.line("@R " + json.dumps({"t": "test", "suite": "failing", "name": "w25q", "pass": False}))
                        + self.line("@DONE failing fail"))
            if suite == "silent":
                self.running = "silent"
                return b""
            return self.line(f"@ERR run unknown suite {suite}")
        return self.line(f"@ERR {c} unknown command")


class FakeSerial:
    def __init__(self, board: FakeBoard | None = None, chunk: int = 0, hello: bool = True) -> None:
        self.board = board or FakeBoard()
        self.rx = bytearray()
        self.tx = bytearray()
        self.sent: list[str] = []
        self.chunk = chunk
        self.timeout = None
        self.vanished = False
        self.lock = threading.Lock()
        if hello:
            self.rx += FakeBoard.line("@HELLO " + json.dumps(INFO))

    def push(self, data: bytes) -> None:
        with self.lock:
            self.rx += data

    @property
    def in_waiting(self) -> int:
        if self.vanished:
            raise hwtest.SerialException("ClearCommError failed (device gone)")
        with self.lock:
            n = len(self.rx)
        return min(n, self.chunk) if self.chunk else n

    def read(self, n: int = 1) -> bytes:
        if self.vanished:
            raise hwtest.SerialException("ReadFile failed (device gone)")
        with self.lock:
            if self.chunk:
                n = min(n, self.chunk)
            data = bytes(self.rx[:n])
            del self.rx[:n]
        if not data:
            time.sleep(0.002)
        return data

    def write(self, data: bytes) -> int:
        if self.vanished:
            raise hwtest.SerialException("WriteFile failed")
        self.tx += data
        while b"\n" in self.tx:
            i = self.tx.index(b"\n")
            line = self.tx[:i].decode().strip()
            del self.tx[:i + 1]
            self.sent.append(line)
            self.push(self.board.handle(line))
        return len(data)

    def close(self) -> None:
        pass


def make_dev(**kw) -> tuple[hwtest.Device, FakeSerial]:
    fs = FakeSerial(**kw)
    return hwtest.Device(fs, "FAKE"), fs


class TestParsing(unittest.TestCase):
    def test_parse_line_kinds(self) -> None:
        self.assertEqual(hwtest.parse_line("hello world").kind, "log")
        m = hwtest.parse_line('@R {"t":"crc","pass":true}')
        self.assertEqual((m.kind, m.obj["t"]), ("R", "crc"))
        m = hwtest.parse_line("@OK info")
        self.assertEqual((m.kind, m.cmd), ("OK", "info"))
        m = hwtest.parse_line("@ERR run unknown suite foo")
        self.assertEqual((m.kind, m.cmd, m.arg), ("ERR", "run", "unknown suite foo"))
        m = hwtest.parse_line("@DONE selftest pass")
        self.assertEqual((m.kind, m.cmd, m.arg), ("DONE", "selftest", "pass"))
        m = hwtest.parse_line("@R {broken json")
        self.assertEqual((m.kind, m.obj), ("R", None))
        self.assertEqual(hwtest.parse_line("@XYZ 1").kind, "other")
        self.assertEqual(hwtest.parse_b_header("@B 12 0a0b0c0d"), (12, 0x0A0B0C0D))
        self.assertIsNone(hwtest.parse_b_header("@B x y"))

    def test_reader_binary_frame_bytewise(self) -> None:
        dev, fs = make_dev(chunk=1)
        fs.write(b"dump\n")
        msgs = []
        while True:
            m = dev.read_msg(2.0)
            self.assertIsNotNone(m)
            msgs.append(m)
            if m.kind == "OK":
                break
        kinds = [m.kind for m in msgs]
        self.assertEqual(kinds, ["HELLO", "B", "OK"])
        b = msgs[1]
        self.assertEqual(b.data, BIN_PAYLOAD)
        self.assertTrue(b.crc_ok)
        self.assertEqual(msgs[2].cmd, "dump")

    def test_reader_bad_crc_keeps_framing(self) -> None:
        dev, fs = make_dev(hello=False)
        tx = dev.transact("dumpbad", 2.0)
        self.assertEqual(tx.status, "ok")
        bs = [m for m in tx.msgs if m.kind == "B"]
        self.assertEqual(len(bs), 1)
        self.assertFalse(bs[0].crc_ok)
        self.assertEqual(bs[0].data, BIN_PAYLOAD)

    def test_malformed_b_header_is_log(self) -> None:
        dev, fs = make_dev(hello=False)
        fs.push(b"@B notanumber zz\r\nnext\r\n")
        self.assertEqual(dev.read_msg(1).kind, "log")
        self.assertEqual(dev.read_msg(1).text, "next")

    def test_garbage_decoded_with_replace(self) -> None:
        dev, fs = make_dev(hello=False)
        fs.push(b"\xff\xfeabc\r\n")
        m = dev.read_msg(1)
        self.assertEqual(m.kind, "log")
        self.assertTrue(m.text.endswith("abc"))


class TestDevice(unittest.TestCase):
    def test_info_ignores_hello(self) -> None:
        dev, _ = make_dev()
        info = dev.info(2.0)
        self.assertEqual(info["fw"], "abc1234")
        self.assertEqual(dev.hello["uid"], INFO["uid"])

    def test_ping_and_err(self) -> None:
        dev, _ = make_dev()
        self.assertEqual(dev.transact("ping", 2).status, "ok")
        tx = dev.transact("bogus", 2)
        self.assertEqual(tx.status, "err")
        self.assertIn("unknown command", tx.final.text)

    def test_timeout(self) -> None:
        dev, fs = make_dev(hello=False)
        fs.board.handle = lambda line: b""  # silent device
        t = time.monotonic()
        tx = dev.transact("ping", 0.3)
        self.assertEqual(tx.status, "timeout")
        self.assertLess(time.monotonic() - t, 1.5)

    def test_disconnect_raises(self) -> None:
        dev, fs = make_dev()
        fs.vanished = True
        with self.assertRaises(hwtest.DeviceDisconnected):
            dev.read_msg(0.2)
        with self.assertRaises(hwtest.DeviceDisconnected):
            dev.send("ping")


class TestRunSuite(unittest.TestCase):
    def test_prompt_answered_from_host(self) -> None:
        dev, fs = make_dev()
        q: "queue.Queue[str]" = queue.Queue()
        banners = []

        def on_prompt(obj):
            banners.append(hwtest.prompt_banner(obj))
            q.put("a\n")

        out = hwtest.run_suite(dev, "run selftest", idle_timeout=2, answers=q, on_prompt=on_prompt)
        self.assertEqual(out.status, "pass")
        self.assertEqual(out.exit_code, 0)
        self.assertIn("answer p1 ok", fs.sent)
        self.assertEqual(len(out.results), 2)
        self.assertEqual(out.prompts[0]["id"], "p1")
        self.assertIn("ON DEVICE: Is the screen red?", banners[0])
        self.assertIn("answer with buttons A/B", banners[0])

    def test_prompt_answered_on_device_buttons(self) -> None:
        dev, fs = make_dev()

        def on_prompt(obj):  # simulate the user pressing B on the device
            threading.Timer(0.1, lambda: fs.push(fs.board.handle("answer p1 fail"))).start()

        out = hwtest.run_suite(dev, "run selftest", idle_timeout=0.05, prompt_timeout=5, on_prompt=on_prompt)
        # idle timeout is suspended while a prompt is pending
        self.assertEqual(out.status, "fail")
        self.assertEqual(out.exit_code, 1)
        self.assertEqual(out.answers[0]["answer"], "fail")

    def test_failing_suite(self) -> None:
        dev, _ = make_dev()
        out = hwtest.run_suite(dev, "run failing", idle_timeout=2)
        self.assertEqual((out.status, out.exit_code), ("fail", 1))

    def test_timeout_sends_abort(self) -> None:
        dev, fs = make_dev()
        out = hwtest.run_suite(dev, "run silent", idle_timeout=0.3)
        self.assertEqual(out.status, "timeout")
        self.assertEqual(out.exit_code, 2)
        self.assertIn("abort", fs.sent)
        self.assertEqual(out.done_line, "@DONE silent abort")

    def test_unknown_suite_is_error(self) -> None:
        dev, _ = make_dev()
        out = hwtest.run_suite(dev, "run nosuch", idle_timeout=2)
        self.assertEqual((out.status, out.exit_code), ("error", 2))

    def test_disconnect_mid_run(self) -> None:
        dev, fs = make_dev()
        threading.Timer(0.2, lambda: setattr(fs, "vanished", True)).start()
        out = hwtest.run_suite(dev, "run silent", idle_timeout=5)
        self.assertEqual(out.status, "disconnected")

    def test_save_results(self) -> None:
        dev, _ = make_dev()
        q: "queue.Queue[str]" = queue.Queue()
        out = hwtest.run_suite(dev, "run selftest", idle_timeout=2, answers=q,
                               on_prompt=lambda o: q.put("ok"))
        out.binaries.append(hwtest.Msg("B", "@B 3 0", data=b"abc", crc=0, crc_ok=False))
        from datetime import datetime
        started = datetime.now().astimezone()
        p = hwtest.save_run_results(out, suite="selftest", command="run selftest", info=INFO,
                                    port="FAKE", started=started, out_dir=str(TMP / "results"))
        self.assertEqual(p.parent.name, INFO["uid"])
        self.assertRegex(p.name, r"^\d{8}-\d{6}-selftest\.json$")
        doc = json.loads(p.read_text(encoding="utf-8"))
        self.assertEqual(doc["status"], "pass")
        self.assertEqual(doc["info"]["fw"], "abc1234")
        self.assertEqual(len(doc["results"]), 2)
        self.assertTrue((p.parent / doc["binaries"][0]["file"]).is_file())
        self.assertIn("host_time_start", doc)


REAL_LIST_USB = (
    "      -------------------------------------------------------------------\r\n"
    "                       STM32CubeProgrammer v2.21.0                  \r\n"
    "      -------------------------------------------------------------------\r\n\r\n"
    "=====  DFU Interface   =====\r\n\r\n"
    "Total number of available STM32 device in DFU mode: 1\r\n\r\n"
    "  Device Index           : USB1\r\n"
    "  USB Bus Number         : 005\r\n"
    "  USB Address Number     : 001\r\n"
    "  Product ID             : STM32  BOOTLOADER\r\n"
    "  Serial number          : 315A356C3435\r\n"
    "  Firmware version       : 0x011a\r\n"
    "  Device ID              : 0x0431\r\n\r\n")


class TestCliParsing(unittest.TestCase):
    def test_list_usb_real_capture(self) -> None:
        devs = hwtest.parse_dfu_list(REAL_LIST_USB)
        self.assertEqual(len(devs), 1)
        self.assertEqual((devs[0].index, devs[0].serial), ("USB1", "315A356C3435"))
        self.assertEqual(devs[0].fields["firmware version"], "0x011a")
        self.assertEqual(devs[0].fields["device id"], "0x0431")

    def test_list_usb_two_and_none(self) -> None:
        second = REAL_LIST_USB.split("Total number")[1].split("\r\n", 1)[1].replace("USB1", "USB2").replace(
            "315A356C3435", "AAAABBBBCCCC")
        devs = hwtest.parse_dfu_list(REAL_LIST_USB + second)
        self.assertEqual([d.serial for d in devs], ["315A356C3435", "AAAABBBBCCCC"])
        self.assertEqual([d.index for d in devs], ["USB1", "USB2"])
        self.assertEqual(hwtest.parse_dfu_list("=====  DFU Interface   =====\n\nNo STM32 device in DFU mode connected\n"), [])

    def test_connect_info(self) -> None:
        text = ("USB speed   : Full Speed (12MBit/s)\nManuf. ID   : STMicroelectronics\n"
                "Product ID  : DFU in FS Mode\nSN          : 315A356C3435\nDFU protocol: 1.1\n"
                "Board       : --\nDevice ID   : 0x0431\nDevice name : STM32F411xC/E\n"
                "Flash size  : 512 KBytes (default)\n\nOPTION BYTES BANK: 0\n\n Read Out Protection:\n"
                "   RDP          : 0xAA (Level 0, no protection)\n")
        info = hwtest.parse_connect_info(text)
        self.assertEqual(info["device id"], "0x0431")
        self.assertEqual(info["dfu protocol"], "1.1")
        self.assertEqual(info["device name"], "STM32F411xC/E")
        self.assertTrue(any("RDP" in ln for ln in info["option_bytes"]))

    def test_pnp_parse(self) -> None:
        txt = ("Instance ID: USB\\VID_0483&PID_DF11\\315A356C3435\nInstance ID: USB\\VID_0483&PID_5740\\X\n"
               "USB\\VID_0483&PID_DF11\\315A356C3435\n")
        self.assertEqual(hwtest.parse_pnp_dfu(txt), ["315A356C3435"])

    def test_safety_guard(self) -> None:
        ok = [["-c", "port=USB1", "sn=X", "-w", "a.bin", "0x08000000", "-v", "-s", "0x08000000"],
              ["-c", "port=USB1", "sn=X", "-u", "0x08000000", "0x400", "o.bin"],
              ["-c", "port=USB1", "sn=X", "-ob", "displ"]]
        for a in ok:
            hwtest.check_cli_args_safe(a)
        bad = [["-c", "port=USB1", "sn=X", "-e", "all"],
               ["-c", "port=USB1", "sn=X", "--erase", "all"],
               ["-c", "port=USB1", "sn=X", "-ob", "RDP=0xBB"],
               ["-c", "port=USB1", "sn=X", "-ob", "displ", "BOR_LEV=0"],
               ["-c", "port=USB1", "sn=X", "-rdu"],
               ["-c", "port=USB1", "sn=X", "-w32", "0x08000000", "0"],
               ["-c", "port=USB1", "-w", "a.bin", "0x08000000"]]
        for a in bad:
            with self.assertRaises(hwtest.UnsafeCliError, msg=str(a)):
                hwtest.check_cli_args_safe(a)

    def test_run_process_cr_lines_errors_and_timeout(self) -> None:
        code = "import sys; sys.stdout.write('a 10%\\r b 100%\\r\\nError: boom\\nok\\n')"
        seen: list[str] = []
        r = hwtest.run_process([sys.executable, "-c", code], 10, seen.append)
        self.assertEqual(r.rc, 0)
        self.assertEqual(r.errors, ["Error: boom"])
        self.assertFalse(r.ok)
        self.assertIn("ok", seen)
        r = hwtest.run_process([sys.executable, "-c", "import time; time.sleep(30)"], 0.5)
        self.assertTrue(r.timed_out)

    def test_key_cli_lines_strip_junk(self) -> None:
        lines = ["\x08\x08\x08  \u2588\u2588\u2588\u2588 50%", "File download complete",
                 "Time elapsed during download operation: 00:00:05.123", "Error: x", "noise"]
        k = hwtest.key_cli_lines(lines)
        self.assertEqual(k, ["File download complete",
                             "Time elapsed during download operation: 00:00:05.123", "Error: x"])
        self.assertEqual(hwtest.clean_text("a\x08\x08b\x1b c"), "ab c")


class TestMisc(unittest.TestCase):
    def test_broken_image(self) -> None:
        img = hwtest.make_broken_image()
        self.assertEqual(len(img), 1024)
        self.assertEqual(img[0:8], bytes([0x00, 0x00, 0x02, 0x20, 0x01, 0x01, 0x00, 0x08]))
        self.assertEqual(img[0x100:0x102], b"\xfe\xe7")
        self.assertEqual(set(img[8:0x100]) | set(img[0x102:]), {0xFF})

    def test_identity(self) -> None:
        self.assertTrue(hwtest.identity_matches(INFO, {"fw": "abc1234", "build": 1763000000}))
        self.assertFalse(hwtest.identity_matches(INFO, {"fw": "abc1234", "build": 1}))
        self.assertTrue(hwtest.identity_matches(INFO, None))
        self.assertFalse(hwtest.identity_matches(None, None))

    def test_cycle_plan_and_summary(self) -> None:
        plan = [hwtest.cycle_trigger(i) for i in range(10)]
        self.assertEqual(plan[0], ("cmd", False))
        self.assertEqual(plan[1], ("touch", False))
        self.assertEqual(plan[4], ("touch", True))
        self.assertEqual(plan[9], ("touch", True))
        cycles = [{"cycle": 1, "trigger": "cmd", "app_to_dfu_s": 1.0, "dfu_to_app_s": 2.0, "op": "write+verify",
                   "program_s": 5.0, "pass": True},
                  {"cycle": 2, "trigger": "touch", "app_to_dfu_s": 0.5, "dfu_to_app_s": None, "op": "start",
                   "pass": False, "reasons": ["x"]}]
        s = hwtest.summarize_cycles(cycles)
        self.assertEqual((s["passed"], s["failed"]), (1, 1))
        self.assertEqual(s["app_to_dfu"]["median"], 0.75)
        self.assertEqual(s["dfu_to_app"]["n"], 1)
        self.assertEqual(s["failures"], [{"cycle": 2, "reasons": ["x"]}])

    def test_overclock_detection(self) -> None:
        self.assertTrue(hwtest.looks_like_overclock("oc_sweep"))
        self.assertTrue(hwtest.looks_like_overclock("overclock"))
        self.assertFalse(hwtest.looks_like_overclock("selftest"))


class TestLock(unittest.TestCase):
    def setUp(self) -> None:
        self.path = TMP / "test.lock"
        if self.path.exists():
            self.path.unlink()

    def test_acquire_release(self) -> None:
        with hwtest.BoardLock("t", path=self.path):
            self.assertEqual(json.loads(self.path.read_text())["pid"], os.getpid())
        self.assertFalse(self.path.exists())

    def test_stale_lock_removed(self) -> None:
        p = subprocess.Popen([sys.executable, "-c", "pass"])
        p.wait()
        self.path.write_text(json.dumps({"pid": p.pid, "cmd": "old"}))
        self.assertFalse(hwtest.pid_alive(p.pid))
        with hwtest.BoardLock("t", path=self.path):
            pass

    def test_busy_lock(self) -> None:
        p = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(20)"])
        try:
            self.path.write_text(json.dumps({"pid": p.pid, "cmd": "other agent"}))
            self.assertTrue(hwtest.pid_alive(p.pid))
            with self.assertRaises(hwtest.LockBusy):
                hwtest.BoardLock("t", wait=0.6, path=self.path).acquire()
        finally:
            p.kill()
            p.wait()
        with hwtest.BoardLock("t", wait=2, path=self.path):  # holder died -> stale
            pass


class TestHwLog(unittest.TestCase):
    def test_append_only_format(self) -> None:
        p = TMP / "log.md"
        p.write_text("# Log\n\n### old entry\n- keep me\n", encoding="utf-8")
        log = hwtest.HwLog(p)
        log.entry("Flash firmware", "STM32_Programmer_CLI -c port=USB1 sn=X", "OK (exit code 0)",
                  ["Output: \x08\x08File download complete"])
        log.entry("Second", None, "FAIL")
        text = p.read_text(encoding="utf-8")
        self.assertTrue(text.startswith("# Log\n\n### old entry\n- keep me\n\n### "))
        self.assertRegex(text, r"### \d{4}-\d\d-\d\d \d\d:\d\d:\d\d \u2014 Flash firmware \[auto\]\n\n"
                               r"- Command: `STM32_Programmer_CLI -c port=USB1 sn=X`\n"
                               r"- Result: OK \(exit code 0\)\n- Output: File download complete\n")
        self.assertIn("\n\n### ", text.split("Flash firmware")[1])
        log.enabled = False
        log.entry("x")
        self.assertEqual(p.read_text(encoding="utf-8"), text)


class TestCliEntry(unittest.TestCase):
    def run_tool(self, *args: str) -> subprocess.CompletedProcess:
        env = dict(os.environ, HWTEST_LOG=str(TMP / "cli_log.md"), HWTEST_LOCK=str(TMP / "cli.lock"))
        return subprocess.run([sys.executable, str(HERE / "hwtest.py"), *args], capture_output=True,
                              text=True, encoding="utf-8", errors="replace", env=env, timeout=120)

    def test_help(self) -> None:
        for sub in ([], ["ports"], ["info"], ["cmd"], ["monitor"], ["run"], ["dfu"], ["flash"],
                    ["dfu-test"], ["lock-status"]):
            r = self.run_tool(*sub, "--help")
            self.assertEqual(r.returncode, 0, f"{sub}: {r.stderr}")
            self.assertIn("usage:", r.stdout)

    def test_parser(self) -> None:
        p = hwtest.build_parser()
        a = p.parse_args(["run", "spin", "8000", "--timeout", "5", "--json"])
        self.assertEqual((a.suite, a.args, a.timeout, a.json), ("spin", ["8000"], 5.0, True))
        a = p.parse_args(["flash", "x.bin", "--no-verify-id", "--wait-lock"])
        self.assertEqual((a.image, a.no_verify_id, a.wait_lock), ("x.bin", True, 600.0))
        a = p.parse_args(["dfu-test", "--cycles", "3", "--manual", "--no-log", "--log", "z.md"])
        self.assertEqual((a.cycles, a.manual, a.no_log, a.log), (3, True, True, "z.md"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
