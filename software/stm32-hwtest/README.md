# stm32-hwtest

Hardware test, calibration and benchmark firmware for the STM-boy v0.1 board
(STM32F411CEU6, HSE 8 MHz). Phase **P0**: project skeleton, USB CDC protocol,
software entry into the ST DFU bootloader, LCD + menu, 10-button input, HWCAL
record in external flash, firmware CRC, `selftest` / `spin` / `prompt` suites.
Phase **P1**: `pins` (pull classification + bridges), W25Q read-only
`read`/`crc`/`scan`, backlight PWM (TIM3_CH1) + `backlight`/`backlight.user`,
`display.*` calibration/visual suites, `input.live`/`input.chord`/
`input.bounce` (EXTI edge log). P3: `w25q.info/readcrc/scratch/sector/pd`, `ram.march`, `flash.check`.
P5: `bench.cpu/mem/flash/irq/lcd/gfx/w25q/audio/sys/all` (+ `tools/gen_budget.py`, `hwtest.py usbbench`).
Suite list and arguments: [PROTOCOL.md](PROTOCOL.md).

Results go to the host over USB CDC as `@R {json}` lines; see
[PROTOCOL.md](PROTOCOL.md). The host side is [tools/hwtest.py](tools/hwtest.py).

## Build

Requirements: STM32CubeCLT 1.20.0 (`arm-none-eabi-gcc` 13.3, `STM32_Programmer_CLI`
on `PATH`), GNU make, Git Bash, Python 3 (`py -3.14` by default; override with
`PYTHON=...`).

```sh
cd software/stm32-hwtest
make                 # -O2 (default)
make OPT=-Os         # or -O3; the level is reported by `info` ("opt")
make clean
```

Vendor code (CMSIS, STM32F4 HAL, ST USB device library) is **not** copied: the
Makefile compiles it from `HALDIR` (default `../stm32-bringup`) into
`build/vendor/...`. Override with `make HALDIR=/path/to/tree`.

Outputs in `build/`:

| File | Purpose |
| --- | --- |
| `hwtest.bin` | canonical image for `0x08000000`, CRC trailer patched by `tools/append_crc.py` |
| `hwtest.hex` | same bytes as the bin (generated from it) |
| `hwtest.elf` / `.map` | debugging |
| `fw_id.json` | `{"fw":"<git short hash>[+dirty]","build":<unix time>}`; the same values are compiled in and reported by `info` |

The build identity lives in `Src/fw_id.c`, which is recompiled on every `make`
(so every `make` relinks).

### Image CRC

The last word of the image (`.crc_trailer`, address `_flash_crc_end`, placed
after the `.data` load image) holds the STM32 hardware-CRC of every byte before
it (poly `0x04C11DB7`, init `0xFFFFFFFF`, 32-bit LE words, MSB first, no
reflection/final XOR). `tools/append_crc.py` patches it after linking;
`--check <bin>` verifies, `--selftest` runs known-answer tests. On the device,
the `crc` command and the `selftest` suite recompute it with the CRC unit.

## Flashing

### First flash (once)

The board must already run stm32-hwtest before `make dfu` can work. Either:

* **BOOT0 + RESET (USB DFU):** hold BOOT0, tap RESET, release BOOT0. The board
  enumerates as `0483:DF11`. Do not touch any buttons while in the bootloader
  (CENTER = PA10 is USART1_RX of the bootloader). Then
  ```sh
  STM32_Programmer_CLI -c port=USB1 -w build/hwtest.bin 0x08000000 -v -s
  ```
* **ST-Link (SWD):** `make flash`
  (`STM32_Programmer_CLI -c port=SWD -w build/hwtest.bin 0x08000000 -v -rst`).

If `STM32_Programmer_CLI -l usb` does not list the board in DFU mode, make sure
"STM32 Bootloader" uses ST's driver (`stm32bootloader.inf`) in Device Manager.

### Every later flash

```sh
make dfu      # = py -3.14 tools/hwtest.py flash build/hwtest.bin
```

`hwtest.py` asks the running firmware to enter DFU (`dfu` command, or the
1200-baud touch), flashes over USB, waits for the CDC port to come back and
checks that `info.fw`/`info.build` match `build/fw_id.json`.

### How software DFU entry works

1. `dfu` command, 1200-baud touch (`SET_LINE_CODING` = 1200, handled inside the
   USB interrupt so it works while a long test runs) or menu "Enter DFU" call
   `dfu_request()`: magic `0xDF11B007` + validity word into the `.noinit` block
   at the top of SRAM (`0x2001FF00`), then `NVIC_SystemReset()`.
2. `Reset_Handler` first does `cpsid i` and calls `early_boot_check()`
   (`Src/early_boot.c`) before `SystemInit` and `.data/.bss` init. It sees the
   magic, clears it, maps system flash at 0, loads SP/PC from `0x1FFF0000` and
   jumps into the ST bootloader from a clean post-reset state.
3. When the bootloader exits (`-s`) it jumps into the application without a
   reset, with PLL/USB still running. `early_boot_check()` detects SYSCLK != HSI
   and performs exactly one clean `NVIC_SystemReset()` (guarded by a counter in
   `.noinit`, cleared after a successful boot). `info.boot` then reports
   `"bootloader_exit"`. `SystemInit()` always sets `VTOR = 0x08000000`.

Recovery if the application is broken: BOOT0 + RESET always reaches the ROM
bootloader (it cannot be erased), or SWD connect-under-reset:
`STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst -w build/hwtest.hex -v -rst`.

## Using it

* Host: `py -3.14 tools/hwtest.py info`, `... run selftest`, see `tools/README.md`.
* Device menu (UP/DOWN move, A or CENTER select, B back): Self test, Tests...
  (scrolling list of every suite; [U] = needs a human), Button
  view (live 10-button map; hold B 1 s to leave), Display info (MADCTL/offsets,
  1 px border, corner markers TL red / TR green / BL blue / BR yellow),
  USB/Info, Enter DFU, Reboot. The status bar shows the firmware hash and the
  COM state (grey = not enumerated, yellow = enumerated, green = port open).
* `@P` prompts: tests that need a human show the question on screen and emit
  `@P {...}`; answer with A (ok) or B (fail) on the device; the device emits
  `@A {...}`. `run prompt [timeout_s]` exercises this path.

Button mask (bit order, also used by later phases and the game firmware
contract): 0 UP, 1 DOWN, 2 LEFT, 3 RIGHT, 4 CENTER, 5 A, 6 B, 7 X, 8 Y,
9 POWER (= OFF_REQ asserted by the ATtiny). 1 kHz scan, 20 ms debounce.

## Memory map

Internal flash: image at `0x08000000`, CRC trailer last. Internal SRAM:

| Region | Address | Size | Notes |
| --- | --- | --- | --- |
| `.data` + `.bss` | `0x20000000` | ~17.7 KB | 8 KB CDC TX ring, line queue, buffers |
| `.noinit_arena` (`g_arena`) | after `.bss` | 72 KB | not zeroed; RAM tests / benchmarks / frame buffers |
| heap + stack | above | 1 KB + >= 4 KB (all remaining RAM) | stack top `0x2001FF00` |
| `.noinit` control block | `0x2001FF00` | 256 B | DFU magic, clean-reset counter, fault breadcrumbs; survives resets |

## External flash (W25Q128) layout

| Range | Owner | Use |
| --- | --- | --- |
| `0x000000-0x0FFFFF` | stm32-retroport | settings / state |
| `0x100000-0xEFFFFF` | stm32-retroport | ROM pack (14 MiB; was 15 MiB, shrunk in `Core/Inc/flash_layout.h`) |
| `0xF00000-0xFDFFFF` | stm32-hwtest | scratch for destructive tests and the battery log |
| `0xFE0000-0xFEFFFF` | stm32-hwtest | overclock / bus-limit journal |
| `0xFF0000-0xFFEFFF` | - | unassigned |
| `0xFFF000-0xFFFFFF` | stm32-hwtest | HWCAL record (one 4 KB sector, halves A `0xFFF000` / B `0xFFF800`) |

Measured on the v0.1 board (2026-11-13, read-only scan, see HW_LOG.md): settings log
`RFS1` in `0x000000-0x001000`; RetroPort ROM pack `RPRP` v2, 142 ROMs, 205.6 KB in
`0x100000-0x133670`; the last 1 MB is blank except `0xFFF000-0xFFFFFF`, which holds a
walking-ones test pattern (`01 02 04 .. 80` x 512) of unknown origin. That pattern is
why HWCAL reports `foreign`; no user data lies in the hwtest region.

HWCAL record = header `{magic "HWC1", version, len, seq, crc32 (zlib)}` +
payload (`Inc/hwcal.h`). Load picks the valid half with the highest `seq`.
Saves ping-pong between halves and only erase the sector when the target half
is not blank (details and the power-loss window in `Src/hwcal.c`). Before the
first write the sector is scanned: if it holds non-0xFF data that is not a
HWCAL record, writes are refused unless forced (`hwcal init force`).

## Safety rules

* The firmware never writes internal flash or option bytes.
* External flash writes are limited to HWCAL (`hwcal init`) in P0; later phases
  only write the scratch / journal regions above.
* `PB2` (AMP_SD) is held low (amplifier off) and PA8 is left as an input in P0/P1.
  The `pins` suite lifts PB2 to a pull-up for at most 500 us with IRQs off.
* LCD backlight: TIM3_CH1 PWM on PA6 (AF2), `Inc/backlight.h`; default full on.
* LCD geometry defaults (calibrated with `display.cal`): landscape MADCTL 0xA0,
  x offset 1, y offset 2; portrait 0x00, x 2, y 1 (`lcd_offsets_for()`).
* `OFF_REQ` (PA2) is honoured at all times (also during suites): the screen is
  blanked and `OFF_ACK` (PA3) driven high, as in stm32-bringup.
* Hard faults store PC/LR/CFSR/HFSR in `.noinit` and reset; `info.faults` /
  `info.fault` report them.
* Do not press buttons while the board is in the ROM bootloader.

## Layout

```
Makefile  STM32F411CEUX_HWTEST.ld  Startup/startup_stm32f411xe.s
Inc/   board clock timebase log cmd suites crc hwcal lcd gfx font input power w25q
       backlight ui usb dfu noinit fw_id + HAL/USB config headers
Src/   main early_boot system_stm32f4xx stm32f4xx_it board clock timebase dfu
       log cmd suites crc hwcal fw_id syscalls
  drv/ lcd gfx font5x7_full input power w25q backlight
  ui/  menu prompt t_display t_flash t_pins t_input t_backlight
  usb/ usb_device usbd_conf usbd_desc usbd_cdc_if
tools/ append_crc.py hwtest.py
```

The 5x7 font (`Src/drv/font5x7_full.c`) is the classic public-domain 5x7
dot-matrix ASCII set, transcribed for this project (treat as CC0).
