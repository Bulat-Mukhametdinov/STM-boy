#!/usr/bin/env bash
# Build the host render harness and emit a PNG for every firmware screen.
#
#   tools/screen_render/render_all.sh
#
# Output: renders/*.png at the repo root.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
BUILD="$ROOT/build/screen_render"
OUT="$ROOT/renders"
PPM="$BUILD/ppm"
PACK="$ROOT/build/rompack.bin"

mkdir -p "$OUT" "$PPM"

# 1. ROM pack (real RPRP image the W25Q128 stub serves to rompack.c).
if [ ! -f "$PACK" ]; then
    echo "==> packing ROMs -> $PACK"
    python3 "$ROOT/tools/pack_rompack.py" "$ROOT/roms" -o "$PACK"
fi

# 2. Configure + build the harness.
echo "==> configuring + building harness"
cmake -S "$HERE" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD" -j >/dev/null

export ROMPACK_BIN="$PACK"
RENDER="$BUILD/render"

# 3. "<screen-id>:<output-name>" — screen-id is what the render binary drives;
#    output-name is the PNG basename written to renders/.
screens=(
    "main_menu:main_menu"
    "emulator_menu:emulator_menu"
    "settings:settings"
    "upload_idle:upload_idle"
    "upload_progress:upload_progress"
    "rom_list_chip8:rom_list_chip8"
    "rom_list_schip:rom_list_schip"
    "rom_list_zx:rom_list_zx"
    "rom_list_empty:rom_list_empty"
    "chip8_game:chip8_game"
    "schip_game:schip_game"
    "zx48_game:zx48_game"
    "theme_0:theme_retro"
    "theme_1:theme_hacker"
    "theme_2:theme_pastel_light"
    "theme_3:theme_pastel_dark"
    "theme_4:theme_soft_violet"
)

# Per-game ROM selection (down-steps in the ROM list) — pick something that
# draws an interesting frame quickly. Override via env, e.g. CHIP8_SEL=3.
CHIP8_SEL="${CHIP8_SEL:-0}"
SCHIP_SEL="${SCHIP_SEL:-0}"
ZX_SEL="${ZX_SEL:-0}"

for entry in "${screens[@]}"; do
    s="${entry%%:*}"      # screen id
    name="${entry##*:}"   # output basename
    sel=0
    case "$s" in
        chip8_game) sel="$CHIP8_SEL" ;;
        schip_game) sel="$SCHIP_SEL" ;;
        zx48_game)  sel="$ZX_SEL" ;;
    esac
    "$RENDER" "$s" "$PPM/$name.ppm" "$sel"
    python3 "$HERE/ppm_to_png.py" "$PPM/$name.ppm" "$OUT/$name.png"
done

echo "==> done. PNGs in $OUT"
ls -1 "$OUT"
