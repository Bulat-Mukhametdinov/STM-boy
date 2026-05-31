# PCB Hardware Package

This directory contains the current PCB artifacts for STM-boy hardware rev v0.1.

## Files

| File | Purpose |
| --- | --- |
| [stm_boy_proj.epro](stm_boy_proj.epro) | Editable PCB project source/export. |
| [schematics.pdf](schematics.pdf) | Human-readable schematic export. |
| [pcb.pdf](pcb.pdf) | PCB layout export. |
| [BOM.csv](BOM.csv) | Bill of materials. |
| [Gerber_file.zip](Gerber_file.zip) | Fabrication Gerber archive. |

## Before Fabrication

Check these items before sending the board to a PCB manufacturer:

- Gerber archive matches the schematic and PCB PDF in this repository.
- Board outline and mounting holes are correct.
- USB-C footprint and display footprint match the actual purchased parts.
- Battery connector polarity is verified.
- LiPo charger configuration and charge current are safe for the selected cell.
- 3.3 V regulator feedback values match the intended output voltage.
- SWD and ATTINY ISP pads are reachable after assembly.

## Missing Export Files

For a cleaner open hardware release, add:

- Pick-and-place/centroid CSV.
- Top-side assembly drawing.
- Bottom-side assembly drawing.
- PCB stackup and board thickness notes.
- DNP list if any BOM lines should not be assembled.
- A revision log describing what was tested on rev v0.1.
