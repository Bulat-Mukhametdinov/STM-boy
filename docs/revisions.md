# Hardware Revisions

This file tracks board revisions, release status, and known issues.

## v0.1

Status: documented prototype revision.

Included artifacts:

- Editable PCB project: `hardware/pcb/stm_boy_proj.epro`.
- Schematics: `hardware/pcb/schematics.pdf`.
- PCB layout: `hardware/pcb/pcb.pdf`.
- BOM: `hardware/pcb/BOM.csv`.
- Gerbers: `hardware/pcb/Gerber_file.zip`.
- Mechanical/CAD notes: `hardware/mechanical/README.md`.
- Breadboard validation firmware: `hardware/breadboard/test_firmware.cpp`.
- ATTINY13A power-controller firmware: `software/attiny13`.
- STM32 bring-up firmware: `software/stm32-bringup`.
- STM32 console firmware: `software/stm32-retroport`.

Test status:

- One board has been assembled and brought up. Results are recorded in
  `docs/bring-up-log-v0.1.md`.
- All major subsystems were exercised on hardware: clocks, display, all nine
  controls, audio, external flash, USB CDC, battery and charger sensing, and the
  ATTINY13A power-off handshake.
- Two board-level faults were found and worked around. See the known issues
  below.
- Power consumption has not been measured yet.
- Mechanical files should be checked against the manufactured PCB before being
  marked as production-ready.

### Known Hardware Issues

| Issue | Status |
| --- | --- |
| USB ESD part `D2` (USBLC6-2P6) was dead on the assembled board, blocking all USB enumeration. Data lines had no continuity through the package. | Worked around by removing `D2` and jumpering `DP` to `D+` and `DN` to `D-`. The board currently has **no ESD protection on the USB data lines**. Restore protection before real use. |
| `BAT_STAT` and `CHRG_STAT` are swapped relative to the original pinout notes. | Firmware uses the verified assignment, `BAT_STAT = PA1`, `CHRG_STAT = PA0`. Re-verify against the schematic before the next board revision. |
| VBUS sense reads high with no USB connected: the `+5V` rail floats to about 2.5 V, putting the divided node inside the MCU indeterminate input band. | Worked around with the PA9 internal pull-down. Next revision should add an external pull-down of about 47 kOhm on the divider node, or lower the divider values. |

### Known Firmware Issues

Tracked in detail in `software/stm32-retroport/README.md`.

| Issue | Status |
| --- | --- |
| Audio still sounds wrong. The idle PWM carrier reached the always-on amplifier as hiss; gating the amplifier instead clipped the attack of short sounds. | Current compromise gates the carrier and keeps the amplifier enabled. Needs a proper ramp or mute strategy. |
| In-game button mapping is unreliable. Emulator keys are hardcoded and do not match many ROMs, worst on the ZX Spectrum core. | Intended fix is a per-ROM keymap carried in the ROM-pack metadata. |
| Battery indicator jumps around. | Two causes identified: the ADC sampling time is programmed for the wrong channel, leaving a far too short sample window for the high-impedance divider, and readings are not smoothed over time while the battery node is a meander when the charger cycles. |
| Power-off from the `PWR` button does not work in the console firmware, although the same handshake works in the bring-up firmware. | Root cause not identified yet. |

Known gaps before a manufacturing-ready release:

- Add pick-and-place/centroid output.
- Add top and bottom assembly drawings.
- Add acrylic/front-panel mechanical drawings.
- Record power consumption measurements listed at the end of the bring-up log.
- Fix or formally accept the known hardware and firmware issues above.
- Document any required factory assembly options.

## Revision Policy

Use semantic hardware-style revision names:

- `v0.x` for prototype boards.
- `v1.0` for the first revision that has been assembled, tested, and documented
  well enough for others to reproduce.

Every release should include source files, manufacturing files, PDFs, BOM,
known issues, and a short test summary.
