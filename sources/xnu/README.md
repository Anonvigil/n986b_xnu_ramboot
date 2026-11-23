# SM-N986B iOS 16.5 XNU source reconstruction

This directory preserves the source changes for the **volatile RAM** XNU
experiment on SM-N986B. The target is iOS 16.5 build `20F66`, Darwin 22.5.0,
and XNU `xnu-8796.121.2`. The H6 kernel reached two read-only 512-byte
block-vnode strategy reads from a verified 1 MiB high-RAM fixture on the
phone. The separate H7 patch is a **host-built candidate** for mounting an
HFS+ root from that device; no H7 live mount is established. See the
[porting record](../../docs/XNU_PORTING.md), [evidence and limits](../../docs/XNU_EVIDENCE.md),
and [older XNU 12377 history](../../docs/XNU_26_HISTORY.md).
The [H6 transport record and offline tools](transport/README.md) preserve the
historical RAM package layout and A/B checks without bundling a live USB sender.

## Pinned inputs

| Source | Revision | Role |
| --- | --- | --- |
| [Scion common kernel](https://github.com/Scion-Project/scion-kernel) | `025f97557ac439651b08f2a906c01addfad77ece` | iOS 16.5 profile, XNU/HFS patches, build system |
| [Apple open-source XNU](https://github.com/apple-oss-distributions/xnu) | tag `xnu-8796.121.2`, commit `c54f35ca767986246321eb901baf8f5ff7923f6a` | kernel base |
| [Apple open-source HFS](https://github.com/apple-oss-distributions/hfs) | tag `hfs-627.100.6`, commit `31d5ec238dfab485e78a9a30b8b92d13e8914f03` | HFS kernel code |

`prepare_source.sh` pins the Scion commit. Scion's `xnu/apply.sh` checks the
Apple XNU commit, and its HFS script checks the HFS commit. The included
`device/n986b/` config selects the Exynos 990 compile target with optional
hardware drivers disabled. The H6 patch includes the host `mawk` compatibility
change, EXYNOS990 machine support, CPU0/GIC bring-up, diagnostic HFS root,
panic log path, and H4b/H5/H6 read-only RAM device. Its SHA-256 is
`4d3272316d08ca2a231f379ef5d36c3f5fbc3e4cfa4cb0f0f11b33682c379937`.

## Reconstruct the H6 source

On a Linux host or WSL Debian with Git, GNU patch, Python, Make, and network
access, start from a **fresh** checkout:

```bash
git clone https://github.com/Scion-Project/scion-kernel.git scion-kernel
git -C scion-kernel checkout 025f97557ac439651b08f2a906c01addfad77ece
bash sources/xnu/prepare_source.sh ./scion-kernel h6
```

In the last command, `sources/xnu/prepare_source.sh` means the path to this
repository's copy of the script; use its absolute path if the repositories
are in different directories. The script copies `device/n986b/`, runs
`make n986b_defconfig`, reconstructs Scion's pinned XNU/HFS tree, applies
[`0001-n986b-ios165-h6.patch`](patches/0001-n986b-ios165-h6.patch), and verifies
SHA-256 for all 26 changed source paths. It refuses to overwrite an existing
device or XNU tree. It does not access a phone.

The equivalent final verification command is:

```bash
python3 sources/xnu/verify_source.py \
  --tree scion-kernel/xnu-src/xnu-8796.121.2 --stage h6
```

The source preparation was replayed on a fresh local Scion clone. The
reconstructed tree was also compared byte for byte with the frozen H6 source,
excluding build output, Git metadata, and Python cache files. The H6 linked
ARM64 Mach-O from the recorded live run has SHA-256
`ac7af9d81b3aa756b257a790f21e9d0301581dfe53dc786e0a6d367a2ef1d6e0`.
That hash is an **artifact identity**, not a promise of bit-for-bit rebuilds
with a changed compiler or environment.

## Build the kernel

The recorded host used WSL Debian, the Scion 16.5 profile, Theos LLVM/ld64,
and an iPhoneOS 16.5 SDK. Install the toolchain and SDK under your own local
paths following Scion's build instructions. Then, from the prepared Scion
checkout:

```bash
export THEOS=/path/to/theos
export OS_PROFILE=ios-16.5
export MACHINE=EXYNOS990
export XNU_ARCH_STRING=arm64
export XNU_SRC="$PWD/xnu-src/xnu-8796.121.2"
export KERNEL="$XNU_SRC/BUILD/obj/DEVELOPMENT_ARM64_EXYNOS990/mach.development.exynos990"
./xnu-deps/setup.sh --stage1
./xnu-deps/setup.sh --stage2
./xnu-build.sh -j8 build
```

The `KERNEL` path is where the recorded H6 build wrote its Mach-O. Building
the kernel does not produce an iOS image or a phone-ready UEFI FD. The
matching H4/v3 UEFI loader source and its build limitations are in
[`uefi/`](uefi/). The earlier ABI/layout adapter in
[`bootstrap-adapter/`](bootstrap-adapter/) is an historical, handoff-disabled
checkpoint; it is not the H6 live UEFI application. The 736-byte XNU 8796
`boot_args` ABI must be used, rather than the 1,152-byte XNU 12377 variant.

## H7 host candidate

To reconstruct the later **untested on-device** H7 source, use a separate
fresh Scion checkout and select `h7`:

```bash
bash sources/xnu/prepare_source.sh ./scion-kernel-h7 h7
python3 sources/xnu/verify_source.py \
  --tree scion-kernel-h7/xnu-src/xnu-8796.121.2 --stage h7
```

[`0002-n986b-ios165-h7-host-only.patch`](patches/0002-n986b-ios165-h7-host-only.patch)
applies after H6. The reconstructed H7 tree matched the frozen host candidate
byte for byte. H7 requires its separately pinned HFS+ fixture and a newly
paired FD/manifest for a live run; neither a live H7 result nor a mount is
claimed here. The fixture, the original iOS 16.5 user-space files, IPSW,
shared cache, firmware, private RAM captures, and stock ECT data are not
included in this public source bundle.

## Source licenses and scope

Scion's XNU modifications and HFS integration are derivatives of Apple's
open-source XNU/HFS code. Preserve their notices and the
[Apple Public Source License 2.0](LICENSE.APSL). New files retain their own
SPDX tags. The board config, original host scripts, and documentation use
the applicable upstream or repository licenses; see [source notices](NOTICE.md).
The root repository MIT license does not relicense Apple's source or
third-party files.

This source bundle is for research and reconstruction. It does not claim a
working iOS UI, APFS mount, storage writes, XNU display/GPU/touch drivers, or
a repeatable live run on another firmware revision.
