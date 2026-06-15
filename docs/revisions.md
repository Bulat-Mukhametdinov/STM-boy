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

Test status:

- Breadboard firmware exists for display, buttons, external flash, and demo UI.
- ATTINY13A firmware builds and includes USBasp flashing documentation.
- Custom PCB bring-up log is not recorded yet.
- STM32 production console firmware is not included yet.
- Mechanical files should be checked against the manufactured PCB before being
  marked as production-ready.

Known gaps before a manufacturing-ready release:

- Add pick-and-place/centroid output.
- Add top and bottom assembly drawings.
- Add acrylic/front-panel mechanical drawings.
- Record first-power and peripheral bring-up measurements.
- Document any required factory assembly options.

## Revision Policy

Use semantic hardware-style revision names:

- `v0.x` for prototype boards.
- `v1.0` for the first revision that has been assembled, tested, and documented
  well enough for others to reproduce.

Every release should include source files, manufacturing files, PDFs, BOM,
known issues, and a short test summary.
