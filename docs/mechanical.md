# Mechanical Design

STM-boy is designed as a PCB-first handheld console: the PCB is both the main
electronic assembly and a visible structural element. Mechanical files should
make the physical stack easy to inspect, reproduce, and modify.

## Expected Package

Place mechanical and 3D files under `hardware/mechanical/`.

Recommended contents:

| File type | Purpose |
| --- | --- |
| STEP / STP | Source-neutral 3D model for inspection and CAD reuse. |
| STL / 3MF | Printable parts, if the project includes printed spacers, covers, or fixtures. |
| DXF / SVG | Laser-cut front panel, acrylic cover, or outline templates. |
| PDF | Dimension drawings for quick review. |
| README.md | Mechanical stack, fasteners, tolerances, and assembly notes. |

## Mechanical Stack

Document the stack from front to back:

1. Transparent front panel or protective cover.
2. Display clearance area.
3. Main PCB.
4. Speaker and battery side.
5. Rear insulation, spacer, or cover if used.

## Fit Checks

Before marking the mechanical package as release-ready, verify:

- Mounting-hole diameter and spacing.
- USB-C connector access.
- Button and joystick clearance.
- Display viewing window and flex/pad clearance.
- Battery size, connector orientation, and adhesive area.
- Speaker clearance and acoustic opening.
- Standoff height and screw length.
- No exposed metal part can short battery, USB, or power rails.

## Naming

Use revisioned names when possible:

- `stm-boy-v0.1-assembly.step`
- `stm-boy-v0.1-front-panel.dxf`
- `stm-boy-v0.1-rear-spacer.stl`
- `stm-boy-v0.1-dimensions.pdf`

This keeps CAD files tied to the matching PCB revision and avoids confusion when
the board outline changes.
