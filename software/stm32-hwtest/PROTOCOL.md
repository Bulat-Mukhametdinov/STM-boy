# stm32-hwtest host protocol

Transport: USB CDC ACM, VID `0x0483`, PID `0x5740`. The USB serial-number
string is derived from the chip UID exactly like `stm32-bringup`
(`usbd_desc.c`), so it matches the serial the ST system bootloader reports in
DFU mode (`0483:DF11`). Nominal baud rate 115200 (ignored by the device).

## Special line coding

| Baud set by host | Effect |
| --- | --- |
| 1200 | "1200-baud touch": the device enters the ST system bootloader (DFU) immediately, from the USB interrupt. The COM port disappears; the host must treat a failed open/close at 1200 as success. |
| anything else | normal operation |

## Host -> device

ASCII lines terminated by `\n` (`\r` ignored). Max 120 chars. Tokens are
space-separated. Commands are case-sensitive.

## Device -> host

ASCII lines terminated by `\r\n`. Lines that start with `@` are machine
lines; all other lines are free-form human log text.

| Line | Meaning |
| --- | --- |
| `@HELLO {json}` | sent once after USB enumeration settles (first DTR or first command); same payload as `info` |
| `@OK <cmd>` | command accepted / finished without a result payload |
| `@ERR <cmd> <message>` | command rejected or failed |
| `@R {json}` | one result object (single line JSON, no newlines inside) |
| `@P {json}` | user prompt shown on the device screen: `{"id":"...","text":"...","answers":["A=ok","B=fail"]}` |
| `@A {json}` | answer to a prompt, produced by device buttons: `{"id":"...","answer":"ok"}` |
| `@DONE <suite> pass|fail|abort` | end of a `run` |
| `@B <len> <crc32hex>` | followed immediately by exactly `<len>` raw bytes (binary dump), CRC-32/ISO-HDLC (zlib) of those bytes |

Every `@R` object has at least `"t"` (result type). Results produced by tests
and benchmarks also carry `"suite"`, `"name"` and, where relevant, `"pass"`.

## Commands (P0)

| Command | Response |
| --- | --- |
| `ping` | `@OK ping` |
| `help` | free text list, then `@OK help` |
| `info` | `@R {"t":"info","uid":"<24 hex>","serial":"<usb serial>","fw":"<git short hash>[+dirty]","build":<unix time>,"clk":<SYSCLK Hz>,"opt":"-O2","busy":"<idle|suite name>","hwcal":<true|false>,"reset":"<reset cause>","flash_kb":512}` then `@OK info` |
| `dfu` | `@OK dfu`, ~50 ms later the device resets into the system bootloader |
| `reset` | `@OK reset`, then software reset into the application |
| `crc` | `@R {"t":"crc","calc":"<hex>","stored":"<hex>","len":<bytes>,"pass":true}` |
| `hwcal get` | `@R {"t":"hwcal", ...fields...}` |
| `w25q id` | `@R {"t":"w25q_id","jedec":"EF4018","uid":"<16 hex>"}` |
| `run <suite>` | zero or more `@R`/`@P` lines, then `@DONE <suite> pass|fail|abort` |
| `abort` | aborts a running suite (`@DONE <suite> abort`) |
| `suites` | `@R {"t":"suites","list":["..."]}` |

P0 suites: `selftest` (flash CRC, W25Q JEDEC, HWCAL load, clock check),
`spin <ms>` (busy loop with IRQs enabled, used by the DFU test to prove the
1200-baud touch works while the main loop is busy).

## Build identity

`make` writes `build/fw_id.json`: `{"fw":"<hash>","build":<unix time>}`. The
same values are compiled in and reported by `info`, so the host can verify
that the freshly built image is the one running.

## Artifacts

`build/hwtest.bin` is the canonical image (CRC trailer patched in by
`tools/append_crc.py`), flashed at `0x08000000`. `build/hwtest.elf` is for
debugging. `build/hwtest.hex` is generated from the patched bin.
