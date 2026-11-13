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
| `suites` | `@R {"t":"suites","list":["..."],"user":["..."]}` (`user` = [U] suites that need a human at the device) |
| `answer <id> ok\|fail` | answers the open device prompt `<id>` from the host (same effect as A / B); `@OK answer`. Accepted while a suite runs. |
| `w25q read <addr_hex> <len>` | `@B <len> <crc32>` + raw bytes, then `@OK w25q read`; `len` 1..65536 (decimal or 0x..) |
| `w25q crc <addr_hex> <len>` | `@R {"t":"w25q_crc","addr":"XXXXXX","len":N,"crc32":"XXXXXXXX","ms":N,"spi_hz":N}`; any length inside the 16 MB; `abort` stops it (`@ERR w25q aborted`) |
| `w25q scan <addr_hex> <len> [step]` | `@R {"t":"w25q_scan","addr","len","step","granules","blank","used","nonff_bytes","first_nonff","last_nonff","ranges":[["start","end"],...],"truncated","ms"}`; every byte is read; a granule of `step` bytes (power of two 256..1M, default 4096, addr/len multiples of step) is used if any byte != 0xFF; consecutive used granules merge into `[start,end)` ranges (hex, max 64) |

All W25Q commands are read-only. Long `crc`/`scan` runs report `busy:"w25q crc|scan"` in `info`; use
`hwtest.py cmd ... --timeout 120` (the whole 16 MB takes ~20-30 s at 12 MHz).

P0 suites: `selftest` (flash CRC, W25Q JEDEC, HWCAL load, clock check),
`spin <ms>` (busy loop with IRQs enabled, used by the DFU test to prove the
1200-baud touch works while the main loop is busy), `prompt`.

## P1 suites

Suite arguments are `key=value` tokens or bare words. Every [U] suite (except
`display.cal`) starts with a prompt "A=start B=skip" whose timeout is `t=<s>`
(default 60 s); no answer => `@R {"t":"skip",...}` + `@DONE <suite> abort`.
Per-step waits use `step=<s>`. Every `@P` is closed by exactly one `@A`
(`ok`, `fail`, `timeout`, `abort`, `done`, `skip`). Calibration results are
kept in RAM only (`lcd_cfg` + `g_hwcal`, visible via `hwcal get`) and reported
as `@R {"t":"cal",...,"saved_to":"ram"}`; nothing is written to the W25Q in P1.

| Suite | Kind | Results |
| --- | --- | --- |
| `pins` | auto | one `@R {"t":"test","name":"PA2","signal","expect","actual","pass","ru","rd","fl","fh","why"}` per pin (`actual` in TIED_HIGH, TIED_LOW, WEAK_HIGH, WEAK_LOW, FLOATING, ODD), `pin_timing` for PB2/PA3, `bridge_<pin>` with `followers`, `restore` |
| `display.info` | auto | `lcd_cfg` + SPI1/PA5/PA6/PA7 register check, full-frame fill time |
| `backlight` | auto | `pwm_<hz>hz_<pct>pct`: TIM3 regs, PSC/ARR, measured Hz and duty on PA6; `off_full` |
| `display.cal [land\|port] [madctl=] [x=] [y=] [t=<idle s>]` | [U] | `cal` start/nudge/save/cancel with `xoff`,`yoff` |
| `display.orient [land\|port]` | [U] | `orient_show` per candidate, `cal` `orient_land`/`orient_port` |
| `display.rgb` | [U] | `look` per colour, `cal` `bgr` |
| `display.invert` | [U] | `look` invoff/invon, `cal` `invert` |
| `display.gamma` | [U] | `gamma_show`, `cal` `gamma` |
| `display.bars`, `display.gradient` | [U] | one `test` |
| `display.spi [div=2\|4\|8] [bits=8\|16] [speed=2\|3]` | [U] | `test` per setting (`spi_hz`,`bits`,`gpio_speed`,`frame_us`), `cal` `spi1_limit`; the suite FAILs only if the default 12 MHz (div 8) flickers |
| `backlight.user` | [U] | `backlight` per frequency (`min_level`, `clean`), `cal` `backlight` |
| `input.live` | [U] | `edge` per debounced edge, `test` `btn_<NAME>` |
| `input.chord` | [U] | `test` per chord (`extra` = unexpected bits), `chord` (info) for joystick diagonals |
| `input.bounce [btn=A] [n=30]` | [U] | `bounce` per button (burst duration / edges min/med/p95/max), `cal` `debounce` = max(2*p95, 5 ms) |

POWER is not part of any input suite: the ATtiny asserts OFF_REQ only after a
>= 500 ms hold + release and then removes power within 2.5 s regardless of
OFF_ACK (`software/attiny13/power_fsm.c`).

## Build identity

`make` writes `build/fw_id.json`: `{"fw":"<hash>","build":<unix time>}`. The
same values are compiled in and reported by `info`, so the host can verify
that the freshly built image is the one running.

## Artifacts

`build/hwtest.bin` is the canonical image (CRC trailer patched in by
`tools/append_crc.py`), flashed at `0x08000000`. `build/hwtest.elf` is for
debugging. `build/hwtest.hex` is generated from the patched bin.
