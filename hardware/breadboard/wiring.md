# Breadboard Wiring

This wiring file matches `test_firmware.cpp`. The breadboard pinout is not the
same as the custom PCB pinout.

## Required Parts

- STM32F411 development board or compatible STM32 board with Arduino core
  support.
- ST7735S 128x160 SPI TFT display.
- W25Q128 SPI flash module or breakout.
- 4 action buttons.
- 5-way joystick or 5 individual buttons.
- 3.3 V power source.

## Buttons

Buttons are active-low in the test firmware. Connect one side of each button to
GND and the other side to the listed MCU pin. The sketch enables internal
pull-ups.

| Control | STM32 pin |
| --- | --- |
| A | PA3 |
| B | PA2 |
| X | PA1 |
| Y | PA0 |
| Up | PB5 |
| Left | PB6 |
| Right | PB7 |
| Center | PB8 |
| Down | PB9 |

## ST7735S Display

| Display signal | STM32 pin |
| --- | --- |
| BLK | PB10 |
| CS | PB0 |
| DC | PB1 |
| RST | PB2 |
| SDA / MOSI | PA7 |
| SCL / SCK | PA5 |
| VCC | 3V3 |
| GND | GND |

## W25Q128 Flash

The display and flash share the SPI bus. Keep separate chip select lines.

| Flash signal | STM32 pin |
| --- | --- |
| CS | PA4 |
| SCK | PA5 |
| MISO | PA6 |
| MOSI | PA7 |
| VCC | 3V3 |
| GND | GND |

## Firmware Notes

- Arduino IDE is expected by the current sketch.
- Required libraries: `Adafruit_GFX` and `Adafruit_ST7735`.
- The sketch owns the TFT chip select through the `Adafruit_ST7735` instance.
  Do not manually toggle TFT CS in display code.
- Flash code should control only the flash CS pin.

## Demo

See [example.gif](example.gif) for the current breadboard demo.
