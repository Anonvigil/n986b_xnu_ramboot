# SM-N986B XNU RAM boot research

Reproducible source and measured results for the **iOS 16.5 (20F66) / Darwin 22.5.0 / XNU 8796.121.2** RAM port on the Exynos 990 Galaxy Note20 Ultra (`SM-N986B`). The tested path used the resident S-Boot Fastboot interface, a volatile EDK II/UEFI handoff, and an Exynos 990 XNU build. It did **not** install iOS or reach an iOS interface.

## Verified boundary

| Path | Last confirmed result |
| --- | --- |
| Diagnostic root and user space | XNU mounted a small HFS+ `md0` root and ran a static ARM64 PID 1. A later test entered the original 20F66 Apple `launchd` and `dyld`, then stopped at a `libSystem.B.dylib` dependency/mapping failure. |
| Separate high-RAM storage work | H6 registered an attested, read-only RAM block device and completed two 512-byte block-vnode `VNOP_STRATEGY` reads with zero writes. |
| Next candidate | H7 adds an HFS+ root path on that block device. Its source and linked kernel passed host checks; no H7 live mount result is claimed. |

These results are separate. The Apple PID 1 experiment used a diagnostic `md0` root; H6 did not mount a filesystem from its new block device.

## Read and rebuild

1. [Porting sequence, fixed source versions, failure landmarks and next gate](docs/XNU_PORTING.md)
2. [Measured live evidence, hashes and limits](docs/XNU_EVIDENCE.md)
3. [H6 source reconstruction and H7 host-only patch](sources/xnu/README.md)
4. [Matching H6 UEFI source, provenance and build limitations](sources/xnu/uefi/README.md)
5. [Earlier, separate XNU 12377 branch](docs/XNU_26_HISTORY.md)
6. [N986B hardware inputs and driver boundaries](docs/XNU_HARDWARE.md)
7. [H6 RAM package format and offline Upload A/B verifier](sources/xnu/transport/README.md)

The source preparation script reconstructs the H6 and H7 XNU trees from pinned public Scion, Apple XNU and Apple HFS commits, then verifies the changed-file SHA-256 values. This was checked on fresh host checkouts. The transport tools reconstruct the historical H6 package format and verify previously collected Upload A/B bytes, but do not send USB traffic or collect device RAM. The source scripts do not create a bit-identical linked kernel or UEFI FD, and the published UEFI recipe has not reproduced its historical FD byte for byte on a clean host. A fresh phone run requires privately supplied version-matched inputs, a newly paired manifest, and independent Upload-mode A/B checks.

The companion [TWRP RAM boot repository](https://github.com/Anonvigil/n986b_twrp_ramboot) records the earlier working Linux/TWRP GUI route. The EFI source scaffold in [`sources/efi/`](sources/efi/README.md) is a copy of that repository's public overlay; the XNU-specific UEFI changes are in [`sources/xnu/uefi/`](sources/xnu/uefi/README.md). TWRP/Linux driver operation is not evidence of an XNU GPU, UFS, touch or display driver.

## Publication scope

This repository contains reviewed source, licenses and filtered measurements. It excludes Apple IPSW and extracted user-space binaries, stock device firmware/ECT, diagnostic HFS/APFS images, raw Upload RAM, device identifiers and secrets. The root MIT license applies only to original material; XNU-derived and EFI/third-party code retain their own notices in [NOTICE](NOTICE.md). The published hashes are evidence anchors, not substitutes for a fresh device run.
