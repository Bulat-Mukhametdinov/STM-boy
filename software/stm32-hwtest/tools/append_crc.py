#!/usr/bin/env python3
"""Patch the CRC trailer of an stm32-hwtest image.

The CRC is computed exactly like the STM32F4 hardware CRC unit after
CRC_CR.RESET:
  * polynomial 0x04C11DB7, initial value 0xFFFFFFFF
  * input is consumed as 32-bit words; each word is the little-endian read of
    4 bytes from flash (what the CPU writes to CRC->DR) and is processed
    MSB first
  * no input/output reflection, no final XOR

It covers every byte of the image except the last 4, and the result is
written into those last 4 bytes (little-endian), i.e. the `.crc_trailer`
word at `_flash_crc_end` (see STM32F411CEUX_HWTEST.ld).

Usage:
  append_crc.py build/hwtest.bin       patch in place, print the CRC
  append_crc.py --check build/hwtest.bin   verify only (exit 1 on mismatch)
  append_crc.py --selftest             run the built-in known-answer tests
"""

import struct
import sys

POLY = 0x04C11DB7
INIT = 0xFFFFFFFF


def _make_table():
    table = []
    for i in range(256):
        c = i << 24
        for _ in range(8):
            c = ((c << 1) ^ POLY) if (c & 0x80000000) else (c << 1)
            c &= 0xFFFFFFFF
        table.append(c)
    return table


_TABLE = _make_table()


def stm32_crc(data: bytes, crc: int = INIT) -> int:
    """Table-driven STM32 CRC over `data` (length must be a multiple of 4)."""
    if len(data) % 4:
        raise ValueError("length must be a multiple of 4")
    t = _TABLE
    for (word,) in struct.iter_unpack("<I", data):
        # The word is shifted in MSB first == its big-endian byte sequence.
        for shift in (24, 16, 8, 0):
            b = (word >> shift) & 0xFF
            crc = ((crc << 8) & 0xFFFFFFFF) ^ t[((crc >> 24) ^ b) & 0xFF]
    return crc


def stm32_crc_bitwise(data: bytes, crc: int = INIT) -> int:
    """Independent reference: literal model of the hardware (word-wise XOR, 32 shifts)."""
    for (word,) in struct.iter_unpack("<I", data):
        crc ^= word
        for _ in range(32):
            crc = ((crc << 1) ^ POLY) if (crc & 0x80000000) else (crc << 1)
            crc &= 0xFFFFFFFF
    return crc


def crc32_mpeg2_bytes(data: bytes) -> int:
    """CRC-32/MPEG-2 (byte-wise, MSB-first) — the catalogued algorithm the
    STM32 unit equals when words are fed in big-endian byte order."""
    crc = INIT
    for b in data:
        crc ^= b << 24
        for _ in range(8):
            crc = ((crc << 1) ^ POLY) if (crc & 0x80000000) else (crc << 1)
            crc &= 0xFFFFFFFF
    return crc


def selftest() -> bool:
    ok = True

    def check(name, got, exp):
        nonlocal ok
        good = got == exp
        ok &= good
        print(f"  {'ok ' if good else 'BAD'} {name}: 0x{got:08X} (expect 0x{exp:08X})")

    print("append_crc selftest")
    # 1) Catalogue check value of CRC-32/MPEG-2 validates the byte-wise reference.
    check("CRC-32/MPEG-2('123456789')", crc32_mpeg2_bytes(b"123456789"), 0x0376E6E7)
    # 2) Well-known STM32 vector: CRC unit after reset, DR <- 0x00000000.
    zero = struct.pack("<I", 0)
    check("stm32 word 0x00000000", stm32_crc(zero), 0xC704DD7B)
    check("stm32 word 0x00000000 (bitwise)", stm32_crc_bitwise(zero), 0xC704DD7B)
    # 3) Word -> MSB-first equivalence with the MPEG-2 reference for random data.
    import random
    rnd = random.Random(1234)
    blob = bytes(rnd.getrandbits(8) for _ in range(4096))
    be = b"".join(struct.pack(">I", w) for (w,) in struct.iter_unpack("<I", blob))
    ref = crc32_mpeg2_bytes(be)
    check("table vs bitwise (4 KB)", stm32_crc(blob), stm32_crc_bitwise(blob))
    check("table vs MPEG-2 of byte-swapped words", stm32_crc(blob), ref)
    # 4) Residue property: appending the CRC word makes the total CRC zero.
    c = stm32_crc(blob)
    check("residue with trailer", stm32_crc(blob + struct.pack("<I", c)), 0)
    print("selftest", "PASS" if ok else "FAIL")
    return ok


def patch(path: str, check_only: bool = False) -> int:
    with open(path, "rb") as f:
        img = bytearray(f.read())
    if len(img) < 8 or len(img) % 4:
        print(f"append_crc: {path}: bad length {len(img)} (must be >=8 and a multiple of 4)",
              file=sys.stderr)
        return 1
    body = bytes(img[:-4])
    crc = stm32_crc(body)
    stored = struct.unpack_from("<I", img, len(img) - 4)[0]
    if check_only:
        good = crc == stored
        print(f"append_crc: {path}: len={len(body)} calc=0x{crc:08X} stored=0x{stored:08X} "
              f"{'OK' if good else 'MISMATCH'}")
        return 0 if good else 1
    if stored not in (0xFFFFFFFF, crc):
        print(f"append_crc: warning: trailer was 0x{stored:08X}, overwriting", file=sys.stderr)
    struct.pack_into("<I", img, len(img) - 4, crc)
    with open(path, "wb") as f:
        f.write(img)
    print(f"append_crc: {path}: covered {len(body)} bytes, crc=0x{crc:08X}, "
          f"trailer @ 0x{0x08000000 + len(body):08X}")
    return 0


def main(argv) -> int:
    if len(argv) == 2 and argv[1] == "--selftest":
        return 0 if selftest() else 1
    if len(argv) == 3 and argv[1] == "--check":
        return patch(argv[2], check_only=True)
    if len(argv) == 2 and not argv[1].startswith("-"):
        return patch(argv[1])
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
