#!/usr/bin/env python3
"""Verify and stage the source of the H6-paired v3 UEFI FD (host only)."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil


HERE = Path(__file__).resolve().parent


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, help="prepared edk2-exynos source tree")
    parser.add_argument("--check-only", action="store_true", help="verify source hashes without copying")
    args = parser.parse_args()
    if not args.check_only and args.tree is None:
        parser.error("--tree is required unless --check-only is set")

    manifest = json.loads((HERE / "source-manifest.json").read_text(encoding="utf-8"))
    files = manifest["source_files"]
    assert len(files) == 21, "unexpected H1 source set"
    for rel, expected in files.items():
        path = Path(rel)
        if path.is_absolute() or ".." in path.parts or path.suffix in {".fd", ".efi", ".bin"}:
            raise ValueError(f"unsafe source path: {rel}")
        source = HERE / path
        if source.is_symlink() or not source.is_file():
            raise ValueError(f"source is missing or a symlink: {rel}")
        data = source.read_bytes()
        if len(data) != expected["bytes"] or hashlib.sha256(data).hexdigest() != expected["sha256"]:
            raise ValueError(f"source hash mismatch: {rel}")

    profile = HERE / "Platform/Samsung/exynos990/Research/Ios165RoStoreH1Branch/Loader"
    inf = (profile / "N986BXnuLoader.inf").read_text(encoding="utf-8")
    source = (profile / "N986BXnuLoader.c").read_text(encoding="utf-8")
    if manifest["boot_app_guid"] not in inf or "-DN986B_ENABLE_XNU_HANDOFF=1" not in inf:
        raise ValueError("boot application identity or handoff flag changed")
    if "if (Header.Version != 3)" not in source or "Dt.ro_store_v1 = Header.Reserved;" not in source:
        raise ValueError("v3 read-only-store contract changed")

    if args.check_only:
        print(f"H1_BRANCH_SOURCE_VERIFIED files={len(files)}; no device I/O")
        return

    tree = args.tree.resolve(strict=True)
    if not tree.is_dir() or not (tree / "Platform/Samsung/exynos990/exynos990.dsc").is_file():
        raise ValueError("not an edk2-exynos tree")
    for rel in files:
        target_rel = rel.removeprefix("board-overrides/")
        destination = tree / target_rel
        destination.parent.mkdir(parents=True, exist_ok=True)
        if destination.is_symlink() or tree not in destination.resolve().parents:
            raise ValueError(f"unsafe destination: {rel}")
        shutil.copyfile(HERE / rel, destination)
    print(f"H1_BRANCH_SOURCE_STAGED files={len(files)} tree={tree}")


if __name__ == "__main__":
    main()
