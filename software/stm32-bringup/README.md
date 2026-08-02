# STM32 Bring-Up Firmware

This directory contains a small STM32F411CEU6 firmware used to validate STM-boy
hardware after assembly. It is intentionally simple and dependency-free: no
graphics library, no emulators, no external flash access. Every subsystem it
touches is one that has to work before the console firmware is worth flashing.

Use this firmware first on a freshly assembled board, then move on to
[stm32-retroport](../stm32-retroport) once the checks below pass.

## What It Validates

| Subsystem | Check |
| --- | --- |
| Clocks | HSE 8 MHz crystal, PLL to 96 MHz SYSCLK, 48 MHz USB clock. |
| Display | ST7735S over SPI1, landscape 160x128, backlight control. |
| Buttons | All 9 controls (D-pad, center, A/B/X/Y) with debounce. |
| Audio | PAM8302 amplifier and TIM1 PWM tone output, one pitch per button. |
| Battery | ADC divider read, millivolts, percent, battery-present detection. |
| Charger | `CHRG_STAT` charge indicator. |
| USB | USB FS device enumeration as a CDC virtual COM port. |
| Power control | ATTINY13A `OFF_REQ` / `OFF_ACK` shutdown handshake. |

## On-Screen Layout

The display shows a status panel and a live button map:

- Battery gauge bar, colored green/yellow/red by charge level.
- Battery voltage in millivolts, and either charge percent or `NO BAT`.
- `ADC PIN:xxxxmV`, the raw divider voltage, for debugging the sense path.
- Two indicator squares: `CHG` (green while charging) and `USB` (blue when VBUS
  is present).
- Nine outlined boxes arranged like the physical controls. Pressing a button
  fills its box and plays that button's tone, so a stuck or miswired control is
  immediately visible.

## USB Debug Output

When USB is connected the board enumerates as a CDC virtual COM port
(VID `0x0483`, PID `0x5740`). It prints one status line per second plus a line
per button edge:

```text
BATT=4198mV PIN=2992mV (99%) PRESENT=1 CHG=0 USB=1
BTN A down
BTN A up
```

| Field | Meaning |
| --- | --- |
| `BATT` | Battery voltage after divider compensation. |
| `PIN` | Raw voltage measured at the ADC pin. |
| `PRESENT` | Battery detected as a real, stable cell. |
| `CHG` | Charger reports charging. |
| `USB` | VBUS present. |

Any serial terminal works; the port speed is irrelevant for USB CDC.

## Battery Sensing Notes

The battery divider is `VOUT - R12 (33k) - BAT_STAT - R13 (82k) - GND`, so the
firmware scales the measured pin voltage by `115/82`.

Battery presence is not a simple voltage threshold. With no cell installed, the
charger cycles and the sense node becomes a meander instead of a stable DC
level. The firmware samples the pin into a rolling window (32 samples, 20 ms
apart) and treats the reading as a real battery only when the window is stable
(spread under 200 mV) and inside a plausible LiPo range (2700-4300 mV).

> Note: on the v0.1 board `BAT_STAT` and `CHRG_STAT` are swapped relative to the
> original schematic notes. This firmware uses the verified assignment:
> `BAT_STAT = PA1` (ADC1_IN1) and `CHRG_STAT = PA0`. See
> [docs/pinout.md](../../docs/pinout.md).

## Power Controller Handshake

The firmware implements the STM32 side of the ATTINY13A soft-power protocol
described in [software/attiny13](../attiny13):

| Signal | Pin | Behavior |
| --- | --- | --- |
| `OFF_REQ` | PA2 | Input, active-low. Debounced for 30 ms before it is accepted. |
| `OFF_ACK` | PA3 | Output, active-high. Driven low from boot, asserted to confirm shutdown. |

On an accepted request the firmware mutes the amplifier, shows `POWER OFF`,
turns the backlight off, and then asserts `OFF_ACK`. The ATTINY cuts the rail
shortly after. Housekeeping takes about 450 ms, well inside the ATTINY's
2500 ms acknowledgement timeout.

`OFF_REQ` is only accepted after it has been observed idle-high at least once,
so a low level at power-up cannot trigger an immediate shutdown.

## Build

Requirements:

- `arm-none-eabi-gcc` and binutils, for example from STM32CubeCLT or the ARM
  GNU toolchain.
- GNU Make.

```sh
make
```

Artifacts are written to `build/`: `stm-boy-bringup.elf`, `.hex`, and `.bin`.

If the toolchain is not on `PATH`, point the build at it:

```sh
make TOOLROOT=/c/ST/STM32CubeCLT_1.20.0/GNU-tools-for-STM32/bin
```

Linker warnings about `_close`, `_lseek`, `_read`, and `_write` are expected.
They come from newlib stubs that this firmware never calls.

## Flash

Use the SWD pads with an ST-Link:

```sh
STM32_Programmer_CLI -c port=SWD freq=4000 -w build/stm-boy-bringup.hex -v -rst
```

Flashing the STM32 does not touch the W25Q128 external flash, so ROM packs and
saved settings survive a firmware update.

## Project Layout

| Path | Purpose |
| --- | --- |
| [Src/main.c](Src/main.c) | Clock setup, status panel, button map, main loop. |
| [Src/st7735.c](Src/st7735.c) | ST7735S SPI driver, text and rectangle drawing. |
| [Src/font5x7.c](Src/font5x7.c) | Minimal 5x7 bitmap font used by the status panel. |
| [Src/buttons.c](Src/buttons.c) | Debounced scanner with press/release edges. |
| [Src/buzzer.c](Src/buzzer.c) | TIM1 PWM tone output and amplifier enable. |
| [Src/power.c](Src/power.c) | ADC battery sensing, charger and VBUS status. |
| [Src/powerctl.c](Src/powerctl.c) | ATTINY13A shutdown handshake. |
| [Src/usbd_*.c](Src) | USB CDC device configuration and descriptors. |
| [Inc/](Inc) | Project headers, including `pinmap.h`. |
| [CMSIS/](CMSIS), [HAL/](HAL), [USB_Device/](USB_Device) | Vendored STM32Cube FW_F4 V1.28.3 sources, under their own ST licenses. |
| [Makefile](Makefile) | Build rules. |
| [STM32F411CEUX_FLASH.ld](STM32F411CEUX_FLASH.ld) | 512 KB flash / 128 KB RAM layout. |
