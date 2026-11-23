# XNU 8796 evidence ledger

This ledger preserves the **observed results and their limits** from the SM-N986B iOS 16.5 RAM-port experiments. Dates are 2026-11-08 to 10. A/B means two independently captured Upload-mode reads of the specified RAM region were compared byte for byte; the raw captures are not distributed. The SHA-256 values below identify retained run inputs and receipts, not downloadable files in this repository.

## Live gates

| Gate | Observation | Independent check | Does not establish |
| --- | --- | --- | --- |
| Stage 10/11 UEFI | 8796 Mach-O parse, 736-byte boot arguments, owned 1 GiB EFI arena, identity checks; handoff disabled in these early builds | Fresh manifest and stage marker in Upload A/B | XNU execution for these particular builds |
| XNU/HFS root | Darwin 22.5.0, `BSD root: md0`, `bsd_init: done`, HFS+ mountroot result `0`, 16,552-byte diagnostic `/sbin/launchd` vnode | 12 Upload A/B regions; manifest linked to send and handoff snapshot | Apple PID 1 or iOS UI in this run |
| Static diagnostic PID 1 | ARM64 user process printed `Hello from user space on Darwin!`, process 1, UID 0; intentionally exited `0` | XNU/FD hashes and 12 Upload A/B regions | Sustained init or Apple user space |
| Original 20F66 Apple PID 1 | 525,088-byte Apple `/sbin/launchd` activated; Apple `libignition` and `hello from launchd.1`; dyld could not find `libSystem.B.dylib` in 8 MiB root | 67,968 payload frames; 12 Upload A/B regions; pinned kernel/FD/root | Complete `launchd`, services, SpringBoard or UI |
| 32 MiB dependency root | Dyld located `libSystem.B.dylib`, then `mmap(addr=0x10B172320, size=0x4000)` returned `EINVAL(22)` and PID 1 exited | 117,120 payload frames; 12 Upload A/B regions; same FD/kernel as prior Apple gate | Successful library load or a generic page-size fix |
| H3 physical store | XNU read and hashed 1 MiB from reserved high RAM | Same-run physical witness and Upload A/B | BSD block device |
| H4b device registration | Read-only `/dev/b2lro0` (block major 1) and `/dev/rb2lro0` (character major 13) registered for a 1 MiB RAM fixture | Fresh manifest, full and edge hashes, Upload A/B | Strategy read or root mount |
| H5 character read | Two 512-byte reads through `ro_char_read → physio → ro_strategy`; zero writes | 12 Upload A/B regions and independent raw audit | Block-vnode read |
| **H6 block-vnode read** | Two 512-byte `VNOP_STRATEGY → spec_strategy → ro_strategy` reads at LBA 0 and 2047; `spec=2 calls=2 reads=2 writes=0`; vnode cleanup | **12 Upload A/B regions**, 22 ordered H4b/H5/H6 console markers, independent raw audit and Android after-check | Buffer-cache read, HFS/APFS mount, Apple PID 1 from this device, or iOS UI |

The H6 collector verdict was `H6_LIVE_VBLK_VNOP_SPECFS_STRATEGY_PASS`; the separate raw-file checker returned `H6_INDEPENDENT_RAW_AB_AND_POST_ADB_PASS`. The saved H6 sender recorded **9 setup + 2,048 high-RAM + 51,712 low-RAM + one terminal** acknowledged OUT operations. The phone returned to Android after Upload host reset; that run's read-only ADB receipts reported completed boot, Knox warranty bit `0`, Verified Boot `green`, and Debug High `0x4948`. These observations apply to the recorded ATE3 session and are not a guarantee for future images.

A separate early panic experiment logged an XNU request to write S-Boot's `E_LOOP` reset marker, then the host observed a fresh `SEC_UPLOAD 04E8:685D` device and matching A/B run marker. This supports a panic-to-Upload route for that run. The retained automated receipt cannot independently witness the absence of physical button input, so it is not a universal or fully unattended recovery guarantee.

## Identity anchors

| Item | SHA-256 |
| --- | --- |
| XNU Mach-O used for static PID 1 and original Apple `launchd` gate | `3c7e36d23922ba1ec9db673a464a158873517f892f952999d73070351cd7ab77` |
| UEFI FD paired with those user-space gates | `bc3badbcfcb6c0a8d849b1e7704deb4f8c7378a7a2f60b396d378fb411cae83a` |
| 8 MiB Apple-entry HFS+ diagnostic root | `662723d96264daee3d6ad5b44752475e278b49a4a599e04789a8cdaf177ebf2b` |
| 32 MiB dependency diagnostic root | `54224de36c609e3f26d08c709b55a5cadaf7ada46ac8bbe918d2753338250367` |
| H6 unstripped ARM64 Mach-O | `ac7af9d81b3aa756b257a790f21e9d0301581dfe53dc786e0a6d367a2ef1d6e0` |
| H6 reused UEFI FD | `b6433e3bdf6ffda64357f46fc592a5d922611241470d942e2f86afa1b19d0201` |
| H6 high-RAM 1 MiB diagnostic witness | `b26a12949fe1ce56f5830a7187a28bbf4b173362a7d0be0c082e84e700795d8a` |
| H6 fresh run manifest | `deb472c2910991208ea57a34be476ffea0f95cf430eb55369450343b1c07c91c` |
| H7 host-linked kernel, **no live result** | `390946837fa12abb9b71bafabefc0b16609617f75ff1df87f9893798bf7d81ee` |

The first iOS 16.5 compile-only Exynos 990 variant (`22dba00d98f8f36a484a533a83048911ca7ad6b5572a9e3ffddf5546c67ff51b`) is **not** the live H6 image. The H7 kernel's host verifier reported `HOST_STATIC_PASS_ONLY` and explicitly `live_h7_root_mount=NOT_TESTED`. Do not reuse an H6 receipt as H7 evidence.

## Why the Apple library stopped

The extracted `libSystem.B.dylib` was 68,736 bytes (SHA-256 `6bc1a6011303c4a9f285c602b5547609b3b4774230135f004b45712cefada7e3`). Dyld treated its cache-packed segments as a standalone disk image. The `__DATA_CONST` segment destination logged at `0x10B172320` has remainder `0x320` modulo 4 KiB and `0x2320` modulo 16 KiB, while the file offset is 16 KiB aligned. XNU's fixed `mmap` path therefore rejects the address with `EINVAL`. Across 36 extracted dependency images, all had at least one incompatible segment mapping. The original dyld cache maps aligned outer cache regions and should be evaluated intact; the host-built image containing it is **not** a live N986B APFS/cache result. See Apple's [dyld-1066.8 disk mapping code](https://github.com/apple-oss-distributions/dyld/blob/dyld-1066.8/dyld/JustInTimeLoader.cpp#L1029-L1063) and [XNU mmap checks](https://github.com/apple-oss-distributions/xnu/blob/xnu-8796.121.2/bsd/kern/kern_mman.c#L292-L402).

## Validation standard for the next run

Record a fresh pre-send Android/bootloader profile, exact Mach-O/FD/root or witness SHA-256 values, transport receipt, and a unique manifest tag. After execution, read bounded Upload regions twice; require A/B equality, matching manifest and handoff snapshot, and source-specific ordered kernel markers. Finish with a new Android status receipt. A photograph or a single host-built binary cannot replace those checks. Store raw captures privately with a separate retention decision; this public repository contains only this filtered ledger.
