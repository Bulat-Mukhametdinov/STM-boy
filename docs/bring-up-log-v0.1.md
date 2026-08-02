# Bring-Up Log: v0.1

First assembled v0.1 board, brought up in July and August 2026 following
[bring-up.md](bring-up.md). This log records what was actually observed,
including the faults found and the workarounds applied.

Firmware used for these checks:
[software/stm32-bringup](../software/stm32-bringup).

## Summary

| Subsystem | Result |
| --- | --- |
| SWD and MCU | Pass. |
| Clocks | Pass. |
| 3.3 V rail | Pass. |
| Display | Pass, after driver configuration fixes. |
| Buttons and joystick | Pass, all 9 controls. |
| Audio | Pass electrically, audible idle hiss. |
| External flash | Pass, ROM packs write and verify. |
| USB | Pass only after bodging the ESD protection part. |
| Battery sense | Pass, after correcting the pin assignment. |
| Charger status | Pass. |
| VBUS sense | Marginal, needs a firmware workaround. |
| Power controller | Pass. |

## MCU, Clocks, And Debug

- ST-Link connects over SWD. Device ID `0x431`, recognized as `STM32F411xC/E`
  with 512 KB flash. Target voltage reported as 3.25 V.
- The external 8 MHz crystal starts and the PLL locks. Verified by reading `RCC`
  registers over SWD: `SYSCLK` runs from the PLL at 96 MHz, giving the exact
  48 MHz USB clock.
- PA11 and PA12 are correctly switched to their USB alternate function.

## Fault: USB ESD Protection Part Is Dead

The board did not enumerate at all. Windows logged no USB attach events, not
even a failed one.

Debug path and finding:

- Firmware was confirmed healthy first: the USB device stack initialized, the
  core reached its main loop, and the D+ pull-up was enabled. All checked by
  halting the core and reading registers over SWD, so the problem was not
  software.
- Continuity through `D2` (USBLC6-2P6) was missing in both directions:
  `DP1`/`DP2` to `D+` and `DN1`/`DN2` to `D-`. Soldering looked good, and the
  adjacent pins measured over 100 kOhm to ground, so it was not a bridge.
- The part is a flow-through ESD array: each data line should read as a near
  short between its two package pins. A dead internal connection explains the
  total absence of enumeration.

Workaround applied: `D2` was removed and the data lines were jumpered directly,
`DP` to `D+` and `DN` to `D-`, leaving the ground pad untouched. USB then
enumerated immediately as a CDC device.

> Warning: the board currently has no ESD protection on `D+` and `D-`. This is
> acceptable on the bench but must be restored before any real use, since a
> pocket-sized console is exactly the kind of device that collects static.

Secondary observation: the test laptop only has USB 3.0 ports, and enumeration
was unreliable on some of them. Worth ruling out early on other boards.

## Fault: BAT_STAT And CHRG_STAT Are Swapped

Battery readings were nonsense in every power scenario: railed high with USB
only, zero with a battery plus USB.

Finding: the two signals are swapped relative to the original pinout notes. The
ADC was reading the charger status line, and the digital charger input was
reading the analog divider, which also produced a phantom blinking charge
indicator.

Confirmed by shorting `CHRG_STAT` to ground and watching the battery ADC drop to
zero, with over 100 kOhm between the pins ruling out a solder bridge.

Corrected assignment, now used by all firmware in this repository:
`BAT_STAT = PA1` (`ADC1_IN1`), `CHRG_STAT = PA0`. See [pinout.md](pinout.md).

The divider itself is correct. Scope measurement: 4.2 V at the divider input
gives 3.0 V at the pin, matching the designed `82k / (33k + 82k)` ratio.

## Battery Node Behaves As A Meander With No Cell

With USB connected and no battery installed, the `BAT` node is not a stable
voltage. The BQ24075 cycles while trying to charge a missing cell, so the sense
pin swings instead of sitting still.

A single ADC sample therefore cannot distinguish "no battery" from "charged
battery". The bring-up firmware samples into a rolling window and treats the
reading as a real cell only when the window is stable and inside a plausible
LiPo range.

## VBUS Sense Reads High Without USB

With the board running from battery only, the `USB` indicator was on.

Measurements: the `+5V` rail floats to about 2.5 V with no USB connected,
apparently back-fed through the charger onto a high-impedance rail. The
`33k / 82k` divider turns that into about 1.75 V at PA9, which falls inside the
STM32 indeterminate input band, roughly 1.16 V to 2.15 V for a 3.3 V supply, so
the pin reads high.

Workaround applied: enable the PA9 internal pull-down in firmware. That drags
the idle node to about 1.1 V, below `VIL`, while a real 5 V VBUS still divides
high enough to read as a solid high. Verified in both states.

> The margin depends on the internal pull-down, which varies between roughly
> 30 kOhm and 50 kOhm across parts and temperature. A future revision should add
> an external pull-down of about 47 kOhm on the divider node, or lower the
> divider resistor values, so the threshold no longer depends on that spread.

## Display

The panel works. Three configuration details were needed:

| Symptom | Cause and fix |
| --- | --- |
| Image rotated | Landscape needs `MADCTL = 0xA0`, with X along the 160 px side. |
| Red and blue swapped | The panel is RGB, not BGR. Clearing the `BGR` bit in `MADCTL` fixed the colors. |
| Noise strip at two edges | The visible area is offset inside the 132x162 controller RAM. Fixed with `COLSTART = 3`, `ROWSTART = 1`, and clearing the full controller RAM once at init so unused pixels are black. |

## Audio

The PAM8302 amplifier and TIM1 PWM output work.

Open problem: the PWM carrier reaches the amplifier while nothing is playing and
is audible as hiss. Gating the amplifier removes the hiss but clips the start of
short sounds, because the amplifier needs time to wake. Gating the carrier
instead, with the amplifier always enabled, is the current compromise and still
does not sound right. See the known issues in
[software/stm32-retroport](../software/stm32-retroport/README.md).

## Power Controller

The ATTINY13A soft-power handshake works end to end with the bring-up firmware:

- Holding `PWR` for at least 500 ms and releasing makes the ATTINY assert
  `OFF_REQ`.
- The STM32 performs shutdown housekeeping and asserts `OFF_ACK`, which is
  active-high.
- The ATTINY cuts the rail immediately after the acknowledgement, well before
  its 2500 ms timeout.

## Not Measured Yet

The following items from [bring-up.md](bring-up.md) still have no recorded
numbers and should be filled in before a release package:

- Idle current on USB and on battery.
- Current with display backlight at minimum and maximum.
- Deep-off current in the ATTINY13A off state.
- Battery charge current.
- 3V3 ripple under display and audio load.
