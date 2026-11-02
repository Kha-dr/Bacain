#!/usr/bin/env python3
"""
BacaIn - Firmware Packaging Tool
Merges ESP32-S3 binaries (bootloader, partition-table, app) into a single factory binary,
generates manifest.json metadata, and optionally copies artifacts to the web firmware directory.

Zero-Mock & Zero-Silent-Fallback compliant (AGENTS.md).
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


def find_binary(build_dir: Path, candidate_relative_paths: list) -> Path:
    """Find the first existing binary among candidate relative paths."""
    for rel_path in candidate_relative_paths:
        candidate = build_dir / rel_path
        if candidate.is_file():
            return candidate
    return None


def resolve_esptool_command() -> list:
    """Find esptool command executable or module across Linux/macOS/Windows."""
    # 1. Standard executable search
    for candidate in ["esptool.py", "esptool", "esptool.exe"]:
        found = shutil.which(candidate)
        if found:
            if found.lower().endswith(".py"):
                return [sys.executable, found]
            return [found]

    # 2. Check PATH directories directly for esptool.py (Windows when .py is not in PATHEXT)
    path_dirs = os.environ.get("PATH", "").split(os.pathsep)
    for p in path_dirs:
        if not p:
            continue
        p_dir = Path(p)
        for name in ["esptool.py", "esptool"]:
            candidate = p_dir / name
            if candidate.is_file():
                if name.endswith(".py"):
                    return [sys.executable, str(candidate)]
                return [str(candidate)]

    # 3. Check if python module esptool is executable
    try:
        res = subprocess.run(
            [sys.executable, "-m", "esptool", "version"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        if res.returncode == 0:
            return [sys.executable, "-m", "esptool"]
    except Exception:
        pass

    return []


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Package BacaIn ESP32-S3 firmware binaries into factory binary and manifest."
    )
    parser.add_argument(
        "--build-dir",
        default="build",
        help="Path to ESP-IDF build directory (default: 'build')",
    )
    parser.add_argument(
        "-o",
        "--output",
        default=None,
        help="Path for output factory binary (default: <build-dir>/bacain_factory.bin)",
    )
    parser.add_argument(
        "--manifest",
        default=None,
        help="Path for output manifest.json (default: <build-dir>/manifest.json)",
    )
    parser.add_argument(
        "--chip",
        default="ESP32-S3",
        help="ESP chip target (default: 'ESP32-S3')",
    )
    parser.add_argument(
        "--flash-size",
        default="8MB",
        help="Flash size for merged image (default: '8MB')",
    )
    parser.add_argument(
        "--flash-mode",
        default="keep",
        help="Flash mode for merged image (default: 'keep')",
    )
    parser.add_argument(
        "--web-dir",
        "--pwa-dir",
        dest="web_dir",
        default="../web/firmware",
        help="Path to web firmware directory for auto-copy (default: '../web/firmware')",
    )
    parser.add_argument(
        "--no-web-copy",
        "--no-pwa-copy",
        dest="no_web_copy",
        action="store_true",
        help="Disable automatic copying to web firmware directory",
    )

    args = parser.parse_args()

    # Determine build directory
    build_dir = Path(args.build_dir)
    if not build_dir.is_absolute():
        if not build_dir.exists() and (Path(__file__).parent / build_dir).exists():
            build_dir = (Path(__file__).parent / build_dir).resolve()
        else:
            build_dir = build_dir.resolve()

    if not build_dir.is_dir():
        sys.stderr.write(
            f"[package_firmware] ERROR: Build directory '{build_dir}' does not exist.\n"
            f"Please run 'idf.py build' first.\n"
        )
        return 1

    # Locate input files with explicit zero-mock validation
    bootloader_bin = find_binary(
        build_dir,
        [
            "bootloader/bootloader.bin",
            "bootloader.bin",
        ],
    )

    partition_table_bin = find_binary(
        build_dir,
        [
            "partition_table/partition-table.bin",
            "partition-table/partition-table.bin",
            "partition-table.bin",
            "partition_table.bin",
        ],
    )

    app_bin = find_binary(
        build_dir,
        [
            "bacain.bin",
            "mini_e_reader.bin",
            "reader_firmware.bin",
        ],
    )

    missing = []
    if not bootloader_bin:
        missing.append("bootloader binary (expected: build/bootloader/bootloader.bin)")
    if not partition_table_bin:
        missing.append(
            "partition table binary (expected: build/partition_table/partition-table.bin)"
        )
    if not app_bin:
        missing.append("app binary (expected: build/bacain.bin, build/mini_e_reader.bin, or build/reader_firmware.bin)")

    if missing:
        sys.stderr.write(
            "[package_firmware] ERROR: Missing required firmware binaries:\n"
            + "\n".join(f"  - {m}" for m in missing)
            + "\nPlease ensure 'idf.py build' completed successfully.\n"
        )
        return 1

    # Standardize / flatten binary names in build directory
    flat_bootloader = build_dir / "bootloader.bin"
    if bootloader_bin != flat_bootloader and not flat_bootloader.exists():
        shutil.copy2(bootloader_bin, flat_bootloader)

    flat_partition = build_dir / "partition-table.bin"
    if partition_table_bin != flat_partition and not flat_partition.exists():
        shutil.copy2(partition_table_bin, flat_partition)

    flat_app = build_dir / "reader_firmware.bin"
    if app_bin != flat_app and not flat_app.exists():
        shutil.copy2(app_bin, flat_app)

    # Determine output factory binary path
    if args.output:
        output_bin = Path(args.output).resolve()
    else:
        output_bin = build_dir / "bacain_factory.bin"

    output_bin.parent.mkdir(parents=True, exist_ok=True)

    # Determine manifest path
    if args.manifest:
        manifest_path = Path(args.manifest).resolve()
    else:
        manifest_path = build_dir / "manifest.json"

    manifest_path.parent.mkdir(parents=True, exist_ok=True)

    # Resolve esptool executable
    esptool_cmd = resolve_esptool_command()
    if not esptool_cmd:
        sys.stderr.write(
            "[package_firmware] ERROR: Neither 'esptool.py' nor python module 'esptool' could be found.\n"
            "Please install esptool ('pip install esptool') or ensure your ESP-IDF environment is activated.\n"
        )
        return 1

    # Normalize chip name for esptool CLI (e.g. ESP32-S3 -> esp32s3)
    chip_arg = args.chip.lower().replace("-", "")

    offsets = {
        "bootloader": "0x0000",
        "partition_table": "0x8000",
        "reader_firmware": "0x20000",
    }

    merge_args = esptool_cmd + [
        "--chip",
        chip_arg,
        "merge_bin",
        "-o",
        str(output_bin),
        "--flash_mode",
        args.flash_mode,
        "--flash_size",
        args.flash_size,
        offsets["bootloader"],
        str(bootloader_bin),
        offsets["partition_table"],
        str(partition_table_bin),
        offsets["reader_firmware"],
        str(app_bin),
    ]

    print("[package_firmware] Running esptool merge_bin...")
    print(f"[package_firmware] Command: {' '.join(merge_args)}")
    try:
        subprocess.run(merge_args, check=True)
    except subprocess.CalledProcessError as e:
        sys.stderr.write(
            f"[package_firmware] ERROR: esptool merge_bin failed with exit code {e.returncode}.\n"
        )
        return 1
    except Exception as e:
        sys.stderr.write(f"[package_firmware] ERROR: Failed to execute esptool: {e}\n")
        return 1

    if not output_bin.is_file() or output_bin.stat().st_size == 0:
        sys.stderr.write(
            f"[package_firmware] ERROR: Expected output file '{output_bin}' was not generated.\n"
        )
        return 1

    print(
        f"[package_firmware] Successfully created factory binary: {output_bin} ({output_bin.stat().st_size} bytes)"
    )

    # Generate manifest.json
    build_date = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    manifest_data = {
        "name": "BacaIn Firmware",
        "chip": args.chip,
        "target": args.chip,
        "flash_size": args.flash_size,
        "build_date": build_date,
        "factory_bin": output_bin.name,
        "parts": [
            {
                "name": "bootloader",
                "offset": offsets["bootloader"],
                "path": flat_bootloader.name,
                "size_bytes": bootloader_bin.stat().st_size,
            },
            {
                "name": "partition_table",
                "offset": offsets["partition_table"],
                "path": flat_partition.name,
                "size_bytes": partition_table_bin.stat().st_size,
            },
            {
                "name": "reader_firmware",
                "offset": offsets["reader_firmware"],
                "path": flat_app.name,
                "size_bytes": app_bin.stat().st_size,
            },
        ],
    }

    try:
        with open(manifest_path, "w", encoding="utf-8") as f:
            json.dump(manifest_data, f, indent=2)
        print(f"[package_firmware] Manifest generated: {manifest_path}")
    except OSError as e:
        sys.stderr.write(f"[package_firmware] ERROR: Failed to write manifest: {e}\n")
        return 1

    # Check and perform copy to web directory if present
    web_dir = Path(args.web_dir)
    if not web_dir.is_absolute():
        candidate_web = (Path(__file__).parent / web_dir).resolve()
        if candidate_web.is_dir():
            web_dir = candidate_web
        else:
            web_dir = web_dir.resolve()

    if not args.no_web_copy and web_dir.is_dir():
        print(f"[package_firmware] Web firmware directory found: {web_dir}")
        files_to_copy = [
            output_bin,
            manifest_path,
            flat_bootloader,
            flat_partition,
            flat_app,
        ]
        for src_file in files_to_copy:
            if src_file.is_file():
                dest_file = web_dir / src_file.name
                shutil.copy2(src_file, dest_file)
                print(f"[package_firmware] Copied {src_file.name} -> {web_dir}")
        print(f"[package_firmware] Successfully copied all firmware artifacts to {web_dir}")
    else:
        print(
            f"[package_firmware] Web firmware directory not found or copy disabled ({web_dir}); skipping copy."
        )

    print("[package_firmware] Packaging complete.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
