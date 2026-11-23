# Hardware Reference: v0.1

Single entry point for anyone writing game firmware for the STM-boy v0.1
board. It collects every hardware fact and proven recipe found with the
`stm32-hwtest` firmware so nothing has to be re-discovered.

> Status: v0.1 final, 2026-11-21 (test sessions 2026-11-13 to 2026-11-21). Items
> still open are marked "Not measured yet" or "Pending". Raw evidence lives
> in [software/stm32-hwtest/HW_LOG.md](../software/stm32-hwtest/HW_LOG.md) and
> `software/stm32-hwtest/tools/results/<uid>/*.json`; the running findings list
> is [software/stm32-hwtest/FINDINGS.md](../software/stm32-hwtest/FINDINGS.md). An
> independent gap review (38 items R1-R38, with a proposed next test session) is
> in [reports/2026-11-21-independent-review.md](../software/stm32-hwtest/reports/2026-11-21-independent-review.md);
> items marked R<n> in this document refer to it.

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
measurement sessions (2026-11-13 to 2026-11-21).

Instruments:

| Instrument | Used for |
| --- | --- |
| Multimeter (series in the battery lead, 200 mA range) | Per-state currents, VBAT/3V3 ADC calibration |
| Oscilloscope Tektronix TDS3032B (300 MHz, x10 probe) | SPI1 SCK, backlight PWM |
| Oscilloscope OWON XDS3202A (x10 probes) | Amplifier-enable dip on +V/VBAT, 3V3 ripple, carrier leak at C24 |
| iPhone + phyphox (Audio Amplitude) | Speaker frequency response, saturation, idle hiss (relative dB) |
| ST-LINK (SWD) | Recovery of an interrupted DFU flash |
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
| BOR is off | Option byte BOR level 3 (1.8-2.1 V) | Enable BOR level 3 (about 2.7 V) for battery use: the W25Q needs 2.7 V or more and writes during a decaying rail can corrupt saves. It can be programmed **from the application** (FLASH->OPTCR, OPTSTRT) as a guarded one-time first-boot step with read-back; not done yet (R6) |

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
- Current versus frequency: see below.

#### Current Versus Frequency

MEASURED 2026-11-19 (demo ray tracer, compute-bound, backlight 100 %, 16-bit,
meter in the battery lead): 48 MHz 50.9 mA / 20.3 fps, 72: 57.3 / 30.4,
84: 60.6 / 35.9, 96: 63.3 / 40.7, 120: 66.8 / 51.0, 144: 69.3 / 61.0.
Linear fit **0.19 mA per MHz + 43.5 mA** fixed (mostly backlight). fps scales
exactly with SYSCLK, so charge per frame falls from 2.51 to 1.14 mA*s: for
compute-bound scenes a higher clock is more energy-efficient. Down-clocking
96 -> 48 MHz saves only ~12 mA; **the backlight level dominates battery life.**


> Warning: anything above 100 MHz is outside the datasheet and **not for
> release**.

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
- **Clock scaling changes the audio timer clock** (TIM1 runs from APB2 x2).
  The NS engine needs 375 kHz / 8 bit, which at 48 MHz becomes 187.5 kHz and
  costs about 55 % CPU. Rule: fix the audio engine and rate per clock profile
  (NS only at 96 or 144 MHz); stop audio or switch to engine B before scaling
  down (R13).

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
| `0x100000-0x1FFFFF` | stm32-retroport | ROM pack `RPRP` v2 (142 ROMs, 205.6 KB at `0x100000-0x133670`) | MEASURED 2026-11-13 |
| `0x200000-0xEFFFFF` | stm32-hwtest | Media container for `media.play` (Big Buck Bunny, 12.6 MiB from `0x200000`). **Overlaps the 14 MiB that stm32-retroport reserves for its ROM pack** (`0x100000-0xEFFFFF`): a larger ROM pack would be overwritten by a media upload and vice versa | MEASURED 2026-11-19 (R17) |
| `0xF00000-0xFDFFFF` | stm32-hwtest | Scratch for destructive tests, power-handshake breadcrumbs, battery log | DESIGN (policy) |
| `0xFE0000-0xFEFFFF` | stm32-hwtest | Overclock / bus-limit journal (32-byte records) | DESIGN (block B) |
| `0xFF0000-0xFFEFFF` | - | Unassigned | |
| `0xFFF000-0xFFFFFF` | stm32-hwtest | HWCAL record, one 4 KB sector, halves A `0xFFF000` and B `0xFFF800` | MEASURED 2026-11-13 / 2026-11-17 |

The last 1 MB was blank (except a walking-ones pattern at `0xFFF000` of unknown
origin, since overwritten by HWCAL) when scanned on 2026-11-13.

> Warning: a game firmware that reuses the 15th MiB must treat HWCAL as board
> calibration (display offsets, ADC gain, OCV table). Keep sector `0xFFF000` or
> move the record deliberately; see [section 11](#11-power-and-battery).

There is **no save-game region yet** (R4). Before the first game, decide one
authoritative map (ROM data, media, saves, settings, HWCAL, test scratch) and
follow these rules (DESIGN, from the review and F4):

- Saves: a dedicated region of 64 KB or more, written as a log of records with
  `{magic, seq, len, crc32}` in at least two sectors (newest valid record wins;
  write the magic last). Never erase the only valid copy.
- Keep the board calibration (HWCAL) **write-once** and separate from runtime
  state (gauge level, settings): today both share sector `0xFFF000`, so every
  second runtime save erases the calibration sector (about 50 ms window), and a
  failed save resets the in-RAM calibration to defaults (`hwcal_load()`), which
  the next save then persists (R3).
- After `OFF_REQ` the ATtiny cuts power after about 2.5 s (RC clock +-10 %):
  finish saves within about 2 s; never start a 64 KB erase after `OFF_REQ`.
- Enable BOR (section 2) so that no write runs on a rail below 2.7 V.
- Erase and program only through the checked driver calls (erase retry when it
  "finished" under 1 ms, WEL check after a page program) and verify by reading
  back.

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
>
> 2026-11-21: the review's hypothesis (CS high shorter than tSHSL 50 ns between
> WREN and the erase opcode) was tested with the exact pre-fix sequence: 0 of
> 1320 erases ignored (4 KB and 64 KB, SPI 12 and 24 MHz, `0xF80000-0xFDFFFF`),
> so it is not the cause; block protection is off (SR3 WPS = 0). The driver now
> also holds CS high at least 200 ns (datasheet), checks WEL after every page
> program (retries an ignored program) and counts retries (`w25q stat`). F4 was
> only ever seen at `0xFE0000` / `0xFFF000` (4 KB) and on the first media upload
> (64 KB).

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

Since 2026-11-21 the debounce is **asymmetric** (R14): a press is reported after
two equal 1 ms samples (about 1-2 ms), the release only after 20 ms of stable
level. Before, every press was delayed by the full 20 ms window. One unexplained
spurious B press was seen on 2026-11-19 (R15, not reproduced).

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

> Estimate (DERIVED, 2026-11-19; optical measurement skipped - no photodiode available, a
> yellow LED gave no usable signal): button edge -> EXTI handler: microseconds; full-frame DMA at
> 48 MHz ~7 ms; wait for the panel scan to reach the area: 0-12.4 ms at 80.6 Hz; TN liquid-crystal
> response ~10-20 ms => **~20-40 ms button-to-pixel**, plus up to one game-loop frame (16.7 ms at
> 60 fps) before the game logic sees the input.

## 9. Audio

### Chain

Chain (DESIGN from the schematic; electrical behaviour MEASURED 2026-11-19):

`PA8 (TIM1_CH1)` -> R24 3k3 -> C24 2n2 to GND (1st-order RC, about 22 kHz) ->
C25 1 uF -> R25 10k -> PAM8302A IN- ; IN+ via C26 + R26 10k to the ground at C24 ->
PAM8302A (filterless class-D, bridged output, SD# = PB2, active high, 3V3 supply,
C22 100 nF + C23 10 uF) -> FBS1810 speaker (J3, Molex).

- **Gain.** PAM8302A (Diodes application note): `Av = 2 x 80k / (10k internal + Rin)`
  with `Rin = R25 + R24 = 13.3k` -> about 6.9 (16.7 dB), DESIGN. MEASURED: the
  1 kHz level stops rising at about **-8 dBFS** digital (the BTL stage clips), so
  the top ~8 dB of the digital range is unusable on v0.1 (F12).
- **The PWM high level is the 3V3 rail.** Any 3V3 noise is on the audio signal
  (see Idle Hiss). The amplifier's own supply rejection is good (input grounded:
  no hiss above background).
- The bridged output must not be probed with the scope ground clip on a speaker
  terminal (shorts the amplifier). Probe single-ended nodes (C24) or use A-B math.

### Carrier Options

TIM1 up-counting, ARR = 2^bits - 1, timer clock 96 MHz. Carrier left at the
amplifier input after the 1st-order RC (MEASURED 2026-11-19, scope at C24, about
0.14 V of each value is probe/ground noise):

| Carrier | Resolution | Leak at C24 (p-p) | Note |
| --- | --- | --- | --- |
| 750 kHz | 7 bit | 0.25 V | |
| **375 kHz** | **8 bit** | 0.42 V | default for the noise-shaped engine |
| 187.5 kHz | 9 bit | 0.74 V | default for engine B in `media.play eng=b` |
| 93.75 kHz | 10 bit | 1.37 V | audible **whine** during silence (F15); avoid |

### Engines

| Engine | Mechanism | Rate | Note |
| --- | --- | --- | --- |
| A | TIM5 update interrupt (priority 3) writes `TIM1->CCR1` per sample | any (tested 22.05 kHz) | Simple; per-sample IRQ overhead |
| B | TIM1 repetition counter paces update events; TIM1_UP -> DMA2 S5 Ch6 circular buffer into `CCR1` (OC1PE + ARPE preload: CCR changes latch on the update event, glitch-free) | carrier / (RCR+1) | Sample-and-hold at the sample rate; quantisation + TPDF dither noise is audible at 8-9 bit |
| **NS** (2026-11-20) | Engine B hardware, but `CCR1` is rewritten **every carrier period** (RCR = 0): the PCM is upsampled (64-tap 4x polyphase FIR, then linear) to the carrier rate and quantised by a **2nd-order error-feedback noise shaper** (+-0.5 LSB TPDF) | 375 kHz / 8 bit (PCM 18 750 Hz) | Quantisation noise pushed above ~20 kHz; `Inc/ns_core.h`, host model `tools/ns_sim.py` |

The synth mixer in `audio.c` has 4 voices, master gain with slew, a clip guard
and the quantiser. **Speaker protection** (2026-11-19, below every caller): the
master gain is trimmed by **-6 dB** and the clip guard can never exceed
**-14 dBFS** (about 0.27 W sine), not even in `audio.clip`; `audio spk <trim>
<ceil>` changes it live (ceiling at most -10 dBFS), not saved. All suite `db=`
values are before the trim.

### CPU Cost

MEASURED 2026-11-13 (`audio.cpu`, SYSCLK 96 MHz):

| Engine | Voices | Cycles per sample | CPU load (incl. IRQ) |
| --- | --- | --- | --- |
| A, 22.05 kHz | 1 | 176 | 5.8 % |
| A, 22.05 kHz | 4 (TPDF dither) | 380 | 10.9 % |
| B, 46.875 kHz | 1 | 168 | 3.9 % |
| B, 46.875 kHz | 4 (TPDF) | 337 | 7.8 % |

MEASURED 2026-11-20 (`media.play mute=1`, video on, 96 MHz): **NS engine 70
cycles per carrier period = 27.4 % CPU** (27.0 % without video); engine B on the
same stream 4.6 %. Worst render block 41.9k of 48k cycles available; 0 audio or
LCD DMA errors, 0 underruns, 15.03 fps. Not optimised yet (target about half:
gain slew at the PCM rate, cheaper interpolation).

`bench.audio` mixing kernels (8 channels PCM8 about 247 cycles/sample, synth
8 ch 224) are in [section 12](#12-capability-budget-and-benchmarks).

### Sample Rate Accuracy

MEASURED 2026-11-13 with the TIM2 time base (same crystal as the PLL, so this
checks the integer timer arithmetic, not the crystal): engine A 22.04869 kHz
(expected 22.04869), engine B 46.87498 and 46.87503 kHz (expected 46.875, last
two runs); one earlier engine B run read 46.95614 kHz (+0.17 %), not reproduced.
`media.play` 2026-11-19: exactly 18 750 Hz (375 kHz / 20), 0 underruns.

### Speaker

FBS1810 (18 x 10 mm): 8 ohm +-20 %, **0.8 W rated / 1.0 W max**, f0 100 Hz,
77 dB at 1 W / 0.1 m (datasheet DOC061610363). Low sensitivity and a tiny
diaphragm: nothing useful below about 800 Hz.

- **Do not desolder the speaker's own leads** and do not pot the tabs with glue:
  the tinsel leads to the voice coil are easily damaged. Splice the connector
  cable to the native leads and strain-relieve them. The first speaker was
  damaged (lead rework and/or the 0 dBFS clip test, about 1.4 W) and crackled on
  every low-frequency signal (F14, resolved by replacing it; the electronics were
  cleared by a long list of elimination tests).
- **Mounting changes the response completely**: a speaker hanging on its leads
  (no baffle) measured +21 dB at 6.3 kHz and a flat -17 dB shelf below 1 kHz;
  in the case the response is as below. Measure in the final enclosure.

### Frequency Response In The Case (MEASURED 2026-11-20)

New speaker assembled in the case; `audio.sweep` (stepped sine, ISO 1/3 octave,
3 s steps, `fmin=200`: the 50-160 Hz steps are silent to protect the speaker),
-12 dBFS before the -6 dB trim; iPhone + phyphox Audio Amplitude at 10 cm in a
quiet room (background -71.5 dB). Relative to the median of 1-8 kHz, not
calibrated SPL. Results `tools/results/<uid>/audio-20261120-sweep-case/`.

| Freq | Level | Freq | Level |
| --- | --- | --- | --- |
| 200-400 Hz | -27 dB | 2.5 kHz | +2 dB |
| 500 / 630 Hz | -24 dB | 3.15 kHz | 0 dB |
| 800 Hz | -18 dB | **4 kHz** | **+14 dB (resonance peak)** |
| 1 kHz | -10 dB | 5 kHz | +3 dB |
| 1.25 kHz | -2 dB | 6.3 kHz | 0 dB |
| 1.6 kHz | +4 dB | 8 kHz | -2 dB |
| 2 kHz | -3 dB | 10 / 12.5 kHz | -6 / -3 dB |

- **Usable band about 1-12.5 kHz**, within +-4 dB except the 4 kHz peak (true
  peak somewhere in 3.5-4.5 kHz at 1/3-octave resolution).
- Amplitude mode counts harmonics too; the 200-630 Hz values may be partly
  distortion products.
- `tools/phyphox_sweep.py` default step period is now 3.50 s (measured; 3.62 s
  misaligned the grid by one step).
- The 2026-11-19 sweep of the first (damaged) speaker is kept in
  `audio-20261119-sweep/` for reference only.

### Speaker EQ (MEASURED by ear 2026-11-20)

`media.play eq=1` (default): 2nd-order Butterworth high-pass **600 Hz** + RBJ
peaking cut **-12 dB at 4 kHz, Q 3**, applied to the PCM before the engine.
Predicted result 1.25-8 kHz within +-4 dB. Listening: "more balanced" than
`eq=0`. Parameters `eqhp`, `eqf`, `eqg`, `eqq`.

### Idle Hiss (MEASURED 2026-11-20)

`audio.hiss` + `media.play att=90` (engine running, signal -90 dB), phone 2 cm
from the speaker in the case, night-quiet room. Level above "amplifier off"
(`tools/results/<uid>/audio-20261120-hiss/hiss.md`):

| Case | Above amp off |
| --- | --- |
| Amplifier on, PA8 low / PA8 hi-Z | +0.3 / -2.9 dB (amplifier self-noise inaudible) |
| **Amplifier on, PA8 high (= 3V3)** | **+12.0 dB** |
| Static 50 % PWM, 750 / 375 / 187.5 / 94 kHz | +7.7 / +11.0 / +18.8 / +14.0 dB |
| **NS engine idle (375k/8b)** | **+9.8 dB** |
| Engine B idle (187k/9b, TPDF) | +24.7 dB |

- The NS engine is **15 dB quieter** than engine B on the real board (the host
  simulation predicted 47 dB in-band; the board is limited by the next floor).
- The remaining hiss equals "PA8 high" and the static 375 kHz PWM: it is **3V3
  rail noise riding on the PWM high level**, not quantisation. Firmware cannot
  remove it.
- Fix (not done on this board, deferred): raise R25 = R26 to **82k** (gain 6.9 ->
  ~1.7, -12 dB; both legs equal) and use the freed digital headroom; at the same
  loudness the 3V3 hiss drops by about 12 dB and 0 dBFS can no longer overdrive
  the speaker. Further: TPS63802 forced PWM while audio plays (3V3 ripple 60-70 ->
  ~15 mV p-p, MEASURED 2026-11-19 with MODE pulled high; rework reverted), or a
  PWM buffer powered from a clean reference rail (v0.2).

### Amplifier Enable And Pause

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

Pause / silence with the amplifier on (MEASURED 2026-11-19, F15): fade the master
gain out, then **float PA8** (`audio_pa8_float(true)`, input mode): no whine and
no hiss (C24 100 mV p-p = probe noise). Resume: drive PA8 again, wait 5 ms,
fade in.

### Rules For Game Audio (DERIVED)

- Keep the driver's speaker protection (-6 dB trim, -14 dBFS ceiling) on v0.1.
  Without it: digital peaks <= -10 dBFS, sustained tones <= -14 dBFS.
- PCM 16-bit mono at 18 750 Hz into the NS engine (375 kHz / 8 bit) for the
  lowest hiss, or engine B (4-8 % CPU) when CPU matters.
- High-pass about 600 Hz and the 4 kHz cut (speaker EQ above) in the mixer;
  emulate bass with harmonics. Never play a full-scale or sustained
  low-frequency tone.
- Float PA8 during longer silences; use the pop-free enable.
- Measurement procedure: phyphox Audio Amplitude while `audio.sweep` /
  `audio.hiss` / `audio.clip` plays, export CSV, `tools/phyphox_sweep.py`
  (stepped sine) or `tools/phyphox_chirp.py`.

### Not Measured Yet

THD, calibrated SPL, the effect of R25/R26 = 82k on hiss, an optimised NS
engine.

> Warning: the PAM8302A shutdown pin reads TIED_LOW instead of a weak 100k
> pull-down (F2).

> F6 (closed): enabling the amplifier only browned out the ATtiny with the
> multimeter's ~7 ohm burden in the battery lead. On battery without the meter
> the rails sag about 50 mV during a -12 dBFS tone (section 11), more than 10x
> margin to a reset.

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

> Warning: F4 erase quirk (mitigated, cause unknown, see section 5): a 4 KB
> **or 64 KB** erase can be rejected and finish in microseconds (all 203 first
> 64 KB erases of the first media upload). Always use the checked erase (waits
> for BUSY, retries if it took under 1 ms) and verify; the 64 KB erase is **not**
> a safe alternative.
>
> `w25q_init()` polls BUSY (up to 3 s) before the JEDEC check since 2026-11-21:
> after a warm reset during an erase the chip ignores `9Fh`, and the flash used
> to look absent for that boot (R5).

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
| S15 | "Game mix" load | 31.6 mA (2026-11-16, too low) / **52 mA at 3.6 V (2026-11-23)** | 124 / **about 187 mW** |
| S10/S11 | Amplifier on, silent / 1 kHz tone (after R25 = R26 = 82k) | **21.1 / 20.4 mA** at 3.6 V (amp + carrier about +11 mA over S2) | MEASURED 2026-11-23; the 200 mA shunt can shut the board down at amp enable (F17) |
| Video | `media.play`, NS audio, new speaker | **78 mA** at 3.6 V | about 280 mW, MEASURED 2026-11-23 |
| S12 | Max load: BL 100 % + LCD DMA + CPU | **60.5 mA** | about 239 mW, MEASURED 2026-11-19 |
| Menu | test UI, backlight 100 % | about 61 mA (2026-11-19) / 67-68 mA at 3.6 V (2026-11-23) | constant-power load: current rises as Vbat falls |
| S16 | Charging current | See charging below | |
| S17 | Current vs SYSCLK | see section 3 (0.19 mA/MHz) | MEASURED 2026-11-19 |
| Off | ATtiny asleep, regulator off | **about 4 uA** (about 35 mAh per year) | MEASURED 2026-11-19 (uA range) |

Idle current on USB: Not measured yet.

All per-state currents above were taken with the TPS63802 in its stock
power-save mode (MODE = GND, R16 fitted). Forced PWM (MODE high) was tried
2026-11-19 and reverted; its idle current was not measured.

Runtime (MEASURED, see the discharge section; both runs are pulsed: 9 min
load / 1 min rest plus 15 min rests near 75 / 50 / 25 / 10 %):

- S12 max load: **9 h 18 min including 1.77 h of rests**; continuous maximum load
  is about **7.85 h** (DERIVED: 1876 mWh / 239 mW) (R9).
- S15 game mix (cycle #2): 11.1 h from about **82 % SoC** (first end-of-rest
  4001 mV), including 2.16 h of rests. With the 2026-11-16 S15 power (124 mW)
  this implies only about 396 mAh against about 500 mAh from S12, so the S15
  power or the capacity is off by about 25 % (R10). Re-metered 2026-11-23: S15 is
  **52 mA (about 187 mW)** at 3.6 V, not 124 mW, which resolves most of the gap.
- Estimates from the 2026-11-23 currents and about 470 mAh (DERIVED, preliminary):
  video with sound (78 mA) about 6 h, menu at backlight 100 % (67 mA) about 7 h,
  S15 game mix without sound (52 mA) about 9 h. The amplifier adds about 11 mA.

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
| LCD sleep-in wait / sleep-out incl. 120 ms wait | 5 ms / 132 ms; since 2026-11-21 SLPIN -> SLPOUT is held to at least 120 ms as the ST7735S requires (sleep-out right after sleep-in: 247 ms) (R27) |
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
On battery without a meter (MEASURED 2026-11-19, scope on +V at C14 and VBAT at
C15, trigger on AMP_SD): no step at amplifier enable; during the 1 kHz -12 dBFS
tone both rails sag **about 50 mV** (envelope; firmware ADC minimum 75-79 mV),
i.e. about 90 mA average x 0.3 ohm plus ripple. Maximum volume is estimated at
100-130 mV: more than 10x margin to an ATtiny reset. F6 closed (meter artefact).

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
| Delivered charge / energy (to cutoff) | about 500 mAh / 1.88 Wh (DERIVED from S12 60.5 mA, F13) |
| Confirmation (cycle #2, 2026-11-20/21) | last 4 h (3.73 V -> cutoff) 4.13 h vs 4.30 h; first hour within 10-20 mV; plot `battlog-20261121-093251-analysis/cycle2.png` |

OCV table (rested open-circuit voltage; from the end of rests, monotone fit;
SoC axis from this cycle: 100 % = start, 0 % = loaded cutoff, not empty):

| SoC % | 0 | 5 | 10 | 15 | 20 | 25 | 30 | 35 | 40 | 45 | 50 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| OCV mV | 3320 | 3475 | 3505 | 3541 | 3568 | 3589 | 3614 | 3647 | 3667 | 3688 | 3741 |

| SoC % | 55 | 60 | 65 | 70 | 75 | 80 | 85 | 90 | 95 | 100 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| OCV mV | 3781 | 3826 | 3865 | 3899 | 3954 | 3994 | 4009 | 4060 | 4110 | 4161 |

(Short 1-min rests are not fully relaxed and bias the table a few mV low.)

Internal resistance, from load-to-rest steps with the measured S12 current:
**244 mOhm** (+1 s median) and **315 mOhm** (+10 s median), n = 100
(`battlog-20261117-080333-analysis-p12meas`); noisy per SoC bin; the charger plug
step gives about 550-770 mOhm (upper bound). HWCAL holds 330 mOhm. (An earlier
analysis with an assumed 207 mW gave 291 / 376 mOhm, R23.)

> Capacity (F13): the cell (LP233350, labelled 310 mAh) delivers about 500 mAh
> to the 3.30 V loaded cutoff (S12 60.5 mA x 9.31 h, minus rests) and takes about
> 470 mAh from empty in a full charge (cycle #2, preliminary: assumes the 175 mA
> design charge current). Both say about 1.5x the label. A direct charge-current
> reading would close F13.
>
> Logger note (F16, fixed 2026-11-21): a resume after a power-off used to restart
> with default parameters and could land after stale records; logger time does not
> count power-off periods.

### Charging (BQ24075)

| Item | Value | Basis |
| --- | --- | --- |
| Fast-charge current | **172 mA** (200 mA range) / 0.17 A (10 A range) at about 3.6-3.8 V; design 174.5 mA (ISET 5.1k) | MEASURED 2026-11-23. The 200 mA range shunt (about 1.8 ohm) lifts the BAT node and makes the charger taper early: measure charge current on a low-burden range |
| CHRG_STAT (PA0) | open-drain, low = charging, high = done / no charge / no USB; R9 10k up | MEASURED 2026-11-17 (`power.info`: CHG low charging) |
| First-phase log | CC charge seen at 3.29 -> 3.73 V over about 50 min | MEASURED 2026-11-17 |
| Full charge from empty (3.30 V loaded cutoff, A-C cable) | CC until 4.20 V at **about 143 min**, CV until CHG release at **194.7 min (3 h 15 min)**; about 470 mAh in (DERIVED, preliminary) | MEASURED 2026-11-21, cycle #2 |
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
7. Persistence (**DESIGN, not implemented**, R2): `{level, mode, seq}` saved on
   level change and before ACK. `hwcal_save()` is only reachable from the
   command line today; see the save rules in section 5 before implementing it.
8. Low battery (**DESIGN; only the battlog suite acts on it**, R2): below 3.50 V
   compensated for 10 s = warning; below 3.40 V compensated (or below 3.30 V
   loaded, 3 samples) = save, show "connect USB", power down.
   - Do **not** park in Stop after the cutoff: Stop draws 1.3 mA and empties the
     about 25 mAh left below the cutoff in about a day, then the cell protection
     trips (R7). Prefer Standby (MCU a few uA, wake on RTC to check VBUS/CHG) or
     the ATtiny power-off; Standby current was never measured.
   - VBUS sense idles at about 1.1 V without a cable (back-feed, F8), right at the
     F411 VIL maximum (about 1.12 V). A false VBUS disables the load compensation
     and the low-battery alarms. Cross-check VBUS with CHG and USB activity before
     trusting it (R8).

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
| Audio engine B, 4 voices (`audio.cpu`) | 7.8 % | 7.8 % |
| Audio NS engine, 18 750 Hz PCM (`media.play`) | 27.4 % | 27.4 % |
| Housekeeping (1 kHz tick, button scan) | 16.7k cycles (0.5 %) | 8.3k cycles (0.5 %) |
| **Left for the game, engine B** | **2.81 Mcycles (87.8 %)** | **1.34 Mcycles (83.9 %)** |
| **Left for the game, NS engine** | **2.18 Mcycles (68.2 %)** | **1.03 Mcycles (64.3 %)** |
| LCD present (DMA, runs in parallel) | 7.0 ms of 33 ms | 7.0 ms of 16.7 ms |
| W25Q streaming (DMA, 24 MHz) | 99 KB | 49 KB |
| Tilemap 20x16 full scroll | 59.7k cycles | 59.7k cycles |

(Until 2026-11-21 this table used the 2.9 % mixing-kernel figure for audio
instead of the measured engine cost, R12.)

Sprites per frame from the remaining cycles at 60 fps (1.42 Mcycles without
audio; scale by 0.94 with engine B or 0.73 with NS), DERIVED from per-sprite
cycles:

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
| `media.play` (2026-11-19/20) | Big Buck Bunny (c) Blender Foundation, CC-BY 3.0: 160x128 8bpp + per-frame palette, 15 fps, 16-bit 18 750 Hz audio, 12.6 MiB at W25Q 0x200000: **15.03 fps, 0 skipped / late frames, 0 audio underruns**, double-read verify 0 mismatches in 303 chunks, pause/resume clean (section 9) |
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

/* Audio: signed 16-bit mono at 18750 Hz. Proposed: NS engine (375 kHz carrier,
   8 bit, 2nd-order noise shaping, ~27 % CPU today) or engine B (4-8 % CPU).
   The platform owns speaker protection (-6 dB trim, -14 dBFS ceiling) and the
   speaker EQ (HP 600 Hz, -12 dB at 4 kHz). */
void plat_audio_push(const int16_t *mono, uint32_t n);
uint32_t plat_audio_free(void);
void plat_audio_amp(bool on);        /* pop-free: PWM mid-scale, 30 ms, SD# high */
void plat_audio_idle(bool on);       /* silence: fade out, float PA8 (no whine) */

/* Storage: ROM data and saves in the W25Q (24 MHz, DMA read, 3 MB/s). */
void plat_storage_read(uint32_t addr, void *dst, uint32_t n);
bool plat_storage_write(uint32_t addr, const void *src, uint32_t n); /* settings only */

/* Battery: level 0..5, mode (discharging, charging, full, no battery), mV. */
void plat_battery(plat_batt_t *out);

/* Power: OFF_REQ seen; game saves, then calls plat_power_off_ack(). */
bool plat_power_off_requested(void);
void plat_power_off_ack(void);
```

Open decisions: NS engine vs engine B (hiss vs CPU; NS needs optimisation
before games can afford it); whether the platform layer owns the 8bpp to RGB565
conversion (recommended, in the DMA ISR).

> Warning (R16): the existing engine header `software/stm32-core/engine/platform.h`
> still describes a 128x160 portrait screen, 7 buttons including `BTN_START`
> with different bit values, and `plat_millis()`. This proposal (160x128
> landscape, 10-bit mask, no Start: POWER is not a usable button) must replace it
> before game code is written; map "Start" to CENTER or a chord, and add
> `plat_millis()` and a save API (`plat_save_read/write`, section 5 rules).

## 14. Flashing And Recovery

| Method | Command | Notes |
| --- | --- | --- |
| Normal reflash (running hwtest) | `make dfu` = `py -3.14 tools/hwtest.py flash build/hwtest.bin` | Firmware enters DFU, host flashes, waits for CDC, checks `fw`/`build` match |
| Software DFU entry | `dfu` command, or a 1200-baud touch on the CDC port, or menu "Enter DFU" | MEASURED 20/20 cycles on 2026-11-13; app to DFU 0.61 s, DFU to app 0.71-0.76 s; flashing a 220 KB image about 12 s |
| First flash / recovery | Hold BOOT0, tap RESET, release BOOT0; board appears as `0483:DF11`; `STM32_Programmer_CLI -c port=USB1 -w build/hwtest.bin 0x08000000 -v -s` | The ROM bootloader cannot be erased. Do not press buttons (CENTER = PA10 = bootloader USART RX) |
| SWD | `make flash` (`STM32_Programmer_CLI -c port=SWD -w ... -rst`) | |
| SWD, broken image | `STM32_Programmer_CLI -c port=SWD mode=UR reset=HWrst -w build/hwtest.bin 0x08000000 -v -rst` | Connect under reset. Used 2026-11-19: an interrupted DFU download had written only the first 4 KB (readback showed the rest erased), the board did not boot; SWD reflash with verify fixed it |

Software DFU entry works by writing magic `0xDF11B007` into the `.noinit` block
and resetting; `Reset_Handler` checks it before clock and RAM init and jumps to
the system bootloader at `0x1FFF0000` from a clean state. After the bootloader
exits with `-s`, a single clean reset is performed (`info.boot` shows
`bootloader_exit`).

Notes: ST-Link V2 clones may read their own supply as target voltage (3.25 V
reported with no 3V3 wire). The F411 ROM bootloader also probes USART2 (PA2/PA3 =
OFF_REQ/OFF_ACK), I2C1 (PB6/PB7 = RIGHT/LEFT), SPI2 (PB12-PB15 = X/A/B/Y) and
I2C3: **press no button** while it runs (R29). The 1200-baud touch resets into
DFU from any host program that opens the port at 1200 baud: keep it in
development builds only (R30). The `.noinit` fault breadcrumbs have no checksum
and held garbage after the overclock sweep (`fault_count` 0x20002081, PCs
0x2800219x): validate them with a magic/CRC before trusting them (R19). Hard faults store PC, LR, CFSR, HFSR in `.noinit` and
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
| F6 | Amplifier enable browns out the ATtiny with extra series resistance in the battery path | **Closed** 2026-11-19: meter artefact (~7 ohm burden); on battery the rails sag about 50 mV during a tone |
| F7 | RTC wake flag clear stopped the RTC (Stop never woke) | Fixed in firmware |
| F8 | Board back-feeds about 1.9-2.5 V onto +5V; USB-C sources check vSafe0V and refuse VBUS | Open (workaround: A-C cable) |
| F9 | Battery log charge flag appeared absent | **Closed**: log was fine, analyzer bug; the logger time base excludes Stop |
| F10 | Vbat, VDDA and die temperature ADC readings get much noisier during long low-load rests (BL off): spikes on all three together | **Explained** 2026-11-19: TPS63802 power-save bursts (~57 kHz at light load, 50-65 mV) seen on +V/VBAT; 3V3 sawtooth ~60-70 mV p-p. Gauge: stronger averaging at low load |
| F11 | Landscape raw MADCTL tears diagonally | **Resolved**: native order `0x80` (default in the test UI) |
| F12 | Audio chain saturates at about -8 dBFS; 0 dBFS (about 1.3 W into a 0.8 W rated / 1.0 W max speaker) made the speaker very hot | Mitigated in firmware: driver trim -6 dB + ceiling -14 dBFS; hardware fix R25 = R26 = 82k |
| F13 | Battery holds about 1.5x its 310 mAh label (about 500 mAh out, about 470 mAh in) | Preliminary; confirm with a direct charge-current reading |
| F14 | Audio crackle on low frequencies | **Resolved** 2026-11-19: damaged speaker (lead rework / clip test); replaced |
| F15 | Whine during audio silence (constant 50 % PWM): carrier leak + 3V3 noise | **Fixed** in firmware: 375 kHz carrier, PA8 floated during pause |
| F16 | Battery logger resume used default parameters; stale records confused the resume position | **Fixed** 2026-11-21 (BCFG block, wipe on new start) |

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
| Second-order audio filter | Carrier leak (F15) |
| R25 = R26 = 82k (or a hardware volume divider) | 3V3 hiss -12 dB at equal loudness; speaker cannot be overdriven (F12) |
| PWM buffer (e.g. single-gate 74LVC1G) powered from a filtered/LDO reference rail | Removes 3V3 noise from the audio signal entirely (the hiss floor measured 2026-11-20) |
| TPS63802 MODE on a free GPIO | Forced PWM while audio plays or the ADC measures (ripple 60-70 -> 15 mV), power-save otherwise (F10) |
| 1 uF at the PAM8302A VDD pin (datasheet), amplifier power-ground path | Datasheet recommendation; cleaner supply return |
| Speaker: higher sensitivity (>= 85 dB 1 W / 0.1 m), larger diaphragm (20-28 mm), sealed back volume or an enclosed speaker-box module; splice leads, never solder the speaker tabs | FBS1810 77 dB, nothing below 800 Hz; F14 |
| Display: IPS panel on ST7789 (e.g. 2.0" 320x240 with 2x pixel doubling of 160x120, or 1.54" 240x240 with 120x120), TE and MISO routed | TN ST7735S: narrow viewing angle, washed colours, no TE (F11) |
| Amplifier soft-start and bulk capacitor | F6 (the pop-free firmware sequence covers clicks, not the supply dip) |
| Hardware high-pass at the amplifier input | C25 = C26 about 4.7 nF with 82k gives about 400 Hz; both legs matched |
| Filtering of VDDA and BAT_STAT (ferrite/RC); consider forced-PWM mode on the TPS63802 | F10 low-load ADC noise |
| Bulk capacitor on +V or ATtiny brown-out detection | F6 |
| SWO test point (PB3 is SPI3_SCK today) and UART pads | Debug without USB |
| Current-sense option in the battery lead | No ammeter needed for power work |
| ATtiny: honour OFF_ACK in the running state | Lets firmware power itself off after a low-battery cutoff |
| ATtiny brown-out detection (HFUSE today 0xFF = BOD off) | Defined behaviour of the power controller on a sagging cell (R31) |
| Battery temperature sense (BQ24075 TS is tied to a fixed 10k) | No thermal qualification while charging in a warm case (R32) |
| LSE crystal or a calibrated LSI | No real-time clock; Stop/Standby timers run on an uncalibrated LSI (R33) |
| BOR enabled in option bytes (firmware or factory step) | Brown-out on a sagging cell |
| Re-check the B button switch | 17.6 ms release bounce |

## Source Contradictions Found

| Topic | Sources | Resolution used here |
| --- | --- | --- |
| Raycaster render 5.1 ms / raytracer 41 fps vs 5.7 ms / 40 fps | demo 14:46 vs 15:20 (2026-11-17) | Latest listed first, earlier in brackets |
| Game-mix runtime 9 h (1.15 Wh / 124 mW) vs 13.2 h (analysis tool) vs 14-16 h | HW_LOG 2026-11-16, `summary.md`, cycle #2 | Cycle #2 logged 11.1 h of S15 from about 70-75 % SoC -> 14-16 h from full (preliminary); the 9 h figure assumed 310 mAh |
| Speaker 0.5 W | repo notes vs datasheet DOC061610363 | 0.8 W rated / 1.0 W max |
| PAM8302A gain 150k/(10k + Rin) (review agent) | vs Diodes application note 2 x 80k/(10k + Rin) | Application note used (gain about 6.9); the hardware advice (82k) is unchanged |
| `phyphox_sweep.py` step period 3.62 s | vs ticks every 3.50 s in the 2026-11-19/20 recordings | 3.50 s (new default) |
| R_int 330 mOhm (HWCAL) vs 291 / 376 mOhm (analysis) | HW_LOG 2026-11-17 vs `summary.md` | Range given |
| Test arena RAM "17.7 KB static" (README) vs about 50 KB (current build) | README vs `nm` 2026-11-18 | Current build used |
| Hardware overview "includes USB ESD protection" | hardware-overview.md vs bring-up log | ESD part is dead / removed on this board |
| F1 "hold-time check pending" | FINDINGS row text vs HW_LOG 2026-11-18 | Closed, user verified |
| 2.5 kHz speaker dip -13 dB | stepped sine vs chirp (2026-11-19), first (damaged) speaker | Superseded by the in-case response of the new speaker (2026-11-20) |
| Hardware scroll step 86-89 us vs 114 us | `display.vscroll` runs 2026-11-17 | 87 us (later run) used; 114 us run generated more per step |
| Audio TX "ramp" recipe (`audio_amp_on(ramp_ms)` ramps after SD#) vs pop-free default | earlier draft vs HW_LOG 2026-11-19 | By-ear test: ramp with amp on pops; precharge then SD# is the default |
