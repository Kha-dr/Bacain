#!/usr/bin/env python3
"""
BacaIn - Firmware Packaging Tool
Merges ESP32-S3 binaries into a single factory binary.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
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
        default=None,
        help="Flash size (e.g. '4MB', '8MB', '16MB'; resolved from boards.json if omitted)",
    )
    parser.add_argument(
        "--board",
        default=None,
        help="Board ID or name from boards/boards.json",
    )
    return parser.parse_args(argv)


def resolve_board_flash_size(
    board_query: str | None,
    output_name: str | None,
    explicit_flash_size: str | None,
) -> str:
    if explicit_flash_size:
        return explicit_flash_size

    boards_json = Path(__file__).parent / "boards" / "boards.json"
    if boards_json.is_file():
        catalog = json.loads(boards_json.read_text(encoding="utf-8"))
        boards = catalog.get("boards", [])
        for b in boards:
            if board_query and board_query in (b.get("id"), b.get("name")):
                if b.get("flash_size"):
                    return b["flash_size"]
            if output_name and output_name in (b.get("output_bin"), b.get("binary")):
                if b.get("flash_size"):
                    return b["flash_size"]
        if len(boards) == 1 and boards[0].get("flash_size"):
            return boards[0]["flash_size"]

    return "8MB"


def parse_flash_size_bytes(flash_size_str: str) -> int:
    m = re.match(r"^\s*(\d+)\s*([KMGT]?B?)\s*$", flash_size_str.strip(), re.IGNORECASE)
    if not m:
        raise ValueError(f"Unsupported flash size format: {flash_size_str}")
    val = int(m.group(1))
    unit = m.group(2).upper()
    if unit in ("M", "MB"):
        return val * 1024 * 1024
    if unit in ("K", "KB"):
        return val * 1024
    if unit in ("G", "GB"):
        return val * 1024 * 1024 * 1024
    return val


def adjust_partition_table_for_flash_size(pt_path: Path, flash_size_bytes: int) -> None:
    raw = bytearray(pt_path.read_bytes())
    storage_entry_off = None
    storage_offset = 0
    old_storage_size = 0
    md5_entry_off = None

    for off in range(0, len(raw) - 31, 32):
        magic0, magic1 = raw[off], raw[off + 1]
        if magic0 == 0xAA and magic1 == 0x50:
            p_offset, p_size = struct.unpack_from("<II", raw, off + 4)
            label_bytes = raw[off + 12 : off + 28]
            label = label_bytes.split(b"\x00", 1)[0].decode("ascii", errors="ignore")
            if label == "storage":
                storage_entry_off = off
                storage_offset = p_offset
                old_storage_size = p_size
        elif magic0 == 0xEB and magic1 == 0xEB:
            md5_entry_off = off
            break
        elif magic0 == 0xFF and magic1 == 0xFF:
            break

    if storage_entry_off is None:
        return

    new_storage_size = flash_size_bytes - storage_offset
    if new_storage_size <= 0 or (new_storage_size % 4096) != 0:
        raise ValueError(
            f"Invalid calculated storage size {new_storage_size} bytes "
            f"(flash_size={flash_size_bytes}, storage_offset=0x{storage_offset:X})"
        )

    if new_storage_size == old_storage_size:
        print(
            f"[package_firmware] Partition 'storage' @ 0x{storage_offset:X} "
            f"already sized to {new_storage_size // 1024}K ({new_storage_size / (1024 * 1024):.2f} MB)"
        )
        return

    struct.pack_into("<I", raw, storage_entry_off + 8, new_storage_size)
    if md5_entry_off is not None:
        digest = hashlib.md5(bytes(raw[:md5_entry_off])).digest()
        raw[md5_entry_off + 16 : md5_entry_off + 32] = digest

    pt_path.write_bytes(bytes(raw))
    print(
        f"[package_firmware] Adjusted partition 'storage' @ 0x{storage_offset:X}: "
        f"{old_storage_size // 1024}K -> {new_storage_size // 1024}K "
        f"({new_storage_size / (1024 * 1024):.2f} MB)"
    )


def resolve_flash_segments(build_dir: Path) -> list[str]:
    flasher_args_path = build_dir / "flasher_args.json"
    if flasher_args_path.is_file():
        data = json.loads(flasher_args_path.read_text(encoding="utf-8"))
        flash_files = data.get("flash_files", {})
        if flash_files:
            sorted_entries = sorted(
                flash_files.items(),
                key=lambda kv: int(kv[0], 16),
            )
            segments: list[str] = []
            for offset_str, rel_path in sorted_entries:
                bin_path = build_dir / rel_path
                if not bin_path.is_file():
                    raise FileNotFoundError(f"Binary not found: {bin_path}")
                segments.extend([offset_str, str(bin_path)])
            return segments

    bootloader = find_bin(build_dir, "bootloader/bootloader.bin", "bootloader.bin")
    partition_table = find_bin(build_dir, "partition_table/partition-table.bin", "partition-table.bin")
    app_bin = find_bin(build_dir, "bacain.bin")
    return [
        "0x0", str(bootloader),
        "0x8000", str(partition_table),
        "0x10000", str(app_bin),
    ]


def main(argv: list | None = None) -> int:
    args = parse_args(argv)

    build_dir = Path(args.build_dir)
    if not build_dir.is_dir() and (Path(__file__).parent / args.build_dir).is_dir():
        build_dir = Path(__file__).parent / args.build_dir
    build_dir = build_dir.resolve()

    if not build_dir.is_dir():
        sys.stderr.write(f"Error: Directory build '{build_dir}' not found.\n")
        return 1

    flash_size = resolve_board_flash_size(args.board, args.output, args.flash_size)
    flash_size_bytes = parse_flash_size_bytes(flash_size)

    segments = resolve_flash_segments(build_dir)
    for i in range(0, len(segments) - 1, 2):
        if int(segments[i], 16) == 0x8000:
            adjust_partition_table_for_flash_size(Path(segments[i + 1]), flash_size_bytes)
            break

    factory_bin = build_dir / args.output
    cmd = get_esptool_cmd() + [
        "--chip", args.chip,
        "merge_bin",
        "-o", str(factory_bin),
        "--flash_mode", "keep",
        "--flash_size", flash_size,
        *segments,
    ]

    print(f"[package_firmware] Running: {' '.join(cmd)}")
    subprocess.run(cmd, check=True)

    print(f"[package_firmware] Finished: {factory_bin.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
