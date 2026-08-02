# RetroPort Console Firmware

RetroPort is the STM32F411CEU6 console firmware for STM-boy: an LVGL-based
handheld shell with a main menu, emulator catalog, USB ROM-pack upload, and
persistent settings in external flash.

> Status: works on v0.1 hardware and is playable, but it is not finished.
> Read [Known Issues](#known-issues) before relying on it. For first power-up of
> a new board use [stm32-bringup](../stm32-bringup) instead, which validates the
> hardware without a UI framework in the way.

## Features

| Area | Details |
| --- | --- |
| UI | LVGL v9.5.0 shell with menus, sliders, battery indicator, and themes. |
| Emulators | CHIP-8, SUPER-CHIP, and an experimental ZX Spectrum 48K core. |
| Storage | W25Q128 external flash holds the ROM pack and persistent settings. |
| ROM upload | USB CDC upload protocol with erase, write, and CRC verification. |
| Audio | TIM1 PWM output mixed from a TIM5 8 kHz sample interrupt. |
| Power | Battery/charger/USB status and the ATTINY13A shutdown handshake. |

## Known Issues

These are open problems on the current code. They are documented rather than
hidden because the firmware is otherwise usable.

| Issue | Detail |
| --- | --- |
| Audio quality | Idle PWM carrier used to reach the always-on amplifier as constant hiss. The current workaround keeps the amplifier enabled and stops the carrier when nothing plays, which removes the hiss but still sounds wrong: short beeps can click or lose their attack. A proper fix likely needs output ramping or a DC-bias/mute strategy rather than hard carrier gating. |
| In-game button mapping | Emulator key mapping is fixed in firmware and does not match many ROMs, so controls inside games feel arbitrary. The ROM-pack format already carries per-ROM metadata; a per-ROM keymap field is the intended fix, so each title can bind physical buttons to its own keys. The ZX Spectrum core is worst affected. |
| Battery indicator jumps | Two causes. First, `Core/Src/power.c` selects ADC channel 1 but programs the sampling time into `SMP0` (channel 0), leaving channel 1 at the 3-cycle reset default. That is far too short for the ~23.6 kOhm divider source impedance, so conversions are noisy. Second, the code averages eight back-to-back conversions but does not smooth over time, while the battery node is a slow meander when the charger cycles. See the rolling-window approach in [stm32-bringup](../stm32-bringup) for a working reference. |
| Power-off from the PWR button | Reported as not working with this firmware on the v0.1 board, although the same ATTINY13A handshake works in the bring-up firmware. The code path exists (`Power_PollShutdown` and `Power_SetOffAck` in `Core/Src/power.c`, dispatched from `App_Task`). Root cause is not identified yet. |
| ZX Spectrum core | Experimental. Loading a snapshot works, but in-game movement and exit handling are unreliable. |
| RAM headroom | The Release build uses about 95.5 percent of the 128 KB SRAM. Adding features or LVGL widgets can push it over; check the linker memory report after every change. |

## Build

Requirements:

- `arm-none-eabi-gcc` and `arm-none-eabi-g++` on `PATH`, for example from
  STM32CubeCLT.
- CMake 3.22 or newer and Ninja.

```sh
cmake --preset Release
cmake --build --preset Release
```

Artifacts are written to `build/Release/`: `stm32-proj.elf`, `.hex`, and `.bin`.
A `Debug` preset is also available.

LVGL v9.5.0 is not vendored here; CMake fetches it with `FetchContent` on the
first configure, so that step needs network access. To build offline, download
the LVGL v9.5.0 source once, unpack it, and point CMake at it:

```sh
cmake --preset Release -DFETCHCONTENT_SOURCE_DIR_LVGL=/path/to/lvgl-9.5.0
cmake --build --preset Release
```

## Flash

```sh
STM32_Programmer_CLI -c port=SWD freq=4000 -w build/Release/stm32-proj.hex -v -rst
```

Flashing the STM32 does not erase the W25Q128, so uploaded ROM packs and saved
settings survive a firmware update.

## ROM Packs

Game ROMs are not stored in the STM32 flash. They are packed into a single
`RPRP` image and uploaded to the external flash over USB CDC.

ROM sources are expected in per-emulator folders. No ROM binaries are included
in this repository; see [roms/README.md](roms/README.md) for the layout and for
the copyright reasons behind that.

Build a pack:

```sh
python tools/pack_rompack.py roms -o build/rompack.bin
```

Upload it (needs `pyserial`, and the board must be on the `USB ROM Upload`
screen so the CDC port is enumerated):

```sh
python tools/flash_rompack_usb.py build/rompack.bin --port COM6
```

Both steps in one command:

```sh
python tools/pack_rompack.py roms -o build/rompack.bin --upload
```

Limits enforced by the packer:

| Constraint | Value |
| --- | --- |
| ROMs per pack | 160 |
| CHIP-8 / SUPER-CHIP ROM size | 3584 bytes (`4096 - 0x200`) |
| ZX Spectrum snapshot size | 49179 bytes (`.sna`) |
| ROM-pack flash region | 15 MiB at offset `0x100000` |

Optional CHIP-8 metadata (proper titles, quirk flags, tick rates) can be pulled
from the [CHIP-8 database](https://github.com/chip-8/chip-8-database):

```sh
python tools/pack_rompack.py roms -o build/rompack.bin --chip8-database /path/to/chip-8-database
```

## Controls

| Context | Action | Buttons |
| --- | --- | --- |
| Menus | Move cursor | Joystick up/down |
| Menus | Adjust slider or stepper | Joystick left/right |
| Menus | Select | A or joystick center |
| Menus | Back | B |
| CHIP-8 game | Exit to ROM list | Hold B + center |
| ZX Spectrum game | Exit to ROM list | Hold center + Y |

Note that in-game emulator keys are a separate mapping and are currently
unreliable; see [Known Issues](#known-issues).

## External Flash Layout

| Region | Offset | Size | Purpose |
| --- | --- | --- | --- |
| State/settings | `0x000000` | 1 MiB | Settings log in the first 4 KiB sector. |
| ROM pack | `0x100000` | Remaining | `RPRP` multi-system ROM-pack image. |

## Project Layout

| Path | Purpose |
| --- | --- |
| [Core/Src/app.cpp](Core/Src/app.cpp) | Application shell, menus, upload and settings screens. |
| [Core/Src/ui/](Core/Src/ui) | Small C++ wrappers over LVGL widgets. |
| [Core/Src/chip8.c](Core/Src/chip8.c), [chip8_screen.cpp](Core/Src/chip8_screen.cpp) | CHIP-8 interpreter and gameplay screen. |
| [Core/Src/zx48.c](Core/Src/zx48.c), [zx48_screen.cpp](Core/Src/zx48_screen.cpp) | Experimental ZX Spectrum 48K runtime. |
| [Core/Src/emulators.cpp](Core/Src/emulators.cpp) | Emulator catalog and shared browser wiring. |
| [Core/Src/rompack.c](Core/Src/rompack.c), [rom_upload.c](Core/Src/rom_upload.c) | ROM-pack reader and USB CDC upload protocol. |
| [Core/Src/w25q128.c](Core/Src/w25q128.c), [settings.c](Core/Src/settings.c) | External flash driver and persistent settings. |
| [Core/Src/st7735.c](Core/Src/st7735.c), [display.c](Core/Src/display.c) | Display driver and LVGL binding. |
| [Core/Src/input.c](Core/Src/input.c), [audio.c](Core/Src/audio.c), [power.c](Core/Src/power.c) | Input scanning, PWM audio mixer, power status. |
| [tools/](tools) | ROM-pack packer, USB uploader, and host-side render helpers. |
| [Drivers/](Drivers), [Middlewares/](Middlewares) | Vendored STM32Cube FW_F4 V1.28.3 sources under their own ST licenses. |
| [AGENTS.md](AGENTS.md) | Detailed architecture and conventions for working on this firmware. |

## Third-Party Components

| Component | License | How it is included |
| --- | --- | --- |
| STM32 HAL, CMSIS, USB Device Library | ST terms, see the `LICENSE.txt` files under `Drivers/` and `Middlewares/` | Vendored. |
| LVGL v9.5.0 | MIT | Fetched by CMake at configure time. |
| superzazu Z80 core | MIT, see `Core/Inc/vendor/superzazu_z80/LICENSE` | Vendored. |
| CHIP-8 database | MIT | Optional, referenced by path at pack time. |

## Hardware Notes

The authoritative pin assignment for the board is
[docs/pinout.md](../../docs/pinout.md). In particular, `BAT_STAT` and
`CHRG_STAT` are swapped on v0.1 relative to the original schematic notes; this
firmware uses the verified assignment in `Core/Inc/board_pins.h`.
