#!/usr/bin/env python3
"""
BacaIn - Firmware Packaging Tool
Merges ESP32-S3 binaries into a single factory binary and generates manifest.json.
"""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
from datetime import datetime, timezone


def find_bin(build_dir: Path, *names: str) -> Path:
    for n in names:
        p = build_dir / n
        if p.is_file():
            return p
    raise FileNotFoundError(f"Binary tidak ditemukan di {build_dir}: {names}")


def get_esptool_cmd() -> list:
    for name in ["esptool.py", "esptool", "esptool.exe"]:
        p = shutil.which(name)
        if p:
            return [sys.executable, p] if p.lower().endswith(".py") else [p]
    return [sys.executable, "-m", "esptool"]


def parse_args(argv: list | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Merge ESP binary files into a factory binary and generate manifest.json"
    )
    parser.add_argument(
        "build_dir",
        nargs="?",
        default="build",
        help="Direktori build output (default: 'build')",
    )
    parser.add_argument(
        "-o",
        "--output",
        default="bacain_factory.bin",
        help="Nama file factory binary (default: 'bacain_factory.bin')",
    )
    parser.add_argument(
        "--chip",
        default="esp32s3",
        help="Tipe chip ESP (default: 'esp32s3')",
    )
    parser.add_argument(
        "--flash-size",
        default="8MB",
        help="Ukuran flash (default: '8MB')",
    )
    parser.add_argument(
        "--name",
        default="BacaIn Firmware",
        help="Nama firmware dalam manifest (default: 'BacaIn Firmware')",
    )
    parser.add_argument(
        "--board",
        default="Waveshare ESP32-S3-ePaper-1.54",
        help="Nama board profil dalam manifest (default: 'Waveshare ESP32-S3-ePaper-1.54')",
    )
    return parser.parse_args(argv)


def main(argv: list | None = None) -> int:
    args = parse_args(argv)

    build_dir = Path(args.build_dir)
    if not build_dir.is_dir() and (Path(__file__).parent / args.build_dir).is_dir():
        build_dir = Path(__file__).parent / args.build_dir
    build_dir = build_dir.resolve()

    if not build_dir.is_dir():
        sys.stderr.write(f"Error: Direktori build '{build_dir}' tidak ditemukan.\n")
        return 1

    bootloader = find_bin(build_dir, "bootloader/bootloader.bin", "bootloader.bin")
    partition_table = find_bin(build_dir, "partition_table/partition-table.bin", "partition-table.bin")
    app_bin = find_bin(build_dir, "bacain.bin", "reader_firmware.bin", "mini_e_reader.bin")

    factory_bin = build_dir / args.output
    cmd = get_esptool_cmd() + [
        "--chip", args.chip,
        "merge_bin",
        "-o", str(factory_bin),
        "--flash_mode", "keep",
        "--flash_size", args.flash_size,
        "0x0", str(bootloader),
        "0x8000", str(partition_table),
        "0x20000", str(app_bin),
    ]

    print(f"[package_firmware] Menjalankan: {' '.join(cmd)}")
    subprocess.run(cmd, check=True)

    manifest = {
        "name": args.name,
        "board": args.board,
        "chip": args.chip,
        "target": args.chip,
        "flash_size": args.flash_size,
        "build_date": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "factory_bin": args.output,
        "parts": [
            {"name": "bootloader", "offset": "0x0", "path": bootloader.name},
            {"name": "partition_table", "offset": "0x8000", "path": partition_table.name},
            {"name": "app", "offset": "0x20000", "path": app_bin.name},
        ],
    }

    manifest_path = build_dir / "manifest.json"
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)

    print(f"[package_firmware] Selesai: {factory_bin.name} & {manifest_path.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
