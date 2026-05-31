# Hardware Overview

STM-boy is a compact handheld console built around an STM32F411 MCU. The design
goal is to keep the device close to a credit-card form factor while still
including the subsystems expected from a self-contained embedded platform:
display, controls, audio, external storage, USB, battery charging, regulated
power, and debug access.

## Concept

The project is inspired by small open handhelds such as Arduboy, but uses a
color TFT display and a more capable STM32 platform. The board is also intended
as a learning device for embedded systems: it exposes practical topics such as
SPI peripherals, USB device mode, external flash, PWM audio, power-path design,
soft power control, and SWD debugging.

The visual style is an exposed "skeleton" layout. The PCB is the main structural
element, with a transparent front cover planned to protect the display and
electronics while keeping the internal architecture visible.

## Functional Blocks

### Main MCU

The main processor is STM32F411CEU6. It handles the user interface, display
rendering, game logic, USB behavior, flash storage, input scanning, audio
generation, and communication with the power controller.

The board includes:

- 8 MHz external crystal.
- Boot and reset controls.
- SWD debug/recovery pads.
- Decoupling and analog supply filtering around the MCU power pins.

### Display

The display is a 1.8 inch ST7735S-based 128x160 TFT panel connected over SPI.
The backlight is switched by a small transistor stage so firmware can control
brightness.

### Input

The console uses a classic handheld input layout:

- 5-way joystick for navigation.
- Four action buttons: A, B, X, Y.
- Dedicated power, reset, and boot controls.

### External Storage

W25Q128 SPI flash provides 16 MB of non-volatile storage. Intended uses include
game assets, sprites, audio data, save files, logs, and menu metadata.

Quad SPI is intentionally not used in the current design; WP# and HOLD# are tied
high.

### Audio

The MCU generates an audio PWM signal, filtered before entering a PAM8302AASCR
class-D amplifier. The amplifier drives a compact speaker through a board
connector.

### USB-C

USB-C provides 5 V input power and USB data. The design includes USB ESD
protection and CC pull-down resistors for device-mode behavior.

Planned firmware workflows:

- USB mass-storage style file transfer.
- User firmware update over USB.
- Recovery/debug through SWD when USB firmware is not available.

### Battery and Power

The board is powered either from USB or a single-cell LiPo battery.

Main power blocks:

- BQ24075 power-path charger for USB/battery source management.
- TPS63802 buck-boost regulator for the system 3.3 V rail.
- ATTINY13A soft power controller to enable/disable the main regulator and
  request graceful shutdown from the STM32.

The battery described in the design notes is LP233350, 310 mAh, with built-in
over-discharge protection.

## Revision Notes

The visible PCB artwork is marked `v0.1`. Treat the current hardware package as
a prototype revision until it has a recorded bring-up log and known-issues list.

Recommended revision labels:

- `v0.1`: first documented PCB prototype.
- `v0.1-breadboard`: wiring and firmware used before the custom PCB.
- `v0.2`: first board revision after bring-up fixes.
