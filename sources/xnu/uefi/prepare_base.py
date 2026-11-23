#!/usr/bin/env python3
"""Prepare the pinned Exynos EDK II tree for the H1/H6 XNU FD without ECT.

This copies the shared public EFI source overlay. It does not build firmware,
generate an ECT header, or stage the H1-specific source files.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


HERE = Path(__file__).resolve().parent
EFI = HERE.parents[1] / "efi"
H1_DSC = "Platform/Samsung/exynos990/Research/Ios165RoStoreH1Branch/c2s-ios165-ro-store-h1-branch-host.dsc"
OLD_SIMPLEINIT_HEADER = "GPLDrivers/Library/SimpleInit/include/uefi.h"
OLD_SIMPLEINIT_HEADER_SHA256 = "e71ca481395b2838ef3a7d2d9bc1feae7f9ac172920289d26a449b8de25b14bb"


def git_head(path: Path) -> str:
    return subprocess.check_output(
        ["git", "-C", str(path), "rev-parse", "HEAD"], text=True
    ).strip()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, help="pinned edk2-exynos checkout")
    parser.add_argument("--check-only", action="store_true", help="validate inputs without copying")
    args = parser.parse_args()
    if not args.check_only and args.tree is None:
        parser.error("--tree is required unless --check-only is set")

    h1 = json.loads((HERE / "source-manifest.json").read_text(encoding="utf-8"))
    efi = json.loads((EFI / "source-manifest.json").read_text(encoding="utf-8"))
    upstream = h1["upstream"]
    expected = {
        "": upstream["edk2_exynos_commit"],
        "Common/edk2": upstream["edk2_commit"],
        "Common/edk2-platforms": upstream["edk2_platforms_commit"],
        "GPLDrivers/Library/SimpleInit": upstream["simpleinit_commit"],
        "Platform/EFI_Binaries": efi["upstream"]["submodules"]["Platform/EFI_Binaries"]["commit"],
    }
    if expected[""] != efi["upstream"]["edk2_exynos"]["commit"]:
        raise ValueError("H1 and shared EFI upstream revisions disagree")
    for rel, key in (
        ("Common/edk2", "Common/edk2"),
        ("Common/edk2-platforms", "Common/edk2-platforms"),
        ("GPLDrivers/Library/SimpleInit", "GPLDrivers/Library/SimpleInit"),
    ):
        if expected[rel] != efi["upstream"]["submodules"][key]["commit"]:
            raise ValueError(f"H1 and shared EFI submodule revisions disagree: {rel}")

    profile = HERE / "Platform/Samsung/exynos990/Research/Ios165RoStoreH1Branch"
    for name in ("c2s-ios165-ro-store-h1-branch-host.dsc", "exynos990-ios165-ro-store-h1-branch-host.fdf"):
        text = (profile / name).read_text(encoding="utf-8")
        if "N986BRecoveryRamLoader" in text or "N986BFreshFullEct" in text:
            raise ValueError(f"H1 profile unexpectedly depends on stock ECT: {name}")

    overlay = EFI / "overlay"
    files = sorted(path for path in overlay.rglob("*") if path.is_file())
    if len(files) != efi["source_overlay"]["file_count"] or len(files) != 72:
        raise ValueError("unexpected shared EFI overlay file count")
    for source in files:
        if source.is_symlink():
            raise ValueError(f"symlink in shared EFI overlay: {source}")

    if args.tree is None:
        print(f"H1_BASE_SOURCE_VERIFIED files={len(files)}; no device I/O")
        return

    tree = args.tree.resolve(strict=True)
    if not tree.is_dir() or not (tree / "Platform/Samsung/exynos990/exynos990.dsc").is_file():
        raise ValueError("not an edk2-exynos tree")
    for rel, commit in expected.items():
        path = tree / rel
        if not path.is_dir() or git_head(path) != commit:
            raise ValueError(f"missing or wrong pinned checkout: {rel or 'edk2-exynos'}")

    old_header = tree / OLD_SIMPLEINIT_HEADER
    if old_header.exists() and hashlib.sha256(old_header.read_bytes()).hexdigest() != OLD_SIMPLEINIT_HEADER_SHA256:
        raise ValueError("unexpected existing SimpleInit header")
    if args.check_only:
        print(f"H1_BASE_INPUTS_VERIFIED files={len(files)}; no files copied")
        return

    for source in files:
        destination = tree / source.relative_to(overlay)
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.is_symlink() or tree not in destination.resolve().parents:
            raise ValueError(f"unsafe destination: {destination}")
        shutil.copyfile(source, destination)
    if old_header.exists():
        old_header.unlink()

    conf = tree / "work/edk2-conf"
    conf.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(tree / "tools/build_rule.txt", conf / "build_rule.txt")
    shutil.copyfile(tree / "tools/tools_def.txt", conf / "tools_def.txt")
    (conf / "target.txt").write_text(
        f"ACTIVE_PLATFORM = {H1_DSC}\n"
        "TARGET = DEBUG\n"
        "TARGET_ARCH = AARCH64\n"
        "TOOL_CHAIN_CONF = tools_def.txt\n"
        "TOOL_CHAIN_TAG = CLANG38\n"
        "BUILD_RULE_CONF = build_rule.txt\n"
        "MAX_CONCURRENT_THREAD_NUMBER = 0\n",
        encoding="ascii",
    )
    print(f"H1_BASE_STAGED files={len(files)} tree={tree}; no ECT used")


if __name__ == "__main__":
    main()
