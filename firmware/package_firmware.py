#!/usr/bin/env python3
"""
BacaIn - Firmware Packaging Tool
Merges ESP32-S3 binaries into a single factory binary.
"""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys


def find_bin(build_dir: Path, *names: str) -> Path:
    for n in names:
        p = build_dir / n
        if p.is_file():
            return p
    raise FileNotFoundError(f"Binary not found in {build_dir}: {names}")


def get_esptool_cmd() -> list:
    for name in ["esptool.py", "esptool", "esptool.exe"]:
        p = shutil.which(name)
        if p:
            return [sys.executable, p] if p.lower().endswith(".py") else [p]
    return [sys.executable, "-m", "esptool"]


def parse_args(argv: list | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Merge ESP binary files into a single factory binary"
    )
    parser.add_argument(
        "build_dir",
        nargs="?",
        default="build",
        help="Directory of build output (default: 'build')",
    )
    parser.add_argument(
        "-o",
        "--output",
        default="bacain_factory.bin",
        help="Name of the factory binary file (default: 'bacain_factory.bin')",
    )
    parser.add_argument(
        "--chip",
        default="esp32s3",
        help="ESP chip type (default: 'esp32s3')",
    )
    parser.add_argument(
        "--flash-size",
        default="8MB",
        help="Flash size (default: '8MB')",
    )
    return parser.parse_args(argv)


def main(argv: list | None = None) -> int:
    args = parse_args(argv)

    build_dir = Path(args.build_dir)
    if not build_dir.is_dir() and (Path(__file__).parent / args.build_dir).is_dir():
        build_dir = Path(__file__).parent / args.build_dir
    build_dir = build_dir.resolve()

    if not build_dir.is_dir():
        sys.stderr.write(f"Error: Directory build '{build_dir}' not found.\n")
        return 1

    bootloader = find_bin(build_dir, "bootloader/bootloader.bin", "bootloader.bin")
    partition_table = find_bin(build_dir, "partition_table/partition-table.bin", "partition-table.bin")
    app_bin = find_bin(build_dir, "bacain.bin")

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

    print(f"[package_firmware] Running: {' '.join(cmd)}")
    subprocess.run(cmd, check=True)

    print(f"[package_firmware] Finished: {factory_bin.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
