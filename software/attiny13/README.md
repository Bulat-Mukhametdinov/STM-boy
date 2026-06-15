# ATTINY13A Power Controller Firmware

This directory contains the STM-boy soft-power controller firmware for the
ATTINY13A. The firmware keeps the main 3.3 V regulator off during storage,
turns the console on from the PWR button, requests graceful shutdown from the
STM32, and provides a forced reset path when the button is held for a long time.

The implementation is intentionally small and deterministic: no dynamic
allocation, no standard-library I/O, one Timer0 millisecond tick, one wake-only
pin-change interrupt, and a finite-state machine in the main loop.

## Hardware Contract

| ATTINY13A pin | Board signal | Direction | Notes |
| --- | --- | --- | --- |
| PB0 | PWR / ISP_MOSI | Input | Active-low button, internal pull-up enabled. |
| PB1 | ISP_MISO | ISP only | Unused by the runtime firmware. |
| PB2 | OFF_ACK / ISP_SCK | Input | STM32 shutdown acknowledgement. |
| PB3 | OFF_REQ | Output | Active-low open-drain style request to STM32. |
| PB4 | EN | Output | Active-high regulator enable. |
| PB5 | RESET / ISP_RST | Reset | Keep reset enabled for ISP programming. |

OFF_REQ is released as Hi-Z when inactive, so the STM32-side pull-up defines the
inactive level. OFF_ACK is sampled as a digital logic signal from the STM32; only
the mechanical PWR button is debounced.

## User Behavior

| State | User action | Result |
| --- | --- | --- |
| Off | Hold PWR for at least `T_POWER_ON_HOLD_MS` | Enable the main regulator. |
| Running | Press shorter than `T_SHUT_HOLD_MS` | Ignored. |
| Running | Hold for `T_SHUT_HOLD_MS`, then release | Assert OFF_REQ and wait for OFF_ACK. |
| Running | Hold for `T_HARD_RESET_MS` | Cut EN for `T_RESET_CUT_MS`, then wait for release and power on again. |
| Shutdown request | STM32 asserts OFF_ACK or timeout expires | Disable EN and return to off lockout. |

Timing constants are configured in [config.h](config.h).

## Build

Requirements:

- `avr-gcc`, `avr-objcopy`, and `avr-size`
- GNU Make
- `avrdude` for programming

Build the default 1.2 MHz firmware:

```sh
make
```

The default Makefile assumes the factory clock setup: internal 9.6 MHz RC
oscillator divided by 8 through the CKDIV8 fuse. This gives a 1.2 MHz system
clock, which is enough for this power-controller FSM and gives better voltage
and current margin than running at full oscillator speed.

```make
F_CPU = 1200000UL
TIMER0_PRESCALER = 8UL
LFUSE = 0x6A
HFUSE = 0xFF
```

If you deliberately disable CKDIV8 and run from the full 9.6 MHz oscillator,
build with the matching CPU clock and Timer0 prescaler:

```sh
make F_CPU=9600000UL TIMER0_PRESCALER=64UL LFUSE=0x7A
```

Build artifacts are written to `build/`. Current checked build size with
avr-gcc 7.3.0 is about 828 bytes of Flash and 12 bytes of SRAM.

## Flashing With USBasp / USB ISP

Use the ATTINY13A ISP pads, not the STM32 SWD pads.

| USBasp signal | STM-boy ISP pad | ATTINY13A pin |
| --- | --- | --- |
| MOSI | MOSI | PB0 |
| MISO | MISO | PB1 |
| SCK | SCK | PB2 |
| RST | RST | PB5 / RESET |
| VCC | PWR | ATTINY supply/reference |
| GND | GND | Ground |

Recommended programming setup:

- Use a USBasp set to a safe target voltage, preferably 3.3 V, or power the
  board normally and use the USBasp VCC pin only as target reference if your
  programmer supports that mode.
- Always connect common GND.
- Do not hold the PWR button while programming; PB0 is also ISP_MOSI.
- Keep the STM32 unpowered, reset, or otherwise Hi-Z on OFF_ACK while
  programming; PB2 is also ISP_SCK.
- Use a slow ISP bit clock for factory-fused chips. The Makefile defaults to
  `-B 10`.

Flash only the firmware:

```sh
make flash
```

Read current fuses:

```sh
make read-fuses
```

Program the recommended fuses for the default 1.2 MHz build:

```sh
make fuses
```

Equivalent direct `avrdude` commands:

```sh
avrdude -p t13 -c usbasp -B 10 -U flash:w:build/power_fsm.hex:i
avrdude -p t13 -c usbasp -B 10 -U lfuse:w:0x6A:m -U hfuse:w:0xFF:m
```

Do not disable SPI programming, do not enable debugWIRE for normal production
flashing, and do not program `RSTDISBL`. Any of those can make regular USBasp
ISP recovery impossible without high-voltage programming.

If `avrdude` is installed outside `PATH`, pass it explicitly:

```sh
make flash AVRDUDE=/path/to/avrdude
```

On Windows, use forward slashes or quote the path:

```sh
make flash AVRDUDE="C:/Users/Bool/AppData/Local/Arduino15/packages/arduino/tools/avrdude/8.0.0-arduino1/bin/avrdude.exe"
```

## Project Layout

| File | Purpose |
| --- | --- |
| [main.c](main.c) | Initialization and main control loop. |
| [config.h](config.h) | Pin assignment, timing, and Timer0 tick configuration. |
| [hw.c](hw.c), [hw.h](hw.h) | ATTINY13A register-level hardware access and sleep entry. |
| [timebase.c](timebase.c), [timebase.h](timebase.h) | Timer0 CTC millisecond counter. |
| [inputs.c](inputs.c), [inputs.h](inputs.h) | PWR debounce and FSM input flags. |
| [power_fsm.c](power_fsm.c), [power_fsm.h](power_fsm.h) | Soft-power finite-state machine. |
| [Makefile](Makefile) | Build, size, flash, and fuse targets. |
| [.gitignore](.gitignore) | Local ignore rules for build products and editor state. |
