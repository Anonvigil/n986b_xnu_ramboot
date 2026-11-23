# Early XNU 8796 handoff adapter checkpoint

These sources preserve the first N986B adaptation of a UEFI XNU loader to Scion's iOS 16.5 ABI. The recorded early host build used **handoff disabled** (`N986B_ENABLE_XNU_HANDOFF=0`) and returned to UEFI Shell after stage 10/11 checks. It is historical source and is **not** the UEFI application paired with the later H6 live result. That matching v3, handoff-enabled loader is in [`../uefi/`](../uefi/README.md).

The key change was replacing the older XNU 12377 `boot_args` layout (1,152 bytes) with the XNU 8796 layout (736 bytes, including a 608-byte command line), plus a guarded 960 MiB XNU window inside the owned 1 GiB EFI arena. `verify_abi.py` and `tests/` contain the original ABI, Mach-O and layout checks. Those host tests validate source assumptions and synthetic EFI descriptors; they cannot prove a particular device memory map or kernel entry.

For current reconstruction, follow the [H6 source preparation](../README.md) and [UEFI H6 profile](../uefi/README.md). Keep this early adapter separate when naming artifacts or interpreting Upload RAM evidence.
