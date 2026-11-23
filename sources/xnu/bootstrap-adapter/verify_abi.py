#!/usr/bin/env python3
"""Compare the copied N986B XNU 8796 handoff with Scion's source ABI.

This compiles assertions against the two actual C declarations. It does not
build firmware or communicate with a phone.
"""

import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


def struct_from(text: str, name: str) -> str:
    start = text.index(f"struct {name} {{")
    end = text.index("};", start) + 2
    return text[start:end]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scion-loader", type=Path, required=True)
    args = parser.parse_args()
    scion = args.scion_loader.read_text(encoding="utf-8")
    define = re.search(r"^#define BOOT_LINE_LENGTH\s+\d+\s*$", scion, re.M)
    if not define:
        raise ValueError("Scion BOOT_LINE_LENGTH was not found")
    video = struct_from(scion, "boot_video").replace("struct boot_video", "struct scion_boot_video")
    boot = struct_from(scion, "boot_args").replace("struct boot_args", "struct scion_boot_args")
    boot = boot.replace("struct boot_video video;", "struct scion_boot_video video;")
    fields = {
        "revision": "revision",
        "version": "version",
        "virtual_base": "virtBase",
        "physical_base": "physBase",
        "memory_size": "memSize",
        "top_of_kernel_data": "topOfKernelData",
        "video": "video",
        "machine_type": "machineType",
        "device_tree": "deviceTreeP",
        "device_tree_length": "deviceTreeLength",
        "command_line": "commandLine",
        "flags": "bootFlags",
        "actual_memory_size": "memSizeActual",
    }
    checks = [
        '_Static_assert(sizeof(struct b2l_boot_args) == sizeof(struct scion_boot_args), "boot_args size");',
        '_Static_assert(sizeof(struct b2l_boot_video) == sizeof(struct scion_boot_video), "video size");',
    ]
    for ours, upstream in fields.items():
        checks.append(
            f'_Static_assert(offsetof(struct b2l_boot_args, {ours}) == '
            f'offsetof(struct scion_boot_args, {upstream}), "{ours} offset");'
        )
        checks.append(
            f'_Static_assert(sizeof(((struct b2l_boot_args *)0)->{ours}) == '
            f'sizeof(((struct scion_boot_args *)0)->{upstream}), "{ours} size");'
        )
    checks += [
        '_Static_assert(sizeof(struct b2l_boot_args) == 736, "XNU 8796 size");',
        '_Static_assert(offsetof(struct b2l_boot_args, flags) == 720, "flags position");',
        '_Static_assert(offsetof(struct b2l_boot_args, actual_memory_size) == 728, "actual RAM position");',
    ]
    source = "\n".join(
        [
            "#include <stddef.h>",
            "#include <stdint.h>",
            '#include "BootArgs.h"',
            define.group(0),
            video,
            boot,
            *checks,
            "int main(void) { return 0; }",
        ]
    )
    include_dir = Path(__file__).resolve().parent / "loader"
    with tempfile.TemporaryDirectory() as temporary:
        path = Path(temporary) / "compare.c"
        path.write_text(source, encoding="utf-8")
        subprocess.run(
            [os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only", "-I", str(include_dir), str(path)],
            check=True,
        )
    loader_source = (include_dir / "N986BXnuLoader.c").read_text(encoding="utf-8")
    if "Snapshot->Version = 3;" not in loader_source:
        raise ValueError("XNU 8796 snapshot is not version 3")
    print("N986B/Scion XNU 8796 boot_args: all 13 fields match (736 bytes)")
    print("snapshot: version 3 selected for the 736-byte boot_args")


if __name__ == "__main__":
    main()
