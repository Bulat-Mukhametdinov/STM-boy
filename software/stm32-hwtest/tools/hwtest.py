#!/usr/bin/env python3
"""hwtest.py - host tool for the stm32-hwtest firmware (STM-boy).

Implements the host side of ../PROTOCOL.md: port discovery, command/response,
test-suite runs with result collection, DFU entry (``dfu`` command or 1200-baud
touch), flashing through STM32_Programmer_CLI (USB DFU) and a DFU reliability
test.

Only needs Python 3.9+ and pyserial.  Run ``hwtest.py --help``.

Exit codes: 0 ok/pass, 1 fail, 2 abort/timeout/no device/error, 3 board locked.

Safety: this tool never writes option bytes, never mass-erases and always
passes ``sn=<serial>`` to STM32_Programmer_CLI (enforced in check_cli_args_safe).
"""
from __future__ import annotations

import argparse
import json
import os
import platform
import queue
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import threading
import time
import zlib
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Any, Callable, Iterable, Optional

try:  # pyserial is required for hardware access but not for --help / parsing.
    import serial  # type: ignore
    from serial.tools import list_ports  # type: ignore
    SerialException: type = serial.SerialException
except ImportError:  # pragma: no cover
    serial = None  # type: ignore
    list_ports = None  # type: ignore

    class SerialException(Exception):  # type: ignore[no-redef]
        pass

# --------------------------------------------------------------------------
# Constants
# --------------------------------------------------------------------------
VID = 0x0483
PID_CDC = 0x5740
PID_DFU = 0xDF11
FLASH_BASE = 0x08000000
BAUD = 115200

TOOLS_DIR = Path(__file__).resolve().parent
PROJECT_DIR = TOOLS_DIR.parent
DEFAULT_BIN = PROJECT_DIR / "build" / "hwtest.bin"
RESULTS_DIR = TOOLS_DIR / "results"
LOCK_PATH = Path(os.environ.get("HWTEST_LOCK", str(TOOLS_DIR / ".board.lock")))
CLI_EXE = os.environ.get("STM32_PROGRAMMER_CLI", "STM32_Programmer_CLI")

EXIT_OK, EXIT_FAIL, EXIT_ABORT, EXIT_LOCKED = 0, 1, 2, 3

BOOT0_HINT = "Hold BOOT0, tap RESET, release BOOT0"
DRIVER_HINT = ("Windows sees 0483:DF11 but STM32_Programmer_CLI does not: in Device Manager "
               "switch 'STM32 Bootloader' back to the ST driver (oem25.inf) instead of "
               "libwdi/libusbK/libusb-win32/WinUSB.")


# --------------------------------------------------------------------------
# Console output: human text to stdout (stderr in --json mode), JSON summary
# to stdout.
# --------------------------------------------------------------------------
class Console:
    json_mode: bool = False

    def say(self, msg: str = "") -> None:
        stream = sys.stderr if self.json_mode else sys.stdout
        print(msg, file=stream, flush=True)

    def warn(self, msg: str) -> None:
        print(f"WARNING: {msg}", file=sys.stderr, flush=True)

    def error(self, msg: str) -> None:
        print(f"ERROR: {msg}", file=sys.stderr, flush=True)

    def emit(self, obj: Any) -> None:
        """Final machine-readable summary (only in --json mode)."""
        if self.json_mode:
            print(json.dumps(obj, default=str), flush=True)


OUT = Console()


# --------------------------------------------------------------------------
# Hardware work log (../HW_LOG.md, append-only, human readable)
# --------------------------------------------------------------------------
CTRL_RE = re.compile(r"[\x00-\x08\x0b-\x1f\x7f]")


def clean_text(s: str) -> str:
    """Strip backspaces/progress-bar junk and control characters."""
    s = CTRL_RE.sub("", s)
    s = re.sub(r"[▀-▟]+", "", s)       # block characters of progress bars
    return re.sub(r"\s{2,}", " ", s).strip()


class HwLog:
    """Appends ``### <time> — <action> [auto]`` entries to HW_LOG.md.  Never rewrites."""

    def __init__(self, path: Optional[Path] = None, enabled: bool = True) -> None:
        self.path = path or Path(os.environ.get("HWTEST_LOG", str(PROJECT_DIR / "HW_LOG.md")))
        self.enabled = enabled

    def entry(self, action: str, command: Optional[str] = None, result: Optional[str] = None,
              details: Iterable[str] = ()) -> None:
        if not self.enabled:
            return
        ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        lines = [f"### {ts} — {clean_text(action)} [auto]", ""]
        if command:
            lines.append(f"- Command: `{clean_text(command)}`")
        if result:
            lines.append(f"- Result: {clean_text(result)}")
        for d in details:
            d = clean_text(str(d))
            if d:
                lines.append(f"- {d}")
        text = "\n".join(lines) + "\n"
        try:
            prefix = ""
            size = self.path.stat().st_size if self.path.exists() else 0
            if size > 0:
                with open(self.path, "rb") as f:
                    f.seek(max(0, size - 2))
                    tail = f.read().replace(b"\r", b"")
                prefix = "" if tail.endswith(b"\n\n") else ("\n" if tail.endswith(b"\n") else "\n\n")
            with open(self.path, "a", encoding="utf-8", newline="\n") as f:
                f.write(prefix + text)
        except OSError as e:
            OUT.warn(f"cannot append to hardware log {self.path}: {e}")


HWLOG = HwLog()
SESSION_NOTES: list[str] = []     # summary bullets for the per-invocation log entry


def note(msg: str) -> None:
    SESSION_NOTES.append(msg)


# --------------------------------------------------------------------------
# Errors
# --------------------------------------------------------------------------
class HwtestError(Exception):
    exit_code = EXIT_ABORT


class NoDevice(HwtestError):
    exit_code = EXIT_ABORT


class DeviceDisconnected(HwtestError):
    exit_code = EXIT_ABORT


class LockBusy(HwtestError):
    exit_code = EXIT_LOCKED


class UnsafeCliError(HwtestError):
    exit_code = EXIT_ABORT


class CliNotFound(HwtestError):
    exit_code = EXIT_ABORT


# --------------------------------------------------------------------------
# Protocol: message parsing
# --------------------------------------------------------------------------
JSON_TAGS = ("HELLO", "R", "P", "A")
B_HEADER_RE = re.compile(r"^@B (\d{1,9}) ([0-9A-Fa-f]{1,8})\s*$")
MAX_LINE = 8192
MAX_BINARY = 64 * 1024 * 1024


@dataclass
class Msg:
    kind: str                 # log | HELLO | OK | ERR | R | P | A | DONE | B | other
    text: str                 # the raw line (for B: the header line)
    cmd: str = ""             # OK/ERR: command; DONE: suite
    arg: str = ""             # ERR: message; DONE: pass|fail|abort
    obj: Any = None           # parsed JSON (HELLO/R/P/A)
    data: Optional[bytes] = None
    crc: Optional[int] = None
    crc_ok: Optional[bool] = None
    t: float = 0.0            # host monotonic receive time

    @property
    def terminal(self) -> bool:
        return self.kind in ("OK", "ERR", "DONE")

    @property
    def cmd_token(self) -> str:
        return self.cmd.split()[0] if self.cmd.split() else ""


def parse_line(text: str) -> Msg:
    """Parse one device line (without line terminator)."""
    if not text.startswith("@") or len(text) < 2:
        return Msg("log", text)
    tag, _, rest = text[1:].partition(" ")
    rest = rest.strip()
    if tag in JSON_TAGS:
        try:
            obj = json.loads(rest) if rest else None
        except ValueError:
            obj = None
        return Msg(tag, text, obj=obj)
    if tag == "OK":
        return Msg("OK", text, cmd=rest)
    if tag == "ERR":
        cmd, _, msg = rest.partition(" ")
        return Msg("ERR", text, cmd=cmd, arg=msg.strip())
    if tag == "DONE":
        parts = rest.split()
        if not parts:
            return Msg("DONE", text, arg="")
        if len(parts) == 1:
            return Msg("DONE", text, cmd="", arg=parts[0])
        return Msg("DONE", text, cmd=" ".join(parts[:-1]), arg=parts[-1])
    return Msg("other", text)


def parse_b_header(text: str) -> Optional[tuple[int, int]]:
    m = B_HEADER_RE.match(text)
    if not m:
        return None
    length = int(m.group(1))
    if length > MAX_BINARY:
        return None
    return length, int(m.group(2), 16)


class LineReader:
    """Byte stream -> Msg.  Handles ``@B <len> <crc>`` binary frames by length."""

    def __init__(self, port: Any, clock: Callable[[], float] = time.monotonic) -> None:
        self.port = port
        self.buf = bytearray()
        self.pending_b: Optional[tuple[int, int, str]] = None
        self.clock = clock

    def _fill(self, wait: float) -> None:
        try:
            n = self.port.in_waiting
            data = self.port.read(n if n else 1)
        except (SerialException, OSError, AttributeError, TypeError, ValueError) as e:
            raise DeviceDisconnected(f"serial port lost: {e}") from e
        if data:
            self.buf += data
        elif wait > 0 and not getattr(self.port, "timeout", None):
            time.sleep(min(wait, 0.005))

    def _try_parse(self) -> Optional[Msg]:
        if self.pending_b is not None:
            length, crc, header = self.pending_b
            if len(self.buf) < length:
                return None
            data = bytes(self.buf[:length])
            del self.buf[:length]
            self.pending_b = None
            return Msg("B", header, data=data, crc=crc,
                       crc_ok=(zlib.crc32(data) & 0xFFFFFFFF) == crc, t=self.clock())
        i = self.buf.find(b"\n")
        if i < 0:
            if len(self.buf) > MAX_LINE:  # garbage without newline: flush as log
                raw = bytes(self.buf)
                self.buf.clear()
                return Msg("log", raw.decode("ascii", errors="replace"), t=self.clock())
            return None
        raw = bytes(self.buf[:i])
        del self.buf[:i + 1]
        text = raw.rstrip(b"\r").decode("ascii", errors="replace")
        if text.startswith("@B "):
            hdr = parse_b_header(text)
            if hdr is not None:
                self.pending_b = (hdr[0], hdr[1], text)
                return self._try_parse()
            return Msg("log", text, t=self.clock())
        m = parse_line(text)
        m.t = self.clock()
        return m

    def read_msg(self, timeout: float) -> Optional[Msg]:
        deadline = self.clock() + max(0.0, timeout)
        while True:
            m = self._try_parse()
            if m is not None:
                return m
            remaining = deadline - self.clock()
            if remaining <= 0:
                return None
            self._fill(remaining)


# --------------------------------------------------------------------------
# Device (CDC)
# --------------------------------------------------------------------------
@dataclass
class Transaction:
    cmd: str
    status: str                       # ok | err | done | timeout
    msgs: list[Msg] = field(default_factory=list)
    final: Optional[Msg] = None
    note: str = ""

    @property
    def results(self) -> list[Any]:
        return [m.obj for m in self.msgs if m.kind == "R"]


class Device:
    def __init__(self, port: Any, name: str = "?") -> None:
        self.port = port
        self.name = name
        self.reader = LineReader(port)
        self.hello: Any = None

    @classmethod
    def open(cls, port_name: str) -> "Device":
        if serial is None:
            raise HwtestError("pyserial is not installed (py -3.14 -m pip install pyserial)")
        try:
            s = serial.Serial(port_name, BAUD, timeout=0.05, write_timeout=2)
            s.dtr = True
        except (SerialException, OSError, ValueError) as e:
            raise DeviceDisconnected(f"cannot open {port_name}: {e}") from e
        return cls(s, port_name)

    def close(self) -> None:
        try:
            self.port.close()
        except Exception:
            pass

    def __enter__(self) -> "Device":
        return self

    def __exit__(self, *exc: Any) -> None:
        self.close()

    def send(self, line: str) -> None:
        data = (line.strip() + "\n").encode("ascii", errors="replace")
        try:
            self.port.write(data)
            flush = getattr(self.port, "flush", None)
            if flush:
                flush()
        except (SerialException, OSError, ValueError) as e:
            raise DeviceDisconnected(f"write to {self.name} failed: {e}") from e

    def read_msg(self, timeout: float) -> Optional[Msg]:
        m = self.reader.read_msg(timeout)
        if m is not None and m.kind == "HELLO":
            self.hello = m.obj
        return m

    def drain(self, duration: float = 0.1) -> list[Msg]:
        out: list[Msg] = []
        end = time.monotonic() + duration
        while time.monotonic() < end:
            m = self.read_msg(max(0.0, end - time.monotonic()))
            if m is not None:
                out.append(m)
        return out

    def transact(self, cmd: str, timeout: float = 5.0,
                 on_msg: Optional[Callable[[Msg], None]] = None) -> Transaction:
        """Send ``cmd`` and collect lines until its @OK/@ERR (or @DONE for run)."""
        token = cmd.split()[0] if cmd.split() else ""
        self.send(cmd)
        tx = Transaction(cmd, "timeout")
        stray: Optional[Msg] = None
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            m = self.read_msg(min(remaining, 0.25))
            if m is None:
                continue
            tx.msgs.append(m)
            if on_msg:
                on_msg(m)
            if m.kind in ("OK", "ERR"):
                if m.cmd_token == token or m.cmd.strip() == cmd.strip():
                    if token == "run" and m.kind == "OK":
                        continue  # a run ends with @DONE
                    tx.status, tx.final = ("ok" if m.kind == "OK" else "err"), m
                    return tx
                stray = m
            elif m.kind == "DONE" and token in ("run", "abort"):
                tx.status, tx.final = "done", m
                return tx
        if stray is not None:
            tx.status, tx.final = ("ok" if stray.kind == "OK" else "err"), stray
            tx.note = f"accepted non-matching terminal line {stray.text!r}"
        return tx

    def info(self, timeout: float = 3.0) -> Optional[dict]:
        tx = self.transact("info", timeout)
        for r in tx.results:
            if isinstance(r, dict) and r.get("t") == "info":
                return r
        return None


# --------------------------------------------------------------------------
# Port discovery (CDC)
# --------------------------------------------------------------------------
@dataclass
class CdcPort:
    device: str
    serial: Optional[str]
    description: str = ""
    hwid: str = ""

    def to_json(self) -> dict:
        return {"port": self.device, "serial": self.serial, "vid": f"{VID:04X}",
                "pid": f"{PID_CDC:04X}", "description": self.description}


def find_cdc_ports(serial_no: Optional[str] = None) -> list[CdcPort]:
    if list_ports is None:
        return []
    out: list[CdcPort] = []
    try:
        ports = list_ports.comports()
    except Exception:
        return []
    for p in ports:
        if p.vid == VID and p.pid == PID_CDC:
            if serial_no and (p.serial_number or "").lower() != serial_no.lower():
                continue
            out.append(CdcPort(p.device, p.serial_number, p.description or "", p.hwid or ""))
    return sorted(out, key=lambda c: c.device)


def resolve_cdc(port: Optional[str], serial_no: Optional[str]) -> CdcPort:
    ports = find_cdc_ports()
    if port:
        for c in ports:
            if c.device.lower() == port.lower():
                return c
        return CdcPort(port, serial_no)
    if serial_no:
        ports = [c for c in ports if (c.serial or "").lower() == serial_no.lower()]
        if not ports:
            raise NoDevice(f"no CDC port 0483:5740 with serial {serial_no}")
    if not ports:
        raise NoDevice("no STM-boy CDC port (0483:5740) found")
    if len(ports) > 1:
        lst = ", ".join(f"{c.device}(sn={c.serial})" for c in ports)
        raise HwtestError(f"several boards found: {lst}; pick one with --serial/--port")
    return ports[0]


def connect(cdc: CdcPort, timeout: float = 3.0) -> Device:
    """Open the CDC port, retrying while it is (re)enumerating."""
    deadline = time.monotonic() + timeout
    last: Exception = NoDevice(f"{cdc.device} not present")
    while True:
        name = cdc.device
        if cdc.serial:
            found = find_cdc_ports(cdc.serial)
            name = found[0].device if found else ""
        if name:
            try:
                return Device.open(name)
            except DeviceDisconnected as e:
                last = e
        if time.monotonic() >= deadline:
            raise last if isinstance(last, HwtestError) else NoDevice(str(last))
        time.sleep(0.2)


def wait_for_cdc_port(serial_no: Optional[str], timeout: float, poll: float = 0.2) -> Optional[CdcPort]:
    deadline = time.monotonic() + timeout
    while True:
        ports = find_cdc_ports(serial_no)
        if ports:
            return ports[0]
        if time.monotonic() >= deadline:
            return None
        time.sleep(poll)


# --------------------------------------------------------------------------
# Subprocess / STM32_Programmer_CLI
# --------------------------------------------------------------------------
@dataclass
class ProcResult:
    rc: int
    output: str
    lines: list[str]
    duration: float
    timed_out: bool = False
    errors: list[str] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return self.rc == 0 and not self.errors and not self.timed_out

    def has(self, needle: str) -> bool:
        n = needle.lower()
        return any(n in ln.lower() for ln in self.lines)


ERROR_LINE_RE = re.compile(r"^\s*error\b", re.I)
PROGRESS_RE = re.compile(r"(\d{1,3})\s*%\s*$")


def split_output_lines(text: str) -> list[str]:
    return [ln for ln in re.split(r"[\r\n]+", text) if ln.strip()]


def run_process(cmdline: list[str], timeout: float = 120.0,
                stream: Optional[Callable[[str], None]] = None) -> ProcResult:
    """Run a process, stream its output line by line (CR or LF), kill on timeout."""
    t0 = time.monotonic()
    try:
        proc = subprocess.Popen(cmdline, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL)
    except FileNotFoundError as e:
        raise CliNotFound(f"{cmdline[0]} not found on PATH") from e
    chunks: list[bytes] = []

    def pump() -> None:
        pending = b""
        assert proc.stdout is not None
        while True:
            b = proc.stdout.read1(4096) if hasattr(proc.stdout, "read1") else proc.stdout.read(1)
            if not b:
                break
            chunks.append(b)
            pending += b
            parts = re.split(rb"[\r\n]", pending)
            pending = parts.pop()
            for p in parts:
                if stream and p.strip():
                    stream(p.decode("utf-8", errors="replace"))
        if stream and pending.strip():
            stream(pending.decode("utf-8", errors="replace"))

    th = threading.Thread(target=pump, daemon=True)
    th.start()
    timed_out = False
    try:
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        proc.kill()
        proc.wait()
    th.join(timeout=5)
    if proc.stdout is not None:
        proc.stdout.close()
    text = b"".join(chunks).decode("utf-8", errors="replace")
    lines = split_output_lines(text)
    errors = [clean_text(ln) for ln in lines if ERROR_LINE_RE.match(clean_text(ln))]
    return ProcResult(proc.returncode if proc.returncode is not None else -1, text, lines,
                      time.monotonic() - t0, timed_out, errors)


FORBIDDEN_CLI = {"-e", "--erase", "-rdu", "--readunprotect", "-w8", "-w16", "-w32", "-w64",
                 "-sl", "--safelib", "-ms", "--mergesbsfu", "-otp", "--otp", "-wdu", "-ssigfoxc",
                 "-sfi", "--sfi", "-ssp", "--ssp", "-rst", "-hardrst", "-tzenreg", "-wl"}


def check_cli_args_safe(args: list[str], require_sn: bool = True) -> None:
    """Refuse anything that could erase everything or touch option bytes."""
    lowered = [a.lower() for a in args]
    for i, a in enumerate(lowered):
        if a in FORBIDDEN_CLI:
            raise UnsafeCliError(f"refusing STM32_Programmer_CLI argument {args[i]!r}")
        if a in ("-ob", "--optionbytes"):
            nxt = lowered[i + 1] if i + 1 < len(lowered) else ""
            if nxt != "displ":
                raise UnsafeCliError("option bytes may only be displayed (-ob displ), never written")
            if any(x.startswith(("rdp=", "wrp", "nwrp", "bor_lev", "iwdg", "nrst", "spr")) for x in lowered[i + 2:]):
                raise UnsafeCliError("option byte assignment detected; refusing")
    if require_sn and ("-c" in lowered or "--connect" in lowered):
        if not any(a.startswith("sn=") for a in lowered):
            raise UnsafeCliError("STM32_Programmer_CLI connect without sn=<serial> refused")


def cli_available() -> Optional[str]:
    return shutil.which(CLI_EXE)


def _progress_filter() -> Callable[[str], None]:
    """Print CLI lines, but collapse progress-bar updates to the 100% line."""
    def show(line: str) -> None:
        s = clean_text(line)
        if not s:
            return
        m = PROGRESS_RE.search(s)
        if m and ("█" in s or len(re.sub(r"[\s\d%▀-▟#=\-\[\]|]", "", s)) == 0):
            if m.group(1) != "100":
                return
        OUT.say("  | " + s.strip())
    return show


KEY_CLI_WORDS = ("error", "warning", "success", "complete", "verified", "device id", "device name",
                 "serial number", "flash size", "time elapsed", "start operation", "no stm32 device",
                 "total number", "dfu protocol", "rdp", "bl version", "firmware version")


def key_cli_lines(lines: list[str], limit: int = 12) -> list[str]:
    out: list[str] = []
    for ln in lines:
        c = clean_text(ln)
        if c and not PROGRESS_RE.search(c) and any(w in c.lower() for w in KEY_CLI_WORDS):
            if c not in out:
                out.append(c)
    return out[:limit]


def run_cli(args: list[str], timeout: float = 180.0, stream: bool = True,
            require_sn: bool = True, action: str = "STM32_Programmer_CLI") -> ProcResult:
    try:
        check_cli_args_safe(args, require_sn=require_sn)
    except UnsafeCliError as e:
        HWLOG.entry(f"{action} (refused)", subprocess.list2cmdline([CLI_EXE, *args]), f"ERROR {e}")
        raise
    try:
        res = run_process([CLI_EXE, *args], timeout, _progress_filter() if stream else None)
    except CliNotFound as e:
        HWLOG.entry(action, subprocess.list2cmdline([CLI_EXE, *args]), f"ERROR {e}")
        raise
    verdict = "OK" if res.ok else "FAIL"
    HWLOG.entry(action, subprocess.list2cmdline([CLI_EXE, *args]),
                f"{verdict} (exit code {res.rc}, {res.duration:.1f} s{', TIMED OUT' if res.timed_out else ''})",
                [f"Output: {ln}" for ln in key_cli_lines(res.lines)])
    return res


# --------------------------------------------------------------------------
# DFU discovery
# --------------------------------------------------------------------------
@dataclass
class DfuDevice:
    index: Optional[str]
    serial: Optional[str]
    source: str                         # "cli" or "pnp"
    fields: dict = field(default_factory=dict)

    def to_json(self) -> dict:
        return {"index": self.index, "serial": self.serial, "source": self.source,
                "fields": self.fields}


KV_RE = re.compile(r"^\s*([A-Za-z][A-Za-z0-9 .()/_#-]*?)\s*:\s*(.*?)\s*$")


def parse_kv(text: str) -> list[tuple[str, str]]:
    out = []
    for ln in split_output_lines(text):
        m = KV_RE.match(ln)
        if m:
            out.append((m.group(1).strip().lower(), m.group(2)))
    return out


def parse_dfu_list(text: str) -> list[DfuDevice]:
    """Parse ``STM32_Programmer_CLI -l usb`` output (format-agnostic key:value blocks)."""
    blocks: list[dict] = []
    cur: Optional[dict] = None
    for key, val in parse_kv(text):
        if "device index" in key or key == "index":
            cur = {}
            blocks.append(cur)
        if cur is None:
            if "serial" in key or key == "sn":
                cur = {}
                blocks.append(cur)
            else:
                continue
        if key in cur and ("serial" in key or key == "sn"):
            cur = {}
            blocks.append(cur)
        cur.setdefault(key, val)
    devs: list[DfuDevice] = []
    for b in blocks:
        index = None
        for k, v in b.items():
            if "index" in k:
                m = re.search(r"usb\d+", v, re.I)
                if m:
                    index = m.group(0).upper()
        sn = None
        for k, v in b.items():
            if "serial" in k or k == "sn":
                sn = v.split()[0] if v.split() else None
                break
        if index is None and sn is None:
            continue
        devs.append(DfuDevice(index, sn, "cli", dict(b)))
    return devs


def list_dfu_cli(timeout: float = 20.0) -> list[DfuDevice]:
    res = run_process([CLI_EXE, "-l", "usb"], timeout)
    return parse_dfu_list(res.output)


PNP_DFU_RE = re.compile(r"USB\\VID_0483&PID_DF11\\([^\s\\&]+)", re.I)


def parse_pnp_dfu(text: str) -> list[str]:
    seen: list[str] = []
    for s in PNP_DFU_RE.findall(text):
        if s not in seen:
            seen.append(s)
    return seen


def pnp_dfu_serials() -> Optional[list[str]]:
    """Serials of present 0483:DF11 devices according to Windows PnP (None if unknown)."""
    if os.name != "nt":
        return None
    for cmd in (["pnputil", "/enum-devices", "/connected"],
                ["powershell", "-NoProfile", "-Command",
                 "Get-PnpDevice -PresentOnly | ? InstanceId -match 'VID_0483&PID_DF11' | % InstanceId"]):
        try:
            r = subprocess.run(cmd, capture_output=True, timeout=20)
        except (OSError, subprocess.TimeoutExpired):
            continue
        if r.returncode == 0:
            return parse_pnp_dfu(r.stdout.decode("utf-8", errors="replace"))
    return None


def find_dfu(serial_no: Optional[str] = None, pnp_fallback: bool = False) -> list[DfuDevice]:
    devs: list[DfuDevice] = []
    try:
        devs = list_dfu_cli()
    except CliNotFound:
        pnp_fallback = True
    if not devs and pnp_fallback:
        devs = [DfuDevice(None, s, "pnp") for s in (pnp_dfu_serials() or [])]
    if serial_no:
        devs = [d for d in devs if (d.serial or "").lower() == serial_no.lower()]
    return devs


def wait_for_dfu(serial_no: Optional[str], timeout: float, poll: float = 0.1) -> Optional[DfuDevice]:
    deadline = time.monotonic() + timeout
    cli_ok = cli_available() is not None
    while True:
        devs = find_dfu(serial_no, pnp_fallback=not cli_ok)
        if devs:
            return devs[0]
        if time.monotonic() >= deadline:
            break
        time.sleep(poll)
    # Diagnostics: PnP sees it but the CLI does not -> driver problem.
    pnp = pnp_dfu_serials() or []
    if cli_ok and pnp and (not serial_no or any(s.lower() == serial_no.lower() for s in pnp)):
        OUT.warn(DRIVER_HINT)
    return None


def connect_args(dfu: DfuDevice) -> list[str]:
    if not dfu.serial:
        raise HwtestError("DFU device has no serial number; refusing to connect without sn=")
    return ["-c", f"port={dfu.index or 'USB1'}", f"sn={dfu.serial}"]


def program(dfu: DfuDevice, image: Path, verify: bool = True, start: bool = True,
            stream: bool = True) -> ProcResult:
    args = connect_args(dfu) + ["-w", str(image)]
    if image.suffix.lower() == ".bin":
        args.append(f"0x{FLASH_BASE:08X}")
    if verify:
        args.append("-v")
    if start:
        args += ["-s", f"0x{FLASH_BASE:08X}"]
    return run_cli(args, timeout=300, stream=stream)


def start_app(dfu: DfuDevice, stream: bool = True) -> ProcResult:
    return run_cli(connect_args(dfu) + ["-s", f"0x{FLASH_BASE:08X}"], timeout=60, stream=stream)


def upload(dfu: DfuDevice, addr: int, size: int, out: Path, stream: bool = True) -> ProcResult:
    return run_cli(connect_args(dfu) + ["-u", f"0x{addr:08X}", f"0x{size:X}", str(out)],
                   timeout=300, stream=stream)


INTERESTING_CONNECT_KEYS = ("device id", "device name", "flash size", "bl version", "bootloader",
                            "firmware version", "dfu protocol", "revision id", "sn", "product id",
                            "device type", "device cpu", "board")


def parse_connect_info(text: str) -> dict:
    """Pick bootloader/device fields out of a ``-c port=USBx`` connect output."""
    out: dict = {}
    ob_lines: list[str] = []
    in_ob = False
    for ln in split_output_lines(text):
        if "option bytes" in ln.lower():
            in_ob = True
            continue
        m = KV_RE.match(ln)
        if not m:
            continue
        k, v = m.group(1).strip().lower(), m.group(2)
        if in_ob:
            ob_lines.append(ln.strip())
            continue
        if any(k == ik or k.startswith(ik) for ik in INTERESTING_CONNECT_KEYS) or "version" in k:
            out.setdefault(k, v)
    if ob_lines:
        out["option_bytes"] = ob_lines
    return out


# --------------------------------------------------------------------------
# DFU entry from the application
# --------------------------------------------------------------------------
def send_dfu_command(port_name: str, dev: Optional[Device] = None, timeout: float = 2.0) -> tuple[bool, str]:
    own = dev is None
    try:
        if dev is None:
            dev = Device.open(port_name)
    except HwtestError as e:
        return False, f"open failed: {e}"
    try:
        tx = dev.transact("dfu", timeout)
        if tx.status == "ok":
            return True, "@OK dfu"
        if tx.status == "err" and tx.final is not None:
            return False, tx.final.text
        return False, "no answer to 'dfu'"
    except DeviceDisconnected as e:
        return True, f"port vanished right after 'dfu' (assumed success): {e}"
    finally:
        if own or dev is not None:
            dev.close()


def touch_1200(port_name: str, dev: Optional["Device"] = None) -> str:
    """1200-baud touch.  The device resets inside SET_LINE_CODING, so any
    exception here is expected and counts as success; the DFU wait decides.

    If `dev` is an already-open connection (e.g. a busy device running a
    suite), the line coding is changed on that handle: Windows COM ports are
    exclusive, so a second open would fail and no SET_LINE_CODING would ever
    reach the device."""
    if serial is None:
        raise HwtestError("pyserial is not installed")
    if dev is not None:
        try:
            dev.port.baudrate = 1200
            time.sleep(0.05)
            return "line coding set to 1200 on the open handle"
        except Exception as e:  # noqa: BLE001 - intended
            return f"exception on open handle (expected): {e}"
        finally:
            dev.close()
    try:
        s = serial.Serial(port_name, 1200, timeout=0.2)
        try:
            s.dtr = True
            time.sleep(0.05)
        finally:
            s.close()
        return "opened+closed at 1200"
    except Exception as e:  # noqa: BLE001 - intended
        return f"exception (expected): {e}"


@dataclass
class DfuEntry:
    method: str
    dfu: Optional[DfuDevice]
    seconds: float
    notes: list[str] = field(default_factory=list)

    def to_json(self) -> dict:
        return {"method": self.method, "seconds": round(self.seconds, 3),
                "dfu": self.dfu.to_json() if self.dfu else None, "notes": self.notes}


def enter_dfu(port_name: str, serial_no: Optional[str], method: str = "auto",
              timeout: float = 10.0, dev: Optional[Device] = None) -> DfuEntry:
    """app -> DFU.  method: auto (command, then 1200-touch), cmd, touch."""
    t0 = time.monotonic()
    notes: list[str] = []
    used = ""
    if method in ("auto", "cmd"):
        ok, note = send_dfu_command(port_name, dev)
        dev = None
        notes.append(f"cmd: {note}")
        if ok:
            used = "cmd"
        elif method == "cmd":
            return DfuEntry("cmd", None, time.monotonic() - t0, notes)
    if not used:
        notes.append(f"touch: {touch_1200(port_name, dev)}")
        dev = None
        used = "touch"
    first_wait = timeout if used == "touch" or method == "cmd" else min(timeout, 4.0)
    dfu = wait_for_dfu(serial_no, first_wait - (time.monotonic() - t0))
    if dfu is None and method == "auto" and used == "cmd":
        if find_cdc_ports(serial_no):
            notes.append("DFU did not appear after 'dfu' command; falling back to 1200-touch")
            notes.append(f"touch: {touch_1200(port_name)}")
            used = "cmd+touch"
        dfu = wait_for_dfu(serial_no, max(0.5, timeout - (time.monotonic() - t0)))
    entry = DfuEntry(used, dfu, time.monotonic() - t0, notes)
    HWLOG.entry(f"Enter DFU from application ({used})",
                f"{'dfu command' if used.startswith('cmd') else '1200-baud touch'} on {port_name} (sn={serial_no})",
                (f"OK DFU device {dfu.index} sn={dfu.serial} after {entry.seconds:.2f} s" if dfu
                 else f"FAIL no DFU device within {timeout:.0f} s"),
                notes)
    return entry


# --------------------------------------------------------------------------
# Build identity / app comeback
# --------------------------------------------------------------------------
def load_fw_id(image: Path) -> Optional[dict]:
    p = image.parent / "fw_id.json"
    try:
        return json.loads(p.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


def identity_matches(info: Optional[dict], expect: Optional[dict]) -> bool:
    if not info:
        return False
    if not expect:
        return True
    return str(info.get("fw")) == str(expect.get("fw")) and \
        str(info.get("build")) == str(expect.get("build"))


@dataclass
class AppWait:
    ok: bool
    info: Optional[dict]
    seconds: float
    first_port_s: Optional[float]
    enumerations: int
    attempts: int
    note: str = ""

    def to_json(self) -> dict:
        return {"ok": self.ok, "seconds": round(self.seconds, 3),
                "first_port_s": None if self.first_port_s is None else round(self.first_port_s, 3),
                "enumerations": self.enumerations, "info_attempts": self.attempts,
                "note": self.note, "info": self.info}


def wait_for_app(serial_no: Optional[str], expect: Optional[dict], timeout: float = 15.0) -> AppWait:
    """Wait for the CDC port to come back and ``info`` to report the expected build.
    The port may appear twice (bootloader leave -> clean reset)."""
    t0 = time.monotonic()
    present = False
    enumerations = 0
    first: Optional[float] = None
    attempts = 0
    last_info: Optional[dict] = None
    note = ""
    while True:
        elapsed = time.monotonic() - t0
        ports = find_cdc_ports(serial_no)
        if ports and not present:
            enumerations += 1
            if first is None:
                first = elapsed
        present = bool(ports)
        if ports:
            attempts += 1
            try:
                with Device.open(ports[0].device) as dev:
                    info = dev.info(timeout=2.0)
                if info is not None:
                    last_info = info
                    if identity_matches(info, expect):
                        return AppWait(True, info, time.monotonic() - t0, first, enumerations, attempts)
                    note = (f"running fw={info.get('fw')} build={info.get('build')}, expected "
                            f"fw={expect.get('fw') if expect else '?'} build={expect.get('build') if expect else '?'}")
            except HwtestError as e:
                note = str(e)
                present = bool(find_cdc_ports(serial_no))
        if time.monotonic() - t0 >= timeout:
            return AppWait(False, last_info, time.monotonic() - t0, first, enumerations, attempts,
                           note or "CDC port did not appear")
        time.sleep(0.2)


# --------------------------------------------------------------------------
# Board lock
# --------------------------------------------------------------------------
def pid_alive(pid: int) -> bool:
    if pid <= 0:
        return False
    if os.name == "nt":
        import ctypes
        from ctypes import wintypes
        k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        k32.OpenProcess.restype = wintypes.HANDLE
        k32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        k32.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
        k32.CloseHandle.argtypes = [wintypes.HANDLE]
        h = k32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
        if not h:
            return ctypes.get_last_error() == 5  # ERROR_ACCESS_DENIED -> exists
        try:
            code = wintypes.DWORD()
            if not k32.GetExitCodeProcess(h, ctypes.byref(code)):
                return True
            return code.value == 259  # STILL_ACTIVE
        finally:
            k32.CloseHandle(h)
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def read_lock(path: Path) -> Optional[dict]:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None


class BoardLock:
    def __init__(self, cmd: str, wait: float = 0.0, path: Optional[Path] = None) -> None:
        self.path = path or LOCK_PATH
        self.cmd = cmd
        self.wait = wait
        self.held = False

    def _stale(self) -> bool:
        info = read_lock(self.path)
        if info is None:
            try:
                return time.time() - self.path.stat().st_mtime > 5.0  # corrupt / half-written
            except OSError:
                return False
        return not pid_alive(int(info.get("pid", 0)))

    def acquire(self) -> None:
        deadline = time.monotonic() + max(0.0, self.wait)
        announced = False
        while True:
            try:
                self.path.parent.mkdir(parents=True, exist_ok=True)
                fd = os.open(str(self.path), os.O_CREAT | os.O_EXCL | os.O_WRONLY)
                with os.fdopen(fd, "w", encoding="utf-8") as f:
                    json.dump({"pid": os.getpid(), "cmd": self.cmd,
                               "time": datetime.now().astimezone().isoformat(timespec="seconds"),
                               "host": platform.node()}, f)
                self.held = True
                return
            except FileExistsError:
                if self._stale():
                    try:
                        self.path.unlink()
                    except OSError:
                        pass
                    continue
                holder = read_lock(self.path) or {}
                if time.monotonic() >= deadline:
                    HWLOG.entry("Board lock busy", self.cmd,
                                f"ERROR locked by pid {holder.get('pid')} ({holder.get('cmd')!r} since "
                                f"{holder.get('time')}){' after waiting' if announced else ''}")
                    raise LockBusy(f"board is locked by pid {holder.get('pid')} "
                                   f"({holder.get('cmd')!r} since {holder.get('time')}); "
                                   f"use --wait-lock [SECONDS] to wait. Lock file: {self.path}")
                if not announced:
                    OUT.say(f"waiting for board lock held by pid {holder.get('pid')} ({holder.get('cmd')!r})...")
                    HWLOG.entry("Waiting for board lock", self.cmd,
                                f"waiting up to {self.wait:.0f} s; held by pid {holder.get('pid')} "
                                f"({holder.get('cmd')!r} since {holder.get('time')})")
                    announced = True
                time.sleep(0.5)

    def release(self) -> None:
        if not self.held:
            return
        info = read_lock(self.path)
        if info is None or info.get("pid") == os.getpid():
            try:
                self.path.unlink()
            except OSError:
                pass
        self.held = False

    def __enter__(self) -> "BoardLock":
        self.acquire()
        return self

    def __exit__(self, *exc: Any) -> None:
        self.release()


# --------------------------------------------------------------------------
# Suite runs
# --------------------------------------------------------------------------
@dataclass
class RunOutcome:
    status: str                  # pass | fail | abort | timeout | error | disconnected
    results: list = field(default_factory=list)
    prompts: list = field(default_factory=list)
    answers: list = field(default_factory=list)
    log: list = field(default_factory=list)
    binaries: list = field(default_factory=list)   # (Msg) B frames
    error: str = ""
    done_line: str = ""

    @property
    def exit_code(self) -> int:
        return {"pass": EXIT_OK, "fail": EXIT_FAIL}.get(self.status, EXIT_ABORT)


ANSWER_WORDS = {"a": "ok", "ok": "ok", "y": "ok", "yes": "ok", "pass": "ok",
                "b": "fail", "fail": "fail", "n": "fail", "no": "fail"}


def prompt_banner(obj: Any) -> str:
    text = obj.get("text", "?") if isinstance(obj, dict) else str(obj)
    pid = obj.get("id", "?") if isinstance(obj, dict) else "?"
    answers = ", ".join(obj.get("answers", [])) if isinstance(obj, dict) else ""
    bar = "=" * 72
    return (f"\n{bar}\n  ON DEVICE: {text} — answer with buttons A/B\n"
            f"  (prompt id={pid}{'; ' + answers if answers else ''})\n{bar}")


def format_msg(m: Msg) -> str:
    if m.kind == "B":
        return f"{m.text}  [{len(m.data or b'')} bytes, crc {'OK' if m.crc_ok else 'MISMATCH'}]"
    if m.kind == "R" and isinstance(m.obj, dict) and "pass" in m.obj:
        tag = "PASS" if m.obj.get("pass") else "FAIL"
        return f"[{tag}] {m.text}"
    return m.text


def run_suite(dev: Device, command: str, *, idle_timeout: float = 120.0,
              total_timeout: float = 0.0, prompt_timeout: float = 600.0,
              answers: Optional["queue.Queue[str]"] = None,
              on_msg: Optional[Callable[[Msg], None]] = None,
              on_prompt: Optional[Callable[[Any], None]] = None) -> RunOutcome:
    """Send ``run ...`` and follow it to @DONE.  The idle timeout is suspended
    while a prompt is pending (it waits for a human), bounded by prompt_timeout."""
    out = RunOutcome("timeout")
    pending: dict[str, float] = {}
    order: list[str] = []
    t0 = last = time.monotonic()

    def finish_abort(reason: str) -> None:
        out.error = reason
        try:
            dev.send("abort")
            end = time.monotonic() + 5.0
            while time.monotonic() < end:
                m = dev.read_msg(0.2)
                if m is None:
                    continue
                handle(m)
                if m.kind == "DONE":
                    out.done_line = m.text
                    break
        except HwtestError:
            pass

    def handle(m: Msg) -> None:
        if on_msg:
            on_msg(m)
        if m.kind == "R":
            out.results.append(m.obj if m.obj is not None else {"t": "unparsed", "raw": m.text})
        elif m.kind == "P":
            out.prompts.append(m.obj)
            pid = str(m.obj.get("id")) if isinstance(m.obj, dict) else "?"
            pending[pid] = time.monotonic()
            order.append(pid)
            if on_prompt:
                on_prompt(m.obj)
        elif m.kind == "A":
            out.answers.append(m.obj)
            pid = str(m.obj.get("id")) if isinstance(m.obj, dict) else "?"
            pending.pop(pid, None)
        elif m.kind == "B":
            out.binaries.append(m)
        elif m.kind in ("log", "other", "HELLO"):
            out.log.append(m.text)

    try:
        dev.send(command)
        while True:
            now = time.monotonic()
            if total_timeout and now - t0 > total_timeout:
                finish_abort(f"total timeout {total_timeout}s")
                out.status = "timeout"
                break
            if pending:
                if prompt_timeout and now - min(pending.values()) > prompt_timeout:
                    finish_abort(f"prompt not answered within {prompt_timeout}s")
                    out.status = "timeout"
                    break
            elif now - last > idle_timeout:
                finish_abort(f"no output for {idle_timeout}s")
                out.status = "timeout"
                break
            if answers is not None:
                while True:
                    try:
                        word = answers.get_nowait().strip().lower()
                    except queue.Empty:
                        break
                    live = [p for p in order if p in pending]
                    ans = ANSWER_WORDS.get(word)
                    if not live:
                        OUT.say("(no pending prompt; host answer ignored)")
                    elif ans is None:
                        OUT.say("(type a/ok or b/fail)")
                    else:
                        dev.send(f"answer {live[-1]} {ans}")
                        out.answers.append({"id": live[-1], "answer": ans, "from": "host"})
            m = dev.read_msg(0.1)
            if m is None:
                continue
            last = time.monotonic()
            handle(m)
            if m.kind == "DONE":
                out.done_line = m.text
                out.status = m.arg if m.arg in ("pass", "fail", "abort") else "error"
                break
            if m.kind == "ERR":
                if m.cmd == "run":
                    out.status, out.error = "error", m.text
                    break
                if m.cmd == "answer":
                    OUT.warn(f"device rejected host answer: {m.text}")
    except DeviceDisconnected as e:
        out.status, out.error = "disconnected", str(e)
    except KeyboardInterrupt:
        finish_abort("interrupted by user (Ctrl+C)")
        out.status = "abort"
    return out


def timestamp() -> str:
    return datetime.now().strftime("%Y%m%d-%H%M%S")


def safe_name(s: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", s).strip("_") or "x"


def results_dir_for(base: Optional[str], info: Optional[dict]) -> Path:
    uid = safe_name(str((info or {}).get("uid") or "unknown"))
    return (Path(base) if base else RESULTS_DIR) / uid


def host_meta() -> dict:
    return {"node": platform.node(), "platform": platform.platform(),
            "python": platform.python_version(), "tool": "hwtest.py"}


def save_run_results(outcome: RunOutcome, *, suite: str, command: str, info: Optional[dict],
                     port: str, started: datetime, out_dir: Optional[str]) -> Path:
    d = results_dir_for(out_dir, info)
    d.mkdir(parents=True, exist_ok=True)
    stem = f"{started.strftime('%Y%m%d-%H%M%S')}-{safe_name(suite)}"
    bins = []
    for i, m in enumerate(outcome.binaries):
        p = d / f"{stem}-b{i}.bin"
        p.write_bytes(m.data or b"")
        bins.append({"file": p.name, "len": len(m.data or b""), "crc": f"{m.crc or 0:08x}",
                     "crc_ok": m.crc_ok, "header": m.text})
    ended = datetime.now().astimezone()
    doc = {
        "kind": "run", "suite": suite, "command": command, "status": outcome.status,
        "exit_code": outcome.exit_code, "done": outcome.done_line, "error": outcome.error,
        "host_time_start": started.isoformat(timespec="seconds"),
        "host_time_end": ended.isoformat(timespec="seconds"),
        "duration_s": round((ended - started).total_seconds(), 3),
        "host": host_meta(), "port": port, "info": info,
        "results": outcome.results, "prompts": outcome.prompts, "answers": outcome.answers,
        "binaries": bins, "log": outcome.log,
    }
    path = d / f"{stem}.json"
    path.write_text(json.dumps(doc, indent=2, default=str), encoding="utf-8")
    return path


def busy_state(info: Optional[dict]) -> str:
    b = str((info or {}).get("busy", "idle") or "idle")
    return b


def looks_like_overclock(busy: str) -> bool:
    b = busy.lower()
    return b.startswith("oc") or "overclock" in b or "sweep" in b


# --------------------------------------------------------------------------
# Broken image for the recovery rehearsal
# --------------------------------------------------------------------------
def make_broken_image() -> bytes:
    """1 KB image: SP=0x20020000, reset -> 0x08000101 (``b .`` at 0x08000100)."""
    img = bytearray(b"\xff" * 1024)
    img[0:4] = (0x20020000).to_bytes(4, "little")
    img[4:8] = (FLASH_BASE + 0x100 + 1).to_bytes(4, "little")
    img[0x100:0x102] = b"\xfe\xe7"
    return bytes(img)


# --------------------------------------------------------------------------
# Helpers for commands
# --------------------------------------------------------------------------
def fmt_s(x: Optional[float]) -> str:
    return "-" if x is None else f"{x:.2f} s"


def stats(values: Iterable[float]) -> dict:
    v = [x for x in values if x is not None]
    if not v:
        return {"n": 0}
    return {"n": len(v), "min": round(min(v), 3), "median": round(statistics.median(v), 3),
            "max": round(max(v), 3), "mean": round(statistics.fmean(v), 3)}


def target_serial(args: argparse.Namespace) -> Optional[str]:
    return getattr(args, "serial", None) or os.environ.get("HWTEST_SERIAL") or None


def target_port(args: argparse.Namespace) -> Optional[str]:
    return getattr(args, "port", None) or os.environ.get("HWTEST_PORT") or None


def print_msg(m: Msg) -> None:
    if OUT.json_mode:
        return
    OUT.say(format_msg(m))


def require_cli() -> None:
    if cli_available() is None:
        raise CliNotFound(f"{CLI_EXE} not found on PATH (STM32CubeProgrammer bin dir)")


# --------------------------------------------------------------------------
# Subcommands
# --------------------------------------------------------------------------
def cmd_ports(args: argparse.Namespace) -> int:
    cdc = find_cdc_ports(target_serial(args))
    cli = cli_available()
    dfu: list[DfuDevice] = []
    cli_err = ""
    if cli:
        try:
            dfu = list_dfu_cli()
        except HwtestError as e:
            cli_err = str(e)
    else:
        cli_err = f"{CLI_EXE} not on PATH"
    pnp = pnp_dfu_serials() if not args.no_pnp else None
    OUT.say(f"CDC ports (0483:5740): {len(cdc)}")
    for c in cdc:
        OUT.say(f"  {c.device:8s} sn={c.serial}  {c.description}")
    if not cdc:
        OUT.say("  none found")
    OUT.say(f"DFU devices via STM32_Programmer_CLI (0483:DF11): {len(dfu)}" + (f"  [{cli_err}]" if cli_err else ""))
    for d in dfu:
        OUT.say(f"  {d.index or '?':6s} sn={d.serial}")
    if not dfu and not cli_err:
        OUT.say("  none found")
    if pnp is not None:
        OUT.say(f"DFU devices via Windows PnP: {len(pnp)}" + ("" if pnp else "  (none)"))
        for s in pnp:
            OUT.say(f"  sn={s}")
        if pnp and cli and not dfu:
            OUT.warn(DRIVER_HINT)
    note("CDC: " + (", ".join(f"{c.device} sn={c.serial}" for c in cdc) or "none"))
    note("DFU (CLI): " + (", ".join(f"{d.index} sn={d.serial}" for d in dfu) or (cli_err or "none")))
    if pnp is not None:
        note("DFU (PnP): " + (", ".join(pnp) or "none"))
    OUT.emit({"ok": True, "cdc": [c.to_json() for c in cdc], "dfu": [d.to_json() for d in dfu],
              "dfu_pnp": pnp, "cli": cli, "cli_error": cli_err or None})
    return EXIT_OK


def cmd_info(args: argparse.Namespace) -> int:
    cdc = resolve_cdc(target_port(args), target_serial(args))
    with connect(cdc, timeout=args.open_timeout) as dev:
        info = dev.info(timeout=args.timeout)
    if info is None:
        OUT.error(f"{cdc.device}: no answer to 'info' within {args.timeout}s")
        OUT.emit({"ok": False, "port": cdc.device, "error": "no info"})
        return EXIT_ABORT
    if not OUT.json_mode:
        OUT.say(f"# {cdc.device} sn={cdc.serial}")
        OUT.say(json.dumps(info, indent=2))
    note(f"{cdc.device}: fw={info.get('fw')} build={info.get('build')} clk={info.get('clk')} "
         f"busy={info.get('busy')} reset={info.get('reset')} uid={info.get('uid')}")
    OUT.emit(info)
    return EXIT_OK


def cmd_cmd(args: argparse.Namespace) -> int:
    text = " ".join(args.text).strip()
    if not text:
        raise HwtestError("empty command")
    cdc = resolve_cdc(target_port(args), target_serial(args))
    saved: list[str] = []

    def on_msg(m: Msg) -> None:
        print_msg(m)
        if m.kind == "B" and args.save_bin:
            Path(args.save_bin).mkdir(parents=True, exist_ok=True)
            p = Path(args.save_bin) / f"{timestamp()}-{len(saved)}.bin"
            p.write_bytes(m.data or b"")
            saved.append(str(p))
            OUT.say(f"  saved {p}")

    with connect(cdc, timeout=args.open_timeout) as dev:
        tx = dev.transact(text, timeout=args.timeout, on_msg=on_msg)
    if tx.note:
        OUT.warn(tx.note)
    rc = EXIT_ABORT
    if tx.status == "ok":
        rc = EXIT_OK
    elif tx.status == "err":
        rc = EXIT_FAIL
    elif tx.status == "done" and tx.final is not None:
        rc = {"pass": EXIT_OK, "fail": EXIT_FAIL}.get(tx.final.arg, EXIT_ABORT)
    if tx.status == "timeout":
        OUT.error(f"no terminal line for {text!r} within {args.timeout}s")
    note(f"{cdc.device}: status={tx.status} final={tx.final.text if tx.final else 'none (timeout)'} "
         f"lines={len(tx.msgs)}")
    for r in tx.results[:5]:
        note(f"@R {json.dumps(r)[:200]}")
    OUT.emit({"ok": rc == 0, "status": tx.status, "final": tx.final.text if tx.final else None,
              "lines": [m.text for m in tx.msgs],
              "results": tx.results, "saved_binaries": saved, "exit_code": rc})
    return rc


def usbbench_run(dev: Device, nbytes: int = 262144, directions: tuple = ("tx", "rx"),
                 timeout: float = 30.0, chunk: int = 4096) -> dict:
    """CDC throughput test against the firmware's `usbbench` command.
    tx = device -> host (@B frame, CRC checked); rx = host -> device (raw bytes, counted by the device)."""
    out: dict = {"bytes": nbytes}
    if "tx" in directions:
        t0 = time.monotonic()
        tx = dev.transact(f"usbbench tx {nbytes}", timeout=timeout)
        el = time.monotonic() - t0
        frames = [m for m in tx.msgs if m.kind == "B"]
        res = [r for r in tx.results if isinstance(r, dict) and r.get("dir") == "tx"]
        got = len(frames[0].data or b"") if frames else 0
        out["tx"] = {"status": tx.status, "received": got, "crc_ok": frames[0].crc_ok if frames else None,
                     "host_s": round(el, 4), "host_kBps": round(got / 1000.0 / el, 1) if el > 0 else 0.0,
                     "device_kBps": res[0].get("kBps") if res else None}
    if "rx" in directions:
        dev.send(f"usbbench rx {nbytes}")
        ready = False
        end = time.monotonic() + 5.0
        while time.monotonic() < end and not ready:
            m = dev.read_msg(0.2)
            if m is not None and m.kind == "R" and isinstance(m.obj, dict) and m.obj.get("state") == "ready":
                ready = True
            elif m is not None and m.kind == "ERR":
                break
        if not ready:
            out["rx"] = {"status": "not_ready"}
        else:
            block = b"U" * chunk
            t0 = time.monotonic()
            left = nbytes
            while left > 0:
                n = min(left, chunk)
                dev.port.write(block[:n])
                left -= n
            el = time.monotonic() - t0
            res = None
            end = time.monotonic() + timeout
            while time.monotonic() < end:
                m = dev.read_msg(0.2)
                if m is None:
                    continue
                if m.kind == "R" and isinstance(m.obj, dict) and m.obj.get("dir") == "rx" and "got" in m.obj:
                    res = m.obj
                if m.kind in ("OK", "ERR") and m.cmd_token == "usbbench":
                    break
            out["rx"] = {"status": "ok" if res and res.get("ok") else "fail", "device_got": res.get("got") if res else None,
                         "host_s": round(el, 4), "host_kBps": round(nbytes / 1000.0 / el, 1) if el > 0 else 0.0,
                         "device_kBps": res.get("kBps") if res else None}
    return out


def cmd_usbbench(args: argparse.Namespace) -> int:
    cdc = resolve_cdc(target_port(args), target_serial(args))
    dirs = ("tx", "rx") if args.direction == "both" else (args.direction,)
    with connect(cdc, timeout=args.open_timeout) as dev:
        r = usbbench_run(dev, args.bytes, dirs, timeout=args.timeout)
    ok = all(r.get(d, {}).get("status") == "ok" and r.get(d, {}).get("crc_ok", True) is not False for d in dirs)
    for d in dirs:
        note(f"usbbench {d}: {json.dumps(r.get(d))}")
        OUT.say(f"usbbench {d}: {json.dumps(r.get(d))}")
    OUT.emit({"ok": ok, **r})
    return EXIT_OK if ok else EXIT_FAIL


def cmd_monitor(args: argparse.Namespace) -> int:
    cdc = resolve_cdc(target_port(args), target_serial(args))
    sn = cdc.serial
    OUT.say(f"# monitoring {cdc.device} sn={sn} (Ctrl+C to quit)")
    dev: Optional[Device] = None
    try:
        while True:
            if dev is None:
                try:
                    dev = connect(cdc, timeout=args.open_timeout)
                    OUT.say(f"# [connected {dev.name}]")
                except HwtestError:
                    if args.no_reconnect:
                        raise
                    time.sleep(0.5)
                    continue
            try:
                m = dev.read_msg(0.5)
            except DeviceDisconnected as e:
                OUT.say(f"# [disconnected: {e}]")
                dev.close()
                dev = None
                if args.no_reconnect:
                    return EXIT_ABORT
                continue
            if m is None:
                continue
            if OUT.json_mode:
                print(json.dumps({"t": round(time.time(), 3), "kind": m.kind, "text": m.text,
                                  "obj": m.obj, "crc_ok": m.crc_ok,
                                  "len": len(m.data) if m.data is not None else None}), flush=True)
            else:
                OUT.say(format_msg(m))
    except KeyboardInterrupt:
        return EXIT_OK
    finally:
        if dev is not None:
            dev.close()


def _stdin_answers() -> "queue.Queue[str]":
    q: "queue.Queue[str]" = queue.Queue()

    def reader() -> None:
        try:
            for line in sys.stdin:
                q.put(line)
        except Exception:
            pass

    threading.Thread(target=reader, daemon=True).start()
    return q


def cmd_run(args: argparse.Namespace) -> int:
    suite = args.suite
    command = " ".join(["run", suite, *args.args])
    cdc = resolve_cdc(target_port(args), target_serial(args))
    started = datetime.now().astimezone()
    with connect(cdc, timeout=args.open_timeout) as dev:
        info = dev.info(timeout=5.0)
        if info is None:
            OUT.warn("no answer to 'info'; results will be filed under 'unknown'")
        busy = busy_state(info)
        if info is not None and busy != "idle" and not args.force:
            OUT.error(f"board is busy ({busy}); use --force or 'cmd abort'")
            OUT.emit({"ok": False, "error": f"busy: {busy}"})
            return EXIT_ABORT
        OUT.say(f"# {dev.name} sn={cdc.serial} fw={(info or {}).get('fw')} -> {command}")
        HWLOG.entry(f"Run suite {suite} (start)", command,
                    f"started on {dev.name} sn={cdc.serial}",
                    [f"Firmware: fw={(info or {}).get('fw')} build={(info or {}).get('build')} busy={busy}"])
        answers = _stdin_answers() if args.answer_from_host else None

        def on_prompt(obj: Any) -> None:
            OUT.say(prompt_banner(obj))
            if args.answer_from_host:
                OUT.say("  ...or answer here: type a/ok or b/fail and press Enter")

        outcome = run_suite(dev, command, idle_timeout=args.timeout,
                            total_timeout=args.total_timeout, prompt_timeout=args.prompt_timeout,
                            answers=answers, on_msg=print_msg, on_prompt=on_prompt)
    path = save_run_results(outcome, suite=suite, command=command, info=info, port=cdc.device,
                            started=started, out_dir=args.out)
    npass = sum(1 for r in outcome.results if isinstance(r, dict) and r.get("pass") is True)
    nfail = sum(1 for r in outcome.results if isinstance(r, dict) and r.get("pass") is False)
    failed = [f"{r.get('suite', '')}/{r.get('name', r.get('t'))}" for r in outcome.results
              if isinstance(r, dict) and r.get("pass") is False]
    OUT.say(f"\n# {suite}: {outcome.status.upper()}  results={len(outcome.results)} "
            f"pass={npass} fail={nfail}" + (f"  ({outcome.error})" if outcome.error else ""))
    for f in failed:
        OUT.say(f"#   FAIL {f}")
    OUT.say(f"# results: {path}")
    note(f"Suite {suite}: {outcome.status.upper()} results={len(outcome.results)} pass={npass} fail={nfail}"
         + (f" ({outcome.error})" if outcome.error else ""))
    if failed:
        note("Failed: " + ", ".join(failed[:20]))
    note(f"Results file: {path}")
    OUT.emit({"ok": outcome.status == "pass", "status": outcome.status, "suite": suite,
              "exit_code": outcome.exit_code, "results_file": str(path), "n_results": len(outcome.results),
              "n_pass": npass, "n_fail": nfail, "failed": failed, "error": outcome.error or None})
    return outcome.exit_code


def cmd_dfu(args: argparse.Namespace) -> int:
    require_cli()
    cdc = resolve_cdc(target_port(args), target_serial(args))
    sn = cdc.serial or target_serial(args)
    OUT.say(f"# {cdc.device} sn={sn}: entering DFU ({args.method})")
    entry = enter_dfu(cdc.device, sn, args.method, timeout=args.timeout)
    for n in entry.notes:
        OUT.say(f"  {n}")
    if entry.dfu is None:
        OUT.error(f"no DFU device (sn={sn}) within {args.timeout}s. {BOOT0_HINT}.")
        OUT.emit({"ok": False, **entry.to_json()})
        return EXIT_ABORT
    OUT.say(f"# DFU {entry.dfu.index} sn={entry.dfu.serial} after {fmt_s(entry.seconds)} via {entry.method}")
    OUT.emit({"ok": True, **entry.to_json()})
    return EXIT_OK


def _get_dfu_for_flash(args: argparse.Namespace, timings: dict, report: dict) -> tuple[Optional[DfuDevice], Optional[str]]:
    sn = target_serial(args)
    port = target_port(args)
    cdc_list = find_cdc_ports(sn)
    if port:
        cdc_list = [c for c in find_cdc_ports() if c.device.lower() == port.lower()] or cdc_list
    if len(cdc_list) > 1:
        raise HwtestError("several boards found; pick one with --serial")
    if cdc_list:
        cdc = cdc_list[0]
        sn = cdc.serial or sn
        report["cdc_port"] = cdc.device
        t = time.monotonic()
        info: Optional[dict] = None
        try:
            with connect(cdc, timeout=3.0) as dev:
                info = dev.info(timeout=3.0)
        except HwtestError as e:
            OUT.warn(f"cannot talk to {cdc.device}: {e}")
        timings["info_before"] = time.monotonic() - t
        report["info_before"] = info
        method = "auto"
        if info is None:
            OUT.warn(f"{cdc.device} is present but does not answer 'info' (firmware hung or busy); "
                     "trying the 1200-baud touch (handled in the USB interrupt)")
            method = "touch"
        else:
            busy = busy_state(info)
            OUT.say(f"# running fw={info.get('fw')} build={info.get('build')} busy={busy}")
            if busy != "idle":
                if looks_like_overclock(busy) and not args.force:
                    raise HwtestError(f"board is in an overclock sweep ({busy}); not interrupting it. "
                                      "Wait for it to finish or use --force.")
                OUT.warn(f"board is busy ({busy}); using the 1200-baud touch")
                method = "touch"
        entry = enter_dfu(cdc.device, sn, method, timeout=10.0)
        timings["app_to_dfu"] = entry.seconds
        report["dfu_entry"] = entry.to_json()
        for n in entry.notes:
            OUT.say(f"  {n}")
        if entry.dfu is not None:
            OUT.say(f"# DFU after {fmt_s(entry.seconds)} via {entry.method}")
            return entry.dfu, sn
        OUT.warn("board did not enter DFU from the application")
    else:
        found = find_dfu(sn)
        if len(found) > 1 and not sn:
            raise HwtestError("several DFU devices; pick one with --serial")
        if found:
            OUT.say(f"# no CDC port, but DFU device present (sn={found[0].serial}); programming directly")
            return found[0], found[0].serial
    OUT.say(f"\n>>> {BOOT0_HINT} (waiting up to {args.boot0_wait:.0f} s for the DFU device) <<<\n")
    t = time.monotonic()
    dfu = wait_for_dfu(sn, args.boot0_wait, poll=0.25)
    timings["wait_boot0"] = time.monotonic() - t
    return dfu, (dfu.serial if dfu else sn)


def cmd_flash(args: argparse.Namespace) -> int:
    require_cli()
    image = Path(args.image) if args.image else DEFAULT_BIN
    if not image.is_file():
        raise HwtestError(f"image not found: {image}")
    expect = None if args.no_verify_id else load_fw_id(image)
    if expect is None and not args.no_verify_id:
        OUT.warn(f"{image.parent / 'fw_id.json'} not found; will accept any running firmware")
    t_all = time.monotonic()
    timings: dict = {}
    report: dict = {"image": str(image), "size": image.stat().st_size, "fw_id": expect}
    dfu, sn = _get_dfu_for_flash(args, timings, report)
    if dfu is None:
        OUT.error(f"no DFU device found. {BOOT0_HINT} and run flash again.")
        OUT.emit({"ok": False, "error": "no DFU device", "timings": timings, **report})
        return EXIT_ABORT
    report["dfu"] = dfu.to_json()
    OUT.say(f"# programming {image.name} ({report['size']} bytes) -> {dfu.index} sn={dfu.serial}")
    res = program(dfu, image, verify=True, start=True, stream=not OUT.json_mode)
    timings["program"] = res.duration
    report["cli_rc"] = res.rc
    report["cli_errors"] = res.errors
    verified = res.has("verified successfully")
    report["cli_verified"] = verified
    if not res.ok:
        OUT.warn(f"STM32_Programmer_CLI rc={res.rc} errors={res.errors or '-'}"
                 f"{' (timed out)' if res.timed_out else ''}")
    t = time.monotonic()
    app = wait_for_app(sn, expect, timeout=args.app_timeout)
    if not app.ok and find_dfu(sn):
        OUT.warn("device still in DFU after programming; issuing start again")
        start_app(dfu, stream=not OUT.json_mode)
        app = wait_for_app(sn, expect, timeout=args.app_timeout)
    timings["dfu_to_app"] = time.monotonic() - t
    timings["total"] = time.monotonic() - t_all
    report["app"] = app.to_json()
    ok = app.ok and (res.ok or verified or app.info is not None)
    if not res.ok and not app.ok:
        ok = False
    OUT.say("\n# timing: " + "  ".join(f"{k}={fmt_s(v)}" for k, v in timings.items()))
    OUT.say(f"# CDC enumerations after start: {app.enumerations}, info attempts: {app.attempts}")
    if ok:
        info = app.info or {}
        OUT.say(f"# OK: running fw={info.get('fw')} build={info.get('build')} reset={info.get('reset')}")
    else:
        OUT.error(f"flash failed: {app.note}")
    note(f"Image: {image} ({report['size']} bytes), expected id: {expect or 'not checked'}")
    note(f"Programmer: exit code {res.rc}, verified={verified}"
         + (f", errors: {'; '.join(res.errors[:3])}" if res.errors else ""))
    note(f"App after start: {'running fw=%s build=%s' % ((app.info or {}).get('fw'), (app.info or {}).get('build')) if app.ok else 'FAIL ' + app.note}"
         f", CDC enumerations={app.enumerations}")
    note("Timing: " + ", ".join(f"{k}={v:.2f}s" for k, v in timings.items()))
    OUT.emit({"ok": ok, "serial": sn, "timings": {k: round(v, 3) for k, v in timings.items()}, **report})
    return EXIT_OK if ok else EXIT_FAIL


# ---- dfu-test ---------------------------------------------------------------
MANUAL_INSTRUCTIONS = f"""\
Manual DFU checks (run: hwtest.py dfu-test --manual):
  1. BOOT0 path: {BOOT0_HINT}; the tool detects DFU, flashes the good image and starts it.
  2. Recovery rehearsal: the tool flashes a deliberately broken 1 KB image (reset handler is an
     infinite loop; only sector 0 is erased), confirms the CDC port does NOT come back, then asks
     you to do BOOT0+RESET and reflashes the good image.
"""


def cycle_trigger(i: int) -> tuple[str, bool]:
    """(method, spin) for 0-based cycle i: every 5th cycle = touch during 'run spin'."""
    if i % 5 == 4:
        return "touch", True
    return ("cmd" if i % 2 == 0 else "touch"), False


def summarize_cycles(cycles: list[dict]) -> dict:
    by_method: dict[str, list[float]] = {}
    for c in cycles:
        if c.get("app_to_dfu_s") is not None:
            key = c.get("trigger", "?") + ("+spin" if c.get("spin") else "")
            by_method.setdefault(key, []).append(c["app_to_dfu_s"])
    return {
        "cycles": len(cycles),
        "passed": sum(1 for c in cycles if c.get("pass")),
        "failed": sum(1 for c in cycles if not c.get("pass")),
        "app_to_dfu": stats(c.get("app_to_dfu_s") for c in cycles),
        "app_to_dfu_by_trigger": {k: stats(v) for k, v in by_method.items()},
        "dfu_to_app": stats(c.get("dfu_to_app_s") for c in cycles),
        "program": stats(c.get("program_s") for c in cycles if c.get("op", "").startswith("write")),
        "failures": [{"cycle": c["cycle"], "reasons": c.get("reasons", [])} for c in cycles if not c.get("pass")],
    }


def _dfu_cycle(i: int, sn: str, image: Path, data: bytes, expect: Optional[dict],
               scratch: Path) -> dict:
    method, spin = cycle_trigger(i)
    c: dict = {"cycle": i + 1, "trigger": method, "spin": spin, "reasons": []}
    reasons: list[str] = c["reasons"]
    cdc = wait_for_cdc_port(sn, 5.0)
    if cdc is None:
        reasons.append("CDC port not present at cycle start")
        return c
    dev: Optional[Device] = None
    if spin:
        try:
            dev = connect(cdc, 3.0)
            dev.send("run spin 8000")
            dev.drain(0.5)
        except HwtestError as e:
            reasons.append(f"could not start 'run spin 8000': {e}")
    # The spin connection stays open and is handed over: for "touch" the line
    # coding is switched to 1200 on that same handle (the real-world case of
    # a host connected to a busy device). enter_dfu closes it.
    entry = enter_dfu(cdc.device, sn, method, timeout=10.0, dev=dev)
    dev = None
    c["app_to_dfu_s"] = round(entry.seconds, 3) if entry.dfu else None
    c["dfu_notes"] = entry.notes
    if entry.dfu is None:
        reasons.append(f"no DFU within 10 s ({method}{' during spin' if spin else ''})")
        return c
    if spin and entry.seconds > 7.5:
        reasons.append(f"DFU only after {entry.seconds:.1f} s: touch apparently not handled during spin")
    dfu = entry.dfu
    c["serial_match"] = (dfu.serial or "").lower() == sn.lower()
    if not c["serial_match"]:
        reasons.append(f"DFU serial {dfu.serial} != CDC serial {sn}")
    conn = run_cli(connect_args(dfu) + (["-ob", "displ"] if i == 0 else []), timeout=60, stream=False)
    c["bootloader"] = parse_connect_info(conn.output)
    if not conn.ok:
        reasons.append(f"DFU connect failed rc={conn.rc} {conn.errors[:2]}")
    if i == 0:
        c["op"] = "write+verify,upload,start"
        w = program(dfu, image, verify=True, start=False, stream=False)
        c["program_s"] = round(w.duration, 3)
        if not w.ok:
            reasons.append(f"write/verify failed rc={w.rc} {w.errors[:2]}")
        up = scratch / "upload.bin"
        u = upload(dfu, FLASH_BASE, len(data), up, stream=False)
        try:
            got = up.read_bytes()
        except OSError:
            got = b""
        mism = next((k for k in range(min(len(got), len(data))) if got[k] != data[k]), None)
        c["upload_match"] = u.ok and len(got) >= len(data) and mism is None
        if not c["upload_match"]:
            reasons.append(f"upload compare failed (rc={u.rc}, got {len(got)} bytes, first mismatch {mism})")
        s = start_app(dfu, stream=False)
        if not s.ok:
            c["start_warning"] = s.errors[:2] or f"rc={s.rc}"
    elif i % 2 == 0:
        c["op"] = "write+verify+start"
        w = program(dfu, image, verify=True, start=True, stream=False)
        c["program_s"] = round(w.duration, 3)
        if not (w.ok or w.has("verified successfully")):
            reasons.append(f"write/verify/start failed rc={w.rc} {w.errors[:2]}")
    else:
        c["op"] = "start"
        s = start_app(dfu, stream=False)
        if not s.ok:
            c["start_warning"] = s.errors[:2] or f"rc={s.rc}"
    app = wait_for_app(sn, expect, timeout=15.0)
    c["dfu_to_app_s"] = round(app.seconds, 3) if app.ok else None
    c["enumerations"] = app.enumerations
    c["info_ok"] = app.ok
    c["reset_cause"] = (app.info or {}).get("reset")
    if not app.ok:
        reasons.append(f"app did not come back with the expected build: {app.note}")
    return c


def cmd_dfu_test(args: argparse.Namespace) -> int:
    require_cli()
    if args.manual:
        return dfu_test_manual(args)
    image = Path(args.bin)
    if not image.is_file():
        raise HwtestError(f"image not found: {image}")
    data = image.read_bytes()
    cdc = resolve_cdc(target_port(args), target_serial(args))
    sn = cdc.serial
    if not sn:
        raise HwtestError("CDC port has no USB serial; cannot match DFU device")
    with connect(cdc, 3.0) as dev:
        info0 = dev.info(5.0)
    if info0 is None:
        raise HwtestError(f"{cdc.device} does not answer 'info'")
    expect = load_fw_id(image) or {"fw": info0.get("fw"), "build": info0.get("build")}
    OUT.say(f"# DFU test: {args.cycles} cycles, sn={sn}, image={image} ({len(data)} bytes)")
    started = datetime.now().astimezone()
    cycles: list[dict] = []
    with tempfile.TemporaryDirectory(prefix="hwtest-") as tmp:
        for i in range(args.cycles):
            try:
                c = _dfu_cycle(i, sn, image, data, expect, Path(tmp))
            except HwtestError as e:
                c = {"cycle": i + 1, "reasons": [f"exception: {e}"]}
            except KeyboardInterrupt:
                OUT.warn("interrupted")
                break
            c["pass"] = not c.get("reasons")
            cycles.append(c)
            HWLOG.entry(f"DFU test cycle {i + 1}/{args.cycles}",
                        f"trigger={c.get('trigger')}{'+spin' if c.get('spin') else ''} op={c.get('op', '-')}",
                        ("PASS" if c["pass"] else "FAIL " + "; ".join(c.get("reasons", []))),
                        [f"app->dfu={c.get('app_to_dfu_s')} s, dfu->app={c.get('dfu_to_app_s')} s, "
                         f"serial_match={c.get('serial_match')}, upload_match={c.get('upload_match', '-')}, "
                         f"enumerations={c.get('enumerations')}"])
            OUT.say(f"  cycle {i + 1:2d}/{args.cycles} {c.get('trigger', '?'):5s}{'+spin' if c.get('spin') else '     '} "
                    f"{c.get('op', '-'):26s} app->dfu {fmt_s(c.get('app_to_dfu_s'))}  "
                    f"dfu->app {fmt_s(c.get('dfu_to_app_s'))}  {'PASS' if c['pass'] else 'FAIL ' + '; '.join(c['reasons'])}")
            if not c["pass"] and not wait_for_cdc_port(sn, 5.0):
                if find_dfu(sn):
                    d = find_dfu(sn)[0]
                    start_app(d, stream=False)
                if not wait_for_app(sn, None, 15.0).ok:
                    OUT.error(f"board lost (neither app nor DFU recovers). {BOOT0_HINT}, then flash.")
                    break
    summary = summarize_cycles(cycles)
    d = results_dir_for(args.out, info0)
    d.mkdir(parents=True, exist_ok=True)
    path = d / f"{started.strftime('%Y%m%d-%H%M%S')}-dfu-test.json"
    path.write_text(json.dumps({"kind": "dfu-test", "host_time_start": started.isoformat(timespec="seconds"),
                                "host_time_end": datetime.now().astimezone().isoformat(timespec="seconds"),
                                "host": host_meta(), "image": str(image), "image_size": len(data),
                                "expect": expect, "info": info0, "summary": summary, "cycles": cycles},
                               indent=2, default=str), encoding="utf-8")
    OUT.say(f"\n# DFU test: {summary['passed']}/{summary['cycles']} passed")
    for k in ("app_to_dfu", "dfu_to_app", "program"):
        s = summary[k]
        if s.get("n"):
            OUT.say(f"#   {k:10s} min {s['min']:.2f}  median {s['median']:.2f}  max {s['max']:.2f} s (n={s['n']})")
    for k, s in summary["app_to_dfu_by_trigger"].items():
        OUT.say(f"#   app_to_dfu[{k}] median {s['median']:.2f} s (n={s['n']})")
    for f in summary["failures"]:
        OUT.say(f"#   cycle {f['cycle']} FAIL: {'; '.join(f['reasons'])}")
    OUT.say(f"# results: {path}\n")
    OUT.say(MANUAL_INSTRUCTIONS)
    ok = summary["cycles"] == args.cycles and summary["failed"] == 0
    note(f"DFU test: {summary['passed']}/{args.cycles} cycles passed")
    for k in ("app_to_dfu", "dfu_to_app"):
        s = summary[k]
        if s.get("n"):
            note(f"{k}: min {s['min']} / median {s['median']} / max {s['max']} s")
    note(f"Results file: {path}")
    OUT.emit({"ok": ok, "results_file": str(path), **summary})
    return EXIT_OK if ok else EXIT_FAIL


def _ask(prompt: str, assume_yes: bool) -> bool:
    if assume_yes:
        OUT.say(prompt + " [auto-yes]")
        return True
    try:
        ans = input(prompt + " [y/N] ")
    except EOFError:
        return False
    return ans.strip().lower() in ("y", "yes")


def dfu_test_manual(args: argparse.Namespace) -> int:
    good = Path(args.bin)
    if not good.is_file():
        raise HwtestError(f"good image not found: {good}")
    expect = load_fw_id(good)
    sn = target_serial(args)
    if not sn:
        cdc = find_cdc_ports()
        sn = cdc[0].serial if len(cdc) == 1 else None
    steps = {"1", "2"} if args.step == "all" else {args.step}
    report: dict = {"kind": "dfu-test-manual", "serial": sn, "steps": {}}
    started = datetime.now().astimezone()
    ok_all = True

    def reflash_good(tag: str) -> dict:
        r: dict = {}
        OUT.say(f"\n>>> {BOOT0_HINT} now (waiting up to {args.boot0_wait:.0f} s) <<<")
        t = time.monotonic()
        dfu = wait_for_dfu(sn, args.boot0_wait, poll=0.25)
        r["boot0_detect_s"] = round(time.monotonic() - t, 3)
        if dfu is None:
            r["error"] = "DFU not detected"
            return r
        r["dfu"] = dfu.to_json()
        OUT.say(f"# DFU detected sn={dfu.serial} after {r['boot0_detect_s']} s; flashing {good.name}")
        res = program(dfu, good, verify=True, start=True, stream=not OUT.json_mode)
        r["program_s"] = round(res.duration, 3)
        r["cli_rc"] = res.rc
        app = wait_for_app(dfu.serial, expect, timeout=15.0)
        r["app"] = app.to_json()
        r["ok"] = app.ok
        OUT.say(f"# {tag}: {'OK' if app.ok else 'FAIL'} ({app.note or 'app running expected build'})")
        return r

    if "1" in steps:
        OUT.say("\n=== Step 1: BOOT0 + RESET path ===")
        r = reflash_good("BOOT0 path")
        report["steps"]["boot0"] = r
        ok_all &= bool(r.get("ok"))
    if "2" in steps:
        OUT.say("\n=== Step 2: recovery rehearsal (deliberately broken image) ===")
        r2: dict = {}
        broken = Path(args.broken_bin) if args.broken_bin else None
        if broken is None:
            d = results_dir_for(args.out, None)
            d.mkdir(parents=True, exist_ok=True)
            broken = d / "broken-1k.bin"
            broken.write_bytes(make_broken_image())
        r2["broken_bin"] = str(broken)
        if not _ask(f"Flash the broken image {broken} (only sector 0 is erased; board will look dead "
                    f"until BOOT0+RESET)?", args.yes):
            r2["skipped"] = True
            report["steps"]["recovery"] = r2
        else:
            dfu = find_dfu(sn)[0] if find_dfu(sn) else None
            if dfu is None:
                cdc = resolve_cdc(target_port(args), sn)
                entry = enter_dfu(cdc.device, cdc.serial, "auto", 10.0)
                dfu = entry.dfu
                sn = sn or cdc.serial
            if dfu is None:
                r2["error"] = "could not enter DFU to flash the broken image"
                ok_all = False
            else:
                res = program(dfu, broken, verify=True, start=True, stream=not OUT.json_mode)
                r2["broken_cli_rc"] = res.rc
                OUT.say("# waiting 10 s: the CDC port must NOT appear")
                came_back = wait_for_cdc_port(sn, 10.0)
                r2["cdc_absent"] = came_back is None
                if came_back is not None:
                    OUT.error(f"CDC port {came_back.device} appeared - the broken image did not take effect")
                    ok_all = False
                else:
                    OUT.say("# good: board is 'bricked' (no CDC). Now recover it.")
                rr = reflash_good("recovery")
                r2["recovery"] = rr
                ok_all &= bool(rr.get("ok")) and bool(r2["cdc_absent"])
            report["steps"]["recovery"] = r2
    report["ok"] = ok_all
    info_now = None
    if sn and find_cdc_ports(sn):
        try:
            with connect(find_cdc_ports(sn)[0], 3.0) as dev:
                info_now = dev.info(3.0)
        except HwtestError:
            pass
    report["info"] = info_now
    d = results_dir_for(args.out, info_now)
    d.mkdir(parents=True, exist_ok=True)
    path = d / f"{started.strftime('%Y%m%d-%H%M%S')}-dfu-test-manual.json"
    path.write_text(json.dumps(report, indent=2, default=str), encoding="utf-8")
    OUT.say(f"\n# manual DFU test: {'PASS' if ok_all else 'FAIL'}; results: {path}")
    for name, st in report["steps"].items():
        note(f"Step {name}: {'skipped' if st.get('skipped') else ('OK' if st.get('ok') or (st.get('recovery') or {}).get('ok') else 'FAIL')}"
             f"{' - ' + st['error'] if st.get('error') else ''}")
    note(f"Results file: {path}")
    OUT.emit({"results_file": str(path), **report})
    return EXIT_OK if ok_all else EXIT_FAIL


def cmd_lock_status(args: argparse.Namespace) -> int:
    info = read_lock(LOCK_PATH)
    if info is None and not LOCK_PATH.exists():
        OUT.say(f"unlocked ({LOCK_PATH})")
        OUT.emit({"locked": False, "path": str(LOCK_PATH)})
        return EXIT_OK
    alive = pid_alive(int((info or {}).get("pid", 0)))
    OUT.say(f"{'LOCKED' if alive else 'stale lock'}: {info} ({LOCK_PATH})")
    OUT.emit({"locked": alive, "stale": not alive, "holder": info, "path": str(LOCK_PATH)})
    return EXIT_OK


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------
HW_COMMANDS = {"info", "cmd", "monitor", "run", "dfu", "flash", "dfu-test", "usbbench"}
LOGGED_COMMANDS = HW_COMMANDS | {"ports"}


def build_parser() -> argparse.ArgumentParser:
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--json", action="store_true",
                        help="machine-readable summary on stdout (human text goes to stderr)")
    common.add_argument("--port", help="COM port (default: autodetect 0483:5740; env HWTEST_PORT)")
    common.add_argument("--serial", help="USB serial of the board (env HWTEST_SERIAL)")
    common.add_argument("--wait-lock", type=float, nargs="?", const=600.0, default=0.0, metavar="SECONDS",
                        help="wait for the board lock instead of failing (default wait 600 s)")
    common.add_argument("--open-timeout", type=float, default=3.0, help="seconds to wait for the port to open")
    common.add_argument("--log", metavar="PATH",
                        help=f"hardware work log to append to (default {HWLOG.path}; env HWTEST_LOG)")
    common.add_argument("--no-log", action="store_true", help="do not append to the hardware work log")

    p = argparse.ArgumentParser(
        prog="hwtest.py", description="Host tool for the stm32-hwtest firmware (STM-boy).",
        epilog="Exit codes: 0 ok/pass, 1 fail, 2 abort/timeout/no device, 3 board locked.")
    sub = p.add_subparsers(dest="command", required=True, metavar="COMMAND")

    s = sub.add_parser("ports", parents=[common], help="list CDC ports and DFU devices")
    s.add_argument("--no-pnp", action="store_true", help="skip the Windows PnP query")
    s.set_defaults(func=cmd_ports)

    s = sub.add_parser("info", parents=[common], help="print the device 'info' JSON")
    s.add_argument("--timeout", type=float, default=3.0)
    s.set_defaults(func=cmd_info)

    s = sub.add_parser("cmd", parents=[common], help="send a raw command, print lines until @OK/@ERR/@DONE")
    s.add_argument("text", nargs="+", help="command text, e.g. 'w25q id'")
    s.add_argument("--timeout", type=float, default=10.0)
    s.add_argument("--save-bin", metavar="DIR", help="save @B binary payloads into DIR")
    s.set_defaults(func=cmd_cmd)

    s = sub.add_parser("usbbench", parents=[common], help="CDC throughput test (device->host @B frame, host->device raw)")
    s.add_argument("--bytes", type=int, default=262144, help="bytes per direction (multiple of 256, <= 1 MB)")
    s.add_argument("--direction", choices=("tx", "rx", "both"), default="both")
    s.add_argument("--timeout", type=float, default=30.0)
    s.set_defaults(func=cmd_usbbench)

    s = sub.add_parser("monitor", parents=[common], help="print all device lines (Ctrl+C to quit)")
    s.add_argument("--no-reconnect", action="store_true", help="exit when the port disappears")
    s.set_defaults(func=cmd_monitor)

    s = sub.add_parser("run", parents=[common], help="run a suite and collect @R results")
    s.add_argument("suite")
    s.add_argument("args", nargs="*", help="suite arguments")
    s.add_argument("--timeout", type=float, default=120.0,
                   help="idle timeout: seconds without any device line (suspended while a prompt is pending)")
    s.add_argument("--total-timeout", type=float, default=0.0, help="hard cap on the whole run (0 = none)")
    s.add_argument("--prompt-timeout", type=float, default=600.0,
                   help="max seconds to wait for a prompt answer (0 = forever)")
    s.add_argument("--out", help=f"results base dir (default {RESULTS_DIR})")
    s.add_argument("--answer-from-host", action="store_true",
                   help="also accept prompt answers typed on the host (sends 'answer <id> ok|fail')")
    s.add_argument("--force", action="store_true", help="run even if the board reports busy")
    s.set_defaults(func=cmd_run)

    s = sub.add_parser("dfu", parents=[common], help="put the board into the ST DFU bootloader")
    s.add_argument("--method", choices=("auto", "cmd", "touch"), default="auto",
                   help="auto = 'dfu' command, fall back to 1200-baud touch")
    s.add_argument("--timeout", type=float, default=10.0)
    s.set_defaults(func=cmd_dfu)

    s = sub.add_parser("flash", parents=[common], help="DFU-flash an image and verify the running build")
    s.add_argument("image", nargs="?", help=f"image (.bin at 0x08000000, or .hex); default {DEFAULT_BIN}")
    s.add_argument("--no-verify-id", action="store_true", help="do not compare info fw/build with fw_id.json")
    s.add_argument("--force", action="store_true", help="interrupt even an overclock sweep")
    s.add_argument("--boot0-wait", type=float, default=60.0, help="seconds to wait for manual BOOT0 DFU")
    s.add_argument("--app-timeout", type=float, default=15.0, help="seconds to wait for the app to return")
    s.set_defaults(func=cmd_flash)

    s = sub.add_parser("dfu-test", parents=[common], help="DFU reliability test (P0b)")
    s.add_argument("--cycles", type=int, default=20)
    s.add_argument("--bin", default=str(DEFAULT_BIN), help="known-good image")
    s.add_argument("--out", help=f"results base dir (default {RESULTS_DIR})")
    s.add_argument("--manual", action="store_true", help="interactive BOOT0 + recovery rehearsal")
    s.add_argument("--step", choices=("1", "2", "all"), default="all", help="manual step(s) to run")
    s.add_argument("--broken-bin", help="broken image for the recovery rehearsal (default: generated 1 KB)")
    s.add_argument("--boot0-wait", type=float, default=60.0)
    s.add_argument("--yes", action="store_true", help="do not ask for confirmation")
    s.set_defaults(func=cmd_dfu_test)

    s = sub.add_parser("lock-status", parents=[common], help="show who holds the board lock")
    s.set_defaults(func=cmd_lock_status)
    return p


def main(argv: Optional[list[str]] = None) -> int:
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(errors="replace")  # type: ignore[union-attr]
        except (AttributeError, ValueError):
            pass
    argv_list = list(argv) if argv is not None else sys.argv[1:]
    args = build_parser().parse_args(argv_list)
    OUT.json_mode = bool(getattr(args, "json", False))
    HWLOG.enabled = not args.no_log
    if args.log:
        HWLOG.path = Path(args.log)
    SESSION_NOTES.clear()
    cmdline = "hwtest.py " + subprocess.list2cmdline(argv_list)
    logged = args.command in LOGGED_COMMANDS
    lock: Optional[BoardLock] = None
    rc = EXIT_ABORT
    verdict = "ERROR"
    try:
        if args.command in HW_COMMANDS:
            lock = BoardLock(cmdline, wait=args.wait_lock)
            lock.acquire()
        rc = int(args.func(args))
        verdict = "OK" if rc == EXIT_OK else "FAIL"
        return rc
    except HwtestError as e:
        OUT.error(str(e))
        OUT.emit({"ok": False, "error": str(e), "exit_code": e.exit_code})
        rc = e.exit_code
        note(f"Error: {type(e).__name__}: {e}")
        return rc
    except KeyboardInterrupt:
        OUT.error("interrupted")
        note("Interrupted by user (Ctrl+C)")
        return EXIT_ABORT
    except Exception as e:  # unexpected bug: log it, then re-raise for the traceback
        note(f"Unexpected exception: {type(e).__name__}: {e}")
        raise
    finally:
        if lock is not None:
            lock.release()
        if logged:
            HWLOG.entry(f"hwtest.py {args.command}", cmdline,
                        f"{verdict} (exit code {rc})", list(SESSION_NOTES))


if __name__ == "__main__":
    sys.exit(main())
