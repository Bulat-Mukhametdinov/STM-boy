# Open Hardware Release Checklist

This checklist is the gap between "files are on GitHub" and a reusable open
hardware project that other people can inspect, build, and improve.

## Must Have

- [x] Add a hardware license.
- [x] Add a firmware/software license when production firmware is included.
- [x] Add a documentation/media license.
- [x] Keep editable source design files in the repository.
- [x] Include exported schematics.
- [x] Include exported PCB layout.
- [x] Include a BOM.
- [x] Include Gerber manufacturing files.
- [ ] Include pick-and-place/centroid files.
- [ ] Include assembly drawings for top and bottom sides.
- [ ] Add a bring-up log for the first assembled PCB.
- [ ] Add a known-issues section per hardware revision.
- [ ] Add clear build/fabrication instructions.
- [ ] Add firmware build and flashing instructions.
- [ ] Add verified mechanical fit notes for 3D/CAD files.

## Recommended Repository Structure

```text
STM-boy/
  README.md
  LICENSES/
    hardware.txt
    firmware.txt
    documentation.txt
  docs/
    hardware-overview.md
    pinout.md
    bring-up.md
    revisions.md
    assembly.md
    firmware.md
    troubleshooting.md
  hardware/
    pcb/
      source/
      fabrication/
      assembly/
      media/
    mechanical/
      README.md
      stm-boy-assembly.step
      front-panel.dxf
      acrylic-panel.dxf
      spacers-and-fasteners.md
    breadboard/
  firmware/
    stm32/
    attiny13a-power-controller/
  releases/
    v0.1/
```

The current repository already has `hardware/pcb`, `hardware/breadboard`, and
basic docs. The most important next directories are `firmware/`,
`hardware/mechanical/`, and revisioned release packages.

## Licensing Options

Pick the license based on the reuse model you want:

| Area | Permissive option | Reciprocal option |
| --- | --- | --- |
| Hardware | CERN-OHL-P-2.0 | CERN-OHL-S-2.0 |
| Firmware | MIT or Apache-2.0 | GPL-3.0 |
| Docs/media | CC BY 4.0 | CC BY-SA 4.0 |

The selected default for this repository is documented in `LICENSE.md`:

- Hardware: `CERN-OHL-S-2.0`.
- Firmware/software: `MIT`.
- Documentation/media: `CC-BY-4.0`.

## What to Write Better

### README

The README should answer these questions in the first screen:

- What is this device?
- What works today?
- What is still experimental?
- What files should a builder open first?
- Can the board be manufactured from the repository?

### Hardware Documentation

Add a short page for every major subsystem:

- Power path and battery safety.
- 3.3 V regulator.
- STM32 clock, boot, reset, and debug.
- Display and backlight.
- External flash.
- Audio amplifier and speaker.
- Buttons and joystick.
- USB-C and ESD protection.
- ATTINY13A soft power controller.

Each page should include the design intent, important component values, firmware
signals, and bring-up checks.

### Manufacturing

Add these missing files if the EDA tool can export them:

- Pick-and-place CSV.
- Top assembly drawing.
- Bottom assembly drawing.
- PCB stackup and thickness notes.
- Board dimensions.
- Recommended PCB finish and soldermask color.
- DNP parts list if any parts should not be assembled by the factory.

### Mechanical Design

The concept mentions a transparent acrylic front panel. Add:

- STEP/STP model of the assembled device or PCB/mechanical stack.
- STL/3MF files for printable parts, if any.
- DXF/SVG/PDF outline for the acrylic panel.
- Screw/standoff specification.
- Display clearance notes.
- Battery placement and adhesive notes.
- Any required insulation film.

### Firmware

Move from a single validation sketch to a documented firmware folder:

- STM32 application firmware.
- ATTINY13A power-controller firmware.
- Build instructions.
- Flashing instructions.
- External flash content format.
- USB update/mass-storage behavior.
- Minimal demo game or hardware test app.

## Release Checklist

For every public hardware release, create a tag and release archive with:

- Source project files.
- Schematics PDF.
- PCB PDF.
- Gerbers.
- Drill files.
- BOM.
- Pick-and-place file.
- Assembly drawings.
- Firmware binary or source commit reference.
- Known issues and tested status.
