#!/bin/sh
set -eu

# Example: refresh RetroPort CHIP-8 ROMs from chip-8-database metadata.
# Usage:
#   sh tools/import_chip8_database_example.sh
#   sh tools/import_chip8_database_example.sh --upload

backup_dir="backups/chip8-roms-$(date +%Y%m%d-%H%M%S)"
database_dir="build/chip-8-database"

if [ ! -f "$database_dir/database/programs.json" ]; then
  rm -rf "$database_dir"
  git clone --depth 1 https://github.com/chip-8/chip-8-database.git "$database_dir"
fi

mkdir -p "$backup_dir"
cp -R roms/chip8/. "$backup_dir"

python3 -B tools/import_chip8_database.py \
  --database "$database_dir" \
  --source "$backup_dir" \
  --output roms/chip8

python3 -B tools/pack_rompack.py roms \
  --chip8-database "$database_dir" \
  -o build/rompack.bin "$@"
