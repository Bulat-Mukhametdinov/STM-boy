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
| `display.tear [auto=1] [t=<s>]` | [U] | `tear`/`cal` per write order x fps x panel Hz (`order` land_mv/native, `fps`, `frmctr1`), auto adds a `bench` `rotate` |
| `display.native [t=<s shown>]` | auto | `test` `mapping`: native-scan self-check (`madctl_phys`, native CASET/RASET of the 4 landscape corners, draw costs in us); draws corner markers + an asymmetric "F" + ramp |
| `display.vscroll [auto=1] [t=<s/phase>] [sps=60]` | [U] | HW scroll (VSCRDEF/VSCRSADD) vs full redraw: `bench` per phase `hw_60sps`/`hw_max` (`gen_cyc`,`write_cyc`,`vscrsadd_cyc`,`step_us`,`scroll_area`) and `sw_60fps`/`sw_max` (`render_cyc`,`present_cyc`,`frame_us`); LEFT/RIGHT phase, B exit |
| `demo [auto=1] [t=<s>] [scene=0..2] [fps=<cap>]` | [U] | real-time cubes / raycaster / raytracer on 8bpp+palette DMA present; `bench` per scene: `fps`,`render_ms`,`present_ms` (wait for DMA),`frame_ms`,`cpu_pct`,`sysclk_mhz`; keys: LEFT/RIGHT or X/Y scene, A overlay, CENTER fps cap, B exit |
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

## P4 audio suites

Hardware: PA8 = TIM1_CH1 PWM (AF1) -> RC LPF -> PAM8302A, SD# = PB2 (active high). Carriers (`cfg=0..3`): 750k/7b, 375k/8b,
187.5k/9b, 93.75k/10b. Engine A (`eng=a`): TIM5 ISR writes CCR1 per sample (any `fs=`). Engine B (default `eng=b`): TIM1 RCR +
TIM1_UP -> DMA2 S5 Ch6 circular 2x512 half buffers, HT/TC IRQ renders (rate = carrier/(RCR+1), e.g. 375k/8 = 46.875 kHz).
Safety: every suite starts with the master gain slewed from silence and the amp turned on with the ramped sequence (`ramp=<ms>`,
default `hwcal.amp_ramp_ms`); peak clip guard -6 dBFS (only `audio.clip` raises it to 0 dBFS, restored on exit); tone `db`
above the guard is clamped (`"clamped":true`); noise RMS is capped at guard-9 dB. All [U] suites open with the usual
`A=start B=skip` prompt (`t=` timeout). **B aborts at any time.** Common args: `cfg=` carrier (default `hwcal.audio_cfg_id-1`
if 1..4, else 1), `eng=a|b`, `fs=` (default 46875), `ramp=`.

Every step draws its id large on the LCD and emits a marker `@R {"t":"step","suite","id","l1","l2","f_hz","db","dur_ms","t_ms",
"n","of"}` (`t_ms` = ms since suite start; use it to line up the phone recording). `{"t":"mark",...}` lines carry sub-events
(sweep `gap`, matrix `tone`, pop `on`/`off`). Verdict prompts are list pickers (UP/DOWN/LEFT/RIGHT + A; B skips; host
`answer <id> ok` takes the highlighted entry) and emit `@R {"t":"cal","suite","name","value","saved_to":"ram"}`;
`audio.hiss`/`audio.matrix` also set `hwcal.audio_cfg_id` (= carrier index + 1) and `audio.pop` sets `hwcal.amp_ramp_ms`, in RAM
only (`hwcal save` persists).

| Suite | Kind | Content / results |
| --- | --- | --- |
| `audio.tone [f=1000] [db=-12] [s=5]` | [U] | step `T1`; `audio_stat` (engine, carrier, `rcr_div`, `fs_exp_mhz`, `fs_meas_mhz` from ISR timestamps over >= 1 s, `isr`/`dma_ht`/`dma_tc`, `late`, `dma_err`, `clipped`, `cyc_avg`/`cyc_max` per render body), `test` |
| `audio.sweep [db=-12] [s=3]` | [U] | 25 ISO 1/3-octave tones 50 Hz-12.5 kHz (ids `S01`..`S25`), 3 s each, then 0.5 s silence + 20 ms 1 kHz tick + 0.1 s (`mark gap`), ~110 s |
| `audio.chirp [db=-12] [s=10]` | [U] | 20 Hz-15 kHz exponential, ids `CH1`..`CH3`, 1 s gaps |
| `audio.noise [type=pink\|white] [s=30] [db=-15]` | [U] | step `N-PINK`/`N-WHITE` (`db` = RMS dBFS), `audio_stat`, `test` |
| `audio.hiss [s=15]` | [U] | 8 idle cases: `H-a` amp off, `H-b1/b2/b3` amp on with PA8 low/high/hi-Z, `H-c1..c4` amp on + PWM 50 % per carrier; pick quietest -> `cal hiss_best` (~2.5 min) |
| `audio.matrix [from=1] [to=48] [idle=8] [tone=8]` | [U] | 4 carriers x Fs {11.025,16,22.05,32 kHz} x dither {OFF,TPDF,NS = TPDF + 1st-order noise shaping}; ids `M01`..`M48`, 8 s idle then 1 kHz -6 dBFS; `mark tone` carries the real `fs_mhz`/`rcr_div`; pick best -> `cal matrix_best` (~13 min) |
| `audio.clip [s=2]` | [U] | 1 kHz then 200 Hz, -20..0 dBFS in 1 dB steps (ids `C1-20`..`C1+0`, `C2-20`..`C2+0`); picks -> `cal clean_max_db_1k`, `clean_max_db_200` (~95 s) |
| `audio.pop` | [U] | 18 amp on/off cycles `P01`..`P18`: NAIVE (PWM 50 % then SD#=1; SD#=0 at 50 %) alternating with RAMP 20/50/100 ms, x3; pick -> `cal amp_ramp_ms` (~70 s) |
| `audio.cpu` | auto, amp OFF | `audio_cpu` per config (engine A/B, 1/4 voices, dither): `cyc_per_sample`, `body_pct_x100` (render cost at 22.05 kHz), `load_pct_x100` (main-loop throughput loss incl. ISR entry/exit + USB), `late`, `dma_err` |

Matrix ids are carrier-major: `M01-M12` 750k/7b, `M13-M24` 375k/8b, `M25-M36` 187k/9b, `M37-M48` 94k/10b; inside a carrier Fs
11k,16k,22k,32k x dither OFF,TPDF,NS (`M19` = 375k/8b 22k OFF, `M20` TPDF, `M21` NS). Host analysis: `tools/audio_fr.py`
(FR table, -6 dB band edges, high-pass/shelf suggestion, matrix table, JSON). TIM5 is shared with `bench.sys` (wake latency): its
ISR is dispatched from the audio `TIM5_IRQHandler` when no audio engine runs.

## Block B: clock profiles and OC sweep

Boot is always 96 MHz (HSE 8 MHz, M=4, N=96, P=2, Q=4). `clock_set_mhz(N)` switches at run time: SYSCLK -> HSI, WS 7, PLL
reprogrammed (VCO = 2*N MHz, P in 2/4/6/8, Q = VCO/48 when integral, PLL-lock timeout -> error and fall back to 96, no reset),
target WS (ceil(N/30)-1, +1 above 100 MHz unless `ws=`), APB1 = SYSCLK/{1,2,4,8} <= 50 MHz, APB2 <= 100 MHz, SYSCLK -> PLL.
`clock_apply_derived()` then recomputes SystemCoreClock, SysTick 1 kHz, TIM2 1 MHz (count preserved), SPI1 (LCD, <= target:
the rate configured at the first switch, demo uses 48 MHz), SPI3 (W25Q, <= its configured rate, default 12 MHz), ADC prescaler
(/4,/6,/8, <= 24.5 MHz), TIM3 backlight 1 kHz, audio TIM1/TIM5/RCR (engine B Fs is quantised by the RCR divider, so it is only
nominal at integer carrier ratios; engine A is exact). USB: always stopped cleanly before the switch (host sees a disconnect and
COM re-enumerates) and restarted afterwards when the PLLQ output is exactly 48 MHz and HCLK >= 30 MHz; otherwise the device runs
without USB until it is back on a USB-capable clock.

- `clk info` -> `@R {"t":"clk","sysclk","hclk","pclk1","pclk2","tim1"(APB1 timers),"tim2"(APB2 timers),"ws","apb1_div","apb2_div","pll_n","pll_p","pll_q","spi1_hz","spi3_hz","adc_hz","bl_hz","usb_ok","usb_up","status"}`
- `clk set <MHz> [ws=N] [apb1_max=MHz] [apb2_max=MHz] [lcd=MHz] [w25q=MHz] [adc=MHz]` -> `@R {"t":"clk","plan":{...,"in_spec":bool}}`,
  then the switch (COM drops), then `clk info` + `@OK clk` if USB came back. 8..300 MHz. Not persistent: reset = 96 MHz.
- `demo`: hold CENTER + UP/DOWN steps through 48/72/84/96/120/144/168/192 (+ frequencies the sweep marked stable above 192);
  a CENTER tap (released without UP/DOWN) still toggles the fps cap. Exit returns to 96 MHz.
- `oc log [clear]` dumps the OC journal as `@R {"t":"oc","ev":"attempt"|"result"|"note",...}` lines + `{"ev":"log_end"}`;
  `oc stable` lists frequencies whose latest result is a pass; `oc status`.

Journal: W25Q 0xFE0000-0xFEFFFF, 32-byte CRC-protected records (read back after every write); copy of the pending ATTEMPT in
`.noinit`. IWDG (~2 s) is armed for the whole suite and only fed from `sys_service()` while the sweep is healthy (a hang or fault
stops the feed). On the next boot an ATTEMPT without RESULT becomes a FAIL record with the reset cause (iwdg/sft/bor/pin/por) or
`fault` (+PC) -- see `oc log` `boot_fail`. Temperature > 85 C aborts. AMP_SD is never raised. A suite always ends with a
reset (the only way to stop the IWDG).

- `run oc.sweep [list=48,64,84,96,100,120,144,168,192,216] [max=260] [step=8] [t=10]`: reference run at 96 MHz, then per
  frequency: ATTEMPT record, switch, checks (timebase TIM2 vs DWT, CPU int/float/memcpy kernels with identical checksums + soak
  `t*150 ms`, flash CRC x20, RAM march 16 KB, W25Q 1 MB CRC vs the 96 MHz reference, LCD raycaster 2 s -> fps, audio engine B
  1 s DMA block count -> Fs, ADC VDDA 3.0..3.6 V + die temperature, USB enumerated flag), RESULT record. After the list it goes
  on in `step` MHz until the first failure or `max`. First failure -> back to 96 MHz, record, stop. Emits
  `@R {"t":"oc","ev":"result",...}` per step (lost while USB is down: use `oc log`).
- `run oc.ws [list=96,120,144]`: lowest flash wait-state count that still passes the self-check with the ART caches off
  (records use step = 0x80|ws; a combination that already has a FAIL record is not retried).

## Block C: game stress, display, video, sleep

W25Q writes in this block stay inside 0xF80000-0xFDBFFF: `stream.video` 0xF80000-0xFDAFFF (header sector + 16 x 8bpp frames of
20480 B at 0xF81000 + 16 x 1bpp frames of 2560 B at 0xFD1000; only rewritten if the header/frame CRCs do not match, `regen=1`
forces), `stress.game` pattern sector 0xFDB000. Gradients/frames are 160x128 column-major (native scan order).

| Suite | Kind | Results |
| --- | --- | --- |
| `stress.game [s=60] [amp=0] [fps=60]` | auto | raycaster at `fps` (0 = unlimited) with native 8bpp DMA present (DMA2 S3, 48 MHz), audio engine B 4 voices 22.05 kHz (DMA2 S5; AMP_SD low unless `amp=1`), W25Q DMA 2 KB/frame from 0xFDB000 (CRC of 9 offsets computed at start), ADC batch every 250 ms, input scan. `progress` every 10 s (`max_us_10s`, `missed`, `late`, `uf`, `dma_err`, `crc_err`); `stress/summary` (`frames`, `fps_x100`, `frame_us_avg/max`, `missed` = frames ending after their slot, `cpu_pct_x100`, `audio_late` = HT+TC pending at ISR entry, `audio_uf` = DMA already wrapped into the half just rendered, `audio_blocks` vs `audio_expected`, `dma_err` = TEIF/DMEIF on S3/S4/S5/DMA1, `fe_lcd/fe_audio/fe_other` = FEIF (informational: direct mode sets FEIF3 on the LCD stream), `crc_err`, `crc_n`, `adc_bad`); `stress/dma_priorities` (PL bits read from the stream registers). Pass = late = uf = dma_err = crc_err = 0 (+ no W25Q timeouts, ADC sane). B or host `abort` ends it. |
| `soak [min=60] [amp=0] [fps=60]` | auto | same load for N minutes; `progress` every 10 s, `{"t":"soak","min":N,"temp_dC","vbat_mv","vdda_mv","frames","missed","max_us","late","uf","dma_err","dma_fe","crc_err","crc_n","adc_bad","vbus","ok"}` per minute, final `stress/summary`. Error counters are also in the on-screen overlay. |
| `display.colmod [auto=1] [t=3]` | [U] | COLMOD 0x03 / 0x05 / 0x06 gradient (gray/R/G/B ramps + hue sweep), full frame by DMA at 48 MHz in native order, `colmod` per mode: `bytes`, `frame_us_min/avg`, `fps_max`, `mbps`; A next, B exit (`answer colmod ok|fail` from the host); 0x05 restored. |
| `input.latency [n=10] [step=30] [marker=1]` | [U] | black screen, each button PRESS edge (EXTI, TIM2 us at ISR entry) toggles the whole screen black->white / white->black by DMA (window staged while idle, re-armed right after each present: no dead time, edges queue; releases ignored, presses < 5 ms apart = bounce). `latency` per press (`mode` = `black_to_white`/`white_to_black`, `edge_to_dma_us`, `edge_to_done_us`, `dma_us`), `latency_stats` per transition and `all`, `latency_note`. Scope marker (`marker=1`): PA8 (amp SD# LOW, PWM off -> plain GPIO) HIGH for 2 ms when the edge is processed, then back to input; probe point **PA8 = MCU side of R24 (3.3k)**. Firmware-side only: film screen + button at 240 fps for the end-to-end number. No press for `step` s ends it. |
| `media.play [loop=1] [auto=<s>] [lead=8000]` | | streams the media container from W25Q 0x200000 (below) and plays it: A overlay (fps, audio underruns, skipped/late), B exit; `auto=s` ignores keys and stops after s seconds. `media_play` result: fps, skipped, audio_underruns, w25q_MBps. Audio peak -9 dBFS. |
| `stream.video [s=4] [regen=1]` | auto | `video_store` (`valid`, `rewritten`, `erase_ms`, `program_ms`); then `video` per phase `8bpp_24MHz`, `8bpp_12MHz`, `1bpp_12MHz`, `1bpp_24MHz` (SPI3 rate): `fps`, `frame_ms`, `w25q_MBps_streamed`, `w25q_alone_us/MBps` (DMA read of one frame without the LCD), `cpu_ms_per_frame_x100`, `w25q_err`. The next frame is read by DMA into the idle framebuffer while the previous one is converted/presented. |
| `power.sleep [hold=<s>] [stop=0]` / `power.sleep report` | auto | `sleep_lcd` (DISPOFF/DISPON/SLPIN/SLPOUT command us, backlight on/off us, `lcd_sleep_in/out_ms`), `sleep_wfi` (TIM5 compare wake, spin vs WFI, min/med/max cycles), `sleep_stop` (RTC wake 2 s: `periph_sleep_us`, `stop_entry_us`, `clk_restart_cyc_hsi` (~cyc/16 = us for HSE+PLL), `periph_wake_ms`, `usb_reenum_ms`, `usb_how`), `sleep_hint` (states to measure with a meter; `hold=<s>` pauses in each `sleep_state` line). USB drops in the Stop part: the numbers stay in RAM, read them with `run power.sleep report`. |
| `power.ampdip [ms=200]` | auto, AMP_SD HIGH | F6 investigation: ADC1 IN1 continuous by DMA (ADCCLK 12 MHz, ~24 kS/s) around PWM on (amp off) -> AMP_SD high (naive) -> 1 kHz -12 dBFS tone 100 ms -> amp off -> 150 ms. `ampdip`: `vbus`, `chg`, `base_mv`, `min_mv`, `dip_mv`, `settle_after_on_us`, `settle_after_off_us`, per-phase min/mean/dip, event times; `ampdip_trace` (5 ms bin minima, mV); `ampdip_note`. With VBUS present the battery sits behind the BQ24075 power path: repeat on battery only for the real value. |

DMA priorities in use (PL bits): audio DMA2 S5 = 3 (very high, NVIC 5), LCD DMA2 S3 = 2 (high, NVIC 2), W25Q RX DMA1 S0 = 2, W25Q
dummy TX DMA1 S5 = 1, ADC DMA2 S4 = 1. Recommended: keep audio S5 very high, LCD S3 high, W25Q RX high/medium, ADC low.


## Media container and `media` commands

`media info` -> `@R {"t":"media_info",...}` (header at 0x200000, `valid`). `media write <addr_hex> <len>` (addr 64 KB aligned inside
0x200000-0xEFFFFF, the only region besides 0xF00000+ that the firmware may write; RetroPort ROM pack 0x100000-0x133670 is unreachable):
device answers `@OK media ready`, the host then sends exactly `len` raw bytes (USB NAK flow control), the device erases each 64 KB
block when reached (erase retried if it "finishes" in < 1 ms, finding F4), programs page by page, reads everything back and replies
`@R {"t":"media_write","crc":<readback zlib crc32>,"rx_crc":..,"ok":..}` + `@OK media write`. Host: `hwtest.py media upload file.bin`
(measured: 13.2 MB in ~94 s = 55 s erase + 35 s program + 6.5 s verify; flash-bound, not USB-bound).

Container (little endian), built by `tools/make_media.py`: header sector 0x1000 B (64 B header, rest 0xFF), then frames.
Header u32 fields: 0 magic 'SBMV' (0x564D4253), 4 version=1, 8 w=160, 12 h=128, 16 fps, 20 frames, 24 audio_rate (= fps x
audio_bytes_per_frame), 28 audio_bits=8, 32 frame_stride (=512+20480+apf), 36 audio_bytes_per_frame, 40 data_offset=0x1000, 44 total_len,
48 data_crc32 (zlib, over data_offset..total_len), 52 audio_gain_cdb (signed, centi-dB; player plays at min(gain, -9 dB)), 56 reserved,
60 hdr_crc32 (zlib over bytes 0..59). Per frame: 512 B RGB565 LE palette (254 colours; 254 = black, 255 = white for the overlay),
20480 B 8bpp pixels in native column-major order (px[x*128+y]), apf bytes unsigned 8-bit mono PCM (peak-normalised to full 8-bit
scale, high-passed at 400 Hz; played with -9 dB gain so the peak is -9 dBFS; the output clip guard is also -9 dBFS).
Attribution of the test clip: Big Buck Bunny (c) copyright 2008, Blender Foundation / www.bigbuckbunny.org, CC-BY 3.0
(the file lives in tools/build/, git-ignored, never committed).

Demo colour depth: `demo` key Y cycles COLMOD 0x05 16 bit / 0x03 12 bit / 0x03 + 4x4 Bayer dither / 0x06 18 bit live (`col=0..3` arg);
restored to 0x05 on exit; the overlay shows mode and present ms.
