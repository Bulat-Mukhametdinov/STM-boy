#!/usr/bin/env python3

import argparse
import binascii
import hashlib
import json
import struct
import sys
import unicodedata
from pathlib import Path

import flash_rompack_usb

MAGIC = b"RPRP"
VERSION = 2
HEADER_SIZE = 64
ALIGNMENT = 16
DEFAULT_OUTPUT = "build/rompack.bin"
FIRMWARE_MAX_ROMS = 160
ROMPACK_FLASH_SIZE_BYTES = 15 * 1024 * 1024

HEADER_FORMAT = "<4sHHHHIIII36s"
ENTRY_SIZE = 72
TITLE_BYTES = 54
ENTRY_FORMAT = "<IIIHHBB54s"

CHIP8_QUIRK_FLAGS = {
    "shift": 0x0001,
    "memoryIncrementByX": 0x0002,
    "memoryLeaveIUnchanged": 0x0004,
    "wrap": 0x0008,
    "jump": 0x0010,
    "vblank": 0x0020,
    "logic": 0x0040,
}

# Systems whose ROMs draw metadata (title/quirks/tickrate) from the chip-8 database.
CHIP8_FAMILY_KEYS = ("chip8", "schip")

SYSTEMS = {
    "chip8": {
        "id": 1,
        "label": "CHIP-8",
        "extensions": (".ch8", ".c8", ".rom", ".bin"),
        "max_rom_size": 4096 - 0x200,
    },
    "schip": {
        "id": 3,
        "label": "SUPER-CHIP",
        "extensions": (".ch8", ".sc8", ".sch", ".c8", ".rom", ".bin"),
        "max_rom_size": 4096 - 0x200,
    },
    "zx48": {
        "id": 2,
        "label": "ZX Spectrum 48K",
        "extensions": (".sna",),
        "max_rom_size": 49179,
        "exact_rom_size": 49179,
    },
}


class RomEntry:
    def __init__(self, path, system_key, system_root, title, data):
        self.path = path
        self.system_key = system_key
        self.system_id = SYSTEMS[system_key]["id"]
        self.system_root = system_root
        self.title = title
        self.data = data
        self.offset = 0
        self.crc = binascii.crc32(data) & 0xFFFFFFFF
        self.option_flags = 0
        self.tickrate = 0
        self.metadata_source = None


def align(value, alignment=ALIGNMENT):
    return (value + alignment - 1) & ~(alignment - 1)


def encode_title(title):
    normalized = unicodedata.normalize("NFC", title).replace("\\", "/")
    encoded = normalized.encode("utf-8")
    if len(encoded) <= TITLE_BYTES:
        return normalized, encoded

    cut = encoded[:TITLE_BYTES]
    while True:
        try:
            trimmed = cut.decode("utf-8")
            return trimmed, trimmed.encode("utf-8")
        except UnicodeDecodeError:
            cut = cut[:-1]


def parse_extensions(value):
    result = []
    for item in value.split(","):
        ext = item.strip().lower()
        if not ext:
            continue
        if not ext.startswith("."):
            ext = "." + ext
        result.append(ext)
    return tuple(result)


def is_hidden(path, root):
    for part in path.relative_to(root).parts:
        if part.startswith("."):
            return True
    return False


def detect_system_for_file(path, selected):
    candidates = []
    for key in sorted(selected):
        config = SYSTEMS[key]
        if path.suffix.lower() in config["extensions"]:
            candidates.append(key)
    if len(candidates) == 1:
        return candidates[0]
    if len(selected) == 1:
        return next(iter(selected))
    known = ", ".join(sorted(selected))
    raise RuntimeError(f"cannot infer emulator system for {path}; pass --system ({known})")


def selected_systems(values):
    if not values:
        return set(SYSTEMS)

    result = set()
    for value in values:
        for item in value.split(","):
            key = item.strip().lower()
            if not key:
                continue
            if key not in SYSTEMS:
                known = ", ".join(sorted(SYSTEMS))
                raise RuntimeError(f"unknown system '{key}'; known systems: {known}")
            result.add(key)
    return result


def discover_system_dirs(root, selected, strict_unknown):
    root_files = []
    unknown_dirs = []
    system_dirs = []

    for path in sorted(root.iterdir(), key=lambda p: p.name.lower()):
        if path.name.startswith("."):
            continue
        if path.is_file():
            root_files.append(path.name)
            continue
        if not path.is_dir():
            continue
        key = path.name.lower()
        if key not in SYSTEMS:
            if strict_unknown:
                unknown_dirs.append(path.name)
            continue
        if key in selected:
            system_dirs.append((key, path))

    if root_files:
        examples = ", ".join(root_files[:3])
        raise RuntimeError(
            f"ROM files must live under emulator folders like roms/chip8/: {examples}"
        )
    if unknown_dirs:
        names = ", ".join(sorted(unknown_dirs))
        raise RuntimeError(f"unknown emulator folders: {names}")
    if not system_dirs:
        wanted = ", ".join(sorted(selected))
        raise RuntimeError(f"no selected emulator folders found under {root}: {wanted}")

    return system_dirs


def discover_roms(root, include_all, extension_override, max_rom_size_override, selected, strict_unknown,
                  excluded_path=None):
    entries = []
    if root.is_file():
        system_key = detect_system_for_file(root, selected)
        config = SYSTEMS[system_key]
        data = root.read_bytes()
        max_rom_size = config["max_rom_size"] if max_rom_size_override is None else max_rom_size_override

        if not data:
            raise RuntimeError(f"empty ROM: {root}")
        if max_rom_size is not None and max_rom_size > 0 and len(data) > max_rom_size:
            raise RuntimeError(
                f"ROM is too large for {config['label']} ({len(data)} > {max_rom_size}): {root}"
            )

        title, _ = encode_title(root.with_suffix("").name)
        entries.append(RomEntry(root, system_key, root.parent, title, data))
        return entries

    for system_key, system_root in discover_system_dirs(root, selected, strict_unknown):
        config = SYSTEMS[system_key]
        extensions = extension_override or config["extensions"]
        max_rom_size = config["max_rom_size"] if max_rom_size_override is None else max_rom_size_override

        for path in sorted(system_root.rglob("*"), key=lambda p: p.relative_to(system_root).as_posix().lower()):
            if not path.is_file() or is_hidden(path, system_root):
                continue
            if excluded_path is not None and path.resolve() == excluded_path:
                continue
            if not include_all and path.suffix.lower() not in extensions:
                continue

            data = path.read_bytes()
            rel = path.relative_to(system_root).as_posix()
            if not data:
                raise RuntimeError(f"empty ROM: {system_key}/{rel}")
            if max_rom_size is not None and max_rom_size > 0 and len(data) > max_rom_size:
                raise RuntimeError(
                    f"ROM is too large for {config['label']} ({len(data)} > {max_rom_size}): "
                    f"{system_key}/{rel}"
                )
            exact_rom_size = config.get("exact_rom_size")
            if max_rom_size_override is None and exact_rom_size is not None and len(data) != exact_rom_size:
                raise RuntimeError(
                    f"ROM has invalid size for {config['label']} ({len(data)} != {exact_rom_size}): "
                    f"{system_key}/{rel}"
                )

            title = path.relative_to(system_root).with_suffix("").as_posix()
            title, _ = encode_title(title)
            entries.append(RomEntry(path, system_key, system_root, title, data))

    if not entries:
        if include_all:
            raise RuntimeError(f"no ROM files found in selected folders under {root}")
        raise RuntimeError(f"no ROM files found in selected folders under {root}")
    return entries


def load_json(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def chip8_database_file(root, name):
    root = Path(root)
    direct = root / name
    nested = root / "database" / name
    if direct.is_file():
        return direct
    if nested.is_file():
        return nested
    raise RuntimeError(f"CHIP-8 database file not found: {name} under {root}")


def normalize_platforms(platforms):
    if isinstance(platforms, dict):
        return {item["id"]: item for item in platforms.values()}
    return {item["id"]: item for item in platforms}


def load_chip8_database(root):
    return {
        "hashes": load_json(chip8_database_file(root, "sha1-hashes.json")),
        "programs": load_json(chip8_database_file(root, "programs.json")),
        "platforms": normalize_platforms(load_json(chip8_database_file(root, "platforms.json"))),
    }


def first_dict(*values):
    for value in values:
        if isinstance(value, dict):
            return value
    return {}


def chip8_options_to_flags(options):
    flags = 0
    for name, bit in CHIP8_QUIRK_FLAGS.items():
        if bool(options.get(name, False)):
            flags |= bit
    return flags


def chip8_flags_to_options(flags):
    return {name: (flags & bit) != 0 for name, bit in CHIP8_QUIRK_FLAGS.items()}


def apply_chip8_import_manifest(entries):
    manifests = {}
    for entry in entries:
        if entry.system_key not in CHIP8_FAMILY_KEYS:
            continue
        manifest_path = entry.system_root / "_chip8_database_import.json"
        if manifest_path not in manifests:
            if manifest_path.is_file():
                rows = load_json(manifest_path)
                manifests[manifest_path] = {
                    str(row.get("sha1", "")).lower(): row
                    for row in rows
                    if isinstance(row, dict) and row.get("sha1")
                }
            else:
                manifests[manifest_path] = {}

        digest = hashlib.sha1(entry.data).hexdigest()
        row = manifests[manifest_path].get(digest)
        if row is None:
            continue
        if row.get("title"):
            entry.title, _ = encode_title(row["title"])
        entry.option_flags = int(row.get("option_flags", entry.option_flags)) & 0xFFFF
        entry.tickrate = int(row.get("tickrate", entry.tickrate or 0)) & 0xFFFF
        entry.metadata_source = "chip8-import-manifest"


def choose_platform(platform_ids, platforms, preference):
    if isinstance(platform_ids, str):
        platform_ids = [platform_ids]
    for wanted in preference:
        if wanted in platform_ids and wanted in platforms:
            return wanted
    for platform_id in platform_ids:
        if platform_id in platforms:
            return platform_id
    return None


def apply_chip8_database(entries, database, platform_preference):
    for entry in entries:
        if entry.system_key not in CHIP8_FAMILY_KEYS:
            continue

        digest = hashlib.sha1(entry.data).hexdigest()
        program_index = database["hashes"].get(digest)
        if program_index is None:
            continue

        program = database["programs"][program_index]
        rom_meta = first_dict(program.get("roms", {}).get(digest))
        platform_ids = rom_meta.get("platforms") or program.get("platforms") or []
        platform_id = choose_platform(platform_ids, database["platforms"], platform_preference)
        if platform_id is None:
            continue

        platform = database["platforms"][platform_id]
        options = dict(platform.get("quirks", {}))
        options.update(first_dict(program.get("options"), program.get("quirks")))
        options.update(first_dict(rom_meta.get("options"), rom_meta.get("quirks")))

        tickrate = rom_meta.get("tickrate", program.get("tickrate", platform.get("defaultTickrate", 0)))
        entry.option_flags = chip8_options_to_flags(options)
        entry.tickrate = int(tickrate or 0)
        if program.get("title"):
            entry.title, _ = encode_title(program["title"])
        entry.metadata_source = f"chip8-db:{platform_id}"


def load_chip8_config(path):
    config = load_json(path)
    if not isinstance(config, dict):
        raise RuntimeError("CHIP-8 config must be a JSON object")
    return config


def config_rom_overrides(config):
    roms = config.get("roms", {})
    if isinstance(roms, list):
        result = {}
        for item in roms:
            if not isinstance(item, dict):
                continue
            for key_name in ("sha1", "path", "title"):
                key = item.get(key_name)
                if key:
                    result[str(key)] = item
        return result
    if isinstance(roms, dict):
        return roms
    return {}


def find_config_override(entry, overrides):
    digest = hashlib.sha1(entry.data).hexdigest()
    rel = entry.path.as_posix()
    candidates = [
        digest,
        entry.path.name,
        rel,
        entry.title,
    ]
    for candidate in candidates:
        if candidate in overrides and isinstance(overrides[candidate], dict):
            return overrides[candidate]
    return None


def apply_chip8_config(entries, config):
    defaults = first_dict(config.get("defaults"))
    if not defaults and ("options" in config or "quirks" in config or "tickrate" in config):
        defaults = config
    overrides = config_rom_overrides(config)

    for entry in entries:
        if entry.system_key not in CHIP8_FAMILY_KEYS:
            continue

        default_options = first_dict(defaults.get("options"), defaults.get("quirks"))
        options = chip8_flags_to_options(entry.option_flags)
        options.update(default_options)
        tickrate = defaults.get("tickrate", entry.tickrate)
        source = "chip8-config:defaults" if default_options or "tickrate" in defaults else None
        if source is not None and defaults.get("title"):
            entry.title, _ = encode_title(defaults["title"])

        override = find_config_override(entry, overrides)
        if override is not None:
            options.update(first_dict(override.get("options"), override.get("quirks")))
            tickrate = override.get("tickrate", tickrate)
            if override.get("title"):
                entry.title, _ = encode_title(override["title"])
            source = "chip8-config:override"

        if source is not None:
            entry.option_flags = chip8_options_to_flags(options)
            entry.tickrate = int(tickrate or 0)
            entry.metadata_source = source


def build_image(entries):
    entry_count = len(entries)
    if entry_count > FIRMWARE_MAX_ROMS:
        raise RuntimeError(f"too many ROMs ({entry_count} > {FIRMWARE_MAX_ROMS})")

    data_offset = align(HEADER_SIZE + ENTRY_SIZE * entry_count)
    data = bytearray()

    for entry in entries:
        absolute_offset = data_offset + len(data)
        entry.offset = absolute_offset
        data.extend(entry.data)
        pad = align(len(data)) - len(data)
        if pad:
            data.extend(b"\xFF" * pad)

    data_size = len(data)
    pack_size = data_offset + data_size
    if pack_size > ROMPACK_FLASH_SIZE_BYTES:
        raise RuntimeError(
            f"ROM pack is too large for ROM flash region ({pack_size} > {ROMPACK_FLASH_SIZE_BYTES})"
        )

    header = struct.pack(
        HEADER_FORMAT,
        MAGIC,
        VERSION,
        HEADER_SIZE,
        ENTRY_SIZE,
        entry_count,
        data_offset,
        data_size,
        pack_size,
        0,
        b"\x00" * 36,
    )

    table = bytearray()
    for entry in entries:
        _, title_bytes = encode_title(entry.title)
        table.extend(struct.pack(
            ENTRY_FORMAT,
            entry.offset,
            len(entry.data),
            entry.crc,
            entry.option_flags,
            entry.tickrate,
            len(title_bytes),
            entry.system_id,
            title_bytes.ljust(TITLE_BYTES, b"\x00"),
        ))

    image = bytearray(header)
    image.extend(table)
    image.extend(b"\xFF" * (data_offset - len(image)))
    image.extend(data)

    crc = binascii.crc32(image) & 0xFFFFFFFF
    struct.pack_into("<I", image, 24, crc)
    return bytes(image), crc


def write_image(path, image):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(image)


def print_manifest(entries, image_size, image_crc):
    counts = {}
    for entry in entries:
        counts[entry.system_key] = counts.get(entry.system_key, 0) + 1

    print(f"format: RPRP v{VERSION}")
    print(f"roms: {len(entries)}")
    print("systems: " + ", ".join(f"{key}={counts[key]}" for key in sorted(counts)))
    print(f"size: {image_size} bytes")
    print(f"crc32: {image_crc:08X}")
    for index, entry in enumerate(entries):
        print(
            f"{index:03d}: system={entry.system_key:<6} offset={entry.offset:08X} "
            f"size={len(entry.data):4d} crc32={entry.crc:08X} "
            f"options={entry.option_flags:04X} tickrate={entry.tickrate} title={entry.title}"
            + (f" metadata={entry.metadata_source}" if entry.metadata_source else "")
        )


def main():
    parser = argparse.ArgumentParser(
        description="Pack emulator ROM folders into a RetroPort RPRP image and optionally upload it over USB CDC."
    )
    parser.add_argument("rom_root", help="folder containing per-emulator ROM folders, or one ROM file")
    parser.add_argument("-o", "--output", default=DEFAULT_OUTPUT,
                        help=f"output image path, default: {DEFAULT_OUTPUT}")
    parser.add_argument("--upload", action="store_true", help="upload the packed image after writing it")
    parser.add_argument("--port", help="CDC serial port for --upload, default: newest /dev/cu.usbmodem*")
    parser.add_argument("--chunk-size", type=int, default=flash_rompack_usb.DEFAULT_CHUNK_SIZE,
                        help=f"bytes per USB WRITE command, default: {flash_rompack_usb.DEFAULT_CHUNK_SIZE}")
    parser.add_argument("--system", action="append",
                        help="system folder to include; can be repeated or comma-separated")
    parser.add_argument("--ext", help="comma-separated ROM extensions to include for all selected systems")
    parser.add_argument("--all-files", action="store_true", help="pack every non-hidden file")
    parser.add_argument("--max-rom-size", type=int,
                        help="override maximum ROM size in bytes for all selected systems; 0 disables the check")
    parser.add_argument("--chip8-database",
                        help="path to chip-8-database checkout or its database folder")
    parser.add_argument("--chip8-config",
                        help="JSON file with CHIP-8 title/options/tickrate overrides")
    parser.add_argument("--chip8-platform-preference", default="modernChip8,originalChip8,chip48",
                        help="comma-separated platform priority for CHIP-8 database ROMs")
    parser.add_argument("--quiet", action="store_true", help="hide per-ROM manifest lines")
    args = parser.parse_args()

    root = Path(args.rom_root).resolve()
    if not root.is_dir() and not root.is_file():
        raise RuntimeError(f"ROM root path not found: {root}")

    output = Path(args.output).resolve()
    extensions = parse_extensions(args.ext) if args.ext else None
    systems = selected_systems(args.system)
    entries = discover_roms(
        root,
        args.all_files,
        extensions,
        args.max_rom_size,
        systems,
        strict_unknown=args.system is None,
        excluded_path=output,
    )
    apply_chip8_import_manifest(entries)
    if args.chip8_database:
        preference = [item.strip() for item in args.chip8_platform_preference.split(",") if item.strip()]
        apply_chip8_database(entries, load_chip8_database(args.chip8_database), preference)
    if args.chip8_config:
        apply_chip8_config(entries, load_chip8_config(args.chip8_config))
    image, image_crc = build_image(entries)
    write_image(output, image)

    print(f"output: {output}")
    if args.quiet:
        print(f"roms: {len(entries)}")
        print(f"size: {len(image)} bytes")
        print(f"crc32: {image_crc:08X}")
    else:
        print_manifest(entries, len(image), image_crc)

    if args.upload:
        flash_rompack_usb.upload_image(str(output), port=args.port, chunk_size=args.chunk_size)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)
