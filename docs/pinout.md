# Pinout

This file keeps the PCB and breadboard pin assignments in one place. Keep it in
sync with the schematic and firmware whenever a signal moves.

## PCB Pinout

| Signal | STM32 pin |
| --- | --- |
| BAT_STAT | PA0 |
| CHRG_STAT | PA1 |
| OFF_REQ | PA2 |
| OFF_ACK | PA3 |
| CS_D | PA4 |
| SCK_D | PA5 |
| BLK | PA6 |
| MOSI_D | PA7 |
| AUDIO_PWM | PA8 |
| VBUS | PA9 |
| CE_BTN | PA10 |
| D- | PA11 |
| D+ | PA12 |
| SWDIO | PA13 |
| SWCLK | PA14 |
| CS_F | PA15 |
| DC | PB0 |
| RST_D | PB1 |
| AMP_SD | PB2 |
| SCK_F | PB3 |
| MISO_F | PB4 |
| MOSI_F | PB5 |
| LEFT_BTN | PB6 |
| RIGHT_BTN | PB7 |
| UP_BTN | PB8 |
| DOWN_BTN | PB9 |
| A_BTN | PB12 |
| B_BTN | PB13 |
| X_BTN | PB14 |
| Y_BTN | PB15 |
| BOOT0 | BOOT0 |

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
