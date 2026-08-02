# Pinout

This file keeps the PCB and breadboard pin assignments in one place. Keep it in
sync with the schematic and firmware whenever a signal moves.

## PCB Pinout

This table reflects the assignment verified on assembled v0.1 hardware and used
by the firmware under [software/](../software).

| Signal | STM32 pin | Notes |
| --- | --- | --- |
| BAT_STAT | PA1 | ADC1_IN1. Swapped versus the original schematic notes, see below. |
| CHRG_STAT | PA0 | Charger status, active-low. Swapped versus the original schematic notes. |
| OFF_REQ | PA2 | Shutdown request from ATTINY13A, active-low. |
| OFF_ACK | PA3 | Shutdown acknowledgement to ATTINY13A, active-high. |
| CS_D | PA4 | Display chip select. |
| SCK_D | PA5 | SPI1 SCK. |
| BLK | PA6 | Display backlight, active-high. |
| MOSI_D | PA7 | SPI1 MOSI. |
| AUDIO_PWM | PA8 | TIM1_CH1. |
| VBUS | PA9 | USB VBUS divider input. |
| CE_BTN | PA10 | Joystick center. |
| D- | PA11 | USB FS DM. |
| D+ | PA12 | USB FS DP. |
| SWDIO | PA13 | SWD debug. |
| SWCLK | PA14 | SWD debug. |
| CS_F | PA15 | External flash chip select. |
| DC | PB0 | Display data/command. |
| RST_D | PB1 | Display reset. |
| AMP_SD | PB2 | Amplifier shutdown, active-high enable. |
| SCK_F | PB3 | SPI3 SCK. |
| MISO_F | PB4 | SPI3 MISO. |
| MOSI_F | PB5 | SPI3 MOSI. |
| RIGHT_BTN | PB6 | Joystick right. |
| LEFT_BTN | PB7 | Joystick left. |
| DOWN_BTN | PB8 | Joystick down. |
| UP_BTN | PB9 | Joystick up. |
| X_BTN | PB12 | Action button, physically left in the diamond. |
| A_BTN | PB13 | Action button, physically bottom. |
| B_BTN | PB14 | Action button, physically right. |
| Y_BTN | PB15 | Action button, physically top. |
| BOOT0 | BOOT0 | Boot strap. |

All buttons and joystick directions are active-low and rely on the MCU internal
pull-ups.

### Battery And Charger Sense Correction

On assembled v0.1 hardware `BAT_STAT` and `CHRG_STAT` are swapped relative to the
original pinout notes, which listed `BAT_STAT` on PA0 and `CHRG_STAT` on PA1.

The swap was confirmed by measurement: pulling `CHRG_STAT` to ground drove the
battery ADC reading to zero, and the battery voltage only became plausible after
the firmware moved battery sensing to PA1 / `ADC1_IN1`. Firmware in this
repository uses the corrected assignment. Re-verify this against the schematic
before spinning a new board revision.

### Action Button Layout

The action buttons are wired in the classic gamepad diamond. Firmware maps the
physical positions as `Y` top, `B` right, `A` bottom, `X` left, which is why the
pin order is not alphabetical.

## Debug Header

| Signal | Notes |
| --- | --- |
| SWDIO | STM32 debug data. |
| SWCLK | STM32 debug clock. |
| SWO | Optional trace output. |
| NRST | Target reset. |
| 3V3 | Target voltage reference. |
| GND | Common ground. |

## ATTINY13A ISP Pads

| ISP signal | ATTINY13A pin/function |
| --- | --- |
| ISP_MOSI | PB0 |
| ISP_MISO | PB1 |
| ISP_SCK | PB2 |
| ISP_RST | PB5 |
| +V | ATTINY supply rail |
| GND | Common ground |

## ATTINY13A Runtime Signals

| ATTINY13A pin | Runtime signal | ISP sharing | Notes |
| --- | --- | --- | --- |
| PB0 | PWR | ISP_MOSI | Active-low button input with internal pull-up. |
| PB1 | Unused | ISP_MISO | Reserved for programming only. |
| PB2 | OFF_ACK | ISP_SCK | STM32 acknowledgement input. |
| PB3 | OFF_REQ | Not shared | Active-low Hi-Z/open-drain style output to STM32. |
| PB4 | EN | Not shared | Active-high enable for the 3.3 V regulator. |
| PB5 | RESET | ISP_RST | Keep reset enabled for normal ISP recovery. |

## Breadboard Test Firmware Pinout

The table below matches
[hardware/breadboard/test_firmware.cpp](../hardware/breadboard/test_firmware.cpp).

### Buttons

| Control | STM32 pin |
| --- | --- |
| A | PA3 |
| B | PA2 |
| X | PA1 |
| Y | PA0 |
| Joystick up | PB5 |
| Joystick left | PB6 |
| Joystick right | PB7 |
| Joystick center | PB8 |
| Joystick down | PB9 |

### Display

| ST7735S signal | STM32 pin |
| --- | --- |
| BLK | PB10 |
| CS | PB0 |
| DC | PB1 |
| RST | PB2 |
| SDA / MOSI | PA7 |
| SCL / SCK | PA5 |
| VCC | 3V3 |
| GND | GND |

### External Flash

| W25Q128 signal | STM32 pin |
| --- | --- |
| CS | PA4 |
| SCK | PA5 |
| MISO | PA6 |
| MOSI | PA7 |
| VCC | 3V3 |
| GND | GND |

## Notes

- The PCB pinout and breadboard pinout are intentionally different.
- The breadboard firmware uses the MCU hardware SPI pins shared by the display
  and external flash, with separate chip select lines.
- The PCB audio and power-control signals are not covered by the current
  breadboard validation sketch.
