#!/usr/bin/env python3
"""Upload a raw ROM-pack image to RetroPort's W25Q128 over USB CDC.

Cross-platform (Windows / macOS / Linux) transport built on pyserial, so it works
with Windows COMx ports as well as /dev/cu.usbmodem*. Install the dependency with:

    python -m pip install pyserial
"""

import argparse
import binascii
import struct
import sys
import time

try:
    import serial
    import serial.tools.list_ports as list_ports
except ImportError:  # pragma: no cover
    sys.stderr.write(
        "error: pyserial is required. Install it with: python -m pip install pyserial\n"
    )
    raise

MAGIC = b"C8UP"
CMD_HELLO = 1
CMD_BEGIN = 2
CMD_WRITE = 3
CMD_END = 4
CMD_ABORT = 5
DEFAULT_CHUNK_SIZE = 4096
BAUDRATE = 115200

# RetroPort's USB CDC identity (STMicroelectronics VCP).
STM_VID = 0x0483
STM_PID = 0x5740


def find_port():
    """Return the most likely RetroPort CDC port across platforms."""
    ports = list(list_ports.comports())
    if not ports:
        raise RuntimeError("no serial ports found; is the board connected and in USB upload mode?")

    # Prefer an exact STM32 VCP VID:PID match.
    matches = [p for p in ports if (p.vid == STM_VID and p.pid == STM_PID)]
    if not matches:
        # Fall back to anything that looks like a USB CDC / usbmodem device.
        matches = [
            p for p in ports
            if "usbmodem" in (p.device or "").lower()
            or "usbserial" in (p.device or "").lower()
            or "CDC" in (p.description or "")
            or "Serial" in (p.description or "")
        ]
    if not matches:
        matches = ports

    # Newest last, like the original tool.
    return sorted(matches, key=lambda p: p.device)[-1].device


def open_port(path):
    ser = serial.Serial()
    ser.port = path
    ser.baudrate = BAUDRATE
    ser.bytesize = serial.EIGHTBITS
    ser.parity = serial.PARITY_NONE
    ser.stopbits = serial.STOPBITS_ONE
    ser.rtscts = False
    ser.xonxoff = False
    ser.dsrdtr = False
    ser.timeout = 0.1
    ser.write_timeout = 5.0
    ser.open()
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    return ser


def frame(cmd, offset=0, length=0, crc=0, payload=b""):
    header = MAGIC + bytes([cmd, 0, 0, 0]) + struct.pack("<III", offset, length, crc)
    return header + payload


def write_all(ser, data):
    ser.write(data)
    ser.flush()


def read_line(ser, timeout=30.0):
    deadline = time.time() + timeout
    data = bytearray()
    while time.time() < deadline:
        chunk = ser.read(256)
        if chunk:
            data.extend(chunk)
            if b"\n" in data:
                line, _, _ = bytes(data).partition(b"\n")
                return line.decode("ascii", errors="replace").rstrip("\r")
    raise TimeoutError("device response timeout")


def wait_for(ser, expect, timeout=30.0):
    while True:
        line = read_line(ser, timeout)
        if line.startswith("PROG "):
            print("device:", line)
            continue
        if not line.startswith("OK " + expect):
            raise RuntimeError(f"device rejected {expect}: {line}")
        return line


def transact(ser, packet, expect, timeout=30.0):
    write_all(ser, packet)
    return wait_for(ser, expect, timeout)


def upload_image(image_path, port=None, chunk_size=DEFAULT_CHUNK_SIZE):
    if chunk_size <= 0 or chunk_size > 4096:
        raise RuntimeError("--chunk-size must be in range 1..4096")

    with open(image_path, "rb") as f:
        image = f.read()

    total = len(image)
    crc = binascii.crc32(image) & 0xFFFFFFFF
    resolved_port = port or find_port()

    print(f"port: {resolved_port}")
    print(f"image: {image_path}")
    print(f"size: {total} bytes")
    print(f"crc32: {crc:08X}")

    ser = open_port(resolved_port)
    try:
        transact(ser, frame(CMD_HELLO), "HELLO", timeout=5.0)
        print("erase: started")
        transact(ser, frame(CMD_BEGIN, 0, total, crc), "BEGIN", timeout=5.0)
        wait_for(ser, "ERASE", timeout=1800.0)
        print("erase: done")

        sent = 0
        while sent < total:
            chunk = image[sent:sent + chunk_size]
            chunk_crc = binascii.crc32(chunk) & 0xFFFFFFFF
            transact(ser, frame(CMD_WRITE, sent, len(chunk), chunk_crc, chunk), "WRITE", timeout=10.0)
            sent += len(chunk)
            print(f"write: {sent}/{total}", end="\r", flush=True)
        print()

        print("verify: started")
        transact(ser, frame(CMD_END, 0, total, crc), "END", timeout=600.0)
        print("verify: done")
    finally:
        ser.close()


def main():
    parser = argparse.ArgumentParser(description="Upload a raw ROM pack image to RetroPort W25Q128 over USB CDC.")
    parser.add_argument("image", help="binary image to write to the ROM-pack flash region")
    parser.add_argument("--port", help="CDC serial port (e.g. COM6). Default: auto-detect STM32 VCP")
    parser.add_argument("--chunk-size", type=int, default=DEFAULT_CHUNK_SIZE,
                        help=f"bytes per WRITE command, default: {DEFAULT_CHUNK_SIZE}")
    parser.add_argument("--list-ports", action="store_true", help="list detected serial ports and exit")
    args = parser.parse_args()

    if args.list_ports:
        for p in sorted(list_ports.comports(), key=lambda x: x.device):
            vid = f"{p.vid:04X}" if p.vid is not None else "----"
            pid = f"{p.pid:04X}" if p.pid is not None else "----"
            print(f"{p.device}\tVID:PID={vid}:{pid}\t{p.description}")
        return

    upload_image(args.image, port=args.port, chunk_size=args.chunk_size)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)
