#!/usr/bin/env python3
"""Offline verification of already-collected historical H6 Upload A/B files.

No device reader is included or invoked. This verifies the H6 byte layout and
runtime markers; it does not authenticate who collected a supplied dump.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

import h6_manifest as h6

REGIONS = (
    ("callback", 340),
    ("callback_state", 16),
    ("witness", h6.HIGH_WITNESS_LENGTH),
    ("xnu_scratch", 4096),
    ("fd_suffix", 0x6B7000),
    ("manifest", h6.MANIFEST_LENGTH),
    ("kernel", h6.KERNEL_LENGTH),
    ("loader_marker", 4096),
    ("handoff_snapshot", 4096),
    ("loader_live", 96),
    ("loader_log_head", 64),
    ("xnu-console", None),
)
XNU_VIRT_BASE = 0xFFFFFFF004000000
XNU_PHYS_BASE = 0x902000000
LOG_PA = 0x905B2CA14
LOG_LEN_PA = 0x905B2CA10
LOG_HEAD = 256 * 1024
LOG_RING = 1024 * 1024
LOG_CAPACITY = 1536 * 1024
SBOOT_OVERWRITE_START = 0x9FF000
SBOOT_OVERWRITE_END = 0xA20000
EDGE_FIRST = b"1e433d6d39450e64162e5581fe0263830922587fb6b1f2a3b3e6da3bd05152f6"
EDGE_LAST = b"c3ff82d8be95b0a77bc1ba154cce41c988b330d83f79247cb0c2e4a72ab4a4da"


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def u64(data: bytes, offset: int) -> int:
    return struct.unpack_from("<Q", data, offset)[0]


def read_pair(directory: Path, name: str, length: int | None) -> bytes:
    a = (directory / f"{name}-A.bin").read_bytes()
    b = (directory / f"{name}-B.bin").read_bytes()
    if a != b or (length is not None and len(a) != length):
        raise ValueError(f"{name}: A/B bytes or expected length differ")
    return a


def check_receipt(path: Path) -> tuple[str, int]:
    receipt = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(receipt, dict) or receipt.get("status") != \
       "OUTS_ATTEMPTED_UPLOAD_AB_REQUIRED" or \
       (receipt.get("setup_acked"), receipt.get("high_acked"),
        receipt.get("low_acked")) != (9, 2048, 51_712) or \
       receipt.get("terminal_attempted") is not True:
        raise ValueError("send receipt lacks completed H6 attempt counters")
    details = receipt.get("fresh_seed_details")
    if not isinstance(details, dict):
        raise ValueError("send receipt lacks fresh session details")
    manifest_sha = details.get("manifest_sha256")
    tag = details.get("session_tag")
    if not isinstance(manifest_sha, str) or \
       re.fullmatch(r"[0-9a-f]{64}", manifest_sha) is None or \
       not isinstance(tag, str) or \
       re.fullmatch(r"0x[0-9a-f]{16}", tag) is None:
        raise ValueError("send receipt manifest SHA/tag is malformed")
    return manifest_sha, int(tag, 16)


def check_kernel_upload(captured: bytes, expected: bytes) -> bool:
    if len(captured) != len(expected) or \
       h6.KERNEL_PA + SBOOT_OVERWRITE_START != 0x91100000 or \
       h6.KERNEL_PA + SBOOT_OVERWRITE_END != 0x91121000:
        raise ValueError("H6 kernel Upload overlap geometry changed")
    if captured[:SBOOT_OVERWRITE_START] != expected[:SBOOT_OVERWRITE_START] or \
       captured[SBOOT_OVERWRITE_END:] != expected[SBOOT_OVERWRITE_END:]:
        raise ValueError("kernel differs outside exact S-Boot crash-summary range")
    changed = captured[SBOOT_OVERWRITE_START:SBOOT_OVERWRITE_END] != \
        expected[SBOOT_OVERWRITE_START:SBOOT_OVERWRITE_END]
    if changed and (not captured[SBOOT_OVERWRITE_START:].startswith(
            b"@ Ramdump Auto Comment") or
            not captured[0xA00000:].startswith(b"<!DOCTYPE html>")):
        raise ValueError("kernel overlap differs without known crash-summary markers")
    return changed


def check_console(log: bytes, tag: int) -> tuple[int, int]:
    text = log.replace(b"\r\n", b"\n")
    if re.search(rb"B2L-H4B?-[^\r\n\x00]*(?:REJECT|ABSENT)", text) or \
       b"B2L-H5-REJECT" in text or b"B2L-H6-REJECT" in text:
        raise ValueError("XNU console has a rejected H4/H5/H6 marker")
    registration = re.findall(
        rb"B2L-H4-REGISTER-OK b2lro0=(\d+) rb2lro0=(\d+) sectors=2048\n", text)
    if len(registration) != 1:
        raise ValueError("H4 block registration is missing or duplicated")
    block_major, raw_major = map(int, registration[0])
    if not (0 <= block_major <= 0xFFFF and 0 <= raw_major <= 0xFFFF):
        raise ValueError("H4 block device major is invalid")
    markers = (
        b"B2L-H4-V3-RECEIPT-PASS\n",
        f"B2L-H4-RECORD-OK tag={tag:x}\n".encode(),
        b"B2L-H4-PHYS-READ-SHA-OK\n",
        f"B2L-H4-1MIB-PASS tag={tag:x} sha256={h6.PINS['witness']} bytes=1048576 scratch=0x940100000\n".encode(),
        b"B2L-H4B-DT-CORE-PASS\n",
        b"B2L-H4-BCOPY-EDGE-PASS first=" + EDGE_FIRST + b" last=" + EDGE_LAST + b"\n",
        b"B2L-H4-WRITE-GATES-PASS\n",
        f"B2L-H4-REGISTER-OK b2lro0={block_major} rb2lro0={raw_major} sectors=2048\n".encode(),
        b"B2L-H5-ENTER pre-root char-physio\n",
        b"B2L-H5-LBA0-CALLBACK-PASS sha256=" + EDGE_FIRST + b"\n",
        b"B2L-H5-LBA2047-CALLBACK-PASS sha256=" + EDGE_LAST + b"\n",
        b"B2L-H5-NO-WRITE calls=2 reads=2 writes=0\n",
        b"B2L-H5-PASS char-physio-strategy-only no-vnode no-root-alias\n",
        b"B2L-H6-ENTER pre-root vblk-vnop-strategy\n",
        b"B2L-H6-PREFLIGHT geometry=512x2048 fread=1 set512x2=1\n",
        b"B2L-H6-BDEVVP-PASS vblk-open-fread\n",
        b"B2L-H6-LBA0-VNOP-PASS sha256=" + EDGE_FIRST + b"\n",
        b"B2L-H6-LBA2047-VNOP-PASS sha256=" + EDGE_LAST + b"\n",
        b"B2L-H6-NO-WRITE spec=2 calls=2 reads=2 writes=0\n",
        b"B2L-H6-CLEANUP vnode-rele-put\n",
        b"B2L-H6-PASS vblk-vnop-specfs-strategy no-root-alias\n",
        b"B2L-H4-NO-ROOT-ALIAS /dev/b2lro0\n",
    )
    positions = [text.find(marker) for marker in markers]
    if any(position < 0 for position in positions) or positions != sorted(positions) or \
       any(text.count(marker) != 1 for marker in markers):
        raise ValueError("H4/H5/H6 runtime markers missing, repeated, or out of order")
    return block_major, raw_major


def verify(directory: Path, receipt_path: Path, fd_path: Path,
           stage_path: Path, kernel_path: Path, witness_path: Path) -> dict:
    fd = h6.read_pinned(fd_path, "fd", h6.FD_LENGTH)
    stage = h6.read_pinned(stage_path, "stage", 340)
    kernel = h6.read_pinned(kernel_path, "kernel", h6.KERNEL_LENGTH)
    witness = h6.read_pinned(witness_path, "witness", h6.HIGH_WITNESS_LENGTH)
    receipt_manifest_sha, receipt_tag = check_receipt(receipt_path)
    data = {name: read_pair(directory, name, length) for name, length in REGIONS}
    if data["callback"] != stage or data["witness"] != witness or \
       data["fd_suffix"] != fd[0x49000:]:
        raise ValueError("callback, witness, or stable FD suffix differs")
    if (u64(data["callback_state"], 0), u64(data["callback_state"], 8)) != (2048, 0):
        raise ValueError("callback count/error differs")

    manifest = data["manifest"]
    seed = h6.HEADER.unpack_from(manifest)[7]
    expected_manifest = h6.build_manifest(kernel, witness, seed)
    if manifest != expected_manifest or sha(manifest) != receipt_manifest_sha or \
       int.from_bytes(seed[:8], "little") != receipt_tag:
        raise ValueError("manifest/receipt session binding differs")
    summary_overwrite = check_kernel_upload(data["kernel"], kernel)

    marker = data["loader_marker"]
    checks = {
        0x860: 11,
        0x910: 0x986B0011,
        0x918: 0x986B1650,
        0x920: h6.HIGH_WITNESS_PA,
        0x928: 0xA0000000,
        0x930: 1,
        0x938: 1,
        0x940: receipt_tag,
        0x968: 1,
    }
    if any(u64(marker, offset) != value for offset, value in checks.items()) or \
       marker[0x8F0:0x910] != bytes.fromhex(receipt_manifest_sha) or \
       marker[0x948:0x968] != bytes.fromhex(h6.PINS["witness"]):
        raise ValueError("UEFI loader marker differs")
    snapshot = data["handoff_snapshot"]
    if snapshot[:8] != b"N986BXHS" or \
       struct.unpack_from("<IIQQ", snapshot, 8) != (3, 856, 0x30, 0) or \
       snapshot[824:856] != bytes.fromhex(receipt_manifest_sha):
        raise ValueError("UEFI handoff snapshot differs")
    virt, phys, size = struct.unpack_from("<QQQ", snapshot, 96)
    if (virt, phys, size) != (XNU_VIRT_BASE, XNU_PHYS_BASE, 960 * 1024 * 1024):
        raise ValueError("XNU boot_args physical/virtual layout differs")

    scratch = data["xnu_scratch"]
    fields = struct.unpack_from("<8sQQQQQ32sQQ", scratch)
    if fields != (b"B2LH4M01", 1, 1, h6.HIGH_WITNESS_PA,
                  h6.HIGH_WITNESS_LENGTH, receipt_tag,
                  bytes.fromhex(h6.PINS["witness"]),
                  receipt_tag ^ 0xFFFFFFFFFFFFFFFF, 0x986B03B1):
        raise ValueError("XNU physical-read scratch commit differs")

    live = data["loader_live"]
    head = data["loader_log_head"]
    if live[:8] != b"XNULIVE!" or live[80:88] != b"XNULIVE2":
        raise ValueError("XNU console live header differs")
    words = struct.unpack("<12Q", live)
    if words[2:4] != (XNU_VIRT_BASE + LOG_PA - XNU_PHYS_BASE,
                      XNU_VIRT_BASE + LOG_LEN_PA - XNU_PHYS_BASE) or \
       words[6:8] != (LOG_HEAD, LOG_RING):
        raise ValueError("XNU console pointers/capacity differ")
    length = struct.unpack_from("<I", head, 16)[0]
    expected_console_length = max(64 * 1024, length)
    if not 0 < length <= LOG_CAPACITY or \
       len(data["xnu-console"]) != expected_console_length:
        raise ValueError("XNU console length differs")
    block_major, raw_major = check_console(data["xnu-console"], receipt_tag)
    return {
        "result": "H6_OFFLINE_AB_CONTENT_CHECK_PASS",
        "device_io": False,
        "ab_regions_checked": len(REGIONS),
        "kernel_sboot_crash_summary_present": summary_overwrite,
        "h4_h5_h6_ordered_markers": 22,
        "block_major": block_major,
        "raw_major": raw_major,
        "h6_strategy_reads": 2,
        "h6_writes": 0,
        "scope": "two read-only pre-root block-vnode strategy callbacks; no root mount or iOS UI",
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump-dir", type=Path, required=True)
    parser.add_argument("--send-receipt", type=Path, required=True)
    parser.add_argument("--fd", type=Path, required=True)
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--witness", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        result = verify(args.dump_dir, args.send_receipt, args.fd,
                        args.stage, args.kernel, args.witness)
        print(json.dumps(result, indent=2, sort_keys=True))
        return 0
    except (OSError, ValueError, KeyError, IndexError, TypeError,
            struct.error, json.JSONDecodeError) as exc:
        print(f"refused={exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
