# N986B UI10V EFI and FD build sources

This directory carries the N986B-specific EDK II source overlay for the
RAM-booted TWRP GUI demonstration. It is scoped to the tested UI10V VTS-off
firmware, not the earlier diagnostic variants. `overlay/` contains the UI10V
application, its EDK II platform descriptions, the high-RAM memory map,
the required platform changes, 22 SimpleInit source changes, and 31 EDK II
submodule source changes carried by the tested snapshot. The upstream
EDK II tree and its submodules are pinned below instead of copied wholesale
into this repository.

The public source cannot produce the frozen binary by itself: the loader
embeds a **stock N986B ECT** copied from the owner's device. That 0x4B000-byte
firmware input is intentionally absent from Git. `prepare_sources.py` accepts
it locally and refuses a size or SHA-256 mismatch. Never commit the generated
`N986BFreshFullEct.h` or the raw ECT.

## Pinned inputs

| Input | Revision or identity |
| --- | --- |
| [edk2-exynos](https://github.com/sonic011gamer/edk2-exynos) | `c00035413a9df469414b31529640bf1da28859db` |
| [TianoCore edk2](https://github.com/tianocore/edk2) submodule | `e7aac7fc137e247edad22f7ee53b9a1fba227397` |
| [edk2-platforms](https://github.com/tianocore/edk2-platforms) submodule | `982212662c71b6c734b7578526071d6b78da3bcc` |
| [SimpleInit](https://github.com/BigfootACA/simple-init) submodule | `29e062805776db5ab7a61d07ef040d43aed71161` |
| `Platform/EFI_Binaries` submodule | `87ac13a39d88790fd1a7f4bf5550f14aa3d8ed15` |
| LLVM MinGW toolchain | `llvm-mingw-20261020`, UCRT x86-64 Windows build |
| Stock ECT, supplied separately | 307,200 bytes, SHA-256 `32a642074aa5168aede2cededf3af62c13cd20eb9212517fe517bfbfbbc70caa` |

The pinned commits, license references, and hashes are also recorded in the
machine-readable [source manifest](source-manifest.json).

The upstream `Platform/EFI_Binaries` submodule contains a prebuilt
`LinuxSimpleMassStorage.efi` referenced by the firmware volume. Its source is
identified in that submodule's INF as
[BigfootACA/linux-simple-mass-storage](https://github.com/BigfootACA/linux-simple-mass-storage).
Keep the upstream submodules and their license notices intact when publishing
derived firmware. The EDK II board source is BSD-2-Clause. The modified
SimpleInit files carry LGPL-3.0-or-later notices; its license text is copied
as [SIMPLEINIT_LICENSE](SIMPLEINIT_LICENSE). SimpleInit also contains other
separately licensed libraries. The EDK II source overlay retains the original
notices from its pinned [edk2 tree](https://github.com/tianocore/edk2);
its PCCTS-derived build-tool files have separate upstream provenance.

## Build on Windows

Install Git, Python 3, LLVM MinGW `20261020`, GNU Make for Windows, and the
EDK II BaseTools executables for Windows (`GenFv.exe`, `GenFw.exe`, `GenSec.exe`,
and the other BaseTools programs). The pinned edk2 BaseTools version expects
the Visual Studio host compiler for its Windows build. Build BaseTools from
that pinned source if they are unavailable, following the
[TianoCore Windows setup guide](https://www.tianocore.org/tianocore-wiki.github.io/build-tooling/environment-setup/windows_systems.html).
The pinned SimpleInit also requires its generated rootfs C/object files. The
documented generator uses WSL with host GCC and `aarch64-linux-gnu-gcc`.
The Python and PowerShell entry points use no machine-specific paths.

The overlay removes the `destructor` attribute from SimpleInit's
`fsdrv_deinitialize` in `src/filesystem/internal.c`. In the pinned toolchain,
that attribute creates an allocated `.fini_array` with an AArch64 relative
relocation that this EDK II `GenFw` cannot convert. This build adjustment has
not been tested on the phone and changes the output from the frozen GUI demo.

```powershell
git clone https://github.com/sonic011gamer/edk2-exynos.git edk2-exynos
git -C edk2-exynos checkout c00035413a9df469414b31529640bf1da28859db
git -C edk2-exynos submodule update --init
git -C edk2-exynos/Common/edk2 submodule update --init
git -C edk2-exynos/GPLDrivers/Library/SimpleInit submodule update --init

python sources/efi/prepare_sources.py --tree edk2-exynos --ect C:\private\N986B-stock-ECT.bin
```

Inside WSL, with the clone accessible under `/mnt/c`, generate the pinned
SimpleInit rootfs files before building the FD:

```bash
cd /mnt/c/path/to/edk2-exynos/GPLDrivers/Library/SimpleInit
mkdir -p build
HOSTCC=gcc CC=aarch64-linux-gnu-gcc \
  bash scripts/gen-rootfs-source.sh "$PWD" "$PWD/build" "$PWD/root"
```

The expected `build/rootfs.bin` SHA-256 is
`4c2e0dbd74ea546d7f0ae2af7ed6dccbde5bbb0fbf39152b5c89cc29c6ac63cd`.
Return to PowerShell in this repository's root:

```powershell
powershell -ExecutionPolicy Bypass -File sources/efi/build_ui10v.ps1 `
  -Tree edk2-exynos -LlvmBin C:\toolchains\llvm-mingw-20261020\bin `
  -MakeExe C:\toolchains\make\make.exe
```

Run these commands from this repository's root; the `edk2-exynos` clone may be
placed elsewhere by changing `--tree` and `-Tree`. A short absolute path is
recommended because EDK II generates deep build paths. The preparation step
checks the top-level commit and each required submodule, overlays 72 source
files, removes the superseded pinned `SimpleInit/include/uefi.h`, generates
the private ECT C header, and creates `work/edk2-conf`. The build
uses `DEBUG_CLANG38`, AARCH64, an FD base of `0x90000000`, and an FD size of
`0x700000`.

Expected outputs within the EDK II clone:

```text
Build/ui10v/DEBUG_CLANG38/FV/EXYNOS990_UEFI.fd
Build/ui10v/DEBUG_CLANG38/AARCH64/N986BUi10V.efi
```

The tested frozen artifacts are labeled `N986B-TWRP-UI10V-VTS-OFF-RAM.fd`
(SHA-256 `0ac10b5ae7ed996102d4ff74ee8f0d2002d57088fa804c93a1d2b271ebb06c01`)
and `N986BUi10V.efi`
(SHA-256 `a274548662f9c88167facf96e1bbe3aa0a7bedf2e2e0c7f589364af2abcd1fa5`).
The script recreates a buildable source configuration, not the exact frozen
binary. A fresh independent EDK II source checkout, locally supplied stock ECT,
newly generated SimpleInit rootfs, LLVM MinGW `20261020`, and previously built
BaseTools executables completed the EFI and FD build on Windows. Its EFI
SHA-256 was `6d9729b0e02ec2146a337a4e77a7f51db28a31e26974367a18b2c8908167fee9`;
its FD SHA-256 was `82dd3f219f7b4870fa4fa08e789c9953cc4f5d4715d370d98c94f6294752fc70`.
Neither matches the frozen GUI demonstration above. This independently built
variant has not been booted on the N986B. The BaseTools executables used for
this check came from the earlier local build; their clean Windows compilation
from the pinned source remains unverified. Do not label these outputs as the
tested release binaries.

## Boot contract

`N986BUi10V.inf` uses firmware-file GUID
`F9F65EBA-151E-4DFC-AA46-D6015D924DD9`, which the platform boot manager
selects when `N986B_RECOVERY_RELOCATED_ECT_BRANCH_BOOT=1`. The loader reads
the separate RAM stream at `0xA8000000` and hands off a Linux kernel with a
relocated initramfs and FDT. The FD alone does not contain the TWRP kernel,
initramfs, or full stream; see the sibling TWRP source recipe for those inputs.

The tested UI10V FDT disables UFS, RPMB, eMMC, and VTS. This was a RAM-only GUI
demonstration. The empty Mount page is expected; do not infer working backup,
restore, install, decryption, or block-device support from the GUI photos.

## Overlay provenance

The overlay copies changed files from the tested UI10V source snapshot,
including the modified SimpleInit submodule files.
Existing upstream files keep their original copyright headers. The original
stock ECT byte array, generated build products, extracted device partitions,
and unrelated diagnostic branches are excluded. Review upstream and
submodule license terms before redistributing a newly built FD.
