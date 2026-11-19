# Hardware Reference: v0.1

Single entry point for anyone writing game firmware for the STM-boy v0.1
board. It collects every hardware fact and proven recipe found with the
`stm32-hwtest` firmware so nothing has to be re-discovered.

> Status: first draft, updated 2026-11-19. Sections that depend on work that has not
> been done yet are marked "Not measured yet" or "Pending". Raw evidence lives
> in [software/stm32-hwtest/HW_LOG.md](../software/stm32-hwtest/HW_LOG.md) and
> `software/stm32-hwtest/tools/results/<uid>/*.json`; the running findings list
> is [software/stm32-hwtest/FINDINGS.md](../software/stm32-hwtest/FINDINGS.md).

## Contents

1. [Scope And How Values Were Measured](#1-scope-and-how-values-were-measured)
2. [Pin Map](#2-pin-map)
3. [Clocks](#3-clocks)
4. [Peripheral Allocation](#4-peripheral-allocation)
5. [Memory](#5-memory)
6. [Display Recipe](#6-display-recipe)
7. [Backlight](#7-backlight)
8. [Input](#8-input)
9. [Audio](#9-audio)
10. [External Flash](#10-external-flash)
11. [Power And Battery](#11-power-and-battery)
12. [Capability Budget And Benchmarks](#12-capability-budget-and-benchmarks)
13. [Platform Layer Contract Proposal](#13-platform-layer-contract-proposal)
14. [Flashing And Recovery](#14-flashing-and-recovery)
15. [Errata And v0.2 Recommendations](#15-errata-and-v02-recommendations)

## 1. Scope And How Values Were Measured

This document describes one assembled **STM-boy v0.1** board (STM32F411CEU6,
UFQFPN48, 512 KB flash, 128 KB SRAM, 8 MHz HSE). Hardware identity:

| Item | Value | Basis |
| --- | --- | --- |
| MCU UID | `002800343435511331323538` | MEASURED 2026-11-13, `flash.check` |
| USB serial / DFU serial | `315A356C3435` (derived from the UID) | MEASURED 2026-11-13 |
| DBGMCU | DEV_ID `0x431`, REV_ID `0x1000` (Rev A), bootloader `0xD0` | MEASURED 2026-11-13, SWD |
| Option bytes | RDP 0, no WRP, BOR **off** (OPTCR `0x0FFFAAED`) | MEASURED 2026-11-13 |
| External flash | W25Q128 (JEDEC `EF4018`), UID `E463A8F31B121931` | MEASURED 2026-11-13 |

Test firmware: [software/stm32-hwtest](../software/stm32-hwtest) (branch
`hwtest-firmware`), built with `arm-none-eabi-gcc` 13.3, `-O2`, results sent to
the host as JSON over USB CDC. Dates in this document are the dates of the
measurement sessions (2026-11-13 to 2026-11-18).

Instruments:

| Instrument | Used for |
| --- | --- |
| Multimeter (series in the battery lead, 200 mA range) | Per-state currents, VBAT/3V3 ADC calibration |
| Oscilloscope Tektronix TDS3032B (300 MHz, x10 probe) | SPI1 SCK, backlight PWM |
| Phone (microphone) | Audio response (planned, not done) |
| Visual inspection by the board owner | Orientation, colors, tearing, scrolling |
| Firmware self-measurement (DWT cycle counter, TIM2 1 MHz) | CPU, DMA, flash, USB benchmarks |

Every value carries one of these tags:

| Tag | Meaning |
| --- | --- |
| MEASURED | Observed on this board; date and source given |
| DESIGN | From schematic notes, datasheets, plan, or firmware defaults; not verified on the board |
| DERIVED | Computed from MEASURED values; the arithmetic is shown or obvious |

> Warning: this is a single board. Part-to-part spread (LCD offsets, battery
> R_int, button bounce, ADC gain) is not characterised.

## 2. Pin Map

Verified-by legend: **pins** = `pins` suite (pull classification and neighbour
bridge test, 2026-11-13, 43 of 44 checks as expected, only PB2 differed);
**scope** = oscilloscope; **visual** = seen by the board owner; **suite** =
named hwtest suite; **bring-up** = [bring-up-log-v0.1.md](bring-up-log-v0.1.md).
The signal names match [pinout.md](pinout.md).

| Pin | Net | Function / AF | External pull | EXTI | Verified by |
| --- | --- | --- | --- | --- | --- |
| PA0 | CHRG_STAT | GPIO in; low while charging | R9 10k up | 0 | pins; `power.info` (CHG low on charger, 2026-11-17) |
| PA1 | BAT_STAT | ADC1_IN1, divider 33k/82k from BAT | none | 1 | pins; ADC gain 1.000 vs multimeter (2026-11-13) |
| PA2 | OFF_REQ | GPIO in, active-low from ATtiny | R17 100k up | 2 | pins; `power.handshake` |
| PA3 | OFF_ACK | GPIO out push-pull, active-high | R20 10k series to ATtiny | 3 | pins; `power.handshake` |
| PA4 | CS_D | GPIO out (LCD chip select) | R30 10k up | 4 | pins; visual |
| PA5 | SCK_D | SPI1_SCK, AF5 | none | 5 | pins; scope 48.01 MHz (2026-11-17) |
| PA6 | BLK | TIM3_CH1, AF2 (backlight) | R27 1k to NPN base, R28 10k base-GND | 6 | pins; scope 1.000 kHz / 49.99 % (2026-11-16) |
| PA7 | MOSI_D | SPI1_MOSI, AF5 (TX only) | none | 7 | pins; visual |
| PA8 | AUDIO_PWM | TIM1_CH1, AF1 | R24 3k3 into RC filter | 8 | pins; `audio.*` electrical only |
| PA9 | VBUS | GPIO in via divider R4 33k / R5 82k | MCU internal pull-down on (workaround) | 9 | pins; bring-up; F8 |
| PA10 | CE_BTN | GPIO in, joystick centre | internal pull-up | 10 | `input.live` |
| PA11 | D- | OTG_FS DM, AF10 | none | - | USB CDC works |
| PA12 | D+ | OTG_FS DP, AF10 | none | - | USB CDC works |
| PA13 | SWDIO | SWD | none | - | SWD connect (2026-11-13) |
| PA14 | SWCLK | SWD | none | - | SWD connect (2026-11-13) |
| PA15 | CS_F | GPIO out (flash chip select) | R3 10k up | 15 | pins; `w25q.*` |
| PB0 | DC | GPIO out (LCD data/command) | none | 0 | pins; visual |
| PB1 | RST_D | GPIO out (LCD reset) | none | 1 | pins; visual |
| PB2 | AMP_SD | GPIO out, PAM8302A shutdown, high = amp on; also **BOOT1** | R23 100k down (reads much stronger, F2) | 2 | pins (mismatch, F2) |
| PB3 | SCK_F | SPI3_SCK, AF6 (default JTDO/SWO) | none | 3 | pins; `w25q.*` |
| PB4 | MISO_F | SPI3_MISO, AF6 (default NJTRST) | none | 4 | pins; `w25q.*` |
| PB5 | MOSI_F | SPI3_MOSI, AF6 | none | 5 | pins; `w25q.*` |
| PB6 | RIGHT_BTN | GPIO in | internal pull-up | 6 | `input.live` |
| PB7 | LEFT_BTN | GPIO in | internal pull-up | 7 | `input.live` |
| PB8 | DOWN_BTN | GPIO in | internal pull-up | 8 | `input.live` |
| PB9 | UP_BTN | GPIO in | internal pull-up | 9 | `input.live` |
| PB12 | X_BTN | GPIO in | internal pull-up | 12 | `input.live` |
| PB13 | A_BTN | GPIO in | internal pull-up | 13 | `input.live` |
| PB14 | B_BTN | GPIO in | internal pull-up | 14 | `input.live` |
| PB15 | Y_BTN | GPIO in | internal pull-up | 15 | `input.live` |
| PB10, PC13-15 | not connected | free (control pins in `pins`) | none | - | pins |
| BOOT0 | strap | high at reset selects the ROM bootloader | - | - | DFU tests |

Notes: pull values and "why" strings come from the `pins` suite expectations
(MEASURED classification: TIED_HIGH, WEAK_HIGH, FLOATING as listed). PA2 reads
WEAK_HIGH, PA0/PA4/PA15 TIED_HIGH, PA6 TIED_LOW, buttons FLOATING (internal
pull-up needed). `BAT_STAT` and `CHRG_STAT` are swapped relative to the first
pinout notes; the table above is the corrected, verified assignment.

### Conflicts And Traps

| Trap | Detail | Recipe |
| --- | --- | --- |
| EXTI9 shared | PA9 (VBUS) and PB9 (UP) both map to EXTI line 9 | Poll VBUS; use EXTI9 only for UP, or for VBUS only while in Stop |
| PA6 multi-function | BLK = TIM3_CH1 (AF2); same pin is SPI1_MISO and TIM1_BKIN | Run SPI1 TX-only (BIDIMODE/BIDIOE); never enable TIM1 break input |
| PB3 / PB4 JTAG default | After reset PB3 = JTDO/SWO, PB4 = NJTRST | Configure explicit AF6 for SPI3 (done in `w25q.c`); SWD stays usable |
| PA15 JTDI default | PA15 is CS_F | Configure as GPIO output right after reset |
| PB2 = BOOT1 | AMP_SD doubles as BOOT1 strap | PB2 is pulled low strongly (F2), so BOOT0 + reset enters system memory as expected; do not drive it high during reset |
| PA10 = USART1_RX of ROM bootloader | CENTER button | Do not press buttons while the board is in the ROM bootloader |
| No UART, no SWO | PB3 (SWO) is SPI3_SCK; no spare UART pads | Debug output only over USB CDC, or SWD halt/read |
| PA9 / USB VBUS | VBUS divider node floats near 1.7 V without USB (F8) | Internal pull-down on PA9 gives about 1.1 V idle |
| BOR is off | Option byte BOR level 3 (1.8-2.1 V) | Consider enabling a higher BOR for battery use (needs SWD, not done by hwtest) |

## 3. Clocks

MEASURED 2026-11-13 (`selftest` clock check, bring-up RCC read-out):

| Item | Value |
| --- | --- |
| Source | HSE 8 MHz crystal, PLL |
| PLL | M = 4 (2 MHz), N = 96 (VCO 192 MHz), P = 2, Q = 4 |
| SYSCLK / AHB | 96 MHz |
| APB1 | 48 MHz (/2), timer clock 96 MHz (x2 rule) |
| APB2 | 96 MHz (/1), timer clock 96 MHz |
| USB | 48 MHz exactly (VCO / Q) |
| Flash | latency 3 wait states, ART I-cache + D-cache + prefetch on |
| ADC | PCLK2 / 4 = 24 MHz |
| SPI1 (LCD) | PCLK2 / 2 = 48 MHz maximum |
| SPI3 (W25Q) | PCLK1 / 2 = 24 MHz maximum |
| TIM2 | 1 MHz free-running 32-bit time base (`time_us()`) |
| SysTick | 1 kHz, priority 1 |

96 MHz is inside the STM32F411 specification (100 MHz maximum) and is the boot
frequency in `stm32-hwtest`. Wait states and ART settings matter: with 3 WS the
ART gives about 28 % on a small loop (18.8 vs 26.0 cycles/iteration, `bench.flash`,
2026-11-13), and running code from SRAM is slower than flash with ART.

### Overclock And Underclock

MEASURED 2026-11-18 with `oc.sweep` (clock profile in `Src/clock_profile.c`:
every SYSCLK change re-derives SysTick, TIM2 1 MHz, SPI1/SPI3 dividers, ADC
prescaler, backlight and audio timers; IWDG + OC journal at W25Q
`0xFE0000-0xFEFFFF`; the device always boots at 96 MHz). Die temperature stayed
at ~28 C. Amplifier off during the sweep.

| SYSCLK MHz | Flash WS | APB1/APB2 div | CPU Mops/s | LCD fps (full frame DMA) | W25Q 1 MB CRC | Audio Fs | Result |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 48 | 1 | 1/1 | 47 | 72 | ok | exact | pass |
| 64 | 2 | 2/1 | 63 | 96 | ok | exact | pass |
| 84 | 2 | 2/1 | 83 | 126 | ok | exact | pass |
| **96** | 3 | 2/1 | 95 | **144** | ok | exact | pass |
| 100 | 3 | 2/1 | 99 | 76 (SPI1 limited to 25 MHz) | ok | exact | pass |
| 120 | 4 | 4/2 | 119 | 91 (SPI1 30 MHz) | ok | exact | pass |
| **144** | 5 | 4/2 | 143 | 109 (SPI1 36 MHz) | ok | exact | pass, **max stable** |
| 152 | 6 | 4/2 | - | - | - | - | FAIL: silent wrong results |
| 168 | 6 | 4/2 | - | - | - | - | FAIL: HardFault |

- CPU throughput scales exactly with SYSCLK; audio Fs stayed within -21 ppm of
  the timer-derived value at every step (timers re-derived correctly).
- **Overclocking does not speed up the display.** Keeping APB2 within spec
  above 100 MHz needs APB2 = SYSCLK/2, so SPI1 = SYSCLK/4 < 48 MHz. **96 MHz is
  the optimum for graphics**; overclock only helps compute-bound work (e.g. the
  ray tracer). Overclocking APB2 itself (SPI1 > 48 MHz) was not tested.
- Minimum flash wait states with ART caches off (`oc.ws`): 2 at 96 MHz, 2 at
  120 MHz, 3 at 144 MHz (spec rule: 3/4/5). One step lower hangs. No margin -
  use the spec values.
- Everything above 100 MHz is out of spec and varies chip to chip: **do not
  ship firmware that depends on it.** At most an optional "turbo" profile.
- USB works only on SYSCLK values giving an exact 48 MHz from PLLQ (96, 120,
  144 MHz in the stable range); the firmware stops/restarts USB around switches.
- Not measured yet: current versus frequency (state S17).

> Warning: anything above 100 MHz is outside the datasheet. When results exist
> they will be marked "not for release".

#### Dynamic Frequency Guidance (DERIVED from the sweep and block C)

| Situation | SYSCLK | Why |
| --- | --- | --- |
| Menus, pause, idle screens, low battery | 48-84 MHz | Scale **down**: CPU load is low there; the display still reaches 72-126 fps |
| Normal gameplay | 96 MHz | Display optimum (SPI1 48 MHz, 146 fps), inside the specification |
| Compute-bound scene (e.g. ray tracer) | 96-144 MHz | Optional out-of-spec "turbo"; USB needs 96, 120 or 144 |

- The display gets **slower** above 96 MHz (APB2/SPI1 divided: 76 fps at 100,
  91 at 120, 109 at 144 MHz), so turbo only pays when the scene is compute
  bound and renders well below the present time.
- A switch costs about 1 ms plus a USB stop/start and re-enumeration (about
  350 ms, see section 11); switch on scene changes, never per frame.
- Never make the game depend on turbo: it is chip-specific and out of spec.

## 4. Peripheral Allocation

The allocation below is what `stm32-hwtest` actually uses (verified in source,
2026-11-18) and is proposed as the game firmware allocation. The original plan
table differed in IRQ priorities (audio DMA 2, LCD DMA 3); the implemented
priorities are listed here.

### DMA

| Stream / channel | Peripheral | Use | NVIC priority | DMA stream priority |
| --- | --- | --- | --- | --- |
| DMA2 S3, Ch3 | SPI1_TX | LCD frames (chunk ping-pong) | 2 | high |
| DMA2 S5, Ch6 | TIM1_UP | Audio sample stream into `TIM1->CCR1` (engine B) | 5 | very high |
| DMA1 S0, Ch0 | SPI3_RX | W25Q reads | none (polled completion) | high |
| DMA1 S5, Ch0 | SPI3_TX | W25Q clock-out of dummy bytes during reads | none | medium |
| DMA2 S4, Ch0 | ADC1 | VBAT / VREFINT scan (480-cycle samples) | none | medium |
| DMA2 S7 | memory to memory | benchmark only; never beats LDM/STM copy | - | - |

The DMA stream priorities are the ones actually used (MEASURED 2026-11-18,
block C: no underrun, DMA error or CRC error under full game-like load). NVIC
priority is the interrupt priority; the stream priority arbitrates the DMA
bus. The LCD stream reports the FIFO error flag (FEIF) about 19 times per frame
in direct mode: benign, not a failure.

Free: DMA1 S1-S4, S6-S7, DMA2 S0-S2, S6.

### Timers

| Timer | Use |
| --- | --- |
| TIM1 | Audio PWM carrier on PA8 (CH1), update event paces the audio DMA |
| TIM2 | 1 MHz time base (32-bit) |
| TIM3 | Backlight PWM on PA6 (CH1), 1 kHz |
| TIM5 | Audio engine A sample interrupt (priority 3); free in engine B |
| RTC (LSI) | Stop-mode wake-up timer (Stop test and battery logger) |
| IWDG | Overclock sweep supervision only |
| Free | TIM4, TIM9, TIM10, TIM11 (plan reserved TIM11 for a HSE measurement, not used) |

### IRQ Priorities (NVIC, 4 bits, no sub-priority)

| Priority | Source |
| --- | --- |
| 1 | SysTick (1 kHz tick and button scan) |
| 2 | DMA2 S3 (display) |
| 3 | TIM5 (audio engine A) |
| 5 | DMA2 S5 (audio engine B refill) |
| 6 | OTG_FS (USB CDC; the 1200-baud touch is handled here) |
| 7 | EXTI2 (OFF_REQ wake), EXTI9_5 / EXTI15_10 (button edges), RTC_WKUP |

### Other Resources

| Resource | State |
| --- | --- |
| ADC1 | VBAT on IN1, VREFINT (IN17), temperature (IN18) |
| SPI1 / SPI3 | LCD / W25Q |
| OTG_FS | USB device, CDC in `stm32-hwtest` |
| Free GPIO | PB10, PC13-15 exist on the MCU but are not routed to pads |
| Free peripherals | I2C, USART1/2/6 (pins not routed or used), SPI2, SDIO not available on UFQFPN48 |

## 5. Memory

### RAM

STM32F411: 128 KB SRAM at `0x20000000`; the linker script reserves the top
256 B (`0x2001FF00`) as a `.noinit` control block (DFU magic, fault
breadcrumbs, survives reset).

MEASURED 2026-11-18 from `arm-none-eabi-size` / `nm` of the current build
(work in progress; numbers drift as the test firmware grows):

| Region | Size | Notes |
| --- | --- | --- |
| Static data + bss (outside the arena) | about 50 KB | CDC TX ring 8 KB, edge log 4 KB, scratch buffers; test firmware only |
| Test arena `g_arena` | 72 KB (`0x12000`) | Not zeroed; frame buffers and benchmark buffers |
| Heap + stack | 1 KB + at least 4 KB, about 7 KB left | Stack top `0x2001FF00` |
| `.noinit` block | 256 B | |

The 2026-11-13 `bench.all` budget reported 80,992 B free including the arena and
7,264 B free outside it (older, smaller build).

Recommended buffers for a game firmware (DESIGN, arithmetic only):

| Buffer | Size | Verdict |
| --- | --- | --- |
| 8bpp frame 160x128 | 20,480 B | |
| 2x 8bpp (double buffer) | 40,960 B | **Fits** with room left (about 65 KB for the game after USB, audio, stack) |
| Palette 256 x RGB565 | 512 B | |
| LCD chunk ping-pong 2 x 8 lines x 160 x 2 B | 5,120 B | Used by the present pipeline |
| Audio double buffer 2 x 512 x 2 B | 2,048 B | Engine B |
| RGB565 frame | 40,960 B | |
| 2x RGB565 (double buffer) | 81,920 B | **Does not fit** comfortably: it is larger than the 72 KB test arena and leaves under 30 KB for everything else; use 8bpp + palette |

RAM test: `ram.march` (March C-, two backgrounds) found 0 errors in 128,000 B
tested (3,072 B live stack untested), 2026-11-13.

### Internal Flash

512 KB at `0x08000000`; the test image is about 240 KB (last word is the
firmware CRC trailer). The firmware never writes internal flash or option bytes.

### External Flash Layout (W25Q128, 16 MB)

| Range | Owner | Use | Basis |
| --- | --- | --- | --- |
| `0x000000-0x0FFFFF` | stm32-retroport | Settings log (`RFS1` at `0x000000-0x001000`) | MEASURED 2026-11-13 (read-only scan) |
| `0x100000-0xEFFFFF` | stm32-retroport | ROM pack `RPRP` v2 (142 ROMs, 205.6 KB at `0x100000-0x133670`); 14 MiB reserved | MEASURED 2026-11-13 |
| `0xF00000-0xFDFFFF` | stm32-hwtest | Scratch for destructive tests, power-handshake breadcrumbs, battery log | DESIGN (policy) |
| `0xFE0000-0xFEFFFF` | stm32-hwtest | Overclock / bus-limit journal (32-byte records) | DESIGN (block B) |
| `0xFF0000-0xFFEFFF` | - | Unassigned | |
| `0xFFF000-0xFFFFFF` | stm32-hwtest | HWCAL record, one 4 KB sector, halves A `0xFFF000` and B `0xFFF800` | MEASURED 2026-11-13 / 2026-11-17 |

The last 1 MB was blank (except a walking-ones pattern at `0xFFF000` of unknown
origin, since overwritten by HWCAL) when scanned on 2026-11-13.

> Warning: a game firmware that reuses the 15th MiB must treat HWCAL as board
> calibration (display offsets, ADC gain, OCV table). Keep sector `0xFFF000` or
> move the record deliberately; see [section 11](#11-power-and-battery).

HWCAL contents ([hwcal.h](../software/stm32-hwtest/Inc/hwcal.h)): header
`{"HWC1", version, len, seq, crc32 (zlib)}` plus payload with display
(`madctl_*`, offsets, bgr, invert, gamma, SPI divider), debounce, backlight
(Hz, min level), `rint_mohm`, `vbat_gain_q16`, `vbat_offset_mv`, `i_chg_ma`,
gauge state, `i_state_ma[18]` and `ocv_mv[21]`. Load picks the valid half with
the highest `seq`; saves ping-pong between halves.

> Warning: F4 (MITIGATED 2026-11-18). `hwcal save` sometimes failed with
> `erase_verify` (SR1 = 02, WEL still set) on the 4 KB erase of `0xFFF000`
> (twice in a row on 2026-11-17; a 4 KB erase at `0xFE0000` was also rejected,
> while the 64 KB erase `0xD8` worked). The driver function
> `w25q_erase_sector()` returned OK in about 11 us without erasing. It now goes
> through `w25q_erase_cmd(0x20)` (55-65 ms, waits for BUSY) and retries when
> the erase finishes in under 1 ms; three consecutive `hwcal save` including a
> sector erase passed. The root cause of the rejected `0x20` command is still
> unknown: check that the erase really took 50+ ms, and retry once.

## 6. Display Recipe

Panel: 1.8 inch ST7735S TFT, 128x160, SPI, write-only (MISO is not wired to the
LCD, no TE pin). Controller GDDRAM is 132x162; the visible 128x160 window sits
inside it with fixed slack.

### Geometry (MEASURED 2026-11-13, `display.cal`, `display.orient`)

| Orientation | MADCTL | X offset | Y offset | Size |
| --- | --- | --- | --- | --- |
| Landscape ("raw") | `0xA0` (MY + MV) | 1 | 2 | 160x128 |
| Portrait | `0x00` | 2 | 1 | 128x160 |
| Landscape in **native scan order** (default in the test firmware) | `0x80` (MY, MV = 0) | row +1 | col +2 | 160x128 logical |

The older bring-up values (column start 3, row start 1) were wrong: they lost
the right column and the top row. Other facts: RGB order (BGR bit = 0),
inversion **off**, gamma table "sitronix" (id 2: the Waveshare ST7735S sample
`E0/E1`) was chosen by eye. Both slacks are symmetric, so mirroring does not
change them.

### Init Sequence (from `Src/drv/lcd.c`, works on this board)

1. Hardware reset: RST low 20 ms, high, wait 150 ms.
2. `SWRESET` (`0x01`), wait 150 ms; `SLPOUT` (`0x11`), wait 255 ms.
3. `FRMCTR1/2` (`0xB1/0xB2`) = `01 2C 2D`; `FRMCTR3` (`0xB3`) = `01 2C 2D 01 2C 2D`;
   `INVCTR` (`0xB4`) = `07`.
4. `PWCTR1` (`0xC0`) = `A2 02 84`; `PWCTR2` (`0xC1`) = `C5`; `PWCTR3` (`0xC2`) =
   `0A 00`; `PWCTR4` (`0xC3`) = `8A 2A`; `PWCTR5` (`0xC4`) = `8A EE`;
   `VMCTR1` (`0xC5`) = `0E`.
5. `INVOFF` (`0x20`), `COLMOD` (`0x3A`) = `05` (RGB565).
6. Gamma `GMCTRP1` (`0xE0`) and `GMCTRN1` (`0xE1`), "sitronix":
   `0F 1A 0F 18 2F 28 20 22 1F 1B 23 37 00 07 02 10` and
   `0F 1B 0F 17 33 2C 29 2E 30 30 39 3F 00 07 03 10`.
7. `NORON` (`0x13`), wait 10 ms; `DISPON` (`0x29`), wait 100 ms.
8. With MADCTL 0 write zeros to the **whole** 132x162 RAM once, otherwise
   noise strips show at two edges. Then set the real `MADCTL`.

### SPI1 At 48 MHz (MEASURED)

| Fact | Value | Source |
| --- | --- | --- |
| Stable clocks | 12, 24 and **48 MHz** (div 8/4/2), 8- and 16-bit frames, no flicker | `display.spi`, 2026-11-13 |
| Datasheet limit | about 15 MHz; 48 MHz is outside it but works on this board | DESIGN |
| Scope SCK at div 2 | 48.01 MHz, duty 48.1 % | scope, 2026-11-17 |
| GPIO speed | 48 MHz needs OSPEEDR **medium or higher**; use high (2) for margin; low gives about 1 V swing and a frozen display | scope, 2026-11-17 |
| Ringing | Strong ringing seen is mostly probe loop; amplitude independent of clock | scope, 2026-11-17 |
| 12 MHz on low speed | clean square, rise 11 ns, 11.97 MHz | scope, 2026-11-17 |

> Warning: 48 MHz SPI to the display runs beyond the controller datasheet.
> It worked on this single board with a visual stress test and a scope check;
> v0.2 should add 22-33 ohm series resistors on SCK and MOSI.

### Frame Transfer Timing (MEASURED 2026-11-17, `bench.lcd`)

| SPI clock | Full frame 160x128 RGB565 | fps | Note |
| --- | --- | --- | --- |
| 12 MHz | 27.3 ms | 36.6 | 1.49 MB/s |
| 24 MHz | 13.7 ms | 73.1 | |
| 48 MHz, DMA 8- or 16-bit | **6.83 ms** | **146** | CPU cost about 100 cycles |
| 48 MHz, polled 16-bit | 6.83 ms | 146 | CPU fully busy |
| 48 MHz, polled 8-bit | 9.39 ms | 106 | only 72 % bus utilisation; use DMA |

Window setup (CASET + RASET + RAMWR) costs about 43 us at 48 MHz. Small
direct rectangles are expensive (8x8 about 0.94 ms, 32x32 about 11 ms at 48 MHz),
so never draw directly to the panel.

### Native Scan Order (default since 2026-11-17, F11, MEASURED by eye)

The panel scans in portrait order. A landscape frame written with `MADCTL 0xA0`
(MV = 1) is written perpendicular to the panel scan and tears diagonally; at
60 fps against a panel refresh of 60.25 Hz the seam drifts right to left about
every 4 s (a beat that cannot be fixed without a TE pin). The recipe:

- Set `MADCTL = 0x80` (MY only, MV = 0). `0x40` shows the picture rotated 180
  degrees.
- Keep the **logical** landscape 160x128 API, but store the frame column-major
  (rotated 90 degrees) so the DMA stream follows the panel scan:
  logical x maps to `RASET` row (+1), logical y maps to `CASET` column (+2);
  corners: TL (col 2, row 1), BR (col 129, row 160) (`display.native`).
- Result: the tear is practically invisible at 30 fps, 60 fps and uncapped, at
  every panel refresh preset. Remaining blur is TN pixel response (ghosting or
  a double image of fast moving bars).
- Since 2026-11-17 this is the default for the whole test-firmware UI
  (`lcd.c` native mode, physical MADCTL `0x80` = logical value with MV cleared):
  the API stays landscape and the driver maps and transposes the coordinates
  (landscape x to `RASET` row +1, y to `CASET` column +2). Only the calibration
  and benchmark suites (`display.cal/orient/spi/tear`, `bench.lcd/gfx`) switch
  back to raw mode.
- Cost of rotating at present time: 1.31 ms per 160x128 RGB565 frame, 2.15 ms
  for 8bpp to RGB565 with rotation. Rendering directly in native order avoids it.

### Panel Refresh (FRMCTR1 Presets)

`FRMCTR1` (`0xB1`) bytes `{RTNA, FPA, BPA}`; panel Hz is computed from the
registers (not measured on the panel):

| Preset | Bytes | Panel rate |
| --- | --- | --- |
| 0 | `05 3C 3C` | 60.25 Hz |
| 1 | `01 36 36` | 75 Hz |
| 2 (init default) | `01 2C 2D` | 80.6 Hz |
| 3 | `01 1F 20` | 90 Hz |
| 4 | `00 0F 10` | 110 Hz |

Guidance: with the native order any preset looks fine. With raw landscape the
best case was 60.25 Hz with 30/60 fps, still with a drifting seam. A refresh
above the frame rate costs panel power (not measured).

### Hardware Vertical Scroll (`VSCRDEF 0x33` / `VSCRSADD 0x37`)

In native order the controller's "vertical" scroll is the **horizontal** scroll
of the landscape image (MEASURED 2026-11-17, `display.vscroll`):

| Method | Cost per 1-px step | Rate |
| --- | --- | --- |
| Hardware scroll + new column written | 8.3k cycles = **87 us** (generate 3.2k, write column 4.9k, `VSCRSADD` 0.2k) | 11,090 steps/s |
| Full redraw (render 0.55M cycles + DMA 0.66M) | 12.57 ms | 79.4 fps max |

The test used scroll area `[TFA, VSA, BFA] = [0, 125, 37]` (a fixed 37-px HUD
panel beside a 125-px scrolling area). The area stays fixed outside `VSA`.
(An earlier run drifted from 78 to 61 fps because newlib `sinf` becomes slow for
large arguments; reduce phases in integer arithmetic.)

Measured rates (SPI 48 MHz): 60 steps/s costs 86-89 us per step; unlimited, the
hardware scroll reaches 11,090 steps/s (8,032 steps/s in the earlier run that
generated more per step, 114 us). Software redraw: 12.6-12.9 ms per frame.

Judder: a scroll step becomes visible only at the next panel refresh. Steps at
a rate different from the panel refresh rate beat against it, so a step is
repeated or skipped periodically; the runs used 60 steps/s against a panel at
60.25 Hz (preset 0) and 80.6 Hz (preset 2, init default), and the user saw
the judder with the mismatched pair. Step at the panel rate (set `FRMCTR1` to
match) or accept the beat; without a TE pin the phase cannot be locked.

### Present Pipeline (8bpp + Palette)

Recommended game path (`bench.gfx`, `demo`, MEASURED):

1. Render into an 8bpp frame buffer (20 KB).
2. Convert 8 lines at a time (1,280 px) through the 256-entry RGB565 palette into
   two ping-pong line buffers (2,560 B each) while DMA2 S3 sends the previous
   chunk; the DMA-ISR starts the next chunk and converts the one after.
3. Conversion cost 124k cycles per frame (3.9 % of a 30 fps frame, 7.8 % at
   60 fps); `convert_unrolled4` is 5 cycles/px against 8 for the plain loop.
4. Present time: 7.00 ms pipelined, 7.96 ms unpipelined at 48 MHz.

### Colour Depth (`COLMOD 0x3A`, MEASURED 2026-11-18, `display.colmod`)

| COLMOD | Format | Bytes per frame | Full frame at 48 MHz | Max fps |
| --- | --- | --- | --- | --- |
| `03` | 12-bit RGB444 | 30,720 | 5.12 ms | 195 |
| `05` | 16-bit RGB565 (default) | 40,960 | 6.83 ms | 146 |
| `06` | 18-bit RGB666 | 61,440 | 10.24 ms | 98 |

User verdict (by eye, test gradients): 12-bit shows clearly visible banding on
dark-to-light (grey and RGB) gradients, less on hue gradients; 16-bit and
18-bit are almost indistinguishable.

Recommendation (DERIVED): **16-bit by default**. The present cost is not the
bottleneck at 60 fps (6.83 ms of a 16.7 ms frame), 18-bit costs 50 % more bus
time for no visible gain, and 12-bit is justified only if more than 146 fps is
needed. Final decision pending a live A/B in real scenes, with ordered dither
for 12-bit.

### Demo Results (MEASURED 2026-11-17, `demo`, SYSCLK 96 MHz, SPI 48 MHz)

Two runs; the later one (15:20) is listed first, the earlier in brackets:

| Scene | fps | Render | Present wait | CPU |
| --- | --- | --- | --- | --- |
| Cubes | 144.1 (143.9) | 3.2 ms (3.1) | 3.6 ms | 48 % |
| Raycaster | 143.9 (143.8) | 5.7 ms (5.1) | 1.1 ms | 84 % (75) |
| Raytracer 80x64 (2x2 scaled) | 40.0 (41.2) | 24.9 ms (24.1) | 0 | 100 % |

Cubes and raycaster are limited by the 6.94 ms present (144 fps), not by
rendering. The two runs used different firmware builds (3dffea5 and 27475cd); the cause
of the difference was not investigated. Treat render times as +/- 10 %.

### Rules

- **Always render full frames to RAM and present them via DMA.** Drawing
  piecemeal directly to the panel (clear region, redraw at 12 MHz polled) shows
  sweeping stripes; the stm32-hwtest menu removed its redraw stripes by moving
  to a frame buffer.
- Use DMA at 48 MHz and present once per frame.
- Landscape at 160x128 logical, native scan order underneath.
- TN ghosting is a panel property; avoid one-pixel-wide fast detail.

## 7. Backlight

- **Drive**: PA6 -> R27 1k -> NPN base (R28 10k base to GND) -> LED string.
  PA6 reads TIED_LOW in the pins suite (MEASURED 2026-11-13).
- **PWM**: TIM3_CH1, AF2, PWM mode, 1 kHz chosen by the board owner
  (`backlight.user`, 2026-11-13); lowest step (level 0) still visible. Frequencies
  200 Hz to 25 kHz are exact.
- **Scope**: 1.000 kHz, +duty 49.99 % at 50 % requested (MEASURED 2026-11-16,
  TDS3032B, x10 probe at R27 on the MCU side). The firmware's own duty readback
  (47.4 %) was an input-register sampling artefact (F3, closed).
- Registers at 96 MHz timer clock, 1 kHz: PSC 1, ARR 47,999.

Current per level (MEASURED 2026-11-16, multimeter in the battery lead, whole
system with idle MCU in WFI; S2 = backlight off at 10.1 mA):

| Backlight | System current | System power | Backlight share (vs S2) |
| --- | --- | --- | --- |
| Off (S2) | 10.1 mA | 41.3 mW | - |
| 10 % | 13.05 mA | 52 mW | 2.95 mA |
| 25 % | 17.45 mA | 68 mW | 7.35 mA |
| 50 % | 24.95 mA | 99 mW | 14.85 mA |
| 75 % | 32.5 mA | 126 mW | 22.4 mA |
| 100 % | 40.25 mA | 155 mW | 30.15 mA |

The meter's series burden (about 7-8 ohm) lowered the board voltage to 3.85-4.09
V; powers are `I x V_board`, the system input power.

## 8. Input

| Item | Value | Basis |
| --- | --- | --- |
| Controls | 5-way joystick (UP, DOWN, LEFT, RIGHT, CENTER) + A, B, X, Y | |
| Wiring | Button to GND, MCU internal pull-ups, active-low | pins: FLOATING + pull-up |
| Layout | Action buttons in a diamond: Y top, B right, A bottom, X left | [pinout.md](pinout.md) |
| Neighbour bridges | none among the 13 button and NC pins | pins, 2026-11-13 |
| Chords | all tested chords and joystick diagonals OK | `input.chord`, 2026-11-13 |

Button mask (10 bits; the contract used by all hwtest code):

| Bit | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Button | UP | DOWN | LEFT | RIGHT | CENTER | A | B | X | Y | POWER |

POWER (bit 9) is not a GPIO. It means "OFF_REQ asserted by the ATtiny" and is
generated only after a >= 0.5 s hold plus release (see section 11).

Bounce (MEASURED 2026-11-13, `input.bounce`, 30 presses per button, edge log
with microsecond timestamps):

| Button | Burst max | Notes |
| --- | --- | --- |
| UP, CENTER, X, RIGHT | 0 to 0.2 ms | clean |
| LEFT, A | 0.3 ms | |
| Y | 1.5 ms | |
| DOWN | 4.0 ms | |
| **B (release)** | **17.6 ms** (p95 5.6 ms, up to 5 edges) | worst case |

Recommendation: 1 kHz scan from SysTick, per-button debounce of **20 ms**, at
least for release. The auto-derived 5 ms value (`max(2 x p95, 5 ms)` over all
buttons) is not safe for B. 20 ms is also what HWCAL stores (`debounce_ms`).
Check the B switch in the next revision.

### Input Latency

Firmware side (`input.latency`: EXTI edge timestamp from the 1 us TIM2 time
base to the start of the LCD DMA that fills the screen): the suite reports
`edge_to_dma_us`, `edge_to_done_us` and `dma_us` per press, for a staged window
(address window set in advance) and a cold one (window set in the reaction
path). The 2026-11-18 run aborted (no button press within the prompt
timeout). **Result: Not measured yet.**

End-to-end procedure (button to light, planned):

1. Sensor: a photodiode, or an ordinary LED used as a light sensor in
   photovoltaic mode (a red LED responds to white light) with a 10-100 k load
   resistor in parallel. Put it on the screen and shield it from room light and
   100 Hz mains flicker.
2. Scope CH1: a marker for the firmware reaction, either PA8 (at R24, MCU side)
   or SPI1 SCK at PA5 (first clock of the frame). CH2: the sensor. Trigger on
   CH1, 5-10 ms/div.
3. Measure from the CH1 edge to the 50 % point of the optical transition.
4. Expected (DERIVED, not measured): firmware reaction in tens of us, plus up to
   one panel period (13-17 ms at 60-75 Hz; the refresh is not synchronised),
   plus the TN liquid-crystal response of several ms.

## 9. Audio

### Chain

Chain (DESIGN from the pins suite and schematic notes; electrical behaviour
MEASURED):

`PA8 (TIM1_CH1)` -> R24 3k3 -> C24 2n2 to GND (RC low-pass, about 22 kHz) ->
C25 / C26 1 uF DC block -> PAM8302A (class-D, bridged output, SD# = PB2, active
high) -> speaker. 3V3-powered.

The bridged output must not be probed with the scope ground clip on a speaker
terminal (shorts the amplifier).

### Carrier Options

TIM1 up-counting, ARR = 2^bits - 1, timer clock 96 MHz:

| Carrier | Resolution | Name |
| --- | --- | --- |
| 750 kHz | 7 bit | `750k/7b` |
| **375 kHz** | **8 bit** | `375k/8b` (default in tests) |
| 187.5 kHz | 9 bit | `187k/9b` |
| 93.75 kHz | 10 bit | `94k/10b` |

### Engines

| Engine | Mechanism | Rate | Note |
| --- | --- | --- | --- |
| A | TIM5 update interrupt (priority 3) writes `TIM1->CCR1` per sample | any (tested 22.05 kHz) | Simple; per-sample IRQ overhead |
| B | TIM1 repetition counter paces update events; TIM1_UP -> DMA2 S5 Ch6 circular buffer into `CCR1`; half/complete interrupt refills 512-sample halves | carrier / (RCR+1); 46.875 kHz at 375k/8b, RCR+1 = 8 | One interrupt per 512 samples (about 10.9 ms) |

The synth mixer in `audio.c` has 4 voices, master gain with slew, a clip guard
(default -6 dBFS peak, master -24 dBFS; real peaks must stay <= -9 dBFS, see below), and a quantiser to the carrier
resolution with optional TPDF dither or first-order noise shaping.

### CPU Cost (MEASURED 2026-11-13, `audio.cpu`, SYSCLK 96 MHz)

| Engine | Voices | Cycles per sample | CPU load (incl. IRQ) |
| --- | --- | --- | --- |
| A, 22.05 kHz | 1 | 176 | 5.8 % |
| A, 22.05 kHz | 4 (TPDF dither) | 380 | 10.9 % |
| B, 46.875 kHz | 1 | 168 | 3.9 % |
| B, 46.875 kHz | 4 (TPDF) | 337 | 7.8 % |
| B, 46.875 kHz | 4 (noise-shaped) | 337 | 7.8 % |

No late buffers and no DMA errors in any run. `bench.audio` mixing kernels (8
channels PCM8 about 247 cycles/sample, synth 8 ch 224) are in
[section 12](#12-capability-budget-and-benchmarks).

### Sample Rate Accuracy

MEASURED 2026-11-13 with the TIM2 time base (same crystal as the PLL, so this
checks the integer timer arithmetic, not the crystal): engine A 22.04869 kHz
(expected 22.04869), engine B 46.87498 and 46.87503 kHz (expected 46.875, last
two runs); one earlier engine B run read 46.95614 kHz (+0.17 %), not reproduced.

### Speaker, Level And Frequency Response (MEASURED 2026-11-19)

Speaker: 8 ohm, 0.5 W (datasheet). Session: iPhone microphone at 10 cm,
phyphox Audio Amplitude, fan background about -55 dB (relative scale of the
app). Values are relative, not calibrated SPL.

Stepped sine (the reference), relative to the median of 1-8 kHz:

| Freq | Level | Freq | Level |
| --- | --- | --- | --- |
| 400 Hz and below | in noise (>= 25 dB down) | 3.15 kHz | +6 dB |
| 500 Hz | in noise (2.7 dB above background) | 4 kHz | +3 dB |
| 630 Hz | -22 dB | 5 kHz | -7 dB |
| 800 Hz | -16 dB | 6.3 kHz | -2 dB |
| 1 kHz | -9 dB | 8 kHz | -2 dB |
| 1.25 kHz | +3 dB | 10 kHz | -5 dB |
| 1.6 kHz | +7 dB | 12.5 kHz | -11 dB |
| 2 kHz | +2 dB | 2.5 kHz | -13 dB (not confirmed by chirp) |

- **Usable band about 800 Hz to 10 kHz.** Below 500 Hz nothing rises above the
  background; the peak is at 1.25-3.15 kHz.
- The chirp (`chirp_fr.csv`, plot `fr_combined.png`) is only a cross-check: the
  phone samples the amplitude at about 10 Hz, so the chirp frequency axis is
  misaligned. Do not use it for dips (the 2.5 kHz dip is suspect); the stepped
  sine is the reference.
- Saturation: the 1 kHz level stops rising between -8 and 0 dBFS digital; 200
  Hz is inaudible below -10 dBFS and above that only distortion and rattle
  appear.
- Power: clean maximum about 0.5 W at -8 dBFS; 0 dBFS (clipped) is about 1.3 W
  into 8 ohm. During the 0 dBFS 200 Hz clip test the speaker got **very hot**
  (F12).

Rules for game audio (DERIVED):

- Digital output peak **<= -9 dBFS**; sustained tones **<= -12 dBFS**.
- High-pass at 400-500 Hz in the mixer; emulate bass with harmonics.
- Never feed a full-scale or sustained low-frequency tone (section 15
  recommends a hardware limit).

Amplifier enable (MEASURED by ear, 2026-11-19, `audio.pop`):

| Variant | Result |
| --- | --- |
| PWM already at mid-scale, then SD# high ("naive") | no audible pop |
| PWM ramp 0 to 50 % with the amplifier already on, 20 / 50 / 100 ms | thump at every ramp length |

Pop-free recipe (default in `audio_amp_on`; HWCAL `amp_ramp_ms = 0`):

1. Set the PWM to mid-scale (50 % duty) with the amplifier off.
2. Wait 30 ms (10k x 1 uF input network).
3. Set SD# (PB2) high.
4. Off: SD# low first, then stop the PWM.

Idle hiss: the user hears hiss while the amplifier is on and idle; it stops
when the amplifier is off. The phone at 2 cm could not show it (all cases within
+-1 dB; 94 / 187 kHz carriers +2 / +1 dB): it is masked by the fan noise.
Pending: repeat in a quiet room with a spectrum.

Measurement procedure: record with phyphox (Audio Amplitude) while the
`audio.sweep`, `audio.chirp`, `audio.hiss` or `audio.clip` suite plays, export
the CSV, then run `tools/phyphox_sweep.py` (stepped sine) or
`tools/phyphox_chirp.py` (chirp) to get `fr.csv`, `fr.md` and `fr.png`.
Results: `tools/results/<uid>/audio-20261119-sweep/`.

### Not Measured Yet

THD, dither benefit and the quiet-room hiss spectrum per carrier state. SPL is
not calibrated.

> Warning: the PAM8302A shutdown pin reads TIED_LOW instead of a weak 100k
> pull-down (F2).

> Warning: F6. Enabling the amplifier browned out the ATtiny (supplied from
> +V) when the battery path had extra series resistance (about 7 ohm of the
> multimeter), which reset the ATtiny and cut power. Do not enable the amplifier
> suddenly on a weak, cold or low battery; ramp the carrier and test at low
> charge without a meter in the lead. v0.2: soft-start or bulk capacitor on +V,
> or ATtiny brown-out detection.

## 10. External Flash

| Item | Value | Basis |
| --- | --- | --- |
| Part | Winbond W25Q128 (JEDEC `EF4018`), 16 MB, SFDP 1.5 | MEASURED 2026-11-13 |
| Block protect | none (BP/TB/SEC/CMP all 0, whole array writable); SR1 `00`, SR2 `02`, SR3 `60` | MEASURED |
| Quad | not used (WP# and HOLD# tied high) | DESIGN |
| Bus | SPI3, mode 0, CS = PA15, up to 24 MHz | |
| Content check | whole-chip CRC identical at 6, 12, 24 MHz (`ECBC1810`) | MEASURED |
| Power-down | `0xB9`, release `0xAB`; first valid JEDEC 6 us after release; commands ignored during tDP | MEASURED |

### Read Throughput (MEASURED 2026-11-13, `bench.w25q`, 64 KB reads)

| SPI clock | Polled | DMA |
| --- | --- | --- |
| 6 MHz | 0.56 MB/s | 0.74 MB/s |
| 12 MHz | 0.88 MB/s | 1.49 MB/s |
| 24 MHz | 1.13 MB/s | **2.99 MB/s** |

`0x03` and `0x0B` give the same speed. Random reads at 24 MHz with DMA: 32 B in
18.6 us, 256 B in 93 us (polled 256 B: 230 us). Frame budget:
about 99 KB per frame at 30 fps and 49 KB at 60 fps if the flash is read
continuously.

### Erase And Program (MEASURED 2026-11-13, `w25q.scratch` on 0xF00000+)

| Operation | Measured | Datasheet typ / max |
| --- | --- | --- |
| Erase 4 KB | 52-57 ms (avg 54) | 45 / 400 ms |
| Erase 32 KB | 230-232 ms | 120 / 1600 ms |
| Erase 64 KB | 268-274 ms | 150 / 2000 ms |
| Page program (256 B) | 368-376 us | 400 / 3000 us |

Patterns 0x55, 0xAA, address and LFSR over 64 KB: 0 mismatches. The hwtest
driver refuses program/erase below `0xF00000`.

> Warning: F4 erase quirk (mitigated, see section 5): a 4 KB erase can be
> rejected and finish in microseconds. Use the checked erase (waits for BUSY,
> retries if it took under 1 ms) and verify; use the 64 KB erase where
> possible.

## 11. Power And Battery

### Architecture (DESIGN, [hardware-overview.md](hardware-overview.md))

BQ24075 power-path charger (USB or battery) -> TPS63802 buck-boost 3.3 V rail ->
ATtiny13A soft-power controller (enable of the regulator; powered from +V).
Battery: LP233350, 310 mAh nominal, 1S LiPo with built-in protection.

### Power-Off Handshake (MEASURED 2026-11-13, 2026-11-18)

| Step | Value | Basis |
| --- | --- | --- |
| Hold POWER, then release | at least 500 ms (`T_SHUT_HOLD_MS`) | ATtiny `config.h`; user-verified 2026-11-18 |
| ATtiny pulls OFF_REQ (PA2) low | open-drain style, 100k pull-up | pins |
| STM32 detect latency | 1 us (EXTI2); debounce confirm 30-31 ms | MEASURED 2026-11-13 |
| STM32 drives OFF_ACK (PA3) high | at about 31 ms after request in the test | MEASURED |
| ATtiny cuts the 3V3 rail | immediately after ACK (breadcrumbs lost = rail really dropped, also with USB attached, ST-Link disconnected) | MEASURED |
| ACK timeout | 2.5 s: the ATtiny cuts power even without ACK | DESIGN (`T_OFF_ACK_TIMEOUT_MS`) |
| Hard reset | holding POWER 8 s | DESIGN (`T_HARD_RESET_MS`) |
| Power-on hold | 100 ms | DESIGN |

F1 (closed 2026-11-18): the ATtiny timer used prescaler /64 while the compare
value was computed for /8, making every interval about 8x too long (hold about
4 s, ACK timeout about 20 s). Fixed in `attiny13/timebase.c`, reflashed on
2026-11-17, user verified power-off works on 2026-11-18. Time values after the fix
were not scope-measured.

Rules for firmware:

- Honour `OFF_REQ` at all times: blank the screen and drive `OFF_ACK` high, after
  saving state (settings, gauge level).
- The ATtiny ignores `OFF_ACK` outside the handshake: the board cannot power
  itself off. After a low-battery cutoff, enter Stop and ask for USB.
- Disconnect the ST-Link (or ensure it does not back-power) before power tests.

### Per-State Power (MEASURED 2026-11-16, `power.current`)

Multimeter 200 mA range in series with the battery (J2), USB out, ST-Link out,
full battery (4.09 V at 10 mA to 3.85 V at 40 mA at the board):

| State | Description | Current | Power |
| --- | --- | --- | --- |
| S1 | Stop (RTC wake), peripherals asleep | 1.3 mA | 5.0 mW |
| S2 | WFI, backlight off, LCD on | 10.1 mA | 41.3 mW |
| S3-S7 | backlight 10 / 25 / 50 / 75 / 100 % | 13.05 / 17.45 / 24.95 / 32.5 / 40.25 mA | 52 / 68 / 99 / 126 / 155 mW |
| S8 | CPU at maximum | 18.8 mA | 76.5 mW |
| S9 | LCD DMA continuous, backlight 50 % | 29.7 mA | 116 mW |
| S13 | W25Q continuous read | 22.1 mA | 89 mW |
| S14 | W25Q erase/program | 23.55 mA | 94 mW |
| S15 | "Game mix" load | 31.6 mA | 124 mW |
| S10/S11 | Amplifier on / 1 kHz tone | **Not measured yet** (F6 brown-out in the meter setup) | |
| S12 | Max load: BL 100 % + LCD DMA + CPU | about 207 mW | DERIVED (sum of increments), not metered |
| S16 | Charging current | See charging below | |
| S17 | CPU maximum vs frequency | **Not measured yet** (block B) | |
| Off | ATtiny asleep, regulator off | below the meter's 2 mA-range resolution (indirect) | MEASURED 2026-11-16 |

Idle current on USB, and exact deep-off current: Not measured yet (the items
still open in the bring-up log).

Runtime estimates: `1.15 Wh / 124 mW` gives about 9 h for the game mix (DERIVED,
HW_LOG 2026-11-16). The battery analysis tool computed longer values, see the
caveat below.

### No-Battery Operation (MEASURED 2026-11-19)

With the cell disconnected and USB attached the board runs the full test load
cleanly (`stress.game` on USB only: 139 fps, 0 audio underruns, 0 DMA errors,
0 CRC errors; the suite failed only on its ADC sanity check). The battery ADC
input floats between 0 and 4.2 V and the gauge reports mode `NO_BATT`.
Firmware must not trust Vbat when the gauge says `NO_BATT`.

### Sleep And Wake Timings (MEASURED 2026-11-18, `power.sleep`, 96 MHz)

| Item | Value |
| --- | --- |
| `DISPOFF` / `DISPON` / `SLPIN` / `SLPOUT` command | 2 us each (SPI) |
| LCD sleep-in wait / sleep-out incl. 120 ms wait | 5 ms / 132 ms |
| Backlight off / on (PWM) | instant |
| Stop entry | 3 us |
| Stop wake: HSE + PLL back at 96 MHz | about 14.5k cycles at HSI = **0.9 ms** |
| USB re-enumeration after `usb_stop`/`usb_start` | about 350 ms (stale CONFIGURED after a plain `DevConnect`) |
| WFI wake (Sleep mode) | 28 cycles median = 0.29 us |

`DISPOFF` keeps GRAM and the backlight on (blank panel). Current per sleep
state needs a meter (see the per-state table above).

### Amplifier Enable Dip (MEASURED 2026-11-18, `power.ampdip`)

On USB power (VBUS present) amplifier enable, a 1 kHz -12 dBFS tone and
amplifier off cause at most **14 mV** dip (PWM with amp off 8 mV, amp on idle
13 mV, tone 14 mV, amp off 8 mV) against about +-6 mV noise at 24 kS/s. The ADC
reads about 0.1 V for the first 45 ms after start (artefact; pre-roll used).
**Pending: repeat on battery only** (F6).

### Discharge Curve And OCV (MEASURED 2026-11-16 to 2026-11-17, `battlog`)

Full cycle on the board, no meter, backlight 100 % + LCD DMA + CPU
("S12", 9 min load, 1 min rest, 15 min rests near 75 / 50 / 25 / 10 %), started
about 17:00:

| Item | Value |
| --- | --- |
| Discharge time to the 3.30 V loaded cutoff | **9 h 18 min 32 s** (start 4.19 V) |
| Stop gap after cutoff | 4 h 42 min at 1.3 mA (6.1 mAh) |
| Die temperature | 20.9-35.0 C over the cycle |
| ADC cross-check at about 3.47 V | multimeter 3.47 V vs 3.460-3.480 V logged (within 10 mV) |
| Delivered charge / energy (to cutoff) | 436 mAh / 1.63 Wh (DERIVED, see caveat) |

OCV table (rested open-circuit voltage; from the end of rests, monotone fit;
SoC axis from this cycle: 100 % = start, 0 % = loaded cutoff, not empty):

| SoC % | 0 | 5 | 10 | 15 | 20 | 25 | 30 | 35 | 40 | 45 | 50 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| OCV mV | 3320 | 3475 | 3505 | 3541 | 3568 | 3589 | 3614 | 3647 | 3667 | 3688 | 3741 |

| SoC % | 55 | 60 | 65 | 70 | 75 | 80 | 85 | 90 | 95 | 100 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| OCV mV | 3781 | 3826 | 3865 | 3899 | 3954 | 3994 | 4009 | 4060 | 4110 | 4161 |

(Short 1-min rests are not fully relaxed and bias the table a few mV low.)

Internal resistance, from load-to-rest steps: **291 mOhm** (+1 s median) and
**376 mOhm** (+10 s median), n = 100; noisy per SoC bin (71-566 mOhm); the
charger plug step gives 570-770 mOhm (upper bound). HWCAL holds 330 mOhm.

> Caveat: the analysis tool took S12 = 207 mW (an estimate) to solve capacity,
> giving 436 mAh, 41 % above the 310 mAh label, and runtime estimates of 13.2 h
> (game mix), 16.5 h (BL 50 % static), 8.2 h (idle menu, BL 100 %), 328 h (Stop).
> The measured 9 h 18 min together with 310 mAh and 3.7 V implies about 123 mW
> average, so the real S12 power is probably lower than 207 mW, or the capacity
> higher than labelled. Treat the tool's runtime table as an upper bound until a
> game-mix discharge is logged.

### Charging (BQ24075)

| Item | Value | Basis |
| --- | --- | --- |
| Fast-charge current | about 175 mA (ISET 5.1k, 174.5 mA nominal) | DESIGN; plug step and the CC phase of the log agree |
| CHRG_STAT (PA0) | open-drain, low = charging, high = done / no charge / no USB; R9 10k up | MEASURED 2026-11-17 (`power.info`: CHG low charging) |
| First-phase log | CC charge seen at 3.29 -> 3.73 V over about 50 min | MEASURED 2026-11-17 |
| Full charge curve and termination time | **Not measured yet** (the logger was aborted by a host bug at about 50 min) | |
| Charge status without battery | the BAT node meanders; a window spread above 200 mV or a voltage outside 2.7-4.35 V means "no battery" | MEASURED bring-up |
| Battery divider | 33k / 82k, gain 1.000 and offset 0 vs multimeter at 4.175 V; VDDA from VREFINT within 0.2 % (3.293 vs 3.300 V) | MEASURED 2026-11-13 |

> Warning: F8. With no cable the `+5V` rail floats at about 2.4 V (the board
> back-feeds it). A USB-C source that requires vSafe0V (below 0.8 V) before
> turning on VBUS, such as a laptop C-C cable, never powers the board: no
> charging and VBUS reads false, while USB data may still work. A USB-A to C
> cable or a wall charger charges normally (confirmed 2026-11-17). Charge with an
> A-C cable until v0.2 fixes the leak.

### Battery Gauge Design (summary)

Voltage-only gauge ([Inc/gauge.h](../software/stm32-hwtest/Inc/gauge.h),
[Src/drv/gauge.c](../software/stm32-hwtest/Src/drv/gauge.c), shown in the status bar):

1. Every 250 ms: 16 samples each of BAT and VREFINT (480-cycle sampling, DMA),
   trimmed mean (drop 4 + 4), `Vbat = raw x 3300 x VREFINT_CAL / (raw_vref x
   4095) x 115 / 82 x gain + offset`.
2. Load compensation: `Vcomp = Vbat + I_est x R_int`, with `I_est` from the
   measured per-state currents (ignored while VBUS is present).
3. Filter: EMA tau 2 s for the first 10 s after boot or a mode change, then 60 s.
4. SoC from the 21-point OCV table above.
5. Display: 5 segments, thresholds 5 / 25 / 45 / 65 / 85 %, hysteresis 3 %,
   flashing outline below 5 %.
6. Lock rules: DISCHARGING never rises (a drop needs 20 s below threshold minus
   hysteresis, at most one per 60 s; emergency rise only after +15 % for 5 min);
   CHARGING never falls and is capped at 4/5 until FULL; FULL (CHG released with
   VBUS) shows 5/5 with a plug; RELAX freezes the level for 3 min after unplug;
   NO_BATT as above.
7. Persistence: `{level, mode, seq}` in HWCAL on level change and before ACK.
8. Low battery: below 3.50 V compensated for 10 s = warning; below 3.40 V
   compensated (or below 3.30 V loaded, 3 samples) = save, show "connect USB",
   sleep, Stop mode.

Replay of the real discharge log on the host (`tools/gauge_replay.c`,
2026-11-17): 0 segment reversals; segment drops at SoC 81 / 61 / 42 / 22 / 2.7 %.
Soak on the device and a full charge replay: Not measured yet.

## 12. Capability Budget And Benchmarks

All numbers: SYSCLK 96 MHz, 3 WS, ART on, `-O2`, flash execution, 1 warm-up and
7 repetitions with minimum / median (`bench.all`, 2026-11-13, except where
stated). One 30 fps frame is 3.2 Mcycles, one 60 fps frame 1.6 Mcycles.

### Per-Frame Budget

| Item | 30 fps | 60 fps |
| --- | --- | --- |
| Frame cycles | 3,200,000 | 1,600,000 |
| 8bpp to RGB565 convert (124k cycles) | 3.9 % | 7.8 % |
| Audio, 4 voices at 22 kHz | 2.9 % | 2.9 % (of one frame: 46.7k cycles) |
| Housekeeping (1 kHz tick, button scan) | 16.7k cycles | 8.3k cycles |
| **Left for the game** | **2.97 Mcycles (92.7 %)** | **1.42 Mcycles (88.8 %)** |
| LCD present (DMA, runs in parallel) | 7.0 ms of 33 ms | 7.0 ms of 16.7 ms |
| W25Q streaming (DMA, 24 MHz) | 99 KB | 49 KB |
| Tilemap 20x16 full scroll | 59.7k cycles | 59.7k cycles |

Sprites per frame from the remaining cycles at 60 fps (1.42 Mcycles), DERIVED
from per-sprite cycles:

| Sprite | Cycles each | Per 60 fps frame |
| --- | --- | --- |
| 8x8 opaque | 93 | about 15,000 |
| 8x8 color-keyed | 605 | about 2,300 |
| 16x16 opaque | 559 | about 2,500 |
| 16x16 color-keyed | 2,475 | about 570 (bench reports 646 against a whole frame) |
| 32x32 opaque | 1,710 | about 830 |
| 32x32 color-keyed | 8,707 | about 160 |

### CPU (cycles per operation, `bench.cpu`)

| Operation | Cycles | Operation | Cycles |
| --- | --- | --- | --- |
| Integer add / mul | 1.2 / 1.3 | `sinf` (newlib) | 164 |
| 32/32 divide (small / large) | 7.3 / 13.3 | `sin` 256-entry LUT + lerp | 29 |
| 64-bit multiply | 11.8 | `sin` polynomial | 61 |
| 64-bit divide | 155.7 | `atan2f` newlib / polynomial | 219 / 57 |
| Q16 multiply (`smmul` / `smull`) | 3.3 / 6.3 | `expf` newlib / poly / LUT | 172 / 95 / 86 |
| `clz` | 5.2 | AABB, 64 objects all pairs | 13 per pair |
| float add / mul / FMA | 2.2 / 2.2 / 3.1 | tile scroll 20x16 | 42k |
| float divide / sqrt | 15 / 17 | CRC32 hardware, 16 KB | 6.3 cycles/word |
| **double add / mul / div** | **91 / 70 / 73 (software)** | CRC32 software | 55.6 cycles/byte |

Rules: the FPU is single precision; `double` is software and 30-60x slower.
Prefer integer or float Q-formats, LUT or polynomial trig, and `smmul`.

### Memory Copy (`bench.mem`, MB/s at 16 KB)

| Method | MB/s | Note |
| --- | --- | --- |
| newlib-nano `memcpy` | 16.0 | byte loop; **very slow, do not use** |
| Byte loop | 16.0 | |
| 32-bit word loop | 76.7 | |
| **LDM/STM unrolled** | **139** | best CPU copy |
| DMA2 mem-to-mem (32-bit) | 101.6 | never beats LDM/STM; frees the CPU |
| `memset` newlib / words / `STM` | 13.7 / 76.7 / **235** | use STM-based fill |
| Unaligned word copy | 42.6 | align buffers |
| DMA 8-bit unaligned | 30.0 | |

### Other

| Item | Result |
| --- | --- |
| Interrupt entry (FPU lazy / no FPU) | 17 cycles; eager FPU 34; tail-chain 25 |
| ADC conversion at 480 cycles | 1,990 cycles (20 us); at 3 cycles 79 cycles |
| USB CDC TX throughput | 995 kB/s |
| Boot to first pixel | 758 ms (`boot_ms` in the budget) |

Full generated tables are produced from the JSON results by
[tools/gen_budget.py](../software/stm32-hwtest/tools/gen_budget.py).

### Block C Results (MEASURED 2026-11-18, 96 MHz, VBUS present)

| Test | Result |
| --- | --- |
| `stress.game` 60 s at 60 fps | raycaster, 4-voice audio DMA, W25Q DMA streaming and ADC together: 3601 frames, 0 missed, frame avg 5.1 ms / max 7.2 ms, **CPU about 31 %**, audio 2585/2585 blocks, 0 late/underrun, 0 TE/DME, 0 CRC errors in 3601 chunks |
| `stress.game` unlimited | 138.8 fps, still clean (2026-11-19 repeat: 138.9 fps, frame max 8.7 ms, CPU 87 %) |
| `soak` 3 min | clean; die 29.4 C, Vbat 4.18 V |
| `stream.video` 8bpp from W25Q | **140 fps** at SPI3 24 MHz (2.87 MB/s streamed, 2.99 MB/s alone); 71.7 fps at 12 MHz; 1bpp 133 fps (present-limited) |
| `display.colmod` | see section 6 |
| Sleep, wake, amplifier dip | see section 11 |

The DMA priorities used in these runs are in the table in section 4.

## 13. Platform Layer Contract Proposal

DESIGN, proposal for `stm32-core`. Not implemented as one API yet; every item is
backed by an hwtest driver.

```c
/* Display: 160x128 logical, frame stored in native scan order. */
void plat_present(const uint8_t *fb8, const uint16_t palette[256]);
bool plat_present_busy(void);        /* DMA chunk pipeline still running */

/* Input: bit 0 UP, 1 DOWN, 2 LEFT, 3 RIGHT, 4 CENTER, 5 A, 6 B, 7 X, 8 Y,
   9 POWER (= OFF_REQ asserted). Debounced (20 ms), updated at 1 kHz. */
uint16_t plat_buttons(void);

/* Audio: signed 16-bit mono at a fixed rate. Proposed: 46875 Hz, engine B,
   8-bit effective carrier resolution (375 kHz); alternative 22050 Hz, engine A. */
void plat_audio_push(const int16_t *mono, uint32_t n);
uint32_t plat_audio_free(void);
void plat_audio_amp(bool on);        /* ramped, F6-safe */

/* Storage: ROM data and saves in the W25Q (24 MHz, DMA read, 3 MB/s). */
void plat_storage_read(uint32_t addr, void *dst, uint32_t n);
bool plat_storage_write(uint32_t addr, const void *src, uint32_t n); /* settings only */

/* Battery: level 0..5, mode (discharging, charging, full, no battery), mV. */
void plat_battery(plat_batt_t *out);

/* Power: OFF_REQ seen; game saves, then calls plat_power_off_ack(). */
bool plat_power_off_requested(void);
void plat_power_off_ack(void);
```

Open decisions: audio rate (46.875 kHz engine B has the lowest CPU cost per
sample, 22.05 kHz is the legacy Retro Port rate); whether the platform layer
owns the 8bpp to RGB565 conversion (recommended, in the DMA ISR).

## 14. Flashing And Recovery

| Method | Command | Notes |
| --- | --- | --- |
| Normal reflash (running hwtest) | `make dfu` = `py -3.14 tools/hwtest.py flash build/hwtest.bin` | Firmware enters DFU, host flashes, waits for CDC, checks `fw`/`build` match |
| Software DFU entry | `dfu` command, or a 1200-baud touch on the CDC port, or menu "Enter DFU" | MEASURED 20/20 cycles on 2026-11-13; app to DFU 0.61 s, DFU to app 0.71-0.76 s; flashing a 220 KB image about 12 s |
| First flash / recovery | Hold BOOT0, tap RESET, release BOOT0; board appears as `0483:DF11`; `STM32_Programmer_CLI -c port=USB1 -w build/hwtest.bin 0x08000000 -v -s` | The ROM bootloader cannot be erased. Do not press buttons (CENTER = PA10 = bootloader USART RX) |
| SWD | `make flash` (`STM32_Programmer_CLI -c port=SWD -w ... -rst`) | |
| SWD, broken image | `STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst -w build/hwtest.hex -v -rst` | Connect under reset |

Software DFU entry works by writing magic `0xDF11B007` into the `.noinit` block
and resetting; `Reset_Handler` checks it before clock and RAM init and jumps to
the system bootloader at `0x1FFF0000` from a clean state. After the bootloader
exits with `-s`, a single clean reset is performed (`info.boot` shows
`bootloader_exit`).

Notes: ST-Link V2 clones may read their own supply as target voltage (3.25 V
reported with no 3V3 wire). Hard faults store PC, LR, CFSR, HFSR in `.noinit` and
reset (`info.faults`). The IWDG is used only by the overclock sweep; a failed
frequency attempt is detected after reset from the journal.

## 15. Errata And v0.2 Recommendations

### Findings

Source: [FINDINGS.md](../software/stm32-hwtest/FINDINGS.md).

| ID | Finding | Status |
| --- | --- | --- |
| F1 | ATtiny13 power-off hold about 4 s instead of 0.5 s (Timer0 prescaler mismatch /64 vs /8) | **Closed** 2026-11-18: fixed, reflashed, user verified POWER |
| F2 | PB2 AMP_SD reads TIED_LOW instead of a weak 100k pull-down; much stronger than 100k | Open: measure the net to GND with the board off |
| F3 | Backlight duty read 47.4 % for 50 % | **Closed** 2026-11-16: scope 1.000 kHz / 49.99 %, firmware artefact |
| F4 | 4 KB erase (`0x20`) sometimes silently rejected (SR1 = 02, WEL stuck); `w25q_erase_sector()` returned in 11 us | **Mitigated** 2026-11-18: checked erase with retry; root cause unknown |
| F5 | Button B release bounce up to 17.6 ms | Use 20 ms release debounce; check the switch |
| F6 | Amplifier enable browns out the ATtiny with extra series resistance in the battery path | Open: USB-powered `power.ampdip` shows only 14 mV; repeat on battery at low charge without a meter |
| F7 | RTC wake flag clear stopped the RTC (Stop never woke) | Fixed in firmware |
| F8 | Board back-feeds about 1.9-2.5 V onto +5V; USB-C sources check vSafe0V and refuse VBUS | Open (workaround: A-C cable) |
| F9 | Battery log charge flag appeared absent | **Closed**: log was fine, analyzer bug; the logger time base excludes Stop |
| F10 | Vbat, VDDA and die temperature ADC readings get much noisier during long low-load rests (BL off): spikes on all three together | Open: likely TPS63802 power-save (MODE = GND) burst ripple on 3V3/VDDA; BAT_STAT has no filter cap. Gauge: stronger averaging at low load |
| F11 | Landscape raw MADCTL tears diagonally | **Resolved**: native order `0x80` (default in the test UI) |
| F12 | Audio chain saturates at about -8 dBFS; 0 dBFS (about 1.3 W into a 0.5 W speaker) made the speaker very hot | New: digital peak <= -9 dBFS, high-pass 400-500 Hz; hardware limit proposed for v0.2 |

Other known hardware faults from [bring-up-log-v0.1.md](bring-up-log-v0.1.md):
USB ESD part D2 dead (removed and bridged, so no ESD protection on D+/D-);
`BAT_STAT`/`CHRG_STAT` swapped versus the first pinout notes; VBUS sense reads
high without USB (internal pull-down workaround).

### Recommendations For v0.2

| Item | Why |
| --- | --- |
| Bleed on +5V (about 10-47k) or find the back-feed so VBUS is below 0.8 V without USB | F8, USB-C vSafe0V; also fixes the VBUS divider margin (external 47k on PA9) |
| Restore USB ESD protection (USBLC6-2P6 footprint/part check) | Part was dead; none fitted now |
| 22-33 ohm series resistors on SCK and MOSI near the MCU | 48 MHz edge ringing and EMI margin |
| TE and MISO from the LCD to the MCU | Tear-free sync and LCD readback; free pins exist |
| Filter capacitor on BAT_STAT (ADC input) | Source impedance of the divider with 480-cycle sampling; noise 68 mV p-p window; F10 |
| Second-order audio filter | Hiss control |
| Amplifier soft-start and bulk capacitor | F6 (the pop-free firmware sequence covers clicks, not the supply dip) |
| Hardware volume limit or input attenuator before the PAM8302A | F12: a firmware bug must not be able to overdrive the 0.5 W speaker (clipped 0 dBFS is about 1.3 W) |
| Filtering of VDDA and BAT_STAT (ferrite/RC); consider forced-PWM mode on the TPS63802 | F10 low-load ADC noise |
| Bulk capacitor on +V or ATtiny brown-out detection | F6 |
| SWO test point (PB3 is SPI3_SCK today) and UART pads | Debug without USB |
| Current-sense option in the battery lead | No ammeter needed for power work |
| ATtiny: honour OFF_ACK in the running state | Lets firmware power itself off after a low-battery cutoff |
| BOR enabled in option bytes (firmware or factory step) | Brown-out on a sagging cell |
| Re-check the B button switch | 17.6 ms release bounce |

## Source Contradictions Found

| Topic | Sources | Resolution used here |
| --- | --- | --- |
| Raycaster render 5.1 ms / raytracer 41 fps vs 5.7 ms / 40 fps | demo 14:46 vs 15:20 (2026-11-17) | Latest listed first, earlier in brackets |
| Game-mix runtime 9 h (1.15 Wh / 124 mW) vs 13.2 h (analysis tool) | HW_LOG 2026-11-16 vs `summary.md` | The tool depends on an assumed S12 power and 436 mAh; 9 h kept as the conservative estimate |
| R_int 330 mOhm (HWCAL) vs 291 / 376 mOhm (analysis) | HW_LOG 2026-11-17 vs `summary.md` | Range given |
| Test arena RAM "17.7 KB static" (README) vs about 50 KB (current build) | README vs `nm` 2026-11-18 | Current build used |
| Hardware overview "includes USB ESD protection" | hardware-overview.md vs bring-up log | ESD part is dead / removed on this board |
| F1 "hold-time check pending" | FINDINGS row text vs HW_LOG 2026-11-18 | Closed, user verified |
| 2.5 kHz speaker dip -13 dB | stepped sine vs chirp (2026-11-19) | Listed with "not confirmed"; chirp axis is misaligned (10 Hz phone sampling) |
| Hardware scroll step 86-89 us vs 114 us | `display.vscroll` runs 2026-11-17 | 87 us (later run) used; 114 us run generated more per step |
| Audio TX "ramp" recipe (`audio_amp_on(ramp_ms)` ramps after SD#) vs pop-free default | earlier draft vs HW_LOG 2026-11-19 | By-ear test: ramp with amp on pops; precharge then SD# is the default |
