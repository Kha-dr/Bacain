#!/usr/bin/env python3
"""
BacaIn - Firmware Packaging Tool
Merges ESP32-S3 binaries into a single factory binary and generates manifest.json.
"""

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


def main() -> int:
    build_arg = sys.argv[1] if len(sys.argv) > 1 else "build"
    build_dir = Path(build_arg)
    if not build_dir.is_dir() and (Path(__file__).parent / build_arg).is_dir():
        build_dir = Path(__file__).parent / build_arg
    build_dir = build_dir.resolve()

    if not build_dir.is_dir():
        sys.stderr.write(f"Error: Direktori build '{build_dir}' tidak ditemukan.\n")
        return 1

    bootloader = find_bin(build_dir, "bootloader/bootloader.bin", "bootloader.bin")
    partition_table = find_bin(build_dir, "partition_table/partition-table.bin", "partition-table.bin")
    app_bin = find_bin(build_dir, "bacain.bin", "reader_firmware.bin", "mini_e_reader.bin")

    factory_bin = build_dir / "bacain_factory.bin"
    cmd = get_esptool_cmd() + [
        "--chip", "esp32s3",
        "merge_bin",
        "-o", str(factory_bin),
        "--flash_mode", "keep",
        "--flash_size", "8MB",
        "0x0", str(bootloader),
        "0x8000", str(partition_table),
        "0x20000", str(app_bin),
    ]

    print(f"[package_firmware] Menjalankan: {' '.join(cmd)}")
    subprocess.run(cmd, check=True)

    manifest = {
        "name": "BacaIn Firmware",
        "chip": "ESP32-S3",
        "target": "ESP32-S3",
        "flash_size": "8MB",
        "build_date": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "factory_bin": factory_bin.name,
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
