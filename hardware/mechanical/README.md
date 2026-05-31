# Mechanical and 3D Files

Put STM-boy mechanical files in this directory.

## Recommended Files

| File | Purpose |
| --- | --- |
| `stm-boy-v0.1-assembly.step` | Full device or board stack model for CAD inspection. |
| `stm-boy-v0.1-front-panel.dxf` | Laser-cut acrylic/front-panel outline. |
| `stm-boy-v0.1-rear-spacer.stl` | Printable spacer or rear mechanical part, if used. |
| `stm-boy-v0.1-dimensions.pdf` | Human-readable dimension drawing. |

## Notes to Record

- PCB thickness.
- Acrylic/front-panel thickness.
- Spacer or standoff height.
- Screw type, thread, and length.
- Battery dimensions and mounting method.
- Display clearance and viewing-window dimensions.
- Speaker clearance and acoustic opening.
- Any insulation needed near the LiPo battery or exposed pads.

## Release Criteria

Before publishing a mechanical release, check the 3D files against the exact PCB
revision in `hardware/pcb/` and record any known fit issues in
`docs/revisions.md`.
