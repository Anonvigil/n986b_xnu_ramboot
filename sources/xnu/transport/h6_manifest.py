#!/usr/bin/env python3
"""Reconstruct the historical H6 manifest and low RAM stream offline.

This program has no USB/ADB code. Input images must be supplied by their owner;
the pinned H6 binaries and session seed are intentionally not distributed.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

FRAME = 512
FD_PA = 0x90000000
FD_LENGTH = 0x700000
MANIFEST_PA = FD_PA + FD_LENGTH
MANIFEST_LENGTH = 4096
KERNEL_PA = MANIFEST_PA + MANIFEST_LENGTH
KERNEL_LENGTH = 19_057_760
STREAM_END = 0x91940000
HIGH_WITNESS_PA = 0x940000000
HIGH_WITNESS_LENGTH = 1_048_576
HIGH_SCRATCH_PA = HIGH_WITNESS_PA + HIGH_WITNESS_LENGTH
SNAPSHOT_PA = 0x95D00000
TERMINAL = b"UEFITERM"
HEADER = struct.Struct("<8sIIQQQ32s256sIQQ32s")
RO_RECORD = struct.Struct("<8sHHIQQII32sQ")

# Historical H6 artifact pins. A changed build needs new pins and independent
# validation; this script must not silently adapt to a different FD/kernel.
PINS = {
    "fd": "b6433e3bdf6ffda64357f46fc592a5d922611241470d942e2f86afa1b19d0201",
    "stage": "5d1f05bd2ef19308fc0e52a00f5da9ac06ff7efe5476574711bb4607333001c7",
    "kernel": "ac7af9d81b3aa756b257a790f21e9d0301581dfe53dc786e0a6d367a2ef1d6e0",
    "witness": "b26a12949fe1ce56f5830a7187a28bbf4b173362a7d0be0c082e84e700795d8a",
}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_pinned(path: Path, name: str, length: int) -> bytes:
    data = path.read_bytes()
    if len(data) != length or digest(data) != PINS[name]:
        raise ValueError(f"{name}: H6 size/SHA-256 pin mismatch")
    return data


def build_manifest(kernel: bytes, witness: bytes, seed: bytes) -> bytes:
    if len(kernel) != KERNEL_LENGTH or digest(kernel) != PINS["kernel"]:
        raise ValueError("kernel: H6 pin mismatch")
    if len(witness) != HIGH_WITNESS_LENGTH or digest(witness) != PINS["witness"]:
        raise ValueError("witness: H6 pin mismatch")
    if len(seed) != 256 or int.from_bytes(seed[:8], "little") == 0:
        raise ValueError("seed must be 256 bytes with nonzero first 8 bytes")
    if KERNEL_PA + len(kernel) > STREAM_END or STREAM_END + 8 > SNAPSHOT_PA:
        raise ValueError("H6 stream and snapshot overlap")

    tag = int.from_bytes(seed[:8], "little")
    result = bytearray(MANIFEST_LENGTH)
    HEADER.pack_into(
        result, 0, b"N986BXNU", 3, MANIFEST_LENGTH,
        KERNEL_PA, len(kernel), STREAM_END, hashlib.sha256(kernel).digest(),
        seed, 2, 0, 0, bytes(32),
    )
    RO_RECORD.pack_into(
        result, HEADER.size, b"B2LROV1\0", 1, RO_RECORD.size, 3,
        HIGH_WITNESS_PA, len(witness), FRAME, 2,
        hashlib.sha256(witness).digest(), tag,
    )
    if RO_RECORD.size != 80 or HEADER.size + RO_RECORD.size > MANIFEST_LENGTH:
        raise ValueError("manifest structure changed")
    return bytes(result)


def build_low_stream(fd: bytes, manifest: bytes, kernel: bytes) -> bytes:
    pad = STREAM_END - (KERNEL_PA + len(kernel))
    if len(fd) != FD_LENGTH or len(manifest) != MANIFEST_LENGTH or \
       len(kernel) != KERNEL_LENGTH or not 0 <= pad <= 0x20000:
        raise ValueError("H6 stream geometry changed")
    low = fd + manifest + kernel + bytes(pad)
    if len(low) != 51_712 * FRAME:
        raise ValueError("H6 low frame count changed")
    return low


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fd", type=Path, required=True)
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--witness", type=Path, required=True)
    parser.add_argument("--seed", type=Path, required=True,
                        help="private, locally generated 256-byte session seed")
    parser.add_argument("--manifest-output", type=Path)
    parser.add_argument("--low-output", type=Path)
    args = parser.parse_args(argv)
    try:
        fd = read_pinned(args.fd, "fd", FD_LENGTH)
        read_pinned(args.stage, "stage", 340)
        kernel = read_pinned(args.kernel, "kernel", KERNEL_LENGTH)
        if kernel[:4] != b"\xcf\xfa\xed\xfe" or \
           struct.unpack_from("<I", kernel, 4)[0] != 0x0100000C:
            raise ValueError("kernel is not ARM64 Mach-O")
        witness = read_pinned(args.witness, "witness", HIGH_WITNESS_LENGTH)
        seed = args.seed.read_bytes()
        manifest = build_manifest(kernel, witness, seed)
        low = build_low_stream(fd, manifest, kernel)
        outputs = [p.resolve() for p in (args.manifest_output, args.low_output)
                   if p is not None]
        inputs = {p.resolve() for p in (args.fd, args.stage, args.kernel,
                                        args.witness, args.seed)}
        if len(outputs) != len(set(outputs)) or any(p in inputs for p in outputs):
            raise ValueError("output path duplicates an input or another output")
        for path, content in ((args.manifest_output, manifest),
                              (args.low_output, low)):
            if path is not None:
                with path.open("xb") as target:
                    target.write(content)
        print(json.dumps({
            "mode": "H6_HOST_ONLY_NO_DEVICE_IO",
            "manifest_sha256": digest(manifest),
            "low_stream_sha256": digest(low),
            "session_tag": f"0x{int.from_bytes(seed[:8], 'little'):016x}",
            "manifest_pa": hex(MANIFEST_PA),
            "kernel_pa": hex(KERNEL_PA),
            "high_witness_pa": hex(HIGH_WITNESS_PA),
            "high_witness_frames": HIGH_WITNESS_LENGTH // FRAME,
            "low_stream_pa": hex(FD_PA),
            "low_stream_end_exclusive": hex(STREAM_END),
            "low_frames": len(low) // FRAME,
            "terminal_pa": hex(STREAM_END),
            "terminal_hex": TERMINAL.hex(),
            "usb_sent": False,
        }, indent=2, sort_keys=True))
        return 0
    except (OSError, ValueError, struct.error) as exc:
        print(f"refused={exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
