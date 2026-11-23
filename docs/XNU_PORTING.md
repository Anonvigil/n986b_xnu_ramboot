# SM-N986B XNU 8796 RAM port

This is the preservation record for the **iOS 16.5 (20F66) / Darwin 22.5.0 / XNU 8796.121.2** experiment on the Exynos 990 Galaxy Note20 Ultra. It records a volatile, device-specific research path. It does **not** describe a working iOS installation or interface. The last independently audited live high-RAM device gate is H6; H7 was built and inspected on the host but has no verified device result.

## Start from these fixed inputs

| Component | Pin or source | Role |
| --- | --- | --- |
| Scion common kernel | [Scion-Project/scion-kernel `025f97557ac439651b08f2a906c01addfad77ece`](https://github.com/Scion-Project/scion-kernel/tree/025f97557ac439651b08f2a906c01addfad77ece) | Build framework, XNU/HFS patch set, iOS 16.5 profile |
| Apple open-source XNU | [xnu-8796.121.2, `c54f35ca767986246321eb901baf8f5ff7923f6a`](https://github.com/apple-oss-distributions/xnu/tree/c54f35ca767986246321eb901baf8f5ff7923f6a) | Scion `xnu/apply.sh` base |
| Apple open-source HFS | [hfs-627.100.6, `31d5ec238dfab485e78a9a30b8b92d13e8914f03`](https://github.com/apple-oss-distributions/hfs/tree/31d5ec238dfab485e78a9a30b8b92d13e8914f03) | Scion HFS source base |
| N986B board and kernel delta | [preserved source bundle](../sources/xnu/README.md) | N986B config, UEFI handoff adapter and H6 XNU changes |
| Apple 20F66 user space | Obtain independently under its own terms; **not distributed here** | Used only in the recorded Apple `launchd`/`dyld` dependency experiment |

The original iOS 16.5 experiment ran on `SM-N986B / N986BXXU0ATE3`. The public source does not include an IPSW, extracted Apple binaries, firmware, ECT, raw RAM images, or a device identity. The N986B UEFI/XNU loader uses the 8796 ABI: **736-byte `boot_args` with a 608-byte command line**. The earlier XNU 12377 loader's 1,152-byte structure cannot be substituted.

## Boot and evidence path

1. Stock Android entered the resident S-Boot Fastboot interface (`18D1:0002`). The research sender copied a pinned FD, a small manifest, a Mach-O kernel and a diagnostic root into volatile RAM. This was the device-specific transport described in [the companion Fastboot analysis](https://github.com/Anonvigil/n986b_twrp_ramboot/blob/5eef2238d9a089df8aed218cc171dee81da569d7/docs/FASTBOOT_VULNERABILITY.md), not standard `fastboot boot`.
2. EDK II/UEFI verified the manifest, Mach-O, memory ownership and handoff layout. The 8796 loader prepared the Apple device tree and `boot_args`, called `ExitBootServices`, then branched to XNU. Upload-mode A/B reads of the same run provided independent RAM evidence.
3. XNU reported Darwin 22.5.0, selected `md0`, mounted a diagnostic HFS+ root and completed BSD initialization. A static ARM64 diagnostic `/sbin/launchd` ran as PID 1, printed from user space as UID 0, and deliberately exited. An earlier stack-alignment fault was fixed by an eight-byte adjustment so the user stack was divisible by 16.
4. Replacing that diagnostic executable with the original 20F66 Apple `launchd` entered Apple `dyld` and printed `hello from launchd.1`. The 8 MiB root lacked `libSystem.B.dylib`; a later 32 MiB root supplied it, but its extracted-cache Mach-O failed a fixed `mmap` with `EINVAL(22)`. Its destination was unaligned under both 4 KiB and 16 KiB page sizes. All 36 examined extracted dependency images had at least one analogous mismatch. A larger coherent original dyld cache or a separately validated mapping strategy is needed.
5. In a separate high-RAM storage branch, XNU verified bounded reads from a 1 MiB fixture at physical `0x940000000`. H4b registered read-only block and character devices, H5 proved two 512-byte character/`physio` reads, and H6 proved two 512-byte block-vnode `VNOP_STRATEGY` reads through `spec_strategy`, with **zero writes**. This device was not selected as the root in H6.

These two branches answer different questions. The `md0` HFS+ and Apple PID 1 runs do **not** prove that the high-RAM block device can mount a filesystem. H6's block-vnode reads do **not** prove Apple user space can run from that device. See [the public evidence ledger](XNU_EVIDENCE.md) for exact hashes and observation limits.

## Rebuild strategy

The reusable source is in [`sources/xnu/`](../sources/xnu/README.md). Begin from the pinned Scion commit and its documented toolchain, copy the N986B device overlay, then apply Scion's 16.5 XNU/HFS setup and the reviewed N986B source delta. The source bundle distinguishes the **H6 live-tested** delta from the **H7 host-only** candidate. Do not combine them in a result claim.

The reviewed [`prepare_source.sh`](../sources/xnu/prepare_source.sh) automates those source-only steps from a fresh pinned Scion checkout and then [`verify_source.py`](../sources/xnu/verify_source.py) checks the resulting 26 H6 source hashes. The H7 option applies its separate patch and checks 27 paths. Both variants were replayed from fresh local checkouts without phone I/O; this is source reconstruction, not a bit-identical kernel or FD build.

The recorded first host build used Debian 13 under WSL2, Theos LLVM/ld64, an iPhoneOS 16.5 SDK, and Scion's build scripts. A Debian `mawk` compatibility patch and an isolated `EXYNOS990` machine variant were needed. The relevant sequence is:

1. Check out Scion at the pinned commit; supply the independently obtained toolchain and SDK, and install the Scion-documented host dependencies.
2. Copy the N986B device overlay from this repository into Scion's `devices/n986b/` and select `OS_PROFILE=ios-16.5`.
3. Run Scion `./xnu/apply.sh`, `./xnu-deps/setup.sh --stage1`, and `./xnu-deps/setup.sh --stage2`; apply the host/machine and N986B XNU changes as detailed in the source bundle.
4. Build with `THEOS=<toolchain-root> OS_PROFILE=ios-16.5 MACHINE=EXYNOS990 JOBS=4 ./tools/make-kernel.sh -j4` (later isolated H6 builds used `./xnu-build.sh -j8 build` with an explicit `XNU_SRC`). Inspect the linked Mach-O, source revision, and SHA-256 before creating any RAM package.
5. Build the matching N986B UEFI handoff module with the 8796 `boot_args`, then verify the FD, manifest, root fixture and kernel **as one paired set**. A new run needs its own fresh receipt and Upload A/B verification. The published source alone is not a ready-to-send image.

The initial Scion `EXYNOS990` compile target selected **zero of 58 optional hardware drivers**. The live early-boot results do not turn Linux/TWRP drivers into XNU drivers. N986B display, touch, Mali-G77 GPU, UFS, interrupt routing, power management and secondary CPU support each need separate native work and evidence. The `EXYNOS990` build constants are compile choices until checked on the device.

## Failure landmarks for a future session

| Last visible or captured marker | Meaning in the recorded experiments | Check next |
| --- | --- | --- |
| UEFI Shell | The firmware ran; the intended boot application did not take over. One older FD omitted its XNU loader FFS because DSC and FDF inclusion differed. | Inspect the actual FD file list/GUID and UEFI application entry before interpreting any kernel result. |
| `ExitBootServices`/handoff marker without XNU log | Loader reached the branch boundary. | Verify same-run handoff snapshot, Mach-O placement, low-memory log pointers and Upload A/B; a screen alone does not prove XNU ran. |
| `BSD root: md0`, HFS mountroot `0` | Diagnostic RAM root was selected and mounted. | Check the **actual** `/sbin/launchd` vnode size/hash, not just the path. An early synthetic-root path exposed the whole image as a zero-header file. |
| `SP_ALIGN` or first-instruction PID 1 fault | EL0 stack did not satisfy 16-byte AArch64 alignment. | Inspect saved `SP & 15` and exec-stack padding; the later static PID 1 run recorded `mod16=0`. |
| `Library not loaded: ...libSystem.B.dylib` | Apple `launchd`/`dyld` ran, then stopped on a dependency. | Separate absent file/cache from the later fixed-`mmap` `EINVAL(22)` caused by cache-packed segment addresses. |
| H4 registration `EINVAL(22)` | Early proof/DT lookup logic rejected the store; H4b fixed the code path. | Use the H4b/H5/H6 source lineage and require its proof markers, not just a `/dev` name. |
| H6 `VNOP_STRATEGY` pass | Two exact sectors were read through a real block vnode with zero writes. | HFS mount and cache admission remain separate gates. |

For diagnosis, prefer a bounded same-run Upload RAM log and manifest over a phone photograph. Record the exact stage and error before changing more than one component; the device can return to Android even when a RAM trial never reached XNU.

## Current next gate

The H7 host candidate changes root selection to the attested read-only high-RAM block device (`rd=b2lro0`) and adds an HFS+ mount path. Its host check linked an ARM64 Mach-O and verified the candidate source/relocations; no H7 transfer, Upload capture or mounted HFS root was established in the retained record. The next live gate is a fresh H7 paired transfer with read-only HFS header and full-image checks, same-run XNU `VFS_ROOT`/`statfs` evidence, strategy read counts, zero writes, and an independent Upload A/B capture. An empty HFS fixture has no Apple `/sbin/launchd`, so a later missing-PID-1 error would not erase a separately proven mount result.

After that, use a separately validated read-only path for the **complete original** dyld shared-cache set. The recorded 20F66 cache is about 2.72 GB and cannot fit in the 960 MiB XNU arena. A host-built APFS image and cache-integrity check exist, but neither an N986B APFS mount nor cache admission was observed. Do not claim iOS services, SpringBoard or a usable interface until separately witnessed.

## Earlier XNU 12377 branch

The earlier iPhoneOS 26.5 / XNU 12377.121.6 experiments used a different ABI and user space. They reached XNU boot tasks and several panic points, including `fsck`, mount and code-signing dependencies, but did not establish iOS UI. Their results are historical engineering context only; they are **not** the 16.5/H6 evidence chain. See [the separate history note](XNU_26_HISTORY.md).

## Reuse and publication limits

This repository preserves reviewed open-source code deltas and a falsifiable measurement summary. It does not retain the private raw A/B captures after the original workspace is deleted; their published SHA-256 values and checked observations cannot substitute for a new run. Never infer a full iOS boot from a kernel logo, an EFI shell, a TWRP screen, a host link, or a single Upload marker. Maintain separate identities for the Mach-O, FD, manifest, root and running device profile at each gate.
