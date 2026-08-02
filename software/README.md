# STM-boy Software

This directory contains firmware components for STM-boy.

| Path | Target | Purpose |
| --- | --- | --- |
| [attiny13](attiny13) | ATTINY13A | Always-on soft-power controller for the main 3.3 V rail. |
| [stm32-bringup](stm32-bringup) | STM32F411CEU6 | Hardware validation firmware: display, buttons, audio, battery, USB CDC, power handshake. |
| [stm32-retroport](stm32-retroport) | STM32F411CEU6 | RetroPort console firmware: LVGL shell, emulators, ROM-pack upload. |

## Which Firmware To Use

On a freshly assembled board, flash [stm32-bringup](stm32-bringup) first. It has
no UI framework or emulators in the way, prints a status line over USB CDC, and
shows every button and power signal on screen, so a wiring or assembly fault is
easy to isolate.

Once the board passes those checks, flash
[stm32-retroport](stm32-retroport) for the actual console experience. It has
open issues that are documented in its README.

The ATTINY13A controller in [attiny13](attiny13) is independent of both and is
programmed through the ISP pads, not SWD.

## Shared Hardware Contract

Both STM32 firmwares use the pin assignment in
[docs/pinout.md](../docs/pinout.md), including the corrected `BAT_STAT` and
`CHRG_STAT` assignment verified during bring-up, and both implement the same
`OFF_REQ` / `OFF_ACK` shutdown handshake with the ATTINY13A.
