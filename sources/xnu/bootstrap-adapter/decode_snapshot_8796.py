#!/usr/bin/env python3
"""Decode a raw N986B XNU 8796 handoff snapshot page from Upload RAM.

The version 3 layout is distinct from the earlier XNU 12377 version 2 page.
This tool is read-only and rejects mixed or truncated layouts.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct

SNAPSHOT_SIZE = 856
BOOT_ARGS_SIZE = 736
HEADER = struct.Struct("<8sIIQQQQII32s")
assert HEADER.size == 88


class SnapshotError(ValueError):
    pass


def parse_snapshot(page: bytes, device_tree: bytes | None = None) -> dict:
    if len(page) < SNAPSHOT_SIZE:
        raise SnapshotError("snapshot is truncated")
    magic, version, size, stage, status, args_pa, dt_pa, dt_size, reserved, dt_sha = HEADER.unpack_from(page)
    if magic != b"N986BXHS" or version != 3 or size != SNAPSHOT_SIZE:
        raise SnapshotError(f"unsupported snapshot: magic={magic!r}, version={version}, size={size}")
    if reserved != 0:
        raise SnapshotError("reserved header word is not zero")
    args = page[88 : 88 + BOOT_ARGS_SIZE]
    revision, boot_version = struct.unpack_from("<HH", args)
    virt_base, phys_base, mem_size, top = struct.unpack_from("<QQQQ", args, 8)
    dt_va = struct.unpack_from("<Q", args, 96)[0]
    args_dt_size = struct.unpack_from("<I", args, 104)[0]
    command_line = args[108:716].split(b"\0", 1)[0]
    flags, actual_size = struct.unpack_from("<QQ", args, 720)
    if (revision, boot_version) != (2, 2):
        raise SnapshotError("unexpected boot_args revision or version")
    if not mem_size or not (phys_base <= args_pa < top <= phys_base + mem_size):
        raise SnapshotError("boot_args pointer or kernel top is outside declared memory")
    if not dt_size or not (phys_base <= dt_pa < args_pa) or dt_size > args_pa - dt_pa:
        raise SnapshotError("device-tree physical range is outside boot_args-owned memory")
    if args_dt_size != dt_size or dt_va != virt_base + dt_pa - phys_base:
        raise SnapshotError("device-tree pointers do not agree")
    if b"\0" not in args[108:716]:
        raise SnapshotError("command line lacks a terminator")
    if device_tree is not None:
        if len(device_tree) != dt_size or hashlib.sha256(device_tree).digest() != dt_sha:
            raise SnapshotError("device-tree length or SHA-256 mismatch")
    return {
        "snapshot_version": version,
        "snapshot_size": size,
        "stage": stage,
        "status": status,
        "boot_args_pa": f"0x{args_pa:x}",
        "boot_args_size": BOOT_ARGS_SIZE,
        "virtual_base": f"0x{virt_base:x}",
        "physical_base": f"0x{phys_base:x}",
        "memory_size": mem_size,
        "top_of_kernel_data": f"0x{top:x}",
        "device_tree_pa": f"0x{dt_pa:x}",
        "device_tree_size": dt_size,
        "device_tree_sha256": dt_sha.hex(),
        "command_line": command_line.decode("ascii", errors="replace"),
        "flags": f"0x{flags:x}",
        "actual_memory_size": actual_size,
        "manifest_sha256": page[824:856].hex(),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("snapshot", type=Path, help="raw 4 KiB snapshot page captured at 0x95D00000")
    parser.add_argument("--device-tree", type=Path, help="optional raw Apple DT blob for SHA check")
    args = parser.parse_args()
    tree = args.device_tree.read_bytes() if args.device_tree else None
    print(json.dumps(parse_snapshot(args.snapshot.read_bytes(), tree), indent=2))


if __name__ == "__main__":
    main()
