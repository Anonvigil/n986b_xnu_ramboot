#!/usr/bin/env python3
"""Apply the N986B UI10V EDK II overlay to a pinned upstream checkout.

The 0x4B000-byte stock ECT is a user-supplied input. It is never copied into
this repository. See README.md for provenance and build prerequisites.
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
from pathlib import Path


UPSTREAM_COMMIT = "c00035413a9df469414b31529640bf1da28859db"
SUBMODULE_COMMITS = {
    "Common/edk2": "e7aac7fc137e247edad22f7ee53b9a1fba227397",
    "Common/edk2-platforms": "982212662c71b6c734b7578526071d6b78da3bcc",
    "GPLDrivers/Library/SimpleInit": "29e062805776db5ab7a61d07ef040d43aed71161",
    "Platform/EFI_Binaries": "87ac13a39d88790fd1a7f4bf5550f14aa3d8ed15",
}
ECT_SIZE = 0x4B000
ECT_SHA256 = "32a642074aa5168aede2cededf3af62c13cd20eb9212517fe517bfbfbbc70caa"
OLD_SIMPLEINIT_HEADER = "GPLDrivers/Library/SimpleInit/include/uefi.h"
OLD_SIMPLEINIT_HEADER_SHA256 = "e71ca481395b2838ef3a7d2d9bc1feae7f9ac172920289d26a449b8de25b14bb"


def git_head(path: Path) -> str:
    return subprocess.check_output(
        ["git", "-C", str(path), "rev-parse", "HEAD"], text=True
    ).strip()


def write_ect_header(raw: bytes, target: Path) -> None:
    lines = [
        "/* User-supplied stock ECT; generated locally, not distributed. */",
        "#ifndef N986B_FRESH_FULL_ECT_H",
        "#define N986B_FRESH_FULL_ECT_H",
        "STATIC CONST UINT8 mN986BFreshFullEct[0x4B000] = {",
    ]
    for start in range(0, len(raw), 16):
        lines.append("  " + ",".join(f"0x{byte:02x}" for byte in raw[start : start + 16]) + ",")
    lines.extend(["};", "#endif", ""])
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text("\n".join(lines), encoding="ascii")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", required=True, type=Path, help="pinned edk2-exynos checkout")
    parser.add_argument("--ect", required=True, type=Path, help="private stock ECT raw file")
    args = parser.parse_args()

    tree = args.tree.resolve(strict=True)
    ect_path = args.ect.resolve(strict=True)
    overlay = Path(__file__).resolve().parent / "overlay"
    if git_head(tree) != UPSTREAM_COMMIT:
        raise SystemExit(f"Wrong edk2-exynos revision: expected {UPSTREAM_COMMIT}")
    for relative, expected in SUBMODULE_COMMITS.items():
        path = tree / relative
        if not path.is_dir() or git_head(path) != expected:
            raise SystemExit(f"Missing or wrong submodule: {relative} at {expected}")

    ect = ect_path.read_bytes()
    digest = hashlib.sha256(ect).hexdigest()
    if len(ect) != ECT_SIZE or digest != ECT_SHA256:
        raise SystemExit(
            f"Wrong stock ECT: expected {ECT_SIZE} bytes and SHA-256 {ECT_SHA256}; "
            f"received {len(ect)} bytes and {digest}"
        )

    old_header = tree / OLD_SIMPLEINIT_HEADER
    if old_header.exists():
        old_digest = hashlib.sha256(old_header.read_bytes()).hexdigest()
        if old_digest != OLD_SIMPLEINIT_HEADER_SHA256:
            raise SystemExit(f"Unexpected existing SimpleInit header: {old_header}")

    files = sorted(path for path in overlay.rglob("*") if path.is_file())
    for source in files:
        destination = tree / source.relative_to(overlay)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)

    if old_header.exists():
        old_header.unlink()

    ect_header = (
        tree
        / "Platform/Samsung/exynos990/Application/N986BRecoveryRamLoader"
        / "N986BFreshFullEct.h"
    )
    write_ect_header(ect, ect_header)

    conf = tree / "work/edk2-conf"
    conf.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(tree / "tools/build_rule.txt", conf / "build_rule.txt")
    shutil.copyfile(tree / "tools/tools_def.txt", conf / "tools_def.txt")
    (conf / "target.txt").write_text(
        "ACTIVE_PLATFORM = Platform/Samsung/exynos990/n986b-ui10v.dsc\n"
        "TARGET = DEBUG\n"
        "TARGET_ARCH = AARCH64\n"
        "TOOL_CHAIN_CONF = tools_def.txt\n"
        "TOOL_CHAIN_TAG = CLANG38\n"
        "BUILD_RULE_CONF = build_rule.txt\n"
        "MAX_CONCURRENT_THREAD_NUMBER = 0\n",
        encoding="ascii",
    )
    print(f"Applied {len(files)} source files; generated private ECT header in {tree}")


if __name__ == "__main__":
    main()
