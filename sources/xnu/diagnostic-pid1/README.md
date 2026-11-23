# Early static PID 1 diagnostic source

This directory preserves two small source inputs from the early iOS 16.5 user-space investigation. `hello_n986b.c` is a freestanding ARM64 program that uses Darwin system calls directly to print a user-space marker, PID and UID before a bounded wait and exit. `entry_align.s` is a separate host candidate that aligns SP before entering the C program. Neither is Apple's `launchd`.

| File | SHA-256 of preserved source | Scope |
| --- | --- | --- |
| [`hello_n986b.c`](hello_n986b.c) | `b340166ca1727dbb580d631cfc14a8bce12b5612c11c4a63417f471b9a0909dd` | Freestanding diagnostic program. |
| [`entry_align.s`](entry_align.s) | `e6c47481d3b6b0f9ecd377e235729266c437e5eb5a147d9936747e8ebefae8ba` | Host-built stack-alignment entry candidate. |

The retained host report built the candidate with an ARM64 XNU 16.5 linker, `-static -nostdlib -ffreestanding -fno-stack-protector -fno-builtin`, entry `_entry_align`, and `-pagezero_size 0x100000000`. Its resulting Mach-O was 16,584 bytes, SHA-256 `43d9f7fca2bd36a7d8552a3f4a4017c5de62ea106f5693fffe977740982f7f17`. This source archive has **not** independently reproduced that binary. The first SP-alignment candidate was host-only; later device evidence of a running static PID 1 is described in [the porting record](../../../docs/XNU_PORTING.md) and should not be attributed to this exact candidate without a paired run manifest.

This is an isolated bring-up program. It does not provide a usable iOS user space, recovery environment, or driver. Its source is part of this repository's original material under the root MIT license; the kernel sources and Apple HFS code retain their separate terms.
