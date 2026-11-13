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

## P3 suites (memory) and commands

| Suite | Results |
| --- | --- |
| `w25q.info` | `test` `jedec` (+uid), `status_regs` (SR1/2/3 + BP/TB/SEC/CMP/SRL/QE/LB/WPS decode), `sfdp` (signature, rev, density) |
| `w25q.readcrc` | `test` `crc_6mhz`/`crc_12mhz`/`crc_24mhz` (whole 16 MB, HW CRC unit, `ms`, `kBps`), `match` |
| `w25q.scratch confirm=yes` | DESTRUCTIVE, scratch 0xF00000-0xFDFFFF only: `blank_scan`, `erase_4k/32k/64k` (min/max/avg ms vs datasheet typ/max 45/400, 120/1600, 150/2000), `page_program` (vs 0.4/3 ms), `pattern_0x55/0xAA/addr/lfsr`, `final_erase` |
| `w25q.sector` | per scratch sector: backup, erase, pattern, verify, restore, verify |
| `w25q.pd` | 0xB9/0xAB timing, JEDEC while down, release-to-first-valid-JEDEC time, 0xAB device ID |
| `ram.march` | transparent March C- (two backgrounds) per 1 KB block, IRQs off per block; `untested` lists live stack + .noinit |
| `flash.check` | `image_crc`, `flash_size` (+UID, DBGMCU id), `option_bytes` (RDP, BOR, WRP, notes; read-only) |

The W25Q driver refuses any program/erase below 0xF00000. New commands: `hwcal set key=value ...` (edit the RAM
calibration), `hwcal save [force]` (write it to the W25Q; refuses a foreign sector without `force`),
`usbbench rx|tx <bytes>` (CDC throughput; host side: `hwtest.py usbbench [--bytes N] [--direction tx|rx|both]`).

## P5 benchmark suites

All `bench.*` suites emit `@R {"t":"bench","suite","name","cyc_min","cyc_med","us"[,"unit","per_unit"][,"mbps"],
"clk","ws","art","opt","loc"[,extras]}` (1 warm-up + 7 reps, min/median, IRQs masked per rep for short kernels).
`bench.cpu`, `bench.mem`, `bench.flash`, `bench.irq`, `bench.lcd`, `bench.gfx`, `bench.w25q` (read-only),
`bench.audio`, `bench.sys` (`stop=1` is accepted but only reports "not implemented"), `bench.all` (all of them, then
two `@R {"t":"budget",...}` lines). `tools/gen_budget.py results.json` renders the results as markdown.

## P2 power suites, P2b battery logger

| Suite | Kind | Results |
| --- | --- | --- |
| `power.info` | auto | `power`: `vdda_mv` (3300*VREFINT_CAL/raw), `vpin_mv`, `vbat_unc_mv`, `vbat_mv` (HWCAL gain/offset), `temp_dC`, `chg`, `vbus`, `presence` (batt/no_batt/unknown: window spread > 200 mV or V outside 2.7-4.35 V), raw codes, `noise_mv`, `spread_mv` |
| `power.adccal` | [U] | joystick entry of multimeter VBAT (J2) + 3V3 (SWD pad) (UP/DOWN 10 mV, LEFT/RIGHT 100 mV, CENTER field, A accept); `adccal_point`, `adccal_fit` (RAM; persist with `hwcal save`) |
| `power.handshake [ack=now\|delay\|never]` | [U] | disconnect SWD first. `handshake` `req` (detection latency); breadcrumbs {mode, t} go to .noinit and W25Q 0xFDF000 before/after ACK plus 100 ms heartbeats. After the next power-on `run power.handshake report` gives `handshake_report` (ACK time, last-alive time = power-cut estimate, verdict) |
| `power.current [s=N] [n=20]` | [U] | per state S1 (Stop, USB drops), S2 (WFI), S3-S7 (backlight 10/25/50/75/100 %), S8 CPU, S9 LCD DMA (BL 50 %), S13 W25Q read, S14 W25Q erase/prog (0xF80000+), S15 game mix, S16 charge (3 readings); S10 (amp) skipped (P4). `current` per reading, stored in `g_hwcal.i_state_ma[state]` (rounded mA, RAM) |
| `power.rint` / `power.rint report` | [U] | no meter: R_int from the BQ24075 CC step (174.5 mA) on USB plug; per-state currents = dV vs S2 / R_int (estimates, hwcal.reserved0 bit0); stored in W25Q 0xFDE000 |
| `battlog` | auto | see below |

Commands: `adccal point <vbat_mv> <v3v3_mv> | fit | list | clear`, `cur <state> <mA>` (answers the open `power.current` reading prompt, else stores directly).

### battlog

`battlog start [dry=1] [resume=1] [new=1] [load=540] [rest=60] [samp=5] [cut=3300] [dur=0]` (host), `battlog stop`, `battlog status`,
`battlog dump [src=ram|flash]`; host: `hwtest.py battlog dump [--src ram|flash] [--out DIR]` writes `battlog-<date>.bin` + `.csv`.
Profile: 9 min S15 (sample 5 s) / 1 min S2 rest (1 s for 60 s, then 10 s), extra 15 min rests at est. 75/50/25/10 %, "CAL POINT DUE" at
Vcomp 4.10/3.90/3.75/3.60/3.45 V, cutoff after 3 consecutive loaded samples < 3.30 V, then "BATTERY EMPTY - CONNECT USB" and Stop (wake: VBUS via
EXTI9 on PA9, or OFF_REQ), then the charge phase (10 s samples) until 30 min after CHG goes high. `dry=1` logs to RAM (512 records) with shortened
timings (load 20 s, rest 10 s, 120 s total). Flash layout: 0xF00000 header page ('BLOG'), 0xF00100 end marker ('BEND'), records from 0xF01000
to 0xF7FFFF (32512). An unfinished log offers "continue" (A) / discard (B) on boot.

Record (16 B, little endian): `t_s u32, vbat_mv u16, vcomp_mv u16, vdda_mv u16, temp_dC i16, load_state u8, flags u8, seq u16`.
Flags: bit0 chg, 1 vbus, 2 rest, 3 cal_due, 4 marker (A press / resume), 5 cutoff, 6 step (load<->rest: loaded V just before the step, then
+1 s, +10 s), 7 chg_step (USB plug/unplug: V before, +1 s, +10 s, +60 s). Vcomp = Vbat + I(state) * R_int.
Dump: `@R {"t":"battlog_dump","records","frames","src",...}`, one `@B <len> <crc>` per <= 64 KB, then `@OK battlog dump`.
Scratch map: 0xF00000-0xF7FFFF battlog, 0xF80000-0xF8FFFF power.current S14, 0xFDE000 power.rint, 0xFDF000 power.handshake.
