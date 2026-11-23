#!/usr/bin/env python3
"""Print the historical H6 Fastboot RAM transfer plan without opening USB.

This is a reference for reconstructing a sender, not a live sender. The original
live program depended on device-specific identity, safety gates and USB code.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

import h6_manifest as h6

FASTBOOT_VID = 0x18D1
FASTBOOT_PID = 0x0002
SET_SEL_REQUEST = 0x30
SET_SEL_REQUEST_TYPE = 0x00  # standard, host-to-device, device recipient
BULK_OUT_ENDPOINT = 0x02
FASTBOOT_STAGING = 0x8B000000
FASTBOOT_CALLBACK = 0xE81EC280
PRETARGET = 0x8B000FF8
CALLBACK_POINTER = 0xE83483F8
ARCH_INVALIDATE_CACHE_RANGE = 0xE8016638
ARCH_SYNC_CACHE_RANGE = 0xE8016658
MMU_DISABLE = 0xE80AC798


def set_sel_payload(pointer: int) -> bytes:
    payload = bytearray(64)
    struct.pack_into("<Q", payload, 0x20, 1)
    struct.pack_into("<Q", payload, 0x30, pointer)
    struct.pack_into("<Q", payload, 0x38, 1)
    return bytes(payload)


def setup_steps(stage: bytes) -> tuple[tuple[str, int, bytes], ...]:
    if len(stage) != 340 or hashlib.sha256(stage).hexdigest() != h6.PINS["stage"]:
        raise ValueError("callback stage differs from historical H6 pin")
    steps = (
        ("prime", FASTBOOT_STAGING, b"getvar:product"),
        ("stage", FASTBOOT_CALLBACK, stage),
        ("invalidate", FASTBOOT_STAGING,
         struct.pack("<Q", ARCH_INVALIDATE_CACHE_RANGE)),
        ("stage", FASTBOOT_CALLBACK, stage),
        ("sync", FASTBOOT_STAGING, struct.pack("<Q", ARCH_SYNC_CACHE_RANGE)),
        ("stage", FASTBOOT_CALLBACK, stage),
        ("disable_mmu", FASTBOOT_STAGING, struct.pack("<Q", MMU_DISABLE)),
        ("stage", FASTBOOT_CALLBACK, stage),
        ("install_callback", PRETARGET, struct.pack("<Q", FASTBOOT_STAGING)),
    )
    if tuple(name for name, _, _ in steps) != (
        "prime", "stage", "invalidate", "stage", "sync", "stage",
        "disable_mmu", "stage", "install_callback",
    ):
        raise ValueError("setup step order changed")
    return steps


def make_plan(stage: bytes) -> dict:
    steps = setup_steps(stage)
    return {
        "mode": "H6_HISTORICAL_HOST_ONLY_SEND_PLAN",
        "device_io": False,
        "usb": {
            "vid": f"0x{FASTBOOT_VID:04x}",
            "pid": f"0x{FASTBOOT_PID:04x}",
            "interface": 0,
            "set_sel_request": f"0x{SET_SEL_REQUEST:02x}",
            "set_sel_request_type": f"0x{SET_SEL_REQUEST_TYPE:02x}",
            "bulk_out_endpoint": f"0x{BULK_OUT_ENDPOINT:02x}",
        },
        "setup": [
            {
                "step": index + 1,
                "label": name,
                "set_sel_target": hex(pointer),
                "set_sel_payload_sha256": hashlib.sha256(
                    set_sel_payload(pointer)).hexdigest(),
                "bulk_bytes": len(payload),
                "bulk_sha256": hashlib.sha256(payload).hexdigest(),
            }
            for index, (name, pointer, payload) in enumerate(steps)
        ],
        "callback_pointer": hex(CALLBACK_POINTER),
        "high_witness": {
            "first_pa": hex(h6.HIGH_WITNESS_PA),
            "frame_bytes": h6.FRAME,
            "frames": h6.HIGH_WITNESS_LENGTH // h6.FRAME,
            "end_exclusive": hex(h6.HIGH_SCRATCH_PA),
        },
        "low_stream": {
            "first_pa": hex(h6.FD_PA),
            "frame_bytes": h6.FRAME,
            "frames": (h6.STREAM_END - h6.FD_PA) // h6.FRAME,
            "end_exclusive": hex(h6.STREAM_END),
        },
        "terminal": {
            "pa": hex(h6.STREAM_END),
            "ascii": h6.TERMINAL.decode("ascii"),
            "bytes": len(h6.TERMINAL),
            "attempts": 1,
        },
        "total_bulk_outs": len(steps) +
            h6.HIGH_WITNESS_LENGTH // h6.FRAME +
            (h6.STREAM_END - h6.FD_PA) // h6.FRAME + 1,
        "stop_rule": "Any short/error/uncertain nonterminal transfer aborts; terminal is never blindly retried; same-run Upload A/B resolves its outcome.",
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        stage = h6.read_pinned(args.stage, "stage", 340)
        print(json.dumps(make_plan(stage), indent=2, sort_keys=True))
        return 0
    except (OSError, ValueError) as exc:
        print(f"refused={exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
