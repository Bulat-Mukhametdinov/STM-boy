# screen_render — off-device screen renderer

Renders every RetroPort firmware screen to PNG on a PC, with no hardware.

It compiles the **real** UI / app / emulator code (`app.cpp`, `emulator_browser.cpp`,
`chip8_screen.cpp`, `zx48_screen.cpp`, `Core/Src/ui/*`, `chip8.c`, `zx48.c`, the
z80 core, `rompack.c`) against a host build of the same LVGL v9.5.0 the firmware
uses. Only the hardware modules are stubbed (`host_stubs.c`) and the display is
redirected to an in-memory framebuffer (`host_display.c`).

## Run

```sh
tools/screen_render/render_all.sh
```

Outputs PNGs to `renders/` at the repo root. First run builds a real RPRP
rom-pack from `roms/` (`build/rompack.bin`) and fetches/builds LVGL.

## How it works

- **One shared framebuffer.** Both the LVGL flush callback and the `ST7735_WritePixels`
  stub write into the same 160x128 RGB565 buffer — exactly as they share one
  physical panel on the device. That's why the emulator game screens (which blit
  their framebuffer directly via `ST7735_WritePixels`, bypassing LVGL) composite
  correctly over the LVGL chrome.
- **Real navigation.** `render_main.cpp` calls `App_Init()` then pushes button
  actions into the input stub to walk the firmware's own state machine to each
  screen. One screen per process invocation keeps state clean.
- **Real ROM data.** The `W25Q128` stub serves `build/rompack.bin` to the
  unmodified `rompack.c`, so ROM lists show real titles/sizes and the emulators
  run real ROMs (CHIP-8 `15 Puzzle`, SUPER-CHIP `Applejak`, ZX `THOR.SNA`).
- **Memory backend.** `lv_conf_host.h` reuses the firmware `lv_conf.h` verbatim
  (identical colours/fonts/draw format) but swaps the 24 KiB builtin pool for the
  C heap — `lv_obj_t` structs are ~2x larger on a 64-bit host and overflow the
  MCU-sized pool. No effect on rendered pixels.

## Render a single screen

```sh
build/screen_render/render <screen> <out.ppm> [rom-list-down-steps]
python3 tools/screen_render/ppm_to_png.py <out.ppm> <out.png>
```

Screen ids: `main_menu`, `emulator_menu`, `settings`, `upload_idle`,
`upload_progress`, `rom_list_chip8|schip|zx|empty`, `chip8_game`, `schip_game`,
`zx48_game`, `theme_0`..`theme_4`.

Pick a different game ROM (Nth entry in its list) via env:
`CHIP8_SEL=17 SCHIP_SEL=3 ZX_SEL=0 tools/screen_render/render_all.sh`.
