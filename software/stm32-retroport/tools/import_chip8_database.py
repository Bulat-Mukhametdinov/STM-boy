#!/usr/bin/env python3

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

DEFAULT_DATABASE_URL = "https://github.com/chip-8/chip-8-database.git"
DEFAULT_ARCHIVE_URL = "https://github.com/JohnEarnest/chip8Archive.git"
DEFAULT_PLATFORMS = ("modernChip8", "originalChip8", "chip48")
ROM_EXTENSIONS = {".ch8", ".c8", ".rom", ".bin"}
CHIP8_QUIRK_FLAGS = {
    "shift": 0x0001,
    "memoryIncrementByX": 0x0002,
    "memoryLeaveIUnchanged": 0x0004,
    "wrap": 0x0008,
    "jump": 0x0010,
    "vblank": 0x0020,
    "logic": 0x0040,
}


def clone_repo(url, destination):
    subprocess.run(
        ["git", "clone", "--depth", "1", url, str(destination)],
        check=True,
        stdout=subprocess.DEVNULL,
    )


def load_json(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def database_file(root, name):
    root = Path(root)
    direct = root / name
    nested = root / "database" / name
    if direct.is_file():
        return direct
    if nested.is_file():
        return nested
    raise RuntimeError(f"database file not found: {name} under {root}")


def sha1(data):
    return hashlib.sha1(data).hexdigest()


def safe_filename(name):
    result = []
    for ch in name.replace("\\", "/").split("/")[-1]:
        if ch in '<>:"/\\|?*' or ord(ch) < 32:
            result.append("_")
        else:
            result.append(ch)
    return "".join(result).strip() or "Untitled.ch8"


def iter_source_files(source_roots):
    for root in source_roots:
        root = Path(root)
        if not root.exists():
            continue
        if root.is_file():
            yield root
            continue
        for path in root.rglob("*"):
            if path.is_file() and path.suffix.lower() in ROM_EXTENSIONS:
                yield path


def index_sources(source_roots):
    by_hash = {}
    for path in iter_source_files(source_roots):
        data = path.read_bytes()
        if not data:
            continue
        digest = sha1(data)
        by_hash.setdefault(digest, path)
    return by_hash


def choose_platform(platforms, supported):
    for platform in platforms or []:
        if platform in supported:
            return platform
    return None


def normalize_platforms(platforms):
    return {item["id"]: item for item in platforms}


def option_flags(options):
    flags = 0
    for name, bit in CHIP8_QUIRK_FLAGS.items():
        if bool(options.get(name, False)):
            flags |= bit
    return flags


def selected_roms(programs, supported_platforms):
    for program in programs:
        title = program.get("title", "Untitled")
        for digest, rom in (program.get("roms") or {}).items():
            platform = choose_platform(rom.get("platforms"), supported_platforms)
            if platform is None:
                continue
            yield digest, program, rom, platform, title


def copy_selected_roms(programs, platforms, source_index, output, supported_platforms, clean):
    output = Path(output)
    if clean and output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True, exist_ok=True)

    imported = 0
    missing = 0
    duplicate_names = {}
    manifest = []

    platform_map = normalize_platforms(platforms)
    for digest, program, rom, platform, title in selected_roms(programs, supported_platforms):
        source = source_index.get(digest)
        if source is None:
            missing += 1
            continue

        file_name = safe_filename(rom.get("file") or f"{title}.ch8")
        stem = Path(file_name).stem
        suffix = Path(file_name).suffix or ".ch8"
        key = file_name.lower()
        duplicate_index = duplicate_names.get(key, 0)
        duplicate_names[key] = duplicate_index + 1
        if duplicate_index > 0:
            file_name = f"{stem} ({duplicate_index + 1}){suffix}"

        target = output / file_name
        shutil.copyfile(source, target)
        imported += 1
        platform_options = dict(platform_map.get(platform, {}).get("quirks", {}))
        platform_options.update(program.get("options", {}) if isinstance(program.get("options"), dict) else {})
        platform_options.update(rom.get("options", {}) if isinstance(rom.get("options"), dict) else {})
        platform_options.update(program.get("quirks", {}) if isinstance(program.get("quirks"), dict) else {})
        platform_options.update(rom.get("quirks", {}) if isinstance(rom.get("quirks"), dict) else {})
        tickrate = rom.get("tickrate", program.get("tickrate", platform_map.get(platform, {}).get("defaultTickrate", 0)))
        manifest.append({
            "file": file_name,
            "sha1": digest,
            "title": title,
            "platform": platform,
            "option_flags": option_flags(platform_options),
            "tickrate": int(tickrate or 0),
            "source": str(source),
        })

    manifest_path = output / "_chip8_database_import.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return imported, missing, manifest_path


def main():
    parser = argparse.ArgumentParser(
        description="Import CHIP-8 ROMs known by chip-8-database from verified local/archive sources."
    )
    parser.add_argument("--database", help="path to chip-8-database checkout or database folder")
    parser.add_argument("--archive", help="path to JohnEarnest/chip8Archive checkout")
    parser.add_argument("--source", action="append", default=[],
                        help="extra folder/file with ROMs to match by SHA1; can be repeated")
    parser.add_argument("--output", default="roms/chip8", help="output folder, default: roms/chip8")
    parser.add_argument("--platform", action="append",
                        help="supported platform; can be repeated or comma-separated")
    parser.add_argument("--keep-existing", action="store_true", help="do not clear output folder before importing")
    args = parser.parse_args()

    supported_platforms = []
    platform_args = args.platform or [",".join(DEFAULT_PLATFORMS)]
    for value in platform_args:
        supported_platforms.extend(item.strip() for item in value.split(",") if item.strip())

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        database = Path(args.database) if args.database else tmp / "chip-8-database"
        archive = Path(args.archive) if args.archive else tmp / "chip8Archive"

        if not args.database:
            clone_repo(DEFAULT_DATABASE_URL, database)
        if not args.archive:
            clone_repo(DEFAULT_ARCHIVE_URL, archive)

        programs = load_json(database_file(database, "programs.json"))
        platforms = load_json(database_file(database, "platforms.json"))
        source_roots = [archive / "roms", *[Path(p) for p in args.source]]
        source_index = index_sources(source_roots)
        imported, missing, manifest_path = copy_selected_roms(
            programs,
            platforms,
            source_index,
            args.output,
            supported_platforms,
            clean=not args.keep_existing,
        )

    print(f"platforms: {', '.join(supported_platforms)}")
    print(f"sources indexed: {len(source_index)}")
    print(f"imported: {imported}")
    print(f"missing binaries: {missing}")
    print(f"manifest: {manifest_path}")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)
