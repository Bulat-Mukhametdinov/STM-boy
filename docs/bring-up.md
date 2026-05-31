# Bring-Up Guide

Use this checklist before powering a newly assembled PCB. Record the results in
a revision-specific bring-up log before publishing a release package.

## Safety

- Use a current-limited bench supply for first power-up.
- Do not connect a LiPo cell until the 3.3 V rail and charger behavior have been
  checked.
- Inspect USB-C, battery, charger, and regulator areas for solder bridges before
  applying power.
- If the board heats up, shut power off immediately and locate the fault before
  trying again.

## 1. Visual Inspection

- Check component orientation for U1, U3, U4, U6, U7, U8, USB-C, display FPC,
  battery connector, and speaker connector.
- Check for shorts around fine-pitch ICs and the USB-C connector.
- Confirm the display connector/pads match the intended panel orientation.
- Confirm no mechanical fastener can short exposed pads or traces.

## 2. Resistance Checks Before Power

Measure resistance with the board unpowered:

| Net | Expected result |
| --- | --- |
| +5V to GND | No short. |
| +V to GND | No short. |
| 3V3 to GND | No short. |
| BAT+ to GND | No short. |
| D+ to GND / D- to GND | No short. |

## 3. USB Power Path

Power from USB or a current-limited 5 V source:

- Confirm +5V is present after the USB-C input.
- Confirm ESD parts and USB-C shield do not heat.
- Confirm the charger input pin sees the expected voltage.
- Confirm the system does not exceed the expected idle current for an
  unprogrammed or reset board.

## 4. 3.3 V Regulator

Enable the main regulator through the power controller path or force the enable
signal only if the schematic supports it safely.

Check:

- 3V3 rail is within tolerance.
- TPS63802 does not overheat.
- MCU VDD and VDDA pins see 3.3 V.
- Display and flash supply pins see 3.3 V.

## 5. Power Controller

Validate the ATTINY13A soft power behavior:

- Short press: main 3.3 V rail turns on.
- Shutdown request: ATTINY asserts OFF_REQ to the STM32.
- Firmware acknowledgement: STM32 asserts OFF_ACK.
- Timeout path: ATTINY eventually shuts the system down if OFF_ACK is missing.
- Off state: current draw is low enough for long-term storage.

## 6. MCU Programming and Debug

- Connect SWD with GND, 3V3 reference, SWDIO, SWCLK, and NRST.
- Verify the debugger can identify the STM32.
- Flash a minimal GPIO blink or display test.
- Verify BOOT0 and reset buttons work as expected.

## 7. Peripherals

| Peripheral | Test |
| --- | --- |
| Display | Initialize ST7735S, draw solid colors, then test backlight PWM. |
| Buttons | Confirm every button reads correctly and has stable debounce behavior. |
| Flash | Read JEDEC ID, erase/write/read one test sector. |
| USB | Check enumeration and VBUS detection. |
| Audio | Play a low-volume PWM tone, then validate amplifier shutdown control. |
| Charger | Validate charge status signal and safe battery charge behavior. |

## 8. Measurements to Publish

Add these results to the release notes or a hardware revision log:

- Idle current on USB.
- Idle current on battery.
- Current with display at minimum and maximum backlight.
- Deep-off current.
- Battery charge current.
- 3V3 ripple under display/audio load.
- Known issues found during bring-up.
