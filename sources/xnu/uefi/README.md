# H6-paired iOS 16.5 UEFI source

This directory preserves the **v3 read-only-store UEFI loader source** used to build the FD reused by the H4b, H5 and H6 live XNU experiments. The frozen H1 branch FD was 7,340,032 bytes, SHA-256 `b6433e3bdf6ffda64357f46fc592a5d922611241470d942e2f86afa1b19d0201`. Its EFI application SHA-256 was `cf097095fa9223b33c8b5c7435fe7e3dccdef7a19d0fcdc7cbac7863ee16068f`; its FFS SHA-256 was `13cf5236a70f77aeeceadeb3264eee4a181c3c5da42c4509d3cb4c020cd3d45e`. The original host verifier compared the H4/H6 FD byte for byte against this H1 FD and checked all 15 loader source hashes. The 21 files preserved here match the host source bytes by SHA-256; see [the manifest](source-manifest.json).

The [early loader adapter](../bootstrap-adapter/README.md) is a distinct handoff-disabled checkpoint. This profile's INF has `N986B_ENABLE_XNU_HANDOFF=1`, application GUID `6993C426-C19A-4C89-8B90-EE39A6E7F8A1`, and a v3 manifest reader. It verifies the high-RAM store and its SHA-256, publishes `/chosen/b2l,ro-store-v1` in the Apple device tree, and rechecks memory ownership immediately before `ExitBootServices`. The manifest binds the separately transmitted XNU Mach-O; it is **not** embedded in the FD. H6 used the H1 FD unchanged with a newly pinned XNU and run manifest.

## Preserved files and provenance

- `Platform/Samsung/exynos990/Research/Ios165RoStoreH1Branch/`: exact H1 branch DSC/FDF and 15-source-file UEFI application snapshot. The app source retains its per-file BSD-style SPDX notices.
- `Platform/Samsung/exynos990/Research/Ios165Reserve/Map/`: the selected N986B high-RAM memory-map library named by the DSC.
- `board-overrides/`: two files from the same host build tree that differ from this repository's later [TWRP EFI overlay](../../efi/README.md). The boot-manager copy selects the XNU application GUID; the SimpleInit copy retains the original destructor line. These source files retain their upstream notices and separate license terms.
- [`prepare_base.py`](prepare_base.py) verifies the pinned upstream revisions and stages the shared 72-file EFI overlay without requiring ECT. [`prepare_h6_fd.py`](prepare_h6_fd.py) then verifies all 21 preserved H1 source hashes and stages them over that base. [`build_h6_fd.ps1`](build_h6_fd.ps1) records the historical AARCH64 `DEBUG_CLANG38` build arguments with portable path parameters.

The pinned upstream source revisions are in [the manifest](source-manifest.json) and the [EFI source recipe](../../efi/README.md): Exynos EDK II `c00035413a9df469414b31529640bf1da28859db`, TianoCore EDK II `e7aac7fc137e247edad22f7ee53b9a1fba227397`, edk2-platforms `982212662c71b6c734b7578526071d6b78da3bcc`, and SimpleInit `29e062805776db5ab7a61d07ef040d43aed71161`. Keep their original licenses and notices. The Exynos/EDK II board source is separately licensed under [its upstream notice](../../efi/UPSTREAM_LICENSE); SimpleInit modifications retain LGPL-3.0-or-later terms and [license text](../../efi/SIMPLEINIT_LICENSE). The XNU kernel delta has separate APSL obligations; see the parent source bundle.

The H1 DSC/FDF also references the N986B preflight module from the existing EFI overlay. A source comparison against the retained host tree found 61 files from that overlay byte-identical, two differences captured here in `board-overrides/`, and nine later TWRP-only source paths absent from the H1 tree. This checks the selected source snapshots; it is not a clean-tree build or proof that the rebuilt FD will be byte-identical.

## Reconstruction on Windows

Start with the pinned edk2-exynos checkout and submodules. Build the pinned EDK II BaseTools for Windows, install LLVM MinGW `20261020` and GNU Make, and prepare the shared source overlay with `prepare_base.py`. The H1 DSC/FDF use `N986BRamPreflight` and the XNU loader; they do not include the separate `N986BRecoveryRamLoader` that embeds stock ECT. The [TWRP UI10V EFI recipe](../../efi/README.md) still requires the owner's separately held ECT, but this H1 source preparation does not. A clean end-to-end H1 FD rebuild from this public package has **not** been verified.

From this repository root, after cloning and checking out the pinned upstream revisions listed above:

```powershell
python sources/xnu/uefi/prepare_base.py --check-only --tree C:\path\to\edk2-exynos
python sources/xnu/uefi/prepare_base.py --tree C:\path\to\edk2-exynos
python sources/xnu/uefi/prepare_h6_fd.py --check-only
python sources/xnu/uefi/prepare_h6_fd.py --tree C:\path\to\edk2-exynos
powershell -ExecutionPolicy Bypass -File sources/xnu/uefi/build_h6_fd.ps1 `
  -Tree C:\path\to\edk2-exynos `
  -LlvmBin C:\path\to\llvm-mingw\bin `
  -MakeExe C:\path\to\make.exe
```

Generate the pinned SimpleInit rootfs source/object files as described in the EFI recipe before running the build. Keep generated firmware, FD and EFI files out of Git. Compare the built FD/EFI hashes with the frozen values above; a different hash is a **new, untested variant**, even if it links. The source was attested to the historical FD on the original host, but this public package has not itself reproduced a byte-identical FD on a clean host or passed a new device run.

The proven H6 result requires the paired [H6 XNU source delta](../README.md), a run-specific manifest, a privately supplied read-only diagnostic fixture and a fresh Upload A/B validation. This directory alone cannot boot XNU or iOS, and it does not include Apple user-space software, firmware, ECT, or raw RAM captures.
