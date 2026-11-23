# Preservation boundary before deleting a research workspace

This public repository preserves source deltas, UEFI source, an offline H6 transport format, hashes and filtered results. It is **not** a complete private experiment archive. Removing the original workspace without a separate private backup loses inputs needed to reproduce the exact historical phone run or independently re-audit its raw evidence.

## What the public repositories preserve

| Public repository | Preserved | Missing for exact replay |
| --- | --- | --- |
| [N986B XNU RAM boot](https://github.com/Anonvigil/n986b_xnu_ramboot) | H6 XNU source patch, H7 host-only patch, H6 UEFI source, callback assembly, diagnostic PID 1 source, historical panic-to-Upload delta, host-only package and A/B validators, filtered evidence. | Exact historical FD and Mach-O binaries, high-RAM fixture bytes, live USB sender and native Upload collector dependency tree, original send/ADB receipts, A/B raw RAM, private session seed, and independently obtained Apple user space. |
| [N986B TWRP RAM boot](https://github.com/Anonvigil/n986b_twrp_ramboot) | GUI-specific UEFI and recovery-stream build source, test notes, candidate Windows launchers, and successful FD/EFI assets. | Exact 75,890,688-byte RAM stream, official Team Win recovery image, live device-tree input and stock ECT for source reconstruction, raw A/B evidence. The released launcher has only offline tests. |

The TWRP stream incorporates the official Team Win image. The original N986B DTB and ECT are device-derived. Do not add them, Apple images, raw RAM, unredacted receipts, or session seeds to a public commit merely to make a release self-contained. The TWRP release also notes that the already published FD/EFI embed stock ECT with unresolved redistribution rights.

## Minimal private preservation set

Before deleting an original workspace, copy and hash-check these **outside the directory being deleted**, with appropriate private storage controls:

1. TWRP: the exact live DTB (SHA-256 `b0e8715a9dcfb55c393a7f1e6cb984e200b2b568ac1b152c1a8b40aae25009d1`), stock ECT (`32a642074aa5168aede2cededf3af62c13cd20eb9212517fe517bfbfbbc70caa`), official `twrp-3.7.0_11-0-c2s.img` (`8a4dc7914d5ce0b6e2dae900433a853ad35a1606b5a94f3e9042ae85c6f2a771`), and the successful 75,890,688-byte RAM stream (`d1bfd029728be897f6bc5721e1c1abfcab41b0a84d5ca20591b8ea0911304f7b`). Preserve the successful send receipt and A/B evidence if independent re-audit matters.
2. XNU H6: the paired FD (SHA-256 `b6433e3bdf6ffda64357f46fc592a5d922611241470d942e2f86afa1b19d0201`), Mach-O (`ac7af9d81b3aa756b257a790f21e9d0301581dfe53dc786e0a6d367a2ef1d6e0`), 1 MiB witness (`b26a12949fe1ce56f5830a7187a28bbf4b173362a7d0be0c082e84e700795d8a`), callback stage, private session record, and same-run Upload A/B files. Preserve the original sender/reader source and its imported modules if exact transfer replay matters.
3. Historical XNU 12377/iOS 26.5 work: retain its local research manuscript, source changes and receipts privately if that branch will be revisited. This repository contains only a short historical summary for it.

Check the actual full SHA-256 of every copied file against its source manifest before removing the workspace. Keep at least one backup copy independent of the current computer. A GitHub commit hash proves what was published, but does not recreate omitted private data or prove a new device run.
