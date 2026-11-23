#!/usr/bin/env python3
"""Verify the pinned N986B XNU source overlay; no device I/O."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys


HERE = Path(__file__).resolve().parent
MANIFEST = HERE / "source-manifest.json"
PATCHES = {
    "h6": HERE / "patches/0001-n986b-ios165-h6.patch",
    "h7": HERE / "patches/0002-n986b-ios165-h7-host-only.patch",
}


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tree", type=Path, required=True,
                        help="XNU source tree after applying the selected patch(es)")
    parser.add_argument("--stage", choices=("h6", "h7"), default="h6")
    args = parser.parse_args()
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    expected = dict(manifest["h6_source_sha256"])
    if args.stage == "h7":
        expected.update(manifest["h7_source_sha256"])

    failures: list[str] = []
    for stage in ("h6", "h7"):
        if stage == "h7" and args.stage == "h6":
            continue
        path = PATCHES[stage]
        if digest(path) != manifest[f"{stage}_patch_sha256"]:
            failures.append(f"patch SHA mismatch: {path.name}")
    for relative, wanted in expected.items():
        path = args.tree / relative
        if not path.is_file():
            failures.append(f"missing: {relative}")
        elif digest(path) != wanted:
            failures.append(f"source SHA mismatch: {relative}")
    if failures:
        for failure in failures:
            print(failure, file=sys.stderr)
        return 1
    print(f"{args.stage.upper()} SOURCE PASS: {len(expected)} files and patches match")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
